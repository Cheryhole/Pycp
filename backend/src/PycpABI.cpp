#include "PycpABI.hpp"
#include "PycpClass.hpp"
#include "PycpFunction.hpp"

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
		Object* field = inst->__get_attribute__(name);
		if (field != nullptr) {
			// 若字段是 Function（如魔术方法），包装为绑定方法使 self 自动绑定；
			// 普通字段（非 Function）直接返回。
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
		Object* v = obj->__get_attribute__(name); // Borrowed
		if (v == nullptr) {
			throw AttributeError("module has no attribute '" + name + "'");
		}
		Incref(v); // Borrowed 转 Owned（调用方负责 Decref）
		return v;
	}
	// 其他类型（类/list 等）：__get_attribute__ 返回 Borrowed。
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
	bool comparable =
		(lhs->type_name() == rhs->type_name()) &&
		(lhs->is_type("Integer") || lhs->is_type("String"));

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
	if (v->is_type("Integer"))
		return static_cast<Integer*>(v)->get_value() == 0;
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
			if (it->second) Decref(it->second);
			it->second = value;
		} else {
			(*env->globals)[name] = value;
		}
		return;
	}

	Decref(value);
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