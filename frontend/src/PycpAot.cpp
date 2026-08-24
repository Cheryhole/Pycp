#include "PycpAot.hpp"
#include "PycpConfig.hpp"

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Pycp::AOT {

// =============================================================
// AOT 指令翻译器
//
// 将字节码 Module（常量池 + 符号表 + 代码对象）逐指令翻译为依赖
// PycpABI 的独立 C++ 源文件。生成的代码不嵌入解释器循环，而是把
// 每条栈式指令展开为对应的 ABI 调用 / 控制流语句。
//
// 多模块（import）支持：
//   - 每个模块生成一个 .cpp，模块私有符号（pycp_fn_N / g_c / g_globals）
//     均为 static，避免跨文件符号冲突。
//   - 每个模块导出一个【非 static】初始化函数 pycp_module_<hash>(void)，
//     返回该模块的 Module*（懒执行，首次调用才运行顶层）。
//   - 入口模块的 .cpp 额外生成 main()，并在其 LOAD_MODULE 处调用被导入
//     模块的 pycp_module_<hash>()。
//   - 模块顶层变量写入 Module 的命名空间（经 g_mod_ns 指针），
//     与 VM 的模块隔离语义一致。
// =============================================================

namespace {

// 将字符串转义为 C++ 字符串字面量内容（含外层引号）。
std::string cpp_string_literal(const std::string& s) {
	std::string out = "\"";
	for (char ch : s) {
		switch (ch) {
			case '\\': out += "\\\\"; break;
			case '"':  out += "\\\""; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(ch) < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\x%02x",
					              static_cast<unsigned char>(ch));
					out += buf;
				} else {
					out += ch;
				}
		}
	}
	out += "\"";
	return out;
}

// 模块名 -> 唯一 C++ 标识符后缀（FNV-1a 哈希，8 位十六进制）。
std::string module_hash(const std::string& name) {
	uint64_t h = 1469598103934665603ULL;
	for (unsigned char c : name) {
		h ^= c;
		h *= 1099511628211ULL;
	}
	char buf[17];
	std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
	return std::string(buf);
}

// 模块初始化函数符号名。
std::string module_init_symbol(const std::string& name) {
	return std::string(Pycp::AOT_MODULE_INIT_PREFIX) + module_hash(name);
}

// 计算单个代码对象指令流的最大可能栈深（保守顺序扫描）。
std::size_t estimate_stack_depth(const Pycp::BC::CodeObject& co) {
	std::size_t depth = 0;
	std::size_t max_depth = 0;
	for (const auto& ins : co.code) {
		switch (ins.op) {
			case Pycp::BC::Op::LOAD_CONST:
			case Pycp::BC::Op::LOAD_VAR:
			case Pycp::BC::Op::LOAD_NONE:
			case Pycp::BC::Op::LOAD_TRUE:
			case Pycp::BC::Op::LOAD_FALSE:
			case Pycp::BC::Op::MAKE_FUNCTION:
			case Pycp::BC::Op::LOAD_MODULE:
				++depth;
				break;
			case Pycp::BC::Op::BINARY_ADD:
			case Pycp::BC::Op::BINARY_SUB:
			case Pycp::BC::Op::BINARY_MUL:
			case Pycp::BC::Op::BINARY_DIV:
			case Pycp::BC::Op::BINARY_POW:
			case Pycp::BC::Op::COMPARE_OP:
				if (depth > 0) --depth;
				break;
			case Pycp::BC::Op::UNARY_NEG:
			case Pycp::BC::Op::POP_TOP:
			case Pycp::BC::Op::RETURN:
			case Pycp::BC::Op::JUMP_IF_FALSE:
			case Pycp::BC::Op::JUMP_IF_TRUE:
				if (depth > 0) --depth;
				break;
			case Pycp::BC::Op::CALL: {
				std::size_t argc = static_cast<std::size_t>(ins.operand);
				depth = (depth > argc + 1) ? (depth - argc - 1) : 0;
				++depth;
				break;
			}
			case Pycp::BC::Op::DUP_TOP:
				++depth;
				break;
			case Pycp::BC::Op::GET_ATTR:
			case Pycp::BC::Op::LOAD_ATTR:
			case Pycp::BC::Op::GET_ITER:
			case Pycp::BC::Op::FOR_ITER:
				// 弹对象/迭代器再压属性值/元素（或 StopIteration 跳转），栈深不变
				break;
			case Pycp::BC::Op::STORE_ATTR:
				// 弹值 + 对象，栈深减 1
				if (depth > 0) --depth;
				break;
			case Pycp::BC::Op::MAKE_CLASS:
				// 弹出 decorator_count 个装饰器后压入类对象。
				// 注：decorator_count 在 ins 的操作数之外的 ClassDef 中，
				// 这里无法精确获知；保守取 depth + 1（类对象压栈净增，
				// 装饰器弹栈在保守估计下忽略，以保证 reserve 充足）。
				++depth;
				break;
			case Pycp::BC::Op::BUILD_LIST: {
				std::size_t n = static_cast<std::size_t>(ins.operand);
				depth = (depth > n) ? (depth - n) + 1 : 1;
				break;
			}
			case Pycp::BC::Op::GET_ITEM:
				// 弹 obj + key 压 1，净 -1。
				if (depth > 0) --depth;
				break;
			case Pycp::BC::Op::SET_ITEM:
				// 弹 obj + key + value，不压。
				depth = (depth >= 3) ? (depth - 3) : 0;
				break;
			case Pycp::BC::Op::JUMP:
			case Pycp::BC::Op::RETURN_NONE:
			case Pycp::BC::Op::HALT:
			default:
				break;
		}
		if (depth > max_depth) max_depth = depth;
	}
	if (max_depth < 16) max_depth = 16;
	return max_depth + 8;
}

