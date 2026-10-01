#include "vm/PycpBytecodeVM.hpp"
#include "abi/PycpABI.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpExtension.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpMap.hpp"

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
		// 入口模块 __name__ 规范值为 "__main__"（命名空间不注入 __name__，
		// 裸名经 LOAD_VAR 回退到 pycp.__name__ 即当前模块名）。
		entry_mod_->set_module_name("__main__");
		// 入口本身是包（`pycp -m pkg`）：标记包模块并登记为当前包，
		// 使清单顶层执行期间的 moduletools.this() 指向它。
		if (package_names_.find(entry_name) != package_names_.end()) {
			entry_mod_->set_package(true);
			Pycp::SetCurrentPackage(entry_mod_);
		}
		module_cache_[entry_name] = entry_mod_;
		global_env_->globals = entry_mod_->get_namespace();
		// 入口函数调用时的 globals = 入口模块命名空间（与 global_env_ 同一 map）。
		bc_owner_[module_] = entry_mod_;
	} else {
		// REPL（无源码模块）：同样创建「入口模块」承载全局命名空间，使
		// @private/@readonly 的绑定级属性（MARK_BINDING）与只读绑定覆盖检查
		// （Environment_Store）有权威登记处——否则 REPL 中 `@readonly a = 1`
		// 之后 `a = 2` 不会被拒绝。占坑键用 MODULE_ENTRY_NAME（该串不是合法
		// 模块名，不会与 import 冲突），使 VM 析构能随 module_cache_ 一并
		// 清理命名空间内的全局值。
		entry_mod_ = Pycp::Module::New(Pycp::MODULE_ENTRY_NAME);
		GC_AddRoot(entry_mod_);
		entry_mod_->set_module_name("__main__");
		module_cache_[Pycp::MODULE_ENTRY_NAME] = entry_mod_;
		global_env_->globals = entry_mod_->get_namespace();
		// REPL 无源文件：向全局命名空间注入 __name__ = "__main__"（原行为）。
		(*global_env_->globals)["__name__"] = Pycp::String::FromCString("__main__");
	}

	// 登记 globals -> entry_mod_：Environment_Store 的只读绑定检查与
	// MARK_BINDING（@private/@readonly 声明）都经该映射查所属模块，
	// 文件模式与 REPL 模式共用（REPL 此前缺失该登记，导致 @readonly 失效）。
	Pycp::BindGlobalsModule(global_env_->globals, entry_mod_);

	// 设置脚本所在目录，作为 import 查找第 3 层（cwd 之后）的候选目录。
	// 取入口模块源路径的目录部分；REPL（module_ == nullptr）无脚本目录，
	// 此时该候选位置不生效。
	if (module_ != nullptr && !module_->source_path.empty()) {
		const std::string& sp = module_->source_path;
		std::size_t slash = sp.find_last_of("/\\");
		Pycp::SetModuleSearchDir(slash == std::string::npos
			? std::string(".") : sp.substr(0, slash));
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
				case ConstKind::FLOAT:
					module_->runtime_consts.push_back(New<Float>(c.float_value));
					break;
				case ConstKind::DECIMAL:
					module_->runtime_consts.push_back(New<Decimal>(c.str_value));
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

	// 初始化当前模块名回退缓存（GC root，VM 持有所有权）。
	name_fallback_ = Pycp::GetCurrentModuleName();
	Pycp::GC_AddRoot(name_fallback_);
}

VM::~VM() {
	// 释放当前模块名回退缓存（先移除 GC root，再 Decref 归零）。
	if (name_fallback_ != nullptr) {
		Pycp::GC_RemoveRoot(name_fallback_);
		Pycp::Decref(name_fallback_);
		name_fallback_ = nullptr;
	}
	// 第一步：显式断开各模块命名空间的引用（清空 map 并 Decref 值），打破
	// 模块间可能形成的循环引用（A import B 且 B import A），避免后续
	// Decref Module 时因环导致连锁析构 / 双重释放。
	for (auto& kv : module_cache_) {
		if (kv.second == nullptr) continue;
		auto* ns = kv.second->get_namespace();
		if (ns != nullptr) {
			for (auto& p : *ns) {
				if (p.second != nullptr) {
					Decref(p.second);
				}
			}
			ns->clear();
		}
	}

	// 第二步：解除各模块对象在 GC 中的 root 标记。
	// 注意：被 import 的用户模块对象，其引用计数已由第一步
	// （清空入口模块 namespace 时对全局变量值 Decref）归零并释放，
	// 因此此处【不可再 Decref】，否则会对已释放对象二次释放。
	// 仅入口模块 entry_mod_ 不被任何全局变量持有，需在此显式 Decref 释放。
	// native 扩展模块（Pycp/io/classtools）由 ImportModule 常驻进程，其
	// 引用最终在 Finalize 的 GC_Collect 中随 root 移除而回收。
	for (auto& kv : module_cache_) {
		if (kv.second != nullptr) {
			GC_RemoveRoot(kv.second);
		}
	}
	if (entry_mod_ != nullptr) {
		Decref(entry_mod_);
	}
	module_cache_.clear();
	bc_owner_.clear();

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

	// 释放由 exec_module 接管的 Module（REPL 场景）。其 runtime_consts
	// 在 exec_module 末尾有意保留（供闭包后续调用），此处统一清理再释放。
	for (Module* om : owned_modules_) {
		for (Object* c : om->runtime_consts) {
			if (c != nullptr) {
				GC_RemoveRoot(c);
				Decref(c);
			}
		}
		om->runtime_consts.clear();
		delete om;
	}
	owned_modules_.clear();
}

void VM::set_current_module(Pycp::Module* m) {
	Pycp::current_module_ = m;
	Pycp::Object* nw = Pycp::GetCurrentModuleName();   // Owned（refcount 1）
	Pycp::GC_RemoveRoot(name_fallback_);
	Pycp::Decref(name_fallback_);
	Pycp::GC_AddRoot(nw);
	name_fallback_ = nw;                                // VM 持有所有权
}

