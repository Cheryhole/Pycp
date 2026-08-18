#include "PycpAot.hpp"
#include "PycpConfig.hpp"

#include <cstdio>
#include <fstream>
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
//     返回该模块的 ModuleObject*（懒执行，首次调用才运行顶层）。
//   - 入口模块的 .cpp 额外生成 main()，并在其 LOAD_MODULE 处调用被导入
//     模块的 pycp_module_<hash>()。
//   - 模块顶层变量写入 ModuleObject 的命名空间（经 g_mod_ns 指针），
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
				// 弹对象再压属性值，栈深不变
				break;
			case Pycp::BC::Op::STORE_ATTR:
				// 弹值 + 对象，栈深减 1
				if (depth > 0) --depth;
				break;
			case Pycp::BC::Op::MAKE_CLASS:
				++depth;
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
void emit_function(std::ostringstream& os, const Pycp::BC::Module& module,
                   const Pycp::BC::CodeObject& co, std::size_t co_idx) {
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
	os << "        Pycp::Closure* cl = static_cast<Pycp::Closure*>(self);\n";
	os << "        env->captured = cl->get_captured();\n";
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
				os << "    { Pycp::ModuleObject* m = "
				   << module_init_symbol(dep) << "();\n";
				os << "      Pycp::Incref(m); st.push_back(m); }\n";
				break;
			}
			case Pycp::BC::Op::GET_ATTR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				const std::string& attr = module.symtab[idx];
				os << "    { Pycp::Object* obj = st.back(); st.pop_back();\n";
				os << "      Pycp::Object* v = Pycp::Module_GetAttr("
				   << "static_cast<Pycp::ModuleObject*>(obj), "
				   << cpp_string_literal(attr) << ");\n";
				os << "      Pycp::Decref(obj);\n";
				os << "      st.push_back(v); Pycp::Incref(v); }\n";
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

			case Pycp::BC::Op::MAKE_FUNCTION: {
				std::size_t fidx = static_cast<std::size_t>(ins.operand);
				os << "    { Pycp::Closure* fn = new Pycp::Closure("
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
				os << "      Pycp::Object* ret = Pycp::Call(callee, args, "
				   << nargs << ");\n";
				os << "      for (std::size_t i = 0; i < " << nargs
				   << "; ++i) Pycp::Decref(args[i]);\n";
				os << "      Pycp::Decref(callee);\n";
				os << "      st.push_back(ret); }\n";
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
	os << "#include \"PycpInteger.hpp\"\n";
	os << "#include \"PycpString.hpp\"\n";
	os << "#include \"PycpModule.hpp\"\n";
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
			os << "Pycp::ModuleObject* " << module_init_symbol(dep) << "();\n";
		}
		if (!all_deps.empty()) os << "\n";
	}

	// ---- 全局状态 ----
	os << "// 全局常量池对象（对应 .pycp 编译期常量池，模块初始化时预构造）\n";
	os << "static Pycp::Object* g_c[" << (module.const_pool.empty() ? 1 : module.const_pool.size()) << "];\n\n";
	os << "// 全局变量表（冗余保留；顶层变量实际写入 g_mod_ns 指向的模块命名空间）\n";
	os << "static std::unordered_map<std::string, Pycp::Object*> g_globals;\n\n";
	os << "// 模块命名空间指针（指向本模块 ModuleObject 的 namespace）\n";
	os << "static std::unordered_map<std::string, Pycp::Object*>* g_mod_ns = nullptr;\n\n";

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
	for (std::size_t i = 1; i < module.code_objects.size(); ++i) {
		emit_function(os, module, module.code_objects[i], i);
	}
	emit_function(os, module, module.code_objects[0], 0);

	// ---- 常量池预构造 ----
	os << "static void pycp_init_consts() {\n";
	for (std::size_t i = 0; i < module.const_pool.size(); ++i) {
		const auto& c = module.const_pool[i];
		switch (c.kind) {
			case Pycp::BC::ConstKind::INTEGER:
				os << "    g_c[" << i << "] = Pycp::Integer_FromLong("
				   << c.int_value << "LL);\n";
				break;
			case Pycp::BC::ConstKind::STRING:
				os << "    g_c[" << i << "] = Pycp::String_FromString("
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
	os << "// 初始化并返回本模块的 ModuleObject（懒执行，首次调用运行顶层）。\n";
	os << "Pycp::ModuleObject* " << module_init_symbol(modname) << "() {\n";
	os << "    static Pycp::ModuleObject* mod = nullptr;\n";
	os << "    static bool done = false;\n";
	os << "    if (!done) {\n";
	os << "        pycp_init_consts();\n";
	os << "        mod = Pycp::Module_New(" << cpp_string_literal(modname) << ");\n";
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