// 单个 CodeObject 翻译为一个 native 函数体。
//   self == nullptr 时 env->globals 指向 g_mod_ns（模块命名空间）。
//   all_deps : 全部 .pycp 依赖模块名集合，用于区分「内建库导入」与
//              「.pycp 依赖模块导入」（LOAD_MODULE 生成不同的加载代码）。
void emit_function(std::ostringstream& os, const Pycp::BC::Module& module,
                   const Pycp::BC::CodeObject& co, std::size_t co_idx,
                   const std::set<std::string>& deps) {
	std::size_t nlocals = co.nlocals;
	std::size_t max_depth = estimate_stack_depth(co);
	std::size_t nparams = co.nparams;

	// 函数签名：self 指向 Closure（携带捕获环境），顶层 self == nullptr
	os << "static Pycp::Object* " << Pycp::AOT_FN_PREFIX << co_idx
	   << "(Pycp::Object* self, Pycp::Object** argv, std::size_t argc) {\n";

	// 构造本函数执行环境
	os << "    std::shared_ptr<Pycp::BC::Environment> env = "
	   << "std::make_shared<Pycp::BC::Environment>();\n";
	os << "    if (self != nullptr) {\n";
	os << "        Pycp::BytecodeFunction* bfn = static_cast<Pycp::BytecodeFunction*>(self);\n";
	os << "        env->captured = bfn->get_captured();\n";
	os << "    }\n";
	os << "    env->globals = g_mod_ns;\n";

	// 局部变量名表
	os << "    env->local_names = {";
	for (std::size_t i = 0; i < co.names.size(); ++i) {
		if (i) os << ", ";
		os << cpp_string_literal(co.names[i]);
	}
	os << "};\n";
	os << "    env->locals.assign(" << nlocals << ", nullptr);\n";

	// 参数绑定（argv[0..nparams) -> env->locals）
	if (nparams > 0) {
		os << "    for (std::size_t i = 0; i < argc && i < " << nparams
		   << "; ++i) {\n";
		os << "        env->locals[i] = argv[i];\n";
		os << "        if (argv[i]) Pycp::Incref(argv[i]);\n";
		os << "    }\n";
	}

	// 操作数栈
	os << "    std::vector<Pycp::Object*> st;\n";
	os << "    st.reserve(" << max_depth << ");\n";

	// 逐指令翻译
	for (std::size_t pc = 0; pc < co.code.size(); ++pc) {
		const auto& ins = co.code[pc];
		os << "L_" << pc << ":;\n";

		switch (ins.op) {
			case Pycp::BC::Op::LOAD_CONST: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				os << "    st.push_back(g_c[" << idx << "]);\n";
				os << "    Pycp::Incref(g_c[" << idx << "]);\n";
				break;
			}
			case Pycp::BC::Op::LOAD_NONE:
				os << "    st.push_back(Pycp::None::instance);\n";
				os << "    Pycp::Incref(Pycp::None::instance);\n";
				break;
			case Pycp::BC::Op::LOAD_TRUE:
				os << "    st.push_back(Pycp::Boolean::True());\n";
				os << "    Pycp::Incref(Pycp::Boolean::True());\n";
				break;
			case Pycp::BC::Op::LOAD_FALSE:
				os << "    st.push_back(Pycp::Boolean::False());\n";
				os << "    Pycp::Incref(Pycp::Boolean::False());\n";
				break;

			case Pycp::BC::Op::LOAD_VAR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				const std::string& name = module.symtab[idx];
				int lineno = (pc < co.linenos.size()) ? co.linenos[pc] : -1;
				os << "    { Pycp::Object* v = Pycp::Environment_Lookup(env.get(), "
				   << cpp_string_literal(name) << ");\n";
				os << "      if (v == nullptr) throw Pycp::NameError("
				   << cpp_string_literal(module.source_path) << ", " << lineno
				   << ", \"name '" << name << "' is not defined\");\n";
				os << "      st.push_back(v); Pycp::Incref(v); }\n";
				break;
			}
			case Pycp::BC::Op::STORE_VAR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				const std::string& name = module.symtab[idx];
				os << "    { Pycp::Object* v = st.back(); st.pop_back();\n";
				os << "      Pycp::Environment_Store(env.get(), "
				   << cpp_string_literal(name) << ", v); }\n";
				break;
			}

			case Pycp::BC::Op::LOAD_MODULE: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				const std::string& dep = module.imports[idx];
				if (deps.count(dep) > 0) {
					// .pycp 依赖模块：调用其生成的初始化函数（跨文件链接）。
					os << "    { Pycp::Module* m = "
					   << module_init_symbol(dep) << "();\n";
					os << "      Pycp::Incref(m); st.push_back(m); }\n";
				} else {
					// 内建库（io/Pycp/classtools 等）：经 LoadNativeModule 加载并缓存。
					os << "    { Pycp::Module* m = pycp_load_native("
					   << cpp_string_literal(dep) << ");\n";
					os << "      Pycp::Incref(m); st.push_back(m); }\n";
				}
				break;
			}
			case Pycp::BC::Op::GET_ATTR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				const std::string& attr = module.symtab[idx];
				os << "    { Pycp::Object* obj = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* v = Pycp::Module::GetAttr("
				   << "static_cast<Pycp::Module*>(obj), "
				   << cpp_string_literal(attr) << ");\n";
				os << "      Pycp::Decref(obj);\n";
				os << "      st.push_back(v); Pycp::Incref(v); }\n";
				break;
			}

			case Pycp::BC::Op::LOAD_ATTR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				const std::string& attr = module.symtab[idx];
				// 通用属性访问（实例/类/模块/文件/list 等）。Pycp::GetAttr 返回
				// Owned（实例方法返回 BoundMethod、list 的 length 包装绑定等），
				// 栈接管这 1 份引用，不额外 Incref/Decref。
				os << "    { Pycp::Object* obj = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* v = Pycp::GetAttr(obj, "
				   << cpp_string_literal(attr) << ");\n";
				os << "      Pycp::Decref(obj);\n";
				os << "      st.push_back(v); }\n";
				break;
			}
			case Pycp::BC::Op::STORE_ATTR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				const std::string& attr = module.symtab[idx];
				// 弹值 + 对象；SetAttr -> __set_attribute__ 内部按需 Incref 存入，
				// 此处释放 value 从栈 pop 带来的引用（与 VM STORE_ATTR 一致，
				// 避免字段持有后栈引用泄漏）。
				os << "    { Pycp::Object* value = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* obj = st.back(); st.pop_back();\n";
				os << "      Pycp::SetAttr(obj, " << cpp_string_literal(attr)
				   << ", value);\n";
				os << "      Pycp::Decref(value);\n";
				os << "      Pycp::Decref(obj); }\n";
				break;
			}

			case Pycp::BC::Op::POP_TOP:
				os << "    Pycp::Decref(st.back()); st.pop_back();\n";
				break;
			case Pycp::BC::Op::DUP_TOP:
				os << "    st.push_back(st.back()); Pycp::Incref(st.back());\n";
				break;

			case Pycp::BC::Op::BINARY_ADD:
			case Pycp::BC::Op::BINARY_SUB:
			case Pycp::BC::Op::BINARY_MUL:
			case Pycp::BC::Op::BINARY_DIV:
			case Pycp::BC::Op::BINARY_POW: {
				const char* fn =
					ins.op == Pycp::BC::Op::BINARY_ADD ? "Add" :
					ins.op == Pycp::BC::Op::BINARY_SUB ? "Sub" :
					ins.op == Pycp::BC::Op::BINARY_MUL ? "Mul" :
					ins.op == Pycp::BC::Op::BINARY_DIV ? "Div" : "Pow";
				os << "    { Pycp::Object* rhs = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* lhs = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* res = Pycp::" << fn << "(lhs, rhs);\n";
				os << "      Pycp::Decref(lhs); Pycp::Decref(rhs);\n";
				os << "      st.push_back(res); }\n";
				break;
			}
			case Pycp::BC::Op::UNARY_NEG:
				os << "    { Pycp::Object* v = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* res = v->__negation__();\n";
				os << "      Pycp::Decref(v);\n";
				os << "      st.push_back(res); }\n";
				break;

			case Pycp::BC::Op::COMPARE_OP:
				os << "    { Pycp::Object* rhs = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* lhs = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* res = Pycp::Compare(lhs, rhs, "
				   << static_cast<int>(ins.operand) << ");\n";
				os << "      Pycp::Decref(lhs); Pycp::Decref(rhs);\n";
				os << "      st.push_back(res); }\n";
				break;

			case Pycp::BC::Op::JUMP: {
				long target = static_cast<long>(pc) + static_cast<long>(ins.operand);
				os << "    goto L_" << target << ";\n";
				break;
			}
			case Pycp::BC::Op::JUMP_IF_FALSE: {
				long target = static_cast<long>(pc) + static_cast<long>(ins.operand);
				os << "    { Pycp::Object* cond = st.back(); st.pop_back();\n";
				os << "      bool f = Pycp::IsFalse(cond); Pycp::Decref(cond);\n";
				os << "      if (f) goto L_" << target << "; }\n";
				break;
			}
			case Pycp::BC::Op::JUMP_IF_TRUE: {
				long target = static_cast<long>(pc) + static_cast<long>(ins.operand);
				os << "    { Pycp::Object* cond = st.back(); st.pop_back();\n";
				os << "      bool f = Pycp::IsFalse(cond); Pycp::Decref(cond);\n";
				os << "      if (!f) goto L_" << target << "; }\n";
				break;
			}
			case Pycp::BC::Op::BREAK: {
				// 由 Codegen 回填为当前最内层循环 end，语义同 JUMP（goto）。
				long target = static_cast<long>(pc) + static_cast<long>(ins.operand);
				os << "    goto L_" << target << ";\n";
				break;
			}

			case Pycp::BC::Op::BUILD_LIST: {
				std::size_t n = static_cast<std::size_t>(ins.operand);
				// 从栈顶依次弹出 n 个元素（栈序逆序），再按源码顺序 append
				// 构造 List（与 VM 语义一致：elems[n-1-i] = pop()）。
				// 弹栈后每份栈引用 Decref；新建 List 为 Owned（refcount=1），
				// 由栈接管（push 后栈持有这 1 份）。
				if (n > 0) {
					os << "    { Pycp::Object* elems[" << n << "];\n";
					for (std::size_t i = 0; i < n; ++i) {
						os << "      elems[" << (n - 1 - i) << "] = st.back(); st.pop_back();\n";
					}
					os << "      Pycp::List* l = Pycp::List::New();\n";
					for (std::size_t i = 0; i < n; ++i) {
						os << "      l->append(elems[" << i << "]); Pycp::Decref(elems[" << i << "]);\n";
					}
					os << "      st.push_back(l); }\n";
				} else {
					os << "    { Pycp::List* l = Pycp::List::New();\n";
					os << "      st.push_back(l); }\n";
				}
				break;
			}
			case Pycp::BC::Op::GET_ITEM: {
				// 弹 key + obj；GetItem 返回 Borrowed（元素由 list 持有），
				// push(res) 使栈持有 1 份引用，故不 Decref(res)。
				os << "    { Pycp::Object* key = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* obj = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* res = Pycp::GetItem(obj, key);\n";
				os << "      Pycp::Decref(obj); Pycp::Decref(key);\n";
				os << "      st.push_back(res); }\n";
				break;
			}
			case Pycp::BC::Op::SET_ITEM: {
				// 弹 value + key + obj；SetItem 返回 Owned（None），不压栈。
				os << "    { Pycp::Object* value = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* key = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* obj = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* res = Pycp::SetItem(obj, key, value);\n";
				os << "      Pycp::Decref(obj); Pycp::Decref(key); Pycp::Decref(value);\n";
				os << "      if (res) Pycp::Decref(res); }\n";
				break;
			}

			case Pycp::BC::Op::MAKE_FUNCTION: {
				std::size_t fidx = static_cast<std::size_t>(ins.operand);
				os << "    { Pycp::BytecodeFunction* fn = new Pycp::BytecodeFunction("
				   << cpp_string_literal(module.code_objects[fidx].name)
				   << ", " << Pycp::AOT_FN_PREFIX << fidx << ", env);\n";
				os << "      Pycp::GC_Track(fn);\n";
				os << "      st.push_back(fn); }\n";
				break;
			}
			case Pycp::BC::Op::CALL: {
				std::size_t nargs = static_cast<std::size_t>(ins.operand);
				os << "    { Pycp::Object* args[" << (nargs == 0 ? 1 : nargs) << "];\n";
				os << "      for (std::size_t i = 0; i < " << nargs << "; ++i) "
				   << "{ args[" << nargs << " - 1 - i] = st.back(); st.pop_back(); }\n";
				os << "      Pycp::Object* callee = st.back(); st.pop_back();\n";
				// 统一调用：Class 走 instantiate（类实例化），其余走 Call（可调用对象）。
				os << "      Pycp::Object* ret;\n";
				os << "      if (dynamic_cast<Pycp::Class*>(callee) != nullptr) {\n";
				os << "        ret = static_cast<Pycp::Class*>(callee)->instantiate(args, "
				   << nargs << ");\n";
				os << "      } else {\n";
				os << "        ret = Pycp::Call(callee, args, " << nargs << ");\n";
				os << "      }\n";
				os << "      for (std::size_t i = 0; i < " << nargs
				   << "; ++i) Pycp::Decref(args[i]);\n";
				os << "      Pycp::Decref(callee);\n";
				os << "      st.push_back(ret); }\n";
				break;
			}

			case Pycp::BC::Op::MAKE_CLASS: {
				std::size_t cidx = static_cast<std::size_t>(ins.operand);
				if (cidx >= module.classes.size()) {
					throw std::runtime_error("AOT: class index out of range.");
				}
				const auto& cdef = module.classes[cidx];
				const std::string& file = module.source_path;
				int lineno = (pc < co.linenos.size()) ? co.linenos[pc] : -1;

				// 1) 构造类对象。
				os << "    { Pycp::Class* cls = Pycp::Class::New("
				   << cpp_string_literal(cdef.name) << ");\n";

				// 2) 继承：父类引用（单标识符或模块.类路径），复制父类成员与方法。
				//    未显式 inherits 时默认自动继承 Pycp.Object（对齐 Python object）。
				{
					std::string parent_ref = cdef.parent_name.empty()
						? std::string("Pycp.Object") : cdef.parent_name;
					os << "      Pycp::Object* parent_obj = nullptr;\n";
					std::size_t dot = parent_ref.find('.');
					if (dot == std::string::npos) {
						os << "      parent_obj = Pycp::Environment_Lookup(env.get(), "
						   << cpp_string_literal(parent_ref) << ");\n";
					} else {
						std::string mod_name = parent_ref.substr(0, dot);
						std::string attr_name = parent_ref.substr(dot + 1);
						os << "      { Pycp::Object* mobj = Pycp::Environment_Lookup(env.get(), "
						   << cpp_string_literal(mod_name) << ");\n";
						// Pycp 可能尚未被当前模块显式 import：经加载辅助取。
						if (mod_name == "Pycp") {
							os << "        if (mobj == nullptr) mobj = pycp_load_native(\"Pycp\");\n";
						}
						os << "        if (mobj != nullptr && dynamic_cast<Pycp::Module*>(mobj) != nullptr) {\n";
						os << "          Pycp::Module* mo = static_cast<Pycp::Module*>(mobj);\n";
						os << "          parent_obj = mo->__get_attribute__("
						   << cpp_string_literal(attr_name) << ");\n";
						os << "        }\n";
						os << "      }\n";
					}
					os << "      if (parent_obj == nullptr || dynamic_cast<Pycp::Class*>(parent_obj) == nullptr) {\n";
					os << "        Pycp::Decref(cls);\n";
					os << "        throw Pycp::NameError(" << cpp_string_literal(file) << ", "
					   << lineno << ", \"parent class '" << parent_ref
					   << "' is not defined\");\n";
					os << "      }\n";
					os << "      Pycp::Class* parent = static_cast<Pycp::Class*>(parent_obj);\n";
					os << "      cls->set_parent(parent);\n";
					os << "      for (const auto& mn : parent->get_member_names())\n";
					os << "        cls->add_member_name(mn, parent->member_is_private(mn));\n";
					os << "      for (const auto& mname : parent->method_names()) {\n";
					os << "        Pycp::Function* pfn = parent->find_method(mname);\n";
					os << "        if (pfn != nullptr) cls->add_method(mname, pfn, parent->method_is_private(mname));\n";
					os << "      }\n";
				}

				// 3) 装饰器栈区基址：装饰器对象位于 MAKE_CLASS 前栈顶。
				os << "      std::size_t deco_base = st.size() - "
				   << cdef.decorator_count << ";\n";

				// 4) 成员变量（声明顺序）：带装饰器的成员经占位对象确定可见性。
				for (std::size_t i = 0; i < cdef.member_names.size(); ++i) {
					bool has_deco = i < cdef.member_decorators.size() &&
					                cdef.member_decorators[i] != UINT32_MAX;
					if (has_deco) {
						os << "      bool priv" << i << " = Pycp::ApplyDecoratorVisibility(st[deco_base + "
						   << cdef.member_decorators[i] << "], " << cpp_string_literal(file)
						   << ", " << lineno << ");\n";
						os << "      cls->add_member_name(" << cpp_string_literal(cdef.member_names[i])
						   << ", priv" << i << ");\n";
					} else {
						os << "      cls->add_member_name(" << cpp_string_literal(cdef.member_names[i])
						   << ", false);\n";
					}
				}

				// 5) 方法：构造 native 模式 BytecodeFunction（指向生成的函数），
				//    带装饰器则经装饰器替换，最后 add_method（含可见性）。
				for (std::size_t i = 0; i < cdef.methods.size(); ++i) {
					std::size_t co_idx = static_cast<std::size_t>(cdef.methods[i].second);
					const std::string& mname = cdef.methods[i].first;
					bool has_deco = i < cdef.method_decorators.size() &&
					                cdef.method_decorators[i] != UINT32_MAX;
					if (has_deco) {
						os << "      Pycp::BytecodeFunction* mfn" << i << " = new Pycp::BytecodeFunction("
						   << cpp_string_literal(module.code_objects[co_idx].name)
						   << ", " << Pycp::AOT_FN_PREFIX << co_idx << ", env);\n";
						os << "      Pycp::GC_Track(mfn" << i << ");\n";
						os << "      Pycp::Object* dm" << i << " = Pycp::ApplyDecorator(st[deco_base + "
						   << cdef.method_decorators[i] << "], mfn" << i << ", " << cpp_string_literal(file)
						   << ", " << lineno << ");\n";
						os << "      if (dm" << i << " == nullptr || !dm" << i << "->is_type(\"Function\")) {\n";
						os << "        Pycp::Decref(mfn" << i << "); if (dm" << i << ") Pycp::Decref(dm" << i << "); Pycp::Decref(cls);\n";
						os << "        throw Pycp::TypeError(" << cpp_string_literal(file) << ", "
						   << lineno << ", \"decorator must return a function.\");\n";
						os << "      }\n";
						os << "      Pycp::Decref(mfn" << i << ");\n";
						os << "      Pycp::BytecodeFunction* mfn2_" << i << " = static_cast<Pycp::BytecodeFunction*>(dm" << i << ");\n";
						os << "      cls->add_method(" << cpp_string_literal(mname)
						   << ", mfn2_" << i << ", mfn2_" << i << "->is_private());\n";
						os << "      Pycp::Decref(mfn2_" << i << ");\n";
					} else {
						os << "      Pycp::BytecodeFunction* mfn" << i << " = new Pycp::BytecodeFunction("
						   << cpp_string_literal(module.code_objects[co_idx].name)
						   << ", " << Pycp::AOT_FN_PREFIX << co_idx << ", env);\n";
						os << "      Pycp::GC_Track(mfn" << i << ");\n";
						os << "      cls->add_method(" << cpp_string_literal(mname)
						   << ", mfn" << i << ", mfn" << i << "->is_private());\n";
						os << "      Pycp::Decref(mfn" << i << ");\n";
					}
				}

				// 6) 弹出装饰器对象（每份栈引用 Decref）。
				os << "      for (std::size_t d = 0; d < " << cdef.decorator_count
				   << "; ++d) { Pycp::Object* deco = st.back(); st.pop_back(); if (deco) Pycp::Decref(deco); }\n";

				// 7) 压入类对象（新建 Class 为 Owned，由栈接管这 1 份引用）。
				os << "      st.push_back(cls); }\n";
				break;
			}

			case Pycp::BC::Op::CHECK_INT: {
				// 校验栈顶为 Integer，仅校验不弹栈（与 VM 语义一致）。
				os << "    { Pycp::Object* v = st.back();\n";
				os << "      if (v == nullptr || !v->is_type(\"Integer\"))\n";
				os << "        throw Pycp::TypeError(\"repeat range value must be an integer.\"); }\n";
				break;
			}
			case Pycp::BC::Op::CHECK_RANGE_DIRECTION: {
				// 校验 repeat 范围方向与步长符号不矛盾（与 VM 语义一致）。
				os << "    { Pycp::Object* so = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* bo = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* ao = st.back(); st.pop_back();\n";
				os << "      if (ao == nullptr || bo == nullptr || so == nullptr ||\n";
				os << "          !ao->is_type(\"Integer\") || !bo->is_type(\"Integer\") ||\n";
				os << "          !so->is_type(\"Integer\"))\n";
				os << "        throw Pycp::TypeError(\"repeat range value must be an integer.\");\n";
				os << "      int64_t av = static_cast<Pycp::Integer*>(ao)->get_value();\n";
				os << "      int64_t bv = static_cast<Pycp::Integer*>(bo)->get_value();\n";
				os << "      int64_t sv = static_cast<Pycp::Integer*>(so)->get_value();\n";
				os << "      Pycp::Decref(ao); Pycp::Decref(bo); Pycp::Decref(so);\n";
				os << "      if ((av < bv && sv < 0) || (av > bv && sv > 0))\n";
				os << "        throw Pycp::ValueError(\"repeat range step contradicts endpoints direction.\"); }\n";
				break;
			}

			case Pycp::BC::Op::GET_ITER: {
				// obj -> obj.__iterator__()（返回全新迭代器）。与 VM 语义一致：
				// 不可迭代时 __iterator__ 抛 TypeError。
				os << "    { Pycp::Object* obj = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* it = obj->__iterator__();\n";
				os << "      Pycp::Decref(obj);\n";
				os << "      st.push_back(it); }\n";
				break;
			}
			case Pycp::BC::Op::FOR_ITER: {
				// it -> it.__next__()：成功压入下一元素；StopIteration 则 goto 目标。
				// 仅本指令捕捉 StopIteration（foreach 默认），其他异常向外传播。
				long target = static_cast<long>(pc) + static_cast<long>(ins.operand);
				os << "    { Pycp::Object* it = st.back(); st.pop_back();\n";
				os << "      try {\n";
				os << "        Pycp::Object* elem = it->__next__();\n";
				os << "        Pycp::Decref(it);\n";
				os << "        st.push_back(elem);\n";
				os << "      } catch (const Pycp::StopIteration&) {\n";
				os << "        Pycp::Decref(it);\n";
				os << "        goto L_" << target << ";\n";
				os << "      }\n";
				os << "    }\n";
				break;
			}

			case Pycp::BC::Op::RETURN: {
				os << "    { Pycp::Object* ret = st.back(); st.pop_back();\n";
				os << "      pycp_cleanup_env(env, st);\n";
				os << "      return ret; }\n";
				break;
			}
			case Pycp::BC::Op::RETURN_NONE:
				os << "    pycp_cleanup_env(env, st);\n";
				os << "    return Pycp::None::instance;\n";
				break;

			case Pycp::BC::Op::HALT:
				os << "    pycp_cleanup_env(env, st);\n";
				os << "    return Pycp::None::instance;\n";
				break;

			default:
				throw std::runtime_error(
					"AOT: unknown opcode " +
					std::to_string(static_cast<int>(ins.op)));
		}
	}

	// 正常流结束兜底
	os << "    pycp_cleanup_env(env, st);\n";
	os << "    return Pycp::None::instance;\n";
	os << "}\n\n";
}

