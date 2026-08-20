#include "PycpBytecodeVM.hpp"
#include "PycpABI.hpp"
#include "PycpGC.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpNone.hpp"
#include "PycpFunction.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpClass.hpp"
#include "PycpNativeExt.hpp"
#include "PycpList.hpp"

#include <vector>

namespace Pycp::BC {

// =============================================================
// VM 构造 / 析构
// =============================================================

VM::VM(Module* module, std::map<std::string, Module*>* registry,
       const std::string& entry_name)
	: module_(module), registry_(registry) {
	global_env_ = std::make_shared<Environment>();

	// 入口模块占坑：创建其 Pycp::Module 并放入缓存，其命名空间即入口顶层
	// globals。这样循环导入时被依赖模块反向 import 入口模块会命中缓存，
	// 拿到「部分初始化」的入口模块，而非重新执行（避免无限递归）。
	if (module_ != nullptr) {
		entry_mod_ = Pycp::Module::New(entry_name.empty() ? Pycp::MODULE_ENTRY_NAME : entry_name);
		GC_AddRoot(entry_mod_);
		module_cache_[entry_name] = entry_mod_;
		global_env_->globals = entry_mod_->get_namespace();
	} else {
		global_env_->globals = new std::unordered_map<std::string, Object*>();
	}

	// 本版起不注入任何内建函数（命名空间仅含用户定义内容）。

	// 若 runtime_consts 为空（编译路径 Compile 未填充），则从 const_pool 构建。
	// 反序列化路径 Deserialize 已填充，则跳过。
	if (module_ && module_->runtime_consts.empty() && !module_->const_pool.empty()) {
		module_->runtime_consts.reserve(module_->const_pool.size());
		for (const auto& c : module_->const_pool) {
			switch (c.kind) {
				case ConstKind::INTEGER:
					module_->runtime_consts.push_back(Integer::FromLong(c.int_value));
					break;
				case ConstKind::STRING:
					module_->runtime_consts.push_back(String::FromCString(c.str_value.c_str()));
					break;
				case ConstKind::NONE:
					module_->runtime_consts.push_back(None::instance);
					Incref(None::instance);
					break;
				default:
					throw VMError("unknown constant kind.");
				}
				GC_AddRoot(module_->runtime_consts.back());
				}
				}
}

VM::~VM() {
	// 第一步：显式断开所有模块命名空间的引用（清空 map 并 Decref 值），
	// 打破模块间可能形成的循环引用（A import B 且 B import A），避免
	// 后续 Decref Module 时因环导致连锁析构 / 双重释放。
	for (auto& kv : module_cache_) {
		if (kv.second == nullptr) continue;
		auto* ns = kv.second->get_namespace();
		if (ns != nullptr) {
			for (auto& p : *ns) {
				if (p.second != nullptr) Decref(p.second);
			}
			ns->clear();
		}
	}

	// 第二步：释放各模块对象（namespace 已空，析构不会再连锁）。
	for (auto& kv : module_cache_) {
		if (kv.second != nullptr) {
			GC_RemoveRoot(kv.second);
			Decref(kv.second);
		}
	}
	module_cache_.clear();

	// 仅当入口 Module 未接管 globals（entry_mod_ 为空）时，
	// global_env_->globals 才是独立 new 出的 map，需要此处释放。
	if (global_env_ && global_env_->globals && entry_mod_ == nullptr) {
		for (auto& kv : *global_env_->globals) {
			Decref(kv.second);
		}
		delete global_env_->globals;
		global_env_->globals = nullptr;
	}
	// 清理 runtime_consts 的 GC root（这些常量对象生命周期绑定到 Module）。
	// 同时清理 import 子模块的 runtime_consts（load_module 中按需构建）。
	if (registry_ != nullptr) {
		for (auto& kv : *registry_) {
			if (kv.second == nullptr) continue;
			for (Object* c : kv.second->runtime_consts) {
				if (c != nullptr) {
					GC_RemoveRoot(c);
					Decref(c);
				}
			}
			kv.second->runtime_consts.clear();
		}
	} else if (module_) {
		for (Object* c : module_->runtime_consts) {
			if (c != nullptr) {
				GC_RemoveRoot(c);
				Decref(c);
			}
		}
		module_->runtime_consts.clear();
	}
}

Object* VM::run() {
	if (module_ == nullptr || module_->code_objects.empty())
		throw VMError("empty module.");
	// 顶层代码对象即 code_objects[0]
	CodeObject* top = &module_->code_objects[0];
	std::shared_ptr<Environment> env = std::make_shared<Environment>();
	env->globals = global_env_->globals;
	// 顶层视为一个无参函数，locals 用于存储全局代码中的临时（实际全部走 globals）
	return execute(top, env, nullptr, 0);
}

Pycp::Module* VM::load_module(const std::string& name) {
	// 命中缓存直接返回
	auto it = module_cache_.find(name);
	if (it != module_cache_.end()) {
		return it->second;
	}

	// 尝试加载动态库扩展（stdlib/io.so、脚本目录 foo.so 等）。
	// 搜索目录取入口文件所在目录（与 .pycp 的 resolve 目录一致）。
	{
		std::string search_dir;
		if (module_ != nullptr && !module_->source_path.empty()) {
			std::size_t slash = module_->source_path.find_last_of("/\\");
			if (slash != std::string::npos)
				search_dir = module_->source_path.substr(0, slash);
		}
		Pycp::Module* extmod = LoadNativeModule(name, search_dir);
		if (extmod != nullptr) {
			GC_AddRoot(extmod);
			module_cache_[name] = extmod;
			return extmod;
		}
	}

	// 未命中：需从注册表查找模块并执行其顶层
	if (registry_ == nullptr) {
		throw ImportError("No module named '" + name + "'");
	}
	auto mit = registry_->find(name);
	if (mit == registry_->end() || mit->second == nullptr) {
		throw ImportError("No module named '" + name + "'");
	}
	Module* sub_module = mit->second;

	// 先占坑：创建 Pycp::Module 并放入缓存（支持循环导入——执行子模块时
	// 若其反向 import 本模块名，会命中这个未填充完的对象而非无限递归）。
	Pycp::Module* modobj = Pycp::Module::New(name);
	GC_AddRoot(modobj);
	module_cache_[name] = modobj;

	// 为子模块构造执行环境：其顶层 globals 指向 Module 的命名空间。
	// 本版起不注入任何内建函数。
	std::shared_ptr<Environment> sub_global = std::make_shared<Environment>();
	sub_global->globals = modobj->get_namespace();

	// 子模块顶层执行需要一个 VM 上下文。为复用 execute 循环，
	// 用同一个 VM 实例（this）执行子模块 code_objects[0]，
	// 但 execute 内部读取 module_->source_path / runtime_consts，
	// 因此需要临时切换 module_ 指针。
	Module* saved_module = module_;
	module_ = sub_module;
	try {
		if (sub_module->code_objects.empty()) {
			throw VMError("empty imported module: " + name);
		}

		// 确保子模块 runtime_consts 已构建（编译路径 Compile 未填充）。
		if (sub_module->runtime_consts.empty() && !sub_module->const_pool.empty()) {
			sub_module->runtime_consts.reserve(sub_module->const_pool.size());
			for (const auto& c : sub_module->const_pool) {
				switch (c.kind) {
					case ConstKind::INTEGER:
						sub_module->runtime_consts.push_back(Integer::FromLong(c.int_value));
						break;
					case ConstKind::STRING:
						sub_module->runtime_consts.push_back(String::FromCString(c.str_value.c_str()));
						break;
					case ConstKind::NONE:
						sub_module->runtime_consts.push_back(None::instance);
						Incref(None::instance);
						break;
					default:
						throw VMError("unknown constant kind.");
				}
				GC_AddRoot(sub_module->runtime_consts.back());
			}
		}

		CodeObject* top = &sub_module->code_objects[0];
		std::shared_ptr<Environment> env = std::make_shared<Environment>();
		env->globals = sub_global->globals;
		Object* ret = execute(top, env, nullptr, 0);
		if (ret != nullptr) Decref(ret);
	} catch (...) {
		// 失败回滚：从缓存移除并释放，避免留下坏模块。
		module_ = saved_module;
		module_cache_.erase(name);
		GC_RemoveRoot(modobj);
		Decref(modobj);
		throw;
	}
	module_ = saved_module;

	return modobj;
}

Object* VM::call(Module* m, size_t co_idx, Object** argv, std::size_t argc,
                 std::shared_ptr<Environment> captured) {
	if (m == nullptr || co_idx >= m->code_objects.size())
		throw VMError("invalid code object index.");
	CodeObject* co = &m->code_objects[co_idx];

	// 跨模块调用：临时切换 module_ 到函数所属模块，使 execute 内部
	// 读取正确的 symtab / source_path / runtime_consts。
	Module* saved_module = module_;
	module_ = m;

	std::shared_ptr<Environment> env = std::make_shared<Environment>();
	// 全局环境指向函数所属模块的 globals。
	// 顶层入口模块用 global_env_；被导入模块的顶层函数需用其模块命名空间。
	env->globals = global_env_->globals;
	env->captured = captured;
	// 局部变量：参数名 + 编译期确定的其他局部名
	env->local_names = co->names;
	env->locals.assign(co->nlocals, nullptr);

	// 方法体执行期间置内部访问标志，放行 private 成员的 self 访问；
	// 同时若首参数为实例（self），压入当前 self 上下文供 super() 使用。
	enter_internal_access();
	bool pushed_self = false;
	if (argc > 0 && argv != nullptr && argv[0] != nullptr &&
	    dynamic_cast<Instance*>(argv[0]) != nullptr) {
		push_current_self(static_cast<Instance*>(argv[0]));
		pushed_self = true;
	}
	Object* ret = nullptr;
	try {
		ret = execute(co, env, argv, argc);
	} catch (...) {
		if (pushed_self) pop_current_self();
		leave_internal_access();
		module_ = saved_module;
		throw;
	}
	if (pushed_self) pop_current_self();
	leave_internal_access();
	module_ = saved_module;
	return ret;
}

// =============================================================
// 执行循环核心
// =============================================================

Object* VM::execute(CodeObject* co,
                    std::shared_ptr<Environment> env,
                    Object** argv, std::size_t argc) {
	std::vector<Object*> stack;
	stack.reserve(64);

	// 参数绑定到局部槽（locals[0..nparams)）
	for (std::size_t i = 0; i < argc && i < env->locals.size(); ++i) {
		env->locals[i] = argv[i];
		if (argv[i] != nullptr) Incref(argv[i]);
	}

	std::size_t dbg_pc = 0;

	// 当前源码位置：文件路径与行号（不含函数名）。
	auto cur_file = [&]() -> std::string { return module_->source_path; };
	auto cur_line = [&]() -> int {
		if (dbg_pc < co->linenos.size() && co->linenos[dbg_pc] > 0) {
			return co->linenos[dbg_pc];
		}
		return -1;
	};

	auto push = [&](Object* v) {
		stack.push_back(v);
		if (v != nullptr) Incref(v);
	};
	auto pop = [&]() -> Object* {
		if (stack.empty())
			throw VMError(cur_file(), cur_line(),
			              "stack underflow at pc=" + std::to_string(dbg_pc));
		Object* v = stack.back();
		stack.pop_back();
		return v;
	};

	// 局部变量名表：编译期已填入 co->names（参数 + 局部名，顺序与 nlocals 对齐）
	// 常量池运行时对象（反序列化后已 GC root）
	const std::vector<Object*>& consts = module_->runtime_consts;

	const std::size_t pc_end = co->code.size();
	try {
	for (std::size_t pc = 0; pc < pc_end; ++pc) {
		dbg_pc = pc;
		const Instruction& ins = co->code[pc];

		switch (ins.op) {
			// ---- 加载 / 存储 ----
			case Op::LOAD_CONST: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= consts.size())
					throw VMError(cur_file(), cur_line(), "constant index out of range.");
				push(consts[idx]);
				break;
			}
			case Op::LOAD_NONE:
				push(None::instance);
				break;

			case Op::LOAD_VAR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw VMError(cur_file(), cur_line(), "symbol index out of range.");
				const std::string& name = module_->symtab[idx];

				// 统一经 ABI 环境接口查找（局部 -> captured 链 -> 全局）
				Object* v = Environment_Lookup(env.get(), name);
				if (v == nullptr)
					throw NameError(cur_file(), cur_line(), "name '" + name + "' is not defined");
				push(v);
				break;
			}

			case Op::STORE_VAR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw VMError(cur_file(), cur_line(), "symbol index out of range.");
				const std::string& name = module_->symtab[idx];
				Object* value = pop(); // 所有权

				// 统一经 ABI 环境接口存储（局部 -> captured 链 -> 全局）
				Environment_Store(env.get(), name, value);
				break;
			}

