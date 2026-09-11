#include "PycpABI.hpp"
#include "PycpBytecode.hpp"   // Environment_KeepNamesOfCodeObject 需读 CodeObject::free_names
#include "PycpClass.hpp"
#include "PycpFunction.hpp"
#include "PycpNativeExt.hpp"
#include <map>
#include <unordered_map>
#include <mutex>

namespace Pycp {

Object* GetAttr(Object* obj, const std::string& name){
	if (obj == nullptr) throw AttributeError("cannot get attribute from null object.");
	// 返回 Owned：调用方负责 Decref。
	// 实例方法：新建绑定方法（self 自动绑定）。
	// 注：instance 的 type_name 为所属类名，无法用字符串与 class 区分，
	// 故用 dynamic_cast 判定（类与实例的「名称」一致，正是用户预期）。
	if (dynamic_cast<Instance*>(obj) != nullptr) {
		Instance* inst = static_cast<Instance*>(obj);
		// 字段优先；其次方法（绑定）。
		// 注：Instance::__get_attribute__ 对类方法已返回 BoundMethod（自带
		// self 绑定，见 PycpClass.cpp get_bound_method），因此这里不得再次
		// 包装——否则每次实例方法调用 self 会被注入两次，带参方法将错位。
		Object* field = inst->__get_attribute__(name);
		if (field != nullptr) {
			// 已绑定方法直接返回（Owned）；其余 Function 包装为绑定方法；
			// 普通字段（非 Function）转 Owned 返回。
			if (dynamic_cast<BoundMethod*>(field) != nullptr) {
				return field;
			}
			if (field->is_type("Function")) {
				return New<BoundMethod>(obj, static_cast<Function*>(field));
			}
			Incref(field);
			return field;
		}
		// 字段不存在时，尝试方法。
		Object* bm = inst->get_bound_method(name);
		if (bm != nullptr) return bm; // 已 Owned（refcount=1）
		throw AttributeError("instance has no attribute '" + name + "'");
	}
	// 文件对象的方法：绑定到文件对象（self 自动绑定）。
	if (obj->is_type("File")) {
		Object* v = obj->__get_attribute__(name);
		if (v == nullptr) {
			throw AttributeError("file has no attribute '" + name + "'");
		}
		if (v->is_type("Function")) {
			// 绑定方法：新建 BoundMethod（Owned）。
			return New<BoundMethod>(obj, static_cast<Function*>(v));
		}
		// 非方法属性：Borrowed 转 Owned。
		Incref(v);
		return v;
	}
	// 模块对象：命名空间里的函数（io.print/io.input 等）是普通函数、不带
	// self，调用时实参直接对应形参。故【不】包装成 BoundMethod（否则会把
	// 模块对象作为 self 插入 argv[0]，导致参数偏移/argc 多 1）。
	// 非函数属性（io.stdout/io.stdin 等 File 对象）返回行为保持不变。
	if (dynamic_cast<Pycp::Module*>(obj) != nullptr) {
		// __get_attribute__ 已统一返回 Owned（普通命名空间成员与魔术方法
		// 包装的 BoundMethod 均在此转为 Owned），故此处不再额外 Incref，
		// 否则模块魔术方法产生的 BoundMethod 会被重复引用计数导致泄漏。
		Object* v = obj->__get_attribute__(name);
		if (v == nullptr) {
			throw AttributeError("module has no attribute '" + name + "'");
		}
		return v; // 已 Owned（调用方负责 Decref）
	}
	// 类对象（Class）：取方法返回【未绑定】函数，self 须由调用方手动传入
	// （对齐 Python `Class.method` 语义）。不包装 BoundMethod，否则会把
	// Class 自身当作 self 注入，导致未实例化直接调用 `Class.method()` 时
	// 静默执行而非报"缺少参数"。类内部 self 调用靠 internal_access 放行。
	if (dynamic_cast<Pycp::Class*>(obj) != nullptr) {
		Object* v = obj->__get_attribute__(name);
		if (v != nullptr) Incref(v); // 返回 Borrowed，转 Owned
		return v;
	}
	// 其他实例类型（list/string/file 等）：__get_attribute__ 返回 Borrowed。
	// 若返回的是 Function（如 list 的 length 方法），包装为绑定方法，
	// 使调用时 self 自动绑定到接收者对象（与 File 分支一致）。
	Object* v = obj->__get_attribute__(name);
	if (v != nullptr && v->is_type("Function")) {
		return New<BoundMethod>(obj, static_cast<Function*>(v));
	}
	if (v != nullptr) Incref(v);
	return v;
}

void SetAttr(Object* obj, const std::string& name, Object* value){
	if (obj == nullptr) {
		if (value != nullptr) Decref(value);
		throw AttributeError("cannot set attribute on null object.");
	}
	obj->__set_attribute__(name, value);
}

Object* GetItem(Object* obj, Object* key){
	if (obj == nullptr) throw TypeError("Cannot get item from null object.");
	return obj->__get_item__(key);
}

Object* SetItem(Object* obj, Object* key, Object* value){
	if (obj == nullptr) throw TypeError("Cannot set item on null object.");
	return obj->__set_item__(key, value);
}

Object* ApplyDecorator(Object* deco, Object* target,
                       const std::string& file, int line){
	if (deco == nullptr) {
		throw TypeError(file, line, "decorator object is null");
	}
	if (!deco->is_type("Function")) {
		throw TypeError(file, line, "decorator is not callable");
	}
	Object* argv[1] = { target };
	Function* fn = static_cast<Function*>(deco);
	// 装饰器返回 Owned；调用方负责接管或释放。
	return fn->invoke(argv, 1);
}

bool ApplyDecoratorVisibility(Object* deco,
                              const std::string& file, int line){
	Object* placeholder = New<Object>("@anonymous");
	Object* result = ApplyDecorator(deco, placeholder, file, line);
	Decref(placeholder);
	if (result == nullptr) {
		throw TypeError(file, line, "decorator returned null");
	}
	bool priv = result->is_private();
	Decref(result);
	return priv;
}

MemberFlags ApplyDecoratorMemberFlags(Object* deco,
                                      const std::string& file, int line) {
	Object* placeholder = New<Object>("@anonymous");
	Object* result = ApplyDecorator(deco, placeholder, file, line);
	Decref(placeholder);
	if (result == nullptr) {
		throw TypeError(file, line, "decorator returned null");
	}
	MemberFlags f;
	f.priv = result->is_private();
	f.readonly = result->is_readonly();
	Decref(result);
	return f;
}

Object* ApplyDecoratorChain(Object** decos, std::size_t count,
                            Object* target,
                            const std::string& file, int line) {
	if (count == 0 || decos == nullptr || target == nullptr) {
		throw TypeError(file, line, "decorator chain requires at least 1 decorator");
	}
	// cur 持有当前中间对象的 Owned 引用；target 的所有权仍归调用方。
	// 由内向外：decos[count-1]（最靠近目标）最先应用。
	Object* cur = target;
	Incref(cur);
	for (std::size_t k = count; k-- > 0; ) {
		Object* next = ApplyDecorator(decos[k], cur, file, line);
		if (next == nullptr || !next->is_type("Function")) {
			Decref(cur);
			if (next != nullptr) Decref(next);
			throw TypeError(file, line, "decorator must return a function.");
		}
		Decref(cur);
		cur = next;
	}
	return cur; // Owned，调用方接管
}

MemberFlags ApplyDecoratorMemberFlagsChain(Object** decos, std::size_t count,
                                           const std::string& file, int line) {
	if (count == 0 || decos == nullptr) {
		throw TypeError(file, line, "decorator chain requires at least 1 decorator");
	}
	// 用同一占位对象由内向外串联应用：private/readonly 等原地设置的标志
	// 在串联过程中自然累加，无需额外的标志合并逻辑。
	Object* placeholder = New<Object>("@anonymous");
	Object* cur = placeholder;
	Incref(cur);
	for (std::size_t k = count; k-- > 0; ) {
		Object* next = ApplyDecorator(decos[k], cur, file, line);
		if (next == nullptr) {
			Decref(cur);
			Decref(placeholder);
			throw TypeError(file, line, "decorator returned null");
		}
		Decref(cur);
		cur = next;
	}
	MemberFlags f;
	f.priv = cur->is_private();
	f.readonly = cur->is_readonly();
	Decref(cur);
	Decref(placeholder);
	return f;
}

// globals map 指针 -> 所属 Module 注册表（供模块只读绑定覆盖检查）。
namespace {
std::mutex g_globals_module_mutex;
std::unordered_map<void*, Module*> g_globals_module;
}

void BindGlobalsModule(void* globals, Module* mod) {
	std::lock_guard<std::mutex> lock(g_globals_module_mutex);
	if (mod != nullptr) {
		g_globals_module[globals] = mod;
	} else {
		g_globals_module.erase(globals);
	}
}

Module* LookupGlobalsModule(void* globals) {
	std::lock_guard<std::mutex> lock(g_globals_module_mutex);
	auto it = g_globals_module.find(globals);
	return (it != g_globals_module.end()) ? it->second : nullptr;
}

void MarkBinding(void* globals, const std::string& name) {
	Module* owner = LookupGlobalsModule(globals);
	if (owner == nullptr || globals == nullptr) return;
	auto* ns = static_cast<std::unordered_map<std::string, Object*>*>(globals);
	auto it = ns->find(name);
	AccessAttrs attrs;
	if (it != ns->end() && it->second != nullptr) {
		attrs.priv = it->second->is_private();
		attrs.readonly = it->second->is_readonly();
	}
	owner->mark_binding(name, attrs);
}

Object* Add(Object* lhs, Object* rhs){
	if (lhs == nullptr || rhs == nullptr) throw TypeError("Cannot add null object.");
	return lhs->__addition__(rhs);
}

Object* Sub(Object* lhs, Object* rhs){
	if (lhs == nullptr || rhs == nullptr) throw TypeError("Cannot subtract null object.");
	return lhs->__subtraction__(rhs);
}

Object* Mul(Object* lhs, Object* rhs){
	if (lhs == nullptr || rhs == nullptr) throw TypeError("Cannot multiply null object.");
	return lhs->__multiplication__(rhs);
}

Object* Div(Object* lhs, Object* rhs){
	if (lhs == nullptr || rhs == nullptr) throw TypeError("Cannot divide null object.");
	return lhs->__division__(rhs);
}

Object* Pow(Object* lhs, Object* rhs){
	if (lhs == nullptr || rhs == nullptr) throw TypeError("Cannot power null object.");
	return lhs->__power__(rhs);
}

Object* Compare(Object* lhs, Object* rhs, int op){
	if (lhs == nullptr || rhs == nullptr)
		throw TypeError("Cannot compare null object.");

	// 仅同类型的 Integer / String 可参与真正的值比较；
	// 其余组合（含 None、跨类型）与 VM COMPARE_OP 语义一致：
	//   EQ → 0（false）、NE → 1（true）、其余抛 TypeError。
	// Boolean 继承 Integer，与 Integer 互通比较（True==1 / False==0）。
	bool both_int_like =
		(dynamic_cast<Integer*>(lhs) != nullptr) &&
		(dynamic_cast<Integer*>(rhs) != nullptr);
	bool comparable =
		((lhs->type_name() == rhs->type_name()) &&
		 (lhs->is_type("Integer") || lhs->is_type("String"))) ||
		both_int_like;

	if (comparable) {
		switch (op) {
			case 0: return lhs->__less_than__(rhs);
			case 1: return lhs->__less_equal__(rhs);
			case 2: return lhs->__equal__(rhs);
			case 3: return lhs->__not_equal__(rhs);
			case 4: return lhs->__greater_than__(rhs);
			case 5: return lhs->__greater_equal__(rhs);
			default: break;
		}
	} else {
		switch (op) {
			case 2: return Integer::instances[0]; // EQ -> false
			case 3: return Integer::instances[1]; // NE -> true
			default: break;
		}
	}

	throw TypeError("cannot compare different types.");
}

bool IsFalse(Object* v){
	if (v == nullptr) return true;
	if (v->is_type("None")) return true;
	// 统一经 __boolean__() 取得布尔对象（等价于隐式 Boolean(v)），
	// 再按整数值判定真假（0 为假，非 0 为真）。Boolean 继承 Integer，
	// dynamic_cast 对 Boolean / Integer 均成功。
	Object* b = v->__boolean__();
	if (Integer* ib = dynamic_cast<Integer*>(b))
		return ib->get_value() == 0;
	// __boolean__ 返回非 Integer 子类时按真处理（理论上不会发生）。
	return false;
}

Object* Call(Object* callable, Object** argv, std::size_t argc){
	if (callable == nullptr) throw TypeError("Cannot call null object.");
	if (!callable->is_type("Function")){
		throw TypeError("Object is not callable.");
	}
	Function* fn = static_cast<Function*>(callable);
	return fn->invoke(argv, argc);
}

BC::Environment* Environment_New(){
	return new BC::Environment();
}

void Environment_Free(BC::Environment* env){
	delete env;
}

Object* Environment_Lookup(BC::Environment* env, const std::string& name){
	if (env == nullptr) return nullptr;

	long local_idx = env->find_local(name);
	if (local_idx >= 0) return env->locals[static_cast<std::size_t>(local_idx)];

	std::shared_ptr<BC::Environment> cap = env->captured;
	while (cap) {
		long c = cap->find_local(name);
		if (c >= 0) return cap->locals[static_cast<std::size_t>(c)];
		cap = cap->captured;
	}

	if (env->globals) {
		auto it = env->globals->find(name);
		if (it != env->globals->end()) return it->second;
	}
	return nullptr;
}

void Environment_Store(BC::Environment* env, const std::string& name,
                       Object* value){
	if (env == nullptr) {
		Decref(value);
		return;
	}

	long local_idx = env->find_local(name);
	if (local_idx >= 0) {
		if (env->locals[static_cast<std::size_t>(local_idx)])
			Decref(env->locals[static_cast<std::size_t>(local_idx)]);
		env->locals[static_cast<std::size_t>(local_idx)] = value;
		return;
	}

	std::shared_ptr<BC::Environment> cap = env->captured;
	while (cap) {
		long c = cap->find_local(name);
		if (c >= 0) {
			if (cap->locals[static_cast<std::size_t>(c)])
				Decref(cap->locals[static_cast<std::size_t>(c)]);
			cap->locals[static_cast<std::size_t>(c)] = value;
			return;
		}
		cap = cap->captured;
	}

	if (env->globals) {
		auto it = env->globals->find(name);
		if (it != env->globals->end()) {
			// 模块常量绑定：以绑定级属性为权威（@readonly 声明经 MARK_BINDING
			// 登记）。不再回退值级 is_readonly，避免被池化共享值（小整数等）
			// 误标只读导致无关变量重赋值被拒。
			Module* owner = LookupGlobalsModule(env->globals);
			if (owner != nullptr && owner->is_readonly_binding(name)) {
				Decref(value); // 消费待写引用（Environment_Store 接管所有权）
				throw AttributeError("cannot reassign read-only binding '" +
				                     name + "'.");
			}
			if (it->second) Decref(it->second);
			it->second = value;
		} else {
			(*env->globals)[name] = value;
		}
		return;
	}

	Decref(value);
}

void Environment_KeepNamesOfCodeObject(BC::Environment* env,
                                       const BC::Module* module,
                                       std::size_t co_idx) {
	if (env == nullptr || module == nullptr) return;
	if (co_idx >= module->code_objects.size()) return;
	for (const std::string& name : module->code_objects[co_idx].free_names) {
		env->keep_names.insert(name);
	}
}

void Environment_KeepNames(BC::Environment* env,
                           const char* const* names, std::size_t count) {
	if (env == nullptr || names == nullptr) return;
	for (std::size_t i = 0; i < count; ++i) {
		if (names[i] != nullptr) env->keep_names.insert(names[i]);
	}
}

void Environment_ReleaseFrame(BC::Environment* env) {
	if (env == nullptr) return;
	const std::size_t n = env->locals.size();
	for (std::size_t i = 0; i < n; ++i) {
		Object* v = env->locals[i];
		if (v == nullptr) continue;
		// 被闭包捕获的槽位保留（交由 ~Environment 释放）；其余立即释放。
		const bool kept = (i < env->local_names.size()) &&
		                  (env->keep_names.count(env->local_names[i]) != 0);
		if (kept) continue;
		env->locals[i] = nullptr;
		Decref(v);
	}
}

// =============================================================
// 模块导入（类似 CPython 的 PyImport_ImportModule）
// =============================================================
// AOT 编译的 .pycp 子模块注册表
//   frontend AOT 在生成的 .cpp 中，于文件作用域（静态初始化阶段，main
//   之前）把各模块初始化函数指针登记进此表；ImportModule 据此「直接调用」
//   对应 PycpModule_<name>（对标用户要求的「直接调用 PycpModule_Initialize」），
//   不依赖 dlsym 与符号导出（避免主可执行文件动态符号表不可见的问题）。
// =============================================================
using AotModuleInitFn = Module* (*)();
static std::map<std::string, AotModuleInitFn>& aot_module_registry() {
	static std::map<std::string, AotModuleInitFn> reg;
	return reg;
}

void RegisterAotModule(const std::string& name, AotModuleInitFn init) {
	aot_module_registry()[name] = init;
}

namespace {
// 脚本所在目录（第 3 层的第二个候选目录），进程级，由 VM 构造时设置。
std::string g_module_search_dir;
std::mutex g_module_search_dir_mutex;

std::string module_search_dir() {
	std::lock_guard<std::mutex> lock(g_module_search_dir_mutex);
	return g_module_search_dir;
}

// 调用 AOT 模块初始化函数（跨模块边界保护异常）。
Module* call_aot_init(AotModuleInitFn init, const std::string& name) {
	Module* mod = nullptr;
	try {
		mod = init();
	} catch (const Pycp::Exception&) {
		throw; // 已含位置信息，直接向上传播
	} catch (const std::exception& e) {
		throw Pycp::Exception("AOT module '" + name +
		                      "' init failed: " + e.what());
	} catch (...) {
		throw Pycp::Exception("unknown exception in AOT module '" + name + "'");
	}
	if (mod == nullptr) {
		throw Pycp::ImportError("AOT module '" + name +
		                        "' init function returned null module.");
	}
	return mod;
}
} // anonymous namespace

void SetModuleSearchDir(const std::string& dir) {
	std::lock_guard<std::mutex> lock(g_module_search_dir_mutex);
	g_module_search_dir = dir;
}

// =============================================================
//  ①进程内符号 / 静态注册表 → ②cwd → ③脚本目录 → ④exe 目录/stdlib。
//
// 顺序要点（「本地优先」）：文件系统层面离用户最近的候选先探测——
// cwd 与脚本目录排在 exe 目录的 stdlib/ 之前，故本地同名模块（含源码）
// 可覆盖内置扩展；每个目录内部按「.pycp 源码 → 同名动态库」尝试。
//
// 全部未命中返回 nullptr（out_source 亦为空），调用方回退到自身机制
// （如 VM::load_module 的 registry 路径）并抛出 ImportError。
// diagnostics 非空时，逐层记录未命中的候选，供 ImportError 展示。
// =============================================================
Module* ImportModule(const std::string& name, BC::Module** out_source,
                     std::string* diagnostics) {
	if (out_source != nullptr) *out_source = nullptr;
	if (diagnostics != nullptr) diagnostics->clear();

	// 进程级缓存（跨 VM 实例、跨 AOT 调用共享）。
	static std::map<std::string, Module*> g_import_cache;
	auto it = g_import_cache.find(name);
	if (it != g_import_cache.end()) {
		return it->second;
	}

	// 命中并缓存：模块对象常驻进程——AddRoot 防止 GC 回收，Incref 抵消
	// 后续（如 ~VM）的 Decref，避免 use-after-free。
	auto cached = [&](Module* mod) -> Module* {
		GC_AddRoot(mod);
		Incref(mod);
		g_import_cache[name] = mod;
		return mod;
	};
	// 命中 .pycp 源码（已编译、尚未执行顶层）：交由 VM 执行。
	auto source_hit = [&]() {
		return out_source != nullptr && *out_source != nullptr;
	};

	// 目录探测结果：kMiss 未命中、kSource 命中源码（*out_source 已填充）、
	// kNative 命中原生动态库（*out 为已缓存的 Module*）。
	enum class Probe { kMiss, kSource, kNative };

	// 在单个候选目录内按「.pycp 源码 → 同名动态库」探测；未命中时把该层
	// 记入 diagnostics，便于 ImportError 直接列出全部尝试过的候选。
	auto probe_dir = [&](const std::string& dir, const char* label,
	                     Module** out) -> Probe {
		Module* r = LoadNativeModuleFrom(dir, name, out_source);
		if (r != nullptr) {
			*out = cached(r);
			return Probe::kNative;
		}
		if (source_hit()) return Probe::kSource;
		if (diagnostics != nullptr) {
			const std::string shown = dir.empty() ? std::string("./") : dir;
			*diagnostics += "\n  - " + std::string(label) + ": " + shown +
			                name + Pycp::EXT_PYCP + " 或同名动态库均未找到";
		}
		return Probe::kMiss;
	};

	// ---- ① 进程内符号 / 静态注册表（不探测文件系统）----
	// 两级同层兜底，缺一不可：
	//   dlsym(RTLD_DEFAULT) 覆盖经 -rdynamic 动态链接进主程序的模块；
	//   静态注册表        覆盖主程序符号不可见（AOT 生成的 exe 默认无
	//                     -rdynamic）与静态库链接的场景。
	Module* m = LoadLinkedModule(name);
	if (m == nullptr) {
		auto ait = aot_module_registry().find(name);
		if (ait != aot_module_registry().end()) {
			m = call_aot_init(ait->second, name);
		}
	}
	if (m != nullptr) return cached(m);
	if (diagnostics != nullptr) {
		*diagnostics += "\n  - 进程内符号 / 静态注册表: 未找到 " +
		                std::string(AOT_MODULE_INIT_PREFIX) + name +
		                "（未静态链接进本程序）";
	}

	// ---- ② 当前工作目录 ----
	Module* hit = nullptr;
	if (probe_dir(std::string(), "当前工作目录", &hit) != Probe::kMiss) {
		return hit; // kSource 时为 nullptr（源码交由 VM 执行）
	}

	// ---- ③ 脚本所在目录（为 "." 时与 cwd 重合，已在上一步覆盖）----
	const std::string script_dir = module_search_dir();
	if (!script_dir.empty() && script_dir != ".") {
		if (probe_dir(script_dir, "脚本所在目录", &hit) != Probe::kMiss) {
			return hit;
		}
	}

	// ---- ④ 可执行文件所在目录的 stdlib/ ----
	const std::string& stdlib_dir = GetStdlibDir();
	if (!stdlib_dir.empty()) {
		if (probe_dir(stdlib_dir, "exe 目录 stdlib/", &hit) != Probe::kMiss) {
			return hit;
		}
	}

	// 全部未命中：交回调用方处理（如解释器 registry）。
	return nullptr;
}

} // namespace Pycp