// 生成单个模块的 .cpp 内容（不含 main）。
//   is_entry : 是否入口模块（入口模块额外生成 main）。
//   all_deps : 全部依赖模块名列表（含入口自身为空串除外），供入口生成
//              被导入模块初始化函数的 extern 声明（跨文件链接）。
std::string emit_module_cpp(const Pycp::BC::Module& module,
                            const std::string& modname,
                            bool is_entry,
                            const std::string& entry_name,
                            const std::vector<std::string>& all_deps) {
	if (module.code_objects.empty()) {
		throw std::runtime_error("AOT: empty module (no code objects).");
	}

	std::ostringstream os;

	// ---- 文件头 ----
	os << "// =====================================================\n";
	os << "// Auto-generated by Pycp AOT compiler.\n";
	os << "// Source: " << module.source_path << "\n";
	os << "// Module: " << (modname.empty() ? std::string(Pycp::MODULE_ENTRY_NAME) : modname) << "\n";
	os << "// Do not edit manually.\n";
	os << "// =====================================================\n\n";

	// ---- 头文件 ----
	os << "#include \"PycpABI.hpp\"\n";
	os << "#include \"PycpGC.hpp\"\n";
	os << "#include \"PycpFunction.hpp\"\n";
	os << "#include \"PycpManager.hpp\"\n";
	os << "#include \"PycpNone.hpp\"\n";
	os << "#include \"PycpBoolean.hpp\"\n";
	os << "#include \"PycpInteger.hpp\"\n";
	os << "#include \"PycpString.hpp\"\n";
	os << "#include \"PycpModule.hpp\"\n";
	os << "#include \"PycpClass.hpp\"\n";
	os << "#include \"PycpList.hpp\"\n";
	os << "#include \"PycpBytecodeVM.hpp\"\n";
	os << "#include \"PycpNativeExt.hpp\"\n";
	os << "#include \"PycpException.hpp\"\n";
	os << "#include <vector>\n";
	os << "#include <string>\n";
	os << "#include <unordered_map>\n";
	os << "#include <memory>\n";
	os << "#include <iostream>\n";
	os << "#include <cstddef>\n\n";

	// ---- 被导入模块初始化函数的 extern 声明（仅入口模块需要跨文件链接）----
	if (is_entry) {
		for (const std::string& dep : all_deps) {
			os << "Pycp::Module* " << module_init_symbol(dep) << "();\n";
		}
		if (!all_deps.empty()) os << "\n";
	}

	// ---- 全局状态 ----
	os << "// 全局常量池对象（对应 .pycp 编译期常量池，模块初始化时预构造）\n";
	os << "static Pycp::Object* g_c[" << (module.const_pool.empty() ? 1 : module.const_pool.size()) << "];\n\n";
	os << "// 全局变量表（冗余保留；顶层变量实际写入 g_mod_ns 指向的模块命名空间）\n";
	os << "static std::unordered_map<std::string, Pycp::Object*> g_globals;\n\n";
	os << "// 模块命名空间指针（指向本模块 Module 的 namespace）\n";
	os << "static std::unordered_map<std::string, Pycp::Object*>* g_mod_ns = nullptr;\n\n";

	// ---- 内建库加载辅助（LOAD_MODULE 遇到非 .pycp 依赖时调用）----
	os << "// 加载内建库/动态库模块（io/Pycp/classtools 等），带缓存。\n";
	os << "static std::unordered_map<std::string, Pycp::Module*> g_native_mods;\n";
	os << "static Pycp::Module* pycp_load_native(const std::string& name) {\n";
	os << "    auto it = g_native_mods.find(name);\n";
	os << "    if (it != g_native_mods.end()) return it->second;\n";
	os << "    Pycp::Module* m = Pycp::LoadNativeModule(name, \".\");\n";
	os << "    if (m == nullptr) throw Pycp::ImportError(\"native module '\" + name + \"' not found\");\n";
	os << "    Pycp::GC_AddRoot(m);\n";
	os << "    g_native_mods[name] = m;\n";
	os << "    return m;\n";
	os << "}\n\n";

	// ---- 辅助函数：环境/栈清理 ----
	os << "static void pycp_cleanup_env(std::shared_ptr<Pycp::BC::Environment>& env, std::vector<Pycp::Object*>& st) {\n";
	os << "    for (Pycp::Object* v : env->locals) { if (v) Pycp::Decref(v); }\n";
	os << "    env->locals.clear();\n";
	os << "    for (Pycp::Object* v : st) Pycp::Decref(v);\n";
	os << "    st.clear();\n";
	os << "}\n\n";

	// ---- 前向声明（支持函数间相互引用，如嵌套闭包）----
	for (std::size_t i = 0; i < module.code_objects.size(); ++i) {
		os << "static Pycp::Object* " << Pycp::AOT_FN_PREFIX << i
		   << "(Pycp::Object* self, Pycp::Object** argv, std::size_t argc);\n";
	}
	os << "\n";

	// ---- 各代码对象翻译 ----
	std::set<std::string> deps(all_deps.begin(), all_deps.end());
	for (std::size_t i = 1; i < module.code_objects.size(); ++i) {
		emit_function(os, module, module.code_objects[i], i, deps);
	}
	emit_function(os, module, module.code_objects[0], 0, deps);

	// ---- 常量池预构造 ----
	os << "static void pycp_init_consts() {\n";
	for (std::size_t i = 0; i < module.const_pool.size(); ++i) {
		const auto& c = module.const_pool[i];
		switch (c.kind) {
			case Pycp::BC::ConstKind::INTEGER:
				os << "    g_c[" << i << "] = Pycp::Integer::FromLong("
				   << c.int_value << "LL);\n";
				break;
			case Pycp::BC::ConstKind::STRING:
				os << "    g_c[" << i << "] = Pycp::String::FromCString("
				   << cpp_string_literal(c.str_value) << ");\n";
				break;
			case Pycp::BC::ConstKind::NONE:
				os << "    g_c[" << i << "] = Pycp::None::instance;\n";
				os << "    Pycp::Incref(Pycp::None::instance);\n";
				break;
			default:
				throw std::runtime_error("AOT: unknown constant kind.");
		}
		os << "    Pycp::GC_AddRoot(g_c[" << i << "]);\n";
	}
	os << "}\n\n";

	// ---- 全局常量清理 ----
	os << "static void pycp_fini_consts() {\n";
	for (std::size_t i = 0; i < module.const_pool.size(); ++i) {
		os << "    Pycp::GC_RemoveRoot(g_c[" << i << "]);\n";
		os << "    Pycp::Decref(g_c[" << i << "]);\n";
	}
	os << "    for (auto& kv : g_globals) { if (kv.second) Pycp::Decref(kv.second); }\n";
	os << "    g_globals.clear();\n";
	os << "}\n\n";

	// ---- 模块初始化函数（非 static，供跨模块调用）----
	os << "// 初始化并返回本模块的 Module（懒执行，首次调用运行顶层）。\n";
	os << "Pycp::Module* " << module_init_symbol(modname) << "() {\n";
	os << "    static Pycp::Module* mod = nullptr;\n";
	os << "    static bool done = false;\n";
	os << "    if (!done) {\n";
	os << "        pycp_init_consts();\n";
	os << "        mod = Pycp::Module::New(" << cpp_string_literal(modname) << ");\n";
	os << "        Pycp::GC_AddRoot(mod);\n";
	os << "        g_mod_ns = mod->get_namespace();\n";
	os << "        Pycp::Object* r = " << Pycp::AOT_FN_PREFIX << "0(nullptr, nullptr, 0);\n";
	os << "        if (r) Pycp::Decref(r);\n";
	os << "        done = true;\n";
	os << "    }\n";
	os << "    return mod;\n";
	os << "}\n\n";

	// ---- 入口 main（仅入口模块生成）----
	if (is_entry) {
		os << "int " << entry_name << "() {\n";
		os << "    Pycp::Initialize();\n";
		os << "    try {\n";
		os << "        " << module_init_symbol(modname) << "();\n";
		os << "        pycp_fini_consts();\n";
		os << "        Pycp::Finalize();\n";
		os << "        return 0;\n";
		os << "    } catch (const Pycp::Exception& e) {\n";
		os << "        std::cerr << e.format() << std::endl;\n";
		os << "        return 1;\n";
		os << "    } catch (const std::exception& e) {\n";
		os << "        std::cerr << \"Error: \" << e.what() << std::endl;\n";
		os << "        return 1;\n";
		os << "    }\n";
		os << "}\n\n";

		os << "int main() {\n";
		os << "    return " << entry_name << "();\n";
		os << "}\n";
	}

	return os.str();
}

} // anonymous namespace