			case Op::POP_TOP: {
				Object* v = pop();
				Decref(v);
				break;
			}
			case Op::DUP_TOP: {
				if (stack.empty())
					throw VMError(cur_file(), cur_line(), "stack underflow.");
				push(stack.back());
				break;
			}

			// ---- 运算 ----
			case Op::BINARY_ADD: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Add(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::BINARY_SUB: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Sub(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::BINARY_MUL: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Mul(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::BINARY_DIV: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Div(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::BINARY_POW: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Pow(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::UNARY_NEG: {
				Object* v = pop();
				Object* res = v->__negation__();
				Decref(v);
				push(res); Decref(res);
				break;
			}

			// ---- 比较 ----
			case Op::COMPARE_OP: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Compare(lhs, rhs, static_cast<int>(ins.operand));
				Decref(lhs); Decref(rhs);
				push(res);
				break;
			}

			// ---- 控制流 ----
			case Op::JUMP: {
				pc = pc + static_cast<std::size_t>(ins.operand) - 1;
				if (pc >= pc_end)
					throw VMError(cur_file(), cur_line(), "jump out of range.");
				break;
			}
			case Op::JUMP_IF_FALSE: {
				Object* cond = pop();
				bool f = IsFalse(cond);
				Decref(cond);
				if (f) {
					pc = pc + static_cast<std::size_t>(ins.operand) - 1;
					if (pc >= pc_end)
						throw VMError(cur_file(), cur_line(), "jump out of range.");
				}
				break;
			}
			case Op::JUMP_IF_TRUE: {
				Object* cond = pop();
				bool f = IsFalse(cond);
				Decref(cond);
				if (!f) {
					pc = pc + static_cast<std::size_t>(ins.operand) - 1;
					if (pc >= pc_end)
						throw VMError(cur_file(), cur_line(), "jump out of range.");
				}
				break;
			}

			// ---- 函数与调用 ----
			case Op::MAKE_FUNCTION: {
				std::size_t fidx = static_cast<std::size_t>(ins.operand);
				if (fidx >= module_->code_objects.size())
					throw VMError(cur_file(), cur_line(), "function index out of range.");
				BytecodeFunction* fn = new BytecodeFunction(this, module_, fidx, env);
				GC_Track(fn);
				push(fn);
				// 函数对象所有权归栈顶，函数指针由 push 持有
				Decref(fn); // push 已 Incref，释放"新建 Owned"这 1 份
				break;
			}

			case Op::MAKE_CLASS: {
				std::size_t cidx = static_cast<std::size_t>(ins.operand);
				if (cidx >= module_->classes.size())
					throw VMError(cur_file(), cur_line(), "class index out of range.");
				const ClassDef& cdef = module_->classes[cidx];

				Class* cls = New<Class>(cdef.name);

				// 继承：查找父类，复制其成员与方法（子类同名覆盖）。
				if (!cdef.parent_name.empty()) {
					Object* parent_obj = nullptr;
					// 父类引用：单标识符（全局变量）或属性访问路径（模块.类）。
					std::size_t dot = cdef.parent_name.find('.');
					if (dot == std::string::npos) {
						parent_obj = Environment_Lookup(env.get(), cdef.parent_name);
					} else {
						// 路径 A.B：先查 A（模块），再从模块取属性 B。
						std::string mod_name = cdef.parent_name.substr(0, dot);
						std::string attr_name = cdef.parent_name.substr(dot + 1);
						Object* mod_obj = Environment_Lookup(env.get(), mod_name);
						if (mod_obj != nullptr && dynamic_cast<Pycp::Module*>(mod_obj) != nullptr) {
							Pycp::Module* mo = static_cast<Pycp::Module*>(mod_obj);
							parent_obj = mo->__getattr__(attr_name);
						}
						}
						if (parent_obj == nullptr || dynamic_cast<Class*>(parent_obj) == nullptr) {
						Decref(cls);
						throw NameError(cur_file(), cur_line(),
						                "parent class '" + cdef.parent_name + "' is not defined");
						}
						Class* parent = static_cast<Class*>(parent_obj);
					cls->set_parent(parent);
					// 复制父类成员（可见性一并复制）。
					for (const auto& mn : parent->get_member_names()) {
						cls->add_member_name(mn, parent->member_is_private(mn));
					}
					// 复制父类方法（复用父类方法对象，add_method 内部 Incref）。
					for (const auto& mname : parent->method_names()) {
						Function* pfn = parent->find_method(mname);
						if (pfn != nullptr) {
							cls->add_method(mname, pfn, parent->method_is_private(mname));
						}
					}
				}

				// 装饰器对象栈区基址：装饰器对象在 MAKE_CLASS 之前按
				// 「先成员变量、后方法」顺序求值压栈，位于当前栈顶。
				// 第 idx 个装饰器 = stack[stack.size() - decorator_count + idx]。
				const std::size_t deco_base =
					stack.size() - static_cast<std::size_t>(cdef.decorator_count);

				// 成员变量名（声明顺序）。带装饰器的成员调用装饰器函数
				// （传一个临时占位对象，装饰器设置其可见性后返回），从
				// 返回对象读 is_private() 得到可见性。成员变量本身无独立
				// 运行时值对象（初始值经 __init_defaults__ 实例化时赋值），
				// 故仅用占位对象确定可见性。
				for (std::size_t i = 0; i < cdef.member_names.size(); ++i) {
					bool priv = false;
					if (i < cdef.member_decorators.size() &&
					    cdef.member_decorators[i] != UINT32_MAX) {
						std::size_t didx = deco_base + cdef.member_decorators[i];
						if (didx >= stack.size()) {
							Decref(cls);
							throw VMError(cur_file(), cur_line(), "decorator stack index out of range.");
						}
						Object* deco = stack[didx];
						priv = Pycp::ApplyDecoratorVisibility(deco, cur_file(), cur_line());
					}
					cls->add_member_name(cdef.member_names[i], priv);
				}
				// 方法：从 code_objects 构造 BytecodeFunction，闭包捕获当前环境。
				for (std::size_t i = 0; i < cdef.methods.size(); ++i) {
					const auto& m = cdef.methods[i];
					std::size_t co_idx = static_cast<std::size_t>(m.second);
					if (co_idx >= module_->code_objects.size()) {
						Decref(cls);
						throw VMError(cur_file(), cur_line(), "method code index out of range.");
					}
					BytecodeFunction* fn = new BytecodeFunction(this, module_, co_idx, env);
					GC_Track(fn);
					if (i < cdef.method_decorators.size() &&
					    cdef.method_decorators[i] != UINT32_MAX) {
						std::size_t didx = deco_base + cdef.method_decorators[i];
						if (didx >= stack.size()) {
							Decref(cls);
							Decref(fn);
							throw VMError(cur_file(), cur_line(), "decorator stack index out of range.");
						}
						Object* deco = stack[didx];
						// 装饰器作为函数：把方法对象传给它，用返回值替换。
						Object* decorated = Pycp::ApplyDecorator(deco, fn, cur_file(), cur_line());
						// 替换方法：释放原 fn，接管 decorated（须为 Function）。
						if (decorated == nullptr || !decorated->is_type("Function")) {
							Decref(cls);
							Decref(fn);
							if (decorated != nullptr) Decref(decorated);
							throw TypeError(cur_file(), cur_line(),
							                "decorator must return a function.");
						}
						Decref(fn);
						fn = static_cast<BytecodeFunction*>(decorated);
					}
					cls->add_method(m.first, fn, fn->is_private()); // add_method 内部 Incref
					Decref(fn);                          // 释放新建 Owned
				}
				// 弹出装饰器对象（每个 pop 出一份栈上引用，需 Decref 释放）。
				for (std::size_t d = 0; d < cdef.decorator_count; ++d) {
					Object* deco = pop();
					if (deco != nullptr) Decref(deco);
				}
				push(cls);
				Decref(cls); // push 已 Incref
				break;
			}

			case Op::CALL: {
				std::size_t nargs = static_cast<std::size_t>(ins.operand);
				if (stack.size() < nargs + 1)
					throw VMError(cur_file(), cur_line(), "call stack underflow.");

				std::vector<Object*> args(nargs);
				for (std::size_t i = 0; i < nargs; ++i)
					args[nargs - 1 - i] = pop();
				Object* callee = pop();

				Object* ret = nullptr;
				if (callee->is_type("Function")) {
					Function* fn = static_cast<Function*>(callee);
					ret = fn->invoke(args.data(), nargs);
				} else if (dynamic_cast<Class*>(callee) != nullptr) {
					// 实例构造：默认创建 Instance（__init_defaults__ +
					// __initialize__），内置类型类（BuiltinTypeClass）则直接
					// 返回内置对象。具体行为由 instantiate 多态分派。
					Class* cls = static_cast<Class*>(callee);
					ret = cls->instantiate(args.data(), nargs);
				} else {
					for (Object* a : args) Decref(a);
					Decref(callee);
					throw TypeError(cur_file(), cur_line(), "object is not callable.");
				}

				for (Object* a : args) Decref(a);
				Decref(callee);
				push(ret);
				Decref(ret);
				break;
			}

			case Op::RETURN: {
				Object* ret = pop();
				for (Object* v : env->locals) if (v) Decref(v);
				env->locals.clear();
				for (Object* v : stack) Decref(v);
				return ret;
			}
			case Op::RETURN_NONE: {
				for (Object* v : env->locals) if (v) Decref(v);
				env->locals.clear();
				for (Object* v : stack) Decref(v);
				return None::instance;
			}

			case Op::LOAD_MODULE: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->imports.size())
					throw VMError(cur_file(), cur_line(), "import index out of range.");
				const std::string& modname = module_->imports[idx];
				Pycp::Module* modobj = load_module(modname);
				push(modobj);
				break;
			}

			case Op::GET_ATTR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw VMError(cur_file(), cur_line(), "symbol index out of range.");
				const std::string& attr = module_->symtab[idx];
				Object* obj = pop(); // 模块对象
				if (obj == nullptr || dynamic_cast<Pycp::Module*>(obj) == nullptr) {
					if (obj != nullptr) Decref(obj);
					throw TypeError(cur_file(), cur_line(), "attribute access on non-module object.");
				}
				Object* val = Pycp::Module::GetAttr(static_cast<Pycp::Module*>(obj), attr);
				Decref(obj);
				push(val);
				break;
			}

			// ---- 通用属性访问（实例 / 类 / 模块 / 文件）----
			case Op::LOAD_ATTR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw VMError(cur_file(), cur_line(), "symbol index out of range.");
				const std::string& attr = module_->symtab[idx];
				Object* obj = pop(); // 被访问对象
				// GetAttr 返回 Owned（实例方法返回 BoundMethod），未找到抛 AttributeError。
				Object* val = GetAttr(obj, attr);
				Decref(obj);
				push(val);
				Decref(val); // push 已 Incref，释放 Owned 这份
				break;
			}