// =============================================================
// C 语言 ABI 转发（保留 PYCP 前缀，extern "C"）
//   纯转发至各类型的静态方法 / namespace Pycp 内无前缀版本。
//   C 端以 void* 操作句柄。
// =============================================================

#ifdef __cplusplus
extern "C" {
#endif

PYCP_C_API void* PYCP_Integer_FromLong_void(long long value){
	return static_cast<void*>(Pycp::Integer::FromLong(value));
}
PYCP_C_API void* PYCP_String_FromCString_void(const char* value){
	return static_cast<void*>(Pycp::String::FromCString(value));
}
PYCP_C_API void* PYCP_Add(void* lhs, void* rhs){
	return static_cast<void*>(Pycp::Add(static_cast<Pycp::Object*>(lhs), static_cast<Pycp::Object*>(rhs)));
}
PYCP_C_API void* PYCP_Sub(void* lhs, void* rhs){
	return static_cast<void*>(Pycp::Sub(static_cast<Pycp::Object*>(lhs), static_cast<Pycp::Object*>(rhs)));
}
PYCP_C_API void* PYCP_Mul(void* lhs, void* rhs){
	return static_cast<void*>(Pycp::Mul(static_cast<Pycp::Object*>(lhs), static_cast<Pycp::Object*>(rhs)));
}
PYCP_C_API void* PYCP_Div(void* lhs, void* rhs){
	return static_cast<void*>(Pycp::Div(static_cast<Pycp::Object*>(lhs), static_cast<Pycp::Object*>(rhs)));
}
PYCP_C_API void* PYCP_Pow(void* lhs, void* rhs){
	return static_cast<void*>(Pycp::Pow(static_cast<Pycp::Object*>(lhs), static_cast<Pycp::Object*>(rhs)));
}
PYCP_C_API void* PYCP_Compare(void* lhs, void* rhs, int op){
	return static_cast<void*>(Pycp::Compare(static_cast<Pycp::Object*>(lhs), static_cast<Pycp::Object*>(rhs), op));
}
PYCP_C_API int PYCP_IsFalse(void* v){
	return Pycp::IsFalse(static_cast<Pycp::Object*>(v)) ? 1 : 0;
}
PYCP_C_API void* PYCP_Call(void* callable, void** argv, std::size_t argc){
	return static_cast<void*>(Pycp::Call(static_cast<Pycp::Object*>(callable), reinterpret_cast<Pycp::Object**>(argv), argc));
}
PYCP_C_API void* PYCP_Class_New_void(const char* name){
	return static_cast<void*>(Pycp::Class::New(std::string(name)));
}
PYCP_C_API void* PYCP_Instance_New_void(void* cls){
	return static_cast<void*>(Pycp::Instance::New(static_cast<Pycp::Class*>(cls)));
}
PYCP_C_API void PYCP_Class_AddMethod(void* cls, const char* name, void* fn){
	Pycp::Class::AddMethod(static_cast<Pycp::Class*>(cls), std::string(name),
	                       static_cast<Pycp::Object*>(fn));
}
PYCP_C_API void* PYCP_GetAttr(void* obj, const char* name){
	return static_cast<void*>(Pycp::GetAttr(static_cast<Pycp::Object*>(obj), std::string(name)));
}
PYCP_C_API void PYCP_SetAttr(void* obj, const char* name, void* value){
	Pycp::SetAttr(static_cast<Pycp::Object*>(obj), std::string(name),
	              static_cast<Pycp::Object*>(value));
}
PYCP_C_API void PYCP_Incref(void* obj){
	Pycp::Incref(static_cast<Pycp::Object*>(obj));
}
PYCP_C_API void PYCP_Decref(void* obj){
	Pycp::Decref(static_cast<Pycp::Object*>(obj));
}
PYCP_C_API void PYCP_GC_Track(void* obj){
	Pycp::GC_Track(static_cast<Pycp::Object*>(obj));
}
PYCP_C_API void PYCP_GC_Untrack(void* obj){
	Pycp::GC_Untrack(static_cast<Pycp::Object*>(obj));
}
PYCP_C_API void PYCP_GC_AddRoot(void* obj){
	Pycp::GC_AddRoot(static_cast<Pycp::Object*>(obj));
}
PYCP_C_API void PYCP_GC_RemoveRoot(void* obj){
	Pycp::GC_RemoveRoot(static_cast<Pycp::Object*>(obj));
}
PYCP_C_API void PYCP_GC_Collect(){
	Pycp::GC_Collect();
}
PYCP_C_API std::size_t PYCP_GC_LiveCount(){
	return Pycp::GC_LiveCount();
}
PYCP_C_API void PYCP_Initialize(){
	Pycp::Initialize();
}
PYCP_C_API void PYCP_Finalize(){
	Pycp::Finalize();
}

#ifdef __cplusplus
}
#endif