std::string EmitCpp(const Pycp::BC::Module& module,
                    const std::string& entry_name) {
	// 单模块模式：视为入口模块（模块名为空，无依赖）。
	std::vector<std::string> no_deps;
	return emit_module_cpp(module, "", /*is_entry=*/true, entry_name, no_deps);
}

std::map<std::string, std::string> EmitCppAll(
    const std::map<std::string, Pycp::BC::Module>& modules,
    const std::string& entry_name) {
	// 收集所有依赖模块名（除入口模块外的所有模块）。
	std::vector<std::string> all_deps;
	for (const auto& kv : modules) {
		if (kv.first != entry_name) all_deps.push_back(kv.first);
	}

	std::map<std::string, std::string> result;
	for (const auto& kv : modules) {
		const std::string& modname = kv.first;
		bool is_entry = (modname == entry_name);
		result[modname] = emit_module_cpp(kv.second, modname, is_entry,
		                                  Pycp::AOT_ENTRY_FN_NAME, all_deps);
	}
	return result;
}

bool EmitCppToFile(const Pycp::BC::Module& module, const std::string& path) {
	std::string src = EmitCpp(module);
	std::ofstream f(path, std::ios::binary);
	if (!f) return false;
	f << src;
	return f.good();
}

} // namespace Pycp::AOT