			case Op::STORE_ATTR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw VMError(cur_file(), cur_line(), "symbol index out of range.");
				const std::string& attr = module_->symtab[idx];
				Object* value = pop(); // 待写入的值（栈持有引用）
				Object* obj = pop();   // 目标对象
				// SetAttr -> __setattr__ 内部按需 Incref 存入；此处释放
				// value 从栈 pop 带来的引用（与 STORE_VAR 的 Environment_Store
				// 接管语义一致，避免字段持有后栈引用泄漏）。
				SetAttr(obj, attr, value);
				Decref(value);
				Decref(obj);
				break;
			}

			// ---- list 字面量与下标运算 ----
			case Op::BUILD_LIST: {
				// 操作数 = 元素个数。从栈顶依次弹出 n 个元素（栈序逆序），
				// 构造 List。
				std::size_t n = static_cast<std::size_t>(ins.operand);
				if (stack.size() < n)
					throw VMError(cur_file(), cur_line(), "build list stack underflow.");
				std::vector<Object*> elems(n);
				for (std::size_t i = 0; i < n; ++i)
					elems[n - 1 - i] = pop();
				// 将元素所有权转交给 list（append 内部 Incref），随后释放
				// 弹出的 n 份栈引用。
				List* list = New<List>();
				for (Object* e : elems) {
					list->append(e);
					Decref(e);
				}
				push(list);
				Decref(list); // push 已 Incref
				break;
			}

			case Op::GET_ITEM: {
				Object* key = pop();
				Object* obj = pop();
				// GetItem 返回 Borrowed（list 元素由 list 持有）。
				// push(res) 使栈持有 1 份引用（供后续 CALL 参数释放或 POP_TOP）；
				// 故此处【不】Decref(res)，避免把 list 持有的引用也减掉。
				Object* res = GetItem(obj, key);
				Decref(obj);
				Decref(key);
				push(res);
				break;
			}

			case Op::SET_ITEM: {
				Object* value = pop();
				Object* key = pop();
				Object* obj = pop();
				Object* res = SetItem(obj, key, value);
				Decref(obj);
				Decref(key);
				Decref(value);
				if (res != nullptr) {
					Decref(res);
				}
				break;
			}

			case Op::HALT: {
				for (Object* v : stack) Decref(v);
				return None::instance;
			}

			default:
				throw VMError(cur_file(), cur_line(), "unknown opcode.");
		}
	}

	for (Object* v : env->locals) if (v) Decref(v);
	env->locals.clear();
	for (Object* v : stack) Decref(v);
	return None::instance;
	}
	catch (const Exception& e) {
		// 若异常已带位置信息（VM 内用 VMError/NameError/TypeError 等带
		// file/lineno 构造），直接重抛；否则为 ABI/运行时底层抛出的错误
		// （如除零、类型错误），补充当前位置后重抛。底层异常的类别标签
		// （如 "ValueError: "）已内嵌在 what() 中，故原样保留。
		if (e.file.empty()) {
			throw Exception(cur_file(), cur_line(), e.what());
		}
		throw;
	}
}

} // namespace Pycp::BC