Object* VM::run() {
	if (module_ == nullptr || module_->code_objects.empty())
		throw VMError("empty module.");
	// 顶层代码对象即 code_objects[0]
	CodeObject* top = &module_->code_objects[0];
	std::shared_ptr<Environment> env = std::make_shared<Environment>();
	env->globals = global_env_->globals;
	// 登记 globals -> entry_mod_（模块只读绑定覆盖检查用）。
	Pycp::BindGlobalsModule(env->globals, entry_mod_);
	// 入口顶层执行期间：当前模块 = entry_mod_，供 pycp.__name__ 回退。
	Pycp::Module* saved_current = Pycp::current_module_;
	set_current_module(entry_mod_);
	try {
		Object* ret = execute(top, env, nullptr, 0);
		set_current_module(saved_current);
		return ret;
	} catch (...) {
		set_current_module(saved_current);
		throw;
	}
}

Object* VM::exec_module(Module* m) {
	if (m == nullptr || m->code_objects.empty())
		throw VMError("empty module.");
	// 确保 runtime_consts 已构建（编译路径 Compile 未填充）。
	if (m->runtime_consts.empty() && !m->const_pool.empty()) {
		m->runtime_consts.reserve(m->const_pool.size());
		for (const auto& c : m->const_pool) {
			switch (c.kind) {
				case ConstKind::INTEGER:
					m->runtime_consts.push_back(Integer::FromLong(c.int_value));
					break;
				case ConstKind::STRING:
					m->runtime_consts.push_back(String::FromCString(c.str_value.c_str()));
					break;
				case ConstKind::FLOAT:
					m->runtime_consts.push_back(New<Float>(c.float_value));
					break;
				case ConstKind::DECIMAL:
					m->runtime_consts.push_back(New<Decimal>(c.str_value));
					break;
				case ConstKind::NONE:
					m->runtime_consts.push_back(None::instance);
					Incref(None::instance);
					break;
				default:
					throw VMError("unknown constant kind.");
			}
			GC_AddRoot(m->runtime_consts.back());
		}
	}

	// 临时切换 module_ 使 execute 内部读取正确的 symtab/source_path/
	// runtime_consts（与 load_module / call 的跨模块执行机制一致）。
	Module* saved_module = module_;
	module_ = m;
	// REPL 无「当前文件」概念，pycp.__name__ 回退为 "__main__"。
	Pycp::Module* saved_current = Pycp::current_module_;
	set_current_module(nullptr);
	Object* result = nullptr;
	try {
		CodeObject* top = &m->code_objects[0];
		std::shared_ptr<Environment> env = std::make_shared<Environment>();
		env->globals = global_env_->globals;
		result = execute(top, env, nullptr, 0);
	} catch (...) {
		module_ = saved_module;
		set_current_module(saved_current);
		// 异常路径同样接管 m（保留 runtime_consts 供可能已存入 globals 的
		// 闭包后续使用），由 VM 析构统一清理。
		owned_modules_.push_back(m);
		throw;
	}
	module_ = saved_module;
	set_current_module(saved_current);
	// 注意：此处【不】清理 m 的 runtime_consts。REPL 模式下 m 内的代码对象
	// （如通过 MAKE_FUNCTION 定义的闭包函数体）后续仍会被调用，常量必须持续
	// 存活。runtime_consts 的清理与 m 本身的释放一并推迟到 VM 析构
	// （见 ~VM 中对 owned_modules_ 的处理）。

	// 接管 m 的生命周期：函数闭包可能仍引用 m，须由 VM 持有至析构。
	owned_modules_.push_back(m);
	return result;
}

Pycp::Module* VM::load_module(const std::string& name) {
	// 命中 VM 局部缓存（含入口占坑、已加载的 registry 子模块）直接返回。
	auto it = module_cache_.find(name);
	if (it != module_cache_.end()) {
		return it->second;
	}

	// ① 进程内符号 / 静态注册表 ② cwd ③ 脚本目录 ④ exe 目录的 stdlib/，
	// 统一经 ABI 入口 ImportModule 处理，命中进程级缓存直接返回。
	// 各层未命中原因记入 diag，供 ImportError 展示（AOT 生成的独立程序
	// 未注册源码编译器钩子，对仅有 .pycp 源码的模块会在此全部落空）。
	Module* src = nullptr;
	std::string diag;
	// src_is_package：命中形式是「模块文件夹清单」（运行期探测所得）。
	// 登记到 package_names_，使后续按「包名集合」的判定路径（包模块标记、
	// 角色校验）对运行期加载的包同样生效。
	bool src_is_package = false;
	Pycp::Module* imported =
		Pycp::ImportModule(name, &src, &diag, &src_is_package);
	if (imported != nullptr) {
		return imported;
	}

	// 命中源码 / 字节码 / 包清单模块：已备好字节码，尚未执行顶层。
	// 接管其生命周期——registry 中的模块由调用方持有，而源码模块是本次
	// import 临时编译（或反序列化）所得，须加入 owned_modules_ 才能被 ~VM
	// 清理 runtime_consts 并释放。
	if (src != nullptr) {
		owned_modules_.push_back(src);
		if (src_is_package) package_names_.insert(name);
		return load_from_bc_module(name, src);
	}

	// 未命中：回退解释器编译期收集的注册表（registry_）。
	if (registry_ == nullptr) {
		throw ImportError("No module named '" + name + "'." + diag);
	}
	auto mit = registry_->find(name);
	if (mit == registry_->end() || mit->second == nullptr) {
		throw ImportError("No module named '" + name + "'." + diag);
	}
	return load_from_bc_module(name, mit->second);
}

// 程序角色：把入口 __name__ 设为 "__main__"（可写）。
//   与 AOT 入口（PycpAot.cpp 注入 __name__）保持同形，避免解释态「靠
//   pycp.__name__ 回退」与 AOT「已注入 globals」两条路径不一致。
//   __name__ **不是**只读绑定：包清单可写 `__name__ = "..."` 覆盖为
//   自定义模块名；真正的只读只由 @readonly 装饰器显式声明产生。
void VM::set_entry_name_main() {
	if (entry_mod_ == nullptr) return;
	auto* ns = entry_mod_->get_namespace();
	auto it = ns->find("__name__");
	if (it != ns->end() && it->second != nullptr) {
		Decref(it->second);
		it->second = nullptr;
	}
	(*ns)["__name__"] = Pycp::String::FromCString("__main__");
	entry_mod_->set_module_name("__main__");
}