// =============================================================
// BytecodeFunction 实现（namespace Pycp）
// =============================================================

namespace Pycp {

BytecodeFunction::BytecodeFunction(BC::VM* vm_, BC::Module* module_,
                                   std::size_t code_idx_,
                                   std::shared_ptr<BC::Environment> captured_)
	: Function(""), vm(vm_), module(module_), code_idx(code_idx_),
	  captured(std::move(captured_)) {
	// 标记为 Bytecode 种类，并取函数名
	kind = FunctionKind::Bytecode;
	if (module && code_idx < module->code_objects.size()) {
		name = module->code_objects[code_idx].name.c_str();
	}
}

// native 模式构造：AOT 产物使用，不依赖 VM。native_fn 指向生成的
// pycp_fn_N（签名恰为 PycpNativeFunction），invoke 直接调用它。
BytecodeFunction::BytecodeFunction(const std::string& name_,
                                   PycpNativeFunction native_fn,
                                   std::shared_ptr<BC::Environment> captured_)
	: Function(""), vm(nullptr), module(nullptr), code_idx(0),
	  captured(std::move(captured_)), native_fn_(native_fn) {
	kind = FunctionKind::Bytecode;
	name = name_.c_str();
}

Object* BytecodeFunction::invoke(Object** argv, std::size_t argc) {
	if (native_fn_ != nullptr) {
		// native 模式：self 传 this，使生成的 pycp_fn_N 能经 get_captured 取捕获环境。
		return native_fn_(this, argv, argc);
	}
	return vm->call(module, code_idx, argv, argc, captured);
}

} // namespace Pycp