// 执行字节码模块的顶层并构造其模块对象（registry 子模块与 .pycp 源码模块共用）。
Pycp::Module* VM::load_from_bc_module(const std::string& name, Module* bc) {
	// 先占坑：创建 Pycp::Module 并放入缓存（支持循环导入——执行子模块时
	// 若其反向 import 本模块名，会命中这个未填充完的对象而非无限递归）。
	Pycp::Module* modobj = Pycp::Module::New(name);
	GC_AddRoot(modobj);
	// module_cache_ 持久持有，计入引用计数（与 native 模块分支一致）。
	Incref(modobj);
	module_cache_[name] = modobj;
	// 登记 字节码模块 -> 模块对象：其函数被调用时 globals 用本模块命名空间。
	bc_owner_[bc] = modobj;

	// 规则 2：import 后自动在该模块命名空间注入 __name__ = 模块名，
	// 使模块内裸名 __name__ 直接可用（refcount 1，命名空间为唯一持有者）。
	// 包模块的 __name__ 即包限定名（如 pkg 或 pkg.sub），且【不】冻结为
	// 只读——清单可用 `__name__ = "..."` 自定义模块名。
	(*modobj->get_namespace())["__name__"] = Pycp::String::FromCString(name.c_str());

	// 包模块：启用脚本属性钩子 / 下标协议，并在其顶层执行期间成为
	// moduletools.this() 的返回对象（退出时恢复上一个包上下文）。
	const bool is_pkg = (package_names_.find(name) != package_names_.end());
	if (is_pkg) modobj->set_package(true);

	// 为子模块构造执行环境：其顶层 globals 指向 Module 的命名空间。
	// 本版起不注入任何内建函数。
	std::shared_ptr<Environment> sub_global = std::make_shared<Environment>();
	sub_global->globals = modobj->get_namespace();
	// 登记 globals -> modobj（模块只读绑定覆盖检查用）。
	Pycp::BindGlobalsModule(sub_global->globals, modobj);

	// 子模块顶层执行需要一个 VM 上下文。为复用 execute 循环，
	// 用同一个 VM 实例（this）执行子模块 code_objects[0]，
	// 但 execute 内部读取 module_->source_path / runtime_consts，
	// 因此需要临时切换 module_ 指针。
	Module* saved_module = module_;
	module_ = bc;
	// 执行期间：当前模块 = 被导入模块，供 pycp.__name__ 回退。
	Pycp::Module* saved_current = Pycp::current_module_;
	set_current_module(modobj);
	// 执行期间：当前包 = 本模块（若为包），供 moduletools.this() 解析。
	Pycp::Module* saved_package = Pycp::GetCurrentPackage();
	// 角色状态按包隔离：执行本包顶层前重置，退出后恢复上层状态。
	const bool saved_declared = Pycp::IsPackageRoleDeclared();
	const Pycp::PackageRole saved_role = Pycp::GetPackageRole();
	if (is_pkg) {
		Pycp::SetCurrentPackage(modobj);
		Pycp::ResetPackageRole();
	}
	try {
		if (bc->code_objects.empty()) {
			throw VMError("empty imported module: " + name);
		}

		// 确保子模块 runtime_consts 已构建（编译路径 Compile 未填充）。
		if (bc->runtime_consts.empty() && !bc->const_pool.empty()) {
			bc->runtime_consts.reserve(bc->const_pool.size());
			for (const auto& c : bc->const_pool) {
				switch (c.kind) {
					case ConstKind::INTEGER:
						bc->runtime_consts.push_back(Integer::FromLong(c.int_value));
						break;
					case ConstKind::STRING:
						bc->runtime_consts.push_back(String::FromCString(c.str_value.c_str()));
						break;
					case ConstKind::FLOAT:
						bc->runtime_consts.push_back(New<Float>(c.float_value));
						break;
					case ConstKind::DECIMAL:
						bc->runtime_consts.push_back(New<Decimal>(c.str_value));
						break;
					case ConstKind::NONE:
						bc->runtime_consts.push_back(None::instance);
						Incref(None::instance);
						break;
					default:
						throw VMError("unknown constant kind.");
				}
				GC_AddRoot(bc->runtime_consts.back());
			}
		}

		CodeObject* top = &bc->code_objects[0];
		std::shared_ptr<Environment> env = std::make_shared<Environment>();
		env->globals = sub_global->globals;
		Object* ret = execute(top, env, nullptr, 0);
		if (ret != nullptr) Decref(ret);
	} catch (...) {
		// 失败回滚：从缓存移除并释放，避免留下坏模块。
		module_ = saved_module;
		set_current_module(saved_current);
		Pycp::SetCurrentPackage(saved_package);
		Pycp::SetPackageRole(saved_role, saved_declared);
		module_cache_.erase(name);
		GC_RemoveRoot(modobj);
		Decref(modobj);
		throw;
	}
	module_ = saved_module;
	set_current_module(saved_current);
	Pycp::SetCurrentPackage(saved_package);

	// 角色校验：被显式声明为「程序」的包不可被 import
	//（对齐用途约定：程序只能经 `pycp -m <pkg>` 或 AOT 编译为可执行文件运行）。
	if (is_pkg) {
		const bool declared = Pycp::IsPackageRoleDeclared();
		const Pycp::PackageRole role = Pycp::GetPackageRole();
		Pycp::SetPackageRole(saved_role, saved_declared); // 先恢复，再抛错
		if (declared && role == Pycp::PackageRole::kProgram) {
			throw ImportError("module '" + name +
			                  "' is declared as a program (moduletools.as_program()); "
			                  "run it with 'pycp -m " + name +
			                  "' instead of importing it.");
		}
	}

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
	// 全局环境指向【函数所属模块】的命名空间：入口模块函数即 global_env_->globals
	// （同一 map）；被导入模块的函数必须看到自己模块的全局名（否则读本模块全局名
	// 会 NameError）。REPL 语句模块不在 bc_owner_ 中，回退到入口命名空间。
	auto owner = bc_owner_.find(m);
	env->globals = (owner != bc_owner_.end() && owner->second != nullptr)
	                   ? owner->second->get_namespace()
	                   : global_env_->globals;
	env->captured = captured;
	// 局部变量：参数名 + 编译期确定的其他局部名
	env->local_names = co->names;
	env->locals.assign(co->nlocals, nullptr);

	// 注意：方法调用上下文（internal_access / current_self / current_class）
	// 已统一由 BytecodeFunction::invoke 中的 MethodCallContext 建立，
	// 解释器与 AOT 两条路径共用，此处不再重复处理。
	Object* ret = nullptr;
	try {
		ret = execute(co, env, argv, argc);
	} catch (...) {
		module_ = saved_module;
		throw;
	}
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

	// 参数落槽：实参已由 BytecodeFunction::invoke 的统一绑定内核按声明顺序
	// 校验并补齐（缺参 / 多参 / 重复赋值均在彼处报错），此处只做线性落槽。
	// 裸 `*` 占位槽对应 nullptr，跳过即可。
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

			case Op::LOAD_TRUE:
				push(Boolean::True());
				break;

			case Op::LOAD_FALSE:
				push(Boolean::False());
				break;

			case Op::LOAD_VAR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw VMError(cur_file(), cur_line(), "symbol index out of range.");
				const std::string& name = module_->symtab[idx];

				// 统一经 ABI 环境接口查找（局部 -> captured 链 -> 全局）
				Object* v = Environment_Lookup(env.get(), name);
				if (v == nullptr) {
					// 规则 1：命名空间未定义 __name__ 时，回退到 pycp.__name__
					// （当前文件名称，缓存于 name_fallback_，GC root 持有）。
					if (name == "__name__") {
						v = name_fallback_;
					} else {
						throw NameError(cur_file(), cur_line(), "name '" + name + "' is not defined");
					}
				}
				push(v);   // push 会 Incref，栈持有真实引用
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

			case Op::MARK_BINDING: {
				// @private/@readonly 声明：读当前模块绑定值的 is_private/
				// is_readonly 并登记模块绑定级属性（访问控制权威来源）。
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw VMError(cur_file(), cur_line(), "symbol index out of range.");
				Pycp::MarkBinding(env->globals, module_->symtab[idx]);
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
			case Op::UNARY_NOT: {
				// not / !：弹值，按真值取反为 Boolean。
				Object* v = pop();
				Object* res = Not(v);
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

			// ---- 成员测试 ----
			case Op::CONTAINS_OP: {
				// 栈: value container -> Boolean。
				// container.__contains__(value)；无 __contains__ 且可迭代则遍历
				// 回退，皆无则 Contains 抛 TypeError。
				Object* container = pop();
				Object* value = pop();
				Object* res = Contains(container, value);
				Decref(container); Decref(value);
				push(res); Decref(res);
				break;
			}

			// ---- 类型断言 ----
			case Op::CHECK_INT: {
				// 校验栈顶是否为 Integer；仅校验不弹栈，保证栈平衡。
				if (stack.empty())
					throw VMError(cur_file(), cur_line(), "stack underflow.");
				Object* v = stack.back();
				if (v == nullptr || !v->is_type("Integer"))
					throw TypeError(cur_file(), cur_line(),
						"repeat range value must be an integer.");
				break;
			}
			case Op::CHECK_RANGE_DIRECTION: {
				// 校验 repeat 范围方向与步长符号不矛盾。
				// 栈布局: a b s（依次弹出）。矛盾条件（对称）：
				//   (a < b 且 s < 0) || (a > b 且 s > 0)；a == b 永不矛盾。
				Object* s = pop();
				Object* b = pop();
				Object* a = pop();
				if (a == nullptr || b == nullptr || s == nullptr ||
				    !a->is_type("Integer") || !b->is_type("Integer") ||
				    !s->is_type("Integer")) {
					Decref(s); Decref(b); Decref(a);
					throw TypeError(cur_file(), cur_line(),
						"repeat range value must be an integer.");
				}
				int64_t av = static_cast<Integer*>(a)->get_value();
				int64_t bv = static_cast<Integer*>(b)->get_value();
				int64_t sv = static_cast<Integer*>(s)->get_value();
				Decref(s); Decref(b); Decref(a);
				if ((av < bv && sv < 0) || (av > bv && sv > 0))
					throw ValueError(cur_file(), cur_line(),
						"repeat range step contradicts endpoints direction.");
				break;
			}

			// ---- 迭代 ----
			case Op::GET_ITER: {
				// obj -> obj.__iterator__()（返回全新迭代器，Owned）。
				// 不可迭代时 __iterator__ 抛 TypeError。
				Object* obj = pop();
				Object* it = obj->__iterator__();
				Decref(obj);
				push(it); Decref(it);
				break;
			}
			case Op::FOR_ITER: {
				// it -> it.__next__()：成功压入下一元素；StopIteration 则按
				// 操作数相对跳转（同 JUMP，operand = target - ins_idx）。
				// 仅本指令捕捉 StopIteration（foreach 默认），其他异常向外传播。
				Object* it = pop();
				try {
					Object* elem = it->__next__();
					Decref(it);
					push(elem); Decref(elem);
				} catch (const StopIteration&) {
					Decref(it);
					const int64_t target = static_cast<int64_t>(pc) + static_cast<int32_t>(ins.operand);
					if (target < 0 || static_cast<std::size_t>(target) >= pc_end)
						throw VMError(cur_file(), cur_line(), "jump out of range.");
					pc = static_cast<std::size_t>(target) - 1; // target=0 时 size_t 回绕，++pc 后到指令 0
				}
				break;
			}

			// ---- 控制流 ----
			case Op::JUMP: {
				const int64_t target = static_cast<int64_t>(pc) + static_cast<int32_t>(ins.operand);
				if (target < 0 || static_cast<std::size_t>(target) >= pc_end)
					throw VMError(cur_file(), cur_line(), "jump out of range.");
				pc = static_cast<std::size_t>(target) - 1; // target=0 时 size_t 回绕，++pc 后到指令 0
				break;
			}
			case Op::JUMP_IF_FALSE: {
				Object* cond = pop();
				bool f = IsFalse(cond);
				Decref(cond);
				if (f) {
					const int64_t target = static_cast<int64_t>(pc) + static_cast<int32_t>(ins.operand);
					if (target < 0 || static_cast<std::size_t>(target) >= pc_end)
						throw VMError(cur_file(), cur_line(), "jump out of range.");
					pc = static_cast<std::size_t>(target) - 1; // target=0 时 size_t 回绕，++pc 后到指令 0
				}
				break;
			}
			case Op::JUMP_IF_TRUE: {
				Object* cond = pop();
				bool f = IsFalse(cond);
				Decref(cond);
				if (!f) {
					const int64_t target = static_cast<int64_t>(pc) + static_cast<int32_t>(ins.operand);
					if (target < 0 || static_cast<std::size_t>(target) >= pc_end)
						throw VMError(cur_file(), cur_line(), "jump out of range.");
					pc = static_cast<std::size_t>(target) - 1; // target=0 时 size_t 回绕，++pc 后到指令 0
				}
				break;
			}
			case Op::BREAK: {
				// 由 Codegen 回填为当前最内层循环 end，语义同 JUMP，
				// 天然仅退出一层循环（无需 VM 维护循环栈）。
				const int64_t target = static_cast<int64_t>(pc) + static_cast<int32_t>(ins.operand);
				if (target < 0 || static_cast<std::size_t>(target) >= pc_end)
					throw VMError(cur_file(), cur_line(), "jump out of range.");
				pc = static_cast<std::size_t>(target) - 1; // target=0 时 size_t 回绕，++pc 后到指令 0
				break;
			}

			// ---- 函数与调用 ----
			case Op::MAKE_FUNCTION: {
				std::size_t fidx = static_cast<std::size_t>(ins.operand);
				if (fidx >= module_->code_objects.size())
					throw VMError(cur_file(), cur_line(), "function index out of range.");
				BytecodeFunction* fn = new BytecodeFunction(this, module_, fidx, env);
				GC_Track(fn);
				// 闭包捕获：登记该函数（含其创建的嵌套函数）引用的自由变量名，
				// 使本帧退出时保留这些局部槽位，避免闭包持有已释放对象。
				Pycp::Environment_KeepNamesOfCodeObject(env.get(), module_, fidx);
				// 挂载默认值：定义点（MAKE_FUNCTION 之前）已按形参顺序把
				// default_count 个默认值对象压栈，此处弹出并以 Owned 转入 fn。
				// （若函数带装饰器，装饰器对象位于默认值之下，栈顶即默认值段。）
				const std::size_t n_def = fn->fn_default_count();
				if (n_def > 0) {
					if (stack.size() < n_def)
						throw VMError(cur_file(), cur_line(),
						              "function default value stack underflow.");
					// 栈顶 n_def 个元素即默认值（形参顺序，底->顶）。
					std::vector<Object*> defs;
					defs.reserve(n_def);
					const std::size_t base = stack.size() - n_def;
					for (std::size_t i = base; i < stack.size(); ++i)
						defs.push_back(stack[i]);
					fn->set_defaults(defs);   // 内部对每个元素 Incref（接管一份 Owned）
					// 释放栈上原有的那 1 份引用（与 set_defaults 的 Incref 抵消，
					// 使默认值仅由函数 defaults_ 持有）。
					for (std::size_t i = 0; i < n_def; ++i) {
						Object* d = stack.back();
						stack.pop_back();
						if (d != nullptr) Decref(d);
					}
				}
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
				// 未显式 inherits 时默认自动继承 pycp.Object（对齐 Python object）。
				// 父类引用：单标识符（全局变量）或属性访问路径（模块.类）。
				std::string parent_ref = cdef.parent_name.empty()
					? std::string("pycp.Object") : cdef.parent_name;
				if (!parent_ref.empty()) {
					Object* parent_obj = nullptr;
					std::size_t dot = parent_ref.find('.');
					if (dot == std::string::npos) {
						parent_obj = Environment_Lookup(env.get(), parent_ref);
					} else {
						// 路径 A.B：先查 A（模块），再从模块取属性 B。
						std::string mod_name = parent_ref.substr(0, dot);
						std::string attr_name = parent_ref.substr(dot + 1);
						Object* mod_obj = Environment_Lookup(env.get(), mod_name);
						if (mod_obj == nullptr && mod_name == "pycp") {
							// pycp 可能尚未被当前模块显式 import，经 VM 加载。
							Pycp::Module* pm = load_module("pycp");
							mod_obj = pm;
						}
						if (mod_obj != nullptr && dynamic_cast<Pycp::Module*>(mod_obj) != nullptr) {
							Pycp::Module* mo = static_cast<Pycp::Module*>(mod_obj);
							parent_obj = mo->__get_attribute__(attr_name);
						}
						}
						if (parent_obj == nullptr || dynamic_cast<Class*>(parent_obj) == nullptr) {
						Decref(cls);
						throw NameError(cur_file(), cur_line(),
						                "parent class '" + parent_ref + "' is not defined");
						}
						Class* parent = static_cast<Class*>(parent_obj);
					cls->set_parent(parent);
					// 复制父类成员（可见性 + 只读一并复制）。
					for (const auto& mn : parent->get_member_names()) {
						cls->add_member_name(mn, parent->member_is_private(mn),
						                     parent->member_is_readonly(mn));
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

				// 方法参数默认值段：位于装饰器对象【之下】（compile 端先压栈）。
				// 总数为各方法 code object default_count 之和；按 cdef.methods
				// 顺序连续存放，遍历方法时用游标顺序取用。
				std::size_t method_default_total = 0;
				for (const auto& m : cdef.methods) {
					const std::size_t mi = static_cast<std::size_t>(m.second);
					if (mi < module_->code_objects.size())
						method_default_total +=
							module_->code_objects[mi].default_count;
				}
				// 默认值段位于装饰器段之下（[deco_base - total, deco_base)），
				// 需保证栈上确有 total 个默认值，即 deco_base >= total。
				if (deco_base < method_default_total) {
					Decref(cls);
					throw VMError(cur_file(), cur_line(),
					              "method default value stack underflow.");
				}
				std::size_t method_default_cursor =
					deco_base - method_default_total;

				// 成员变量名（声明顺序）。带装饰器的成员调用装饰器函数
				// （传一个临时占位对象，装饰器设置其可见性/只读后返回），从
				// 返回对象读 is_private()/is_readonly() 得到成员标志。
				// 成员变量本身无独立运行时值对象（初始值经 __init_defaults__
				// 实例化时赋值），故仅用占位对象确定标志。
				for (std::size_t i = 0; i < cdef.member_names.size(); ++i) {
					if (i < cdef.member_decorators.size() &&
					    !cdef.member_decorators[i].empty()) {
						const std::vector<uint32_t>& group = cdef.member_decorators[i];
						// 组内槽位按源码顺序连续存放；范围校验后交由 ABI 由内向外
						// 串联应用（同一占位对象，private/readonly 标志自然累加）。
						std::vector<Object*> decos;
						decos.reserve(group.size());
						for (uint32_t slot : group) {
							const std::size_t didx = deco_base + slot;
							if (didx >= stack.size()) {
								Decref(cls);
								throw VMError(cur_file(), cur_line(), "decorator stack index out of range.");
							}
							decos.push_back(stack[didx]);
						}
						Pycp::MemberFlags mf = Pycp::ApplyDecoratorMemberFlagsChain(
							decos.data(), decos.size(), cur_file(), cur_line());
						cls->add_member_name(cdef.member_names[i], mf.priv, mf.readonly);
					} else {
						cls->add_member_name(cdef.member_names[i], false, false);
					}
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
					// 挂载该方法的默认值（编译期已按方法/形参顺序压入默认值段）。
					const std::size_t m_n_def = fn->fn_default_count();
					if (m_n_def > 0) {
						if (method_default_cursor + m_n_def > stack.size()) {
							Decref(cls);
							Decref(fn);
							throw VMError(cur_file(), cur_line(),
							              "method default value index out of range.");
						}
						std::vector<Object*> defs;
						defs.reserve(m_n_def);
						for (std::size_t k = 0; k < m_n_def; ++k)
							defs.push_back(stack[method_default_cursor + k]);
						method_default_cursor += m_n_def;
						fn->set_defaults(defs); // 内部对每个元素 Incref（接管一份 Owned）
					}
					if (i < cdef.method_decorators.size() &&
					    !cdef.method_decorators[i].empty()) {
						const std::vector<uint32_t>& group = cdef.method_decorators[i];
						std::vector<Object*> decos;
						decos.reserve(group.size());
						for (uint32_t slot : group) {
							const std::size_t didx = deco_base + slot;
							if (didx >= stack.size()) {
								Decref(cls);
								Decref(fn);
								throw VMError(cur_file(), cur_line(), "decorator stack index out of range.");
							}
							decos.push_back(stack[didx]);
						}
						// 叠加装饰器：由内向外逐层包裹方法对象（每次返回值须为
						// Function），最后用返回值替换原方法。
						Object* decorated = Pycp::ApplyDecoratorChain(
							decos.data(), decos.size(), fn, cur_file(), cur_line());
						Decref(fn);
						fn = static_cast<BytecodeFunction*>(decorated);
					}
					cls->add_method(m.first, fn, fn->is_private()); // add_method 内部 Incref
					fn->set_owner_class(cls);   // 供 super() 解析当前方法所属类
					Decref(fn);                          // 释放新建 Owned
				}
				// 弹出装饰器对象（每个 pop 出一份栈上引用，需 Decref 释放）。
				for (std::size_t d = 0; d < cdef.decorator_count; ++d) {
					Object* deco = pop();
					if (deco != nullptr) Decref(deco);
				}
				// 弹出方法默认值段（位于装饰器段之下）。默认值已由各方法
				// set_defaults 接管一份 Owned，此处释放栈上的原始引用与之抵消。
				for (std::size_t d = 0; d < method_default_total; ++d) {
					Object* dv = pop();
					if (dv != nullptr) Decref(dv);
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
					// 数组形态入口：负责打包容器，并在未绑定方法调用
					// （Class.method(obj, ...)）时把首个实参提升为接收者。
					ret = fn->invoke(nullptr, args.data(), nargs);
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

			case Op::CALL_KW: {
				// 操作数打包：(nkw << 16) | npos（高 16 位关键字个数，低 16 位位置实参个数）。
				const std::size_t nkw =
				    static_cast<std::size_t>(ins.operand) >> 16;
				const std::size_t npos =
				    static_cast<std::size_t>(ins.operand) & 0xFFFFu;
				if (stack.size() < npos + 2 * nkw + 1)
					throw VMError(cur_file(), cur_line(), "call stack underflow.");

				// 栈布局：[callee][pos...][name0][val0]...[nameN][valN]
				// 逆序弹出「名字 + 值」对，组装关键字实参字典。
				Map* kwargs = Pycp::NewKwargs();   // Owned
				for (std::size_t i = 0; i < nkw; ++i) {
					Object* val = pop();
					Object* nms = pop();
					Pycp::KwargsSet(kwargs, nms, val);
					if (nms != nullptr) Decref(nms);
					if (val != nullptr) Decref(val);
				}

				std::vector<Object*> args(npos);
				for (std::size_t i = 0; i < npos; ++i)
					args[npos - 1 - i] = pop();
				Object* callee = pop();

				// 统一调用入口（与 AOT 生成代码共用 CallKw）：Function 走
				// 容器形态 invoke（未绑定方法调用由该重载提升接收者），
				// Class 走 instantiate（含 kwargs 透传给 __initialize__）。
				Object* ret = Pycp::CallKw(
				    callee, npos == 0 ? nullptr : args.data(), npos, kwargs);

				Decref(kwargs);
				for (Object* a : args) Decref(a);
				Decref(callee);
				push(ret);
				Decref(ret);
				break;
			}

			case Op::RETURN: {
				Object* ret = pop();
				// 释放局部槽：被闭包捕获的槽位保留（由 ~Environment 释放）。
				Pycp::Environment_ReleaseFrame(env.get());
				for (Object* v : stack) Decref(v);
				return ret;
			}
			case Op::RETURN_NONE: {
				Pycp::Environment_ReleaseFrame(env.get());
				for (Object* v : stack) Decref(v);
				return None::instance;
			}

			case Op::LOAD_MODULE: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->imports.size())
					throw VMError(cur_file(), cur_line(), "import index out of range.");
				const std::string& modname = module_->imports[idx];
				Pycp::Module* modobj = load_module(modname);
				// push 按栈引用约定对模块对象 Incref（值级真实引用）。
				// 注意：此处【不能】在 push 后再 Decref 抵消——load_module
				// 返回的模块仅由 module_cache_ 持有一份引用（New 的初始引用
				// 已被 cache 的 Incref 接管），栈上必须保留一份真实引用，
				// 后续 STORE_VAR / POP_TOP 才能正确配对抵消。
				// 若在此处抵消，则 `from mod import x` 的字节码
				// （LOAD_MODULE / DUP_TOP / LOAD_ATTR / POP_TOP）会对模块
				// 净减 1 份引用（DUP_TOP 增 1、LOAD_ATTR 与 POP_TOP 各减 1），
				// 使模块引用计数跌破 cache 持有的一份，导致析构时入口命名空间
				// Decref 把模块释放，而 module_cache_ 仍持有悬空指针 → 双重释放。
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
				Decref(val); // push 已 Incref，释放 Owned 这份（与 LOAD_ATTR 一致）
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
				// SetAttr -> __set_attribute__ 内部按需 Incref 存入；此处释放
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

			case Op::BUILD_MAP: {
				// 操作数 = 键值对个数。栈上每对为 key value（k 紧邻 v，
				// 先压 key 后压 value，故栈顶依次为 ... k1 v1 k2 v2）。
				// 逆序弹出时按 v=pop(); k=pop(); 还原源码顺序。
				std::size_t n = static_cast<std::size_t>(ins.operand);
				if (stack.size() < 2 * n)
					throw VMError(cur_file(), cur_line(), "build map stack underflow.");
				std::vector<std::pair<Object*, Object*>> pairs(n);
				for (std::size_t i = 0; i < n; ++i) {
					Object* v = pop();
					Object* k = pop();
					pairs[n - 1 - i] = {k, v};
				}
				// 构造 Map 并填充：__set_item__ 内部对键/值 Incref（Map 持有），
				// 随后释放弹出的 2n 份栈引用。
				Map* m = Map::New();
				for (auto& kv : pairs) {
					m->__set_item__(kv.first, kv.second);
					Decref(kv.first);
					Decref(kv.second);
				}
				push(m);
				Decref(m); // push 已 Incref
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

	// 正常走到代码末尾（无 RETURN）：同样按闭包捕获情况释放局部槽。
	Pycp::Environment_ReleaseFrame(env.get());
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
		const BC::CodeObject& co = module->code_objects[code_idx];
		name = co.name.c_str();
		fn_nparams_ = co.nparams;
		fn_default_count_ = co.default_count;
		// 形参形态表与形参名：解释器路径直接取 CodeObject 元信息。
		fn_param_kinds_ = co.param_kinds;
		if (fn_param_kinds_.size() != fn_nparams_) {
			fn_param_kinds_ = BC::SynthesizeParamKinds(fn_nparams_, fn_default_count_);
		}
		const std::size_t np =
		    std::min<std::size_t>(co.nparams, co.names.size());
		fn_param_names_.assign(co.names.begin(), co.names.begin() + np);
		fn_param_names_.resize(fn_nparams_);   // 名字缺失时补空串（仅影响报错文案）
	}
}

// native 模式构造：AOT 产物使用，不依赖 VM。native_fn 指向生成的
// pycp_fn_N（数组形态 PycpCompiledFunction），invoke 还原数组后直接调用它。
BytecodeFunction::BytecodeFunction(const std::string& name_,
                                   PycpCompiledFunction native_fn,
                                   std::shared_ptr<BC::Environment> captured_)
	: Function(""), vm(nullptr), module(nullptr), code_idx(0),
	  captured(std::move(captured_)), native_fn_(native_fn) {
	kind = FunctionKind::Bytecode;
	name = name_.c_str();
	// 参数元信息（nparams/default_count）由 AOT 生成代码在创建后经
	// set_param_info 注入（native 构造无从读取 CodeObject）。
}

BytecodeFunction::~BytecodeFunction() {
	for (Object* d : defaults_) {
		if (d != nullptr) Decref(d);
	}
}

void BytecodeFunction::set_param_info(uint16_t nparams, uint16_t default_count,
                                      std::vector<uint8_t> param_kinds,
                                      std::vector<std::string> param_names) {
	fn_nparams_ = nparams;
	fn_default_count_ = default_count;
	fn_param_kinds_ = std::move(param_kinds);
	if (fn_param_kinds_.size() != fn_nparams_) {
		// 旧 AOT 产物未注入形态表：回退为纯位置形态，行为不变。
		fn_param_kinds_ = BC::SynthesizeParamKinds(fn_nparams_, fn_default_count_);
	}
	fn_param_names_ = std::move(param_names);
	fn_param_names_.resize(fn_nparams_);
}

void BytecodeFunction::set_defaults(const std::vector<Object*>& vals) {
	for (Object* v : vals) {
		if (v != nullptr) Incref(v);
		defaults_.push_back(v);
	}
}

void BytecodeFunction::foreach_ref(const std::function<void(Object*)>& visit) {
	for (Object* d : defaults_) {
		if (d != nullptr) visit(d);
	}
}

namespace {

// 方法调用上下文（RAII）：解释器与 AOT 两条路径共用，保证行为一致。
//
// 背景：这三个 thread_local 上下文此前只在解释器路径建立，AOT 路径
// （native_fn_ 分支）完全缺失，导致同一份源码在两种模式下行为不同：
//   - 缺 internal_access -> 方法体内 self.x 访问 private 成员被误拦，
//     抛 AttributeError "'x' is private in class '...'"
//   - 缺 current_self    -> super() 取不到接收者，抛 TypeError
//     "super() used outside a method."
//   - 缺 current_class   -> 即便 self 可用，super() 也会按最派生实例类
//     解析父类，继承链上重复 super 会递归回自身
// 三者语义耦合、必须同进同出，故收在一处统一处理。
class MethodCallContext {
public:
	// receiver 为绑定内核解析出的接收者（self 缺省时回退到首个位置实参，
	// 与旧「argv[0] 即接收者」约定等价），不再直接读 argv[0]。
	MethodCallContext(Class* owner, Object* receiver)
		: pushed_self_(false), pushed_class_(false) {
		if (owner != nullptr) {
			Pycp::push_current_class(owner);
			pushed_class_ = true;
		}
		Pycp::enter_internal_access();
		if (receiver != nullptr) {
			if (Instance* inst = dynamic_cast<Instance*>(receiver)) {
				Pycp::push_current_self(inst);
				pushed_self_ = true;
			}
		}
	}
	~MethodCallContext() {
		if (pushed_self_) Pycp::pop_current_self();
		Pycp::leave_internal_access();
		if (pushed_class_) Pycp::pop_current_class();
	}
	MethodCallContext(const MethodCallContext&) = delete;
	MethodCallContext& operator=(const MethodCallContext&) = delete;

private:
	bool pushed_self_;
	bool pushed_class_;
};

} // anonymous namespace

namespace {

// CPython 风格参数名列表渲染：'a' / 'a' and 'b' / 'a', 'b', and 'c'
std::string RenderParamNames(const std::vector<std::string>& names) {
	std::string out;
	for (std::size_t i = 0; i < names.size(); ++i) {
		if (i > 0) {
			if (names.size() == 2) out += " and ";
			else if (i + 1 == names.size()) out += ", and ";
			else out += ", ";
		}
		out += '\'';
		out += names[i];
		out += '\'';
	}
	return out;
}

// 语言层绑定失败文案（对齐 CPython 原文案，含单复数与连接词）。
// 原生规范表有各自的历史文案（tests/native_args 有断言），两者互不影响。
std::string FormatBindError(const std::string& fn_name,
                            const Extension::BindError& err) {
	std::string msg = fn_name + "() ";
	const std::size_t n = err.names.size();
	switch (err.code) {
	case Extension::BindErrorCode::TooManyPositional:
		msg += "takes " + std::to_string(err.expected) + " positional " +
		       (err.expected == 1 ? "argument" : "arguments") + " but " +
		       std::to_string(err.got) + " " +
		       (err.got == 1 ? "was" : "were") + " given";
		break;
	case Extension::BindErrorCode::MissingPositional:
		msg += "missing " + std::to_string(n) + " required positional " +
		       (n == 1 ? "argument" : "arguments") + ": " +
		       RenderParamNames(err.names);
		break;
	case Extension::BindErrorCode::MissingKeywordOnly:
		msg += "missing " + std::to_string(n) + " required keyword-only " +
		       (n == 1 ? "argument" : "arguments") + ": " +
		       RenderParamNames(err.names);
		break;
	case Extension::BindErrorCode::MultipleValues:
		msg += "got multiple values for argument '" +
		       (n > 0 ? err.names[0] : std::string()) + "'";
		break;
	case Extension::BindErrorCode::UnexpectedKeyword:
		msg += "got an unexpected keyword argument '" +
		       (n > 0 ? err.names[0] : std::string()) + "'";
		break;
	default:
		msg += "invalid arguments";
		break;
	}
	return msg;
}

} // namespace

Object* BytecodeFunction::invoke(Object* self, FixedList* args, Map* kwargs) {
	using BC::ParamKindCode;

	// 1) 位置实参视图：self 前置为第 0 个实参（与旧实现 BoundMethod 把
	//    接收者塞进 argv[0] 的形态逐字一致）。
	std::vector<Object*> pos;
	const std::size_t given = (args != nullptr) ? args->size() : 0;
	pos.reserve(given + 1);
	if (self != nullptr) pos.push_back(self);
	for (std::size_t i = 0; i < given; ++i) pos.push_back(args->at(i));

	// 2) 形参形态表（解释器构造时取自 CodeObject；native 模式由 AOT 经
	//    set_param_info 注入；皆缺失时按旧语义（全位置）回退）。
	const std::size_t nparams = fn_nparams_;
	std::vector<uint8_t> kinds = fn_param_kinds_;
	if (kinds.size() != nparams) {
		kinds = BC::SynthesizeParamKinds(nparams, fn_default_count_);
	}

	// 3) 由元信息构造规范表（声明顺序）：名字取参数名表，默认值按声明
	//    顺序消费 defaults_（每个 Optional 形参各占一项）。裸 `*` 为占位
	//    槽（不入规范表），但它之后的位置形态一律视为关键字-only。
	std::vector<Extension::ArgSpec> specs;
	specs.reserve(nparams);
	std::size_t di = 0;
	bool kwonly = false;
	for (std::size_t i = 0; i < nparams; ++i) {
		const char* nm =
		    (i < fn_param_names_.size()) ? fn_param_names_[i].c_str() : "";
		const uint8_t k = kinds[i];
		if (k == static_cast<uint8_t>(ParamKindCode::BareStar)) {
			kwonly = true;
			continue;
		}
		if (k == static_cast<uint8_t>(ParamKindCode::Rest)) {
			specs.push_back(Extension::Arg::Rest(nm));
			kwonly = true;
			continue;
		}
		if (k == static_cast<uint8_t>(ParamKindCode::RestKeywords)) {
			specs.push_back(Extension::Arg::RestKeywords(nm));
			continue;
		}
		Object* def = nullptr;
		if (k == static_cast<uint8_t>(ParamKindCode::Optional)) {
			if (di < defaults_.size()) def = defaults_[di];
			++di;
		}
		if (kwonly) {
			// 关键字-only 为内部形态（无公开工厂）：default_value 为 nullptr
			// 即必填，非空即取默认值——与被删的 Arg::Keyword 语义逐字一致。
			specs.push_back(Extension::ArgSpec{nm, def, Extension::ArgKind::Keyword});
		} else if (def != nullptr) {
			specs.push_back(Extension::Arg::Optional(nm, def));
		} else {
			specs.push_back(Extension::Arg::Required(nm));
		}
	}

	// 4) 统一绑定内核：位置 → *args → 关键字分拣 → 默认值 → 缺参检查。
	//    失败时按 CPython 文案报 TypeError（解释器与 AOT 共用本门，故
	//    两侧行为与文案完全一致）。
	Extension::BoundArgs bound;
	Extension::BindError err;
	if (!Extension::BindParams(specs.data(), specs.size(),
	                           pos.empty() ? nullptr : pos.data(), pos.size(),
	                           kwargs, bound, err)) {
		throw TypeError(FormatBindError(name, err));
	}

	// 5) 展开为「声明顺序」的完整实参缓冲区（裸 `*` 占位槽保持 nullptr）。
	//    缓冲区必须声明在函数作用域：argv 在 MethodCallContext / vm->call /
	//    原生 stub 中持续使用，若退化为块内局部变量会变悬空指针。
	std::vector<Object*> full(nparams, nullptr);
	std::size_t si = 0;
	for (std::size_t i = 0; i < nparams; ++i) {
		if (kinds[i] == static_cast<uint8_t>(ParamKindCode::BareStar)) continue;
		if (si < bound.slots.size()) full[i] = bound.slots[si];
		++si;
	}
	Object**    argv = full.empty() ? nullptr : full.data();
	std::size_t argc = full.size();

	// 接收者：优先显式 self（BoundMethod 注入 / 未绑定方法调用提升），
	// 否则回退到首个位置实参（与旧「argv[0] 即接收者」约定等价）。
	Object* receiver = (self != nullptr) ? self : (full.empty() ? nullptr : full[0]);
	MethodCallContext ctx(owner_class_, receiver);
	if (native_fn_ != nullptr) {
		// native 模式（AOT）：self 传 this，使生成的 pycp_fn_N 能经
		// get_captured 取捕获环境。
		return native_fn_(this, argv, argc);
	}
	return vm->call(module, code_idx, argv, argc, captured);
}

} // namespace Pycp