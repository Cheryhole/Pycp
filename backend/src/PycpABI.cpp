#include "PycpABI.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpFunction.hpp"
#include "PycpGC.hpp"
#include "PycpManager.hpp"
#include "PycpModule.hpp"
#include "PycpClass.hpp"

#include <istream>
#include <ostream>

namespace Pycp {

Object* Integer_FromLong(long long value){
	return New<Integer>(static_cast<int64_t>(value));
}

Object* String_FromString(const char* value){
	return New<String>(std::string(value));
}

ModuleObject* Module_New(const std::string& name){
	return New<ModuleObject>(name);
}

Object* Module_GetAttr(ModuleObject* mod, const std::string& name){
	if (mod == nullptr) throw AttributeError("cannot get attribute from null module.");
	return mod->__getattr__(name);
}

ClassObject* Class_New(const std::string& name){
	return New<ClassObject>(name);
}

InstanceObject* Instance_New(ClassObject* cls){
	return New<InstanceObject>(cls);
}

// File_FromStream 的实现已迁至 builtin_libraries/io/FileFromStream.cpp
//（FileObject 是 io 内建库的专属对象）。

void Class_AddMemberName(ClassObject* cls, const std::string& name){
	if (cls == nullptr) throw TypeError("cannot add member to null class.");
	cls->add_member_name(name);
}

void Class_AddMethod(ClassObject* cls, const std::string& name, Object* fn){
	if (cls == nullptr) throw TypeError("cannot add method to null class.");
	if (fn == nullptr || fn->type != Type::FUNCTION)
		throw TypeError("method must be a function.");
	cls->add_method(name, static_cast<Function*>(fn));
}

Object* GetAttr(Object* obj, const std::string& name){
	if (obj == nullptr) throw AttributeError("cannot get attribute from null object.");
	// 返回 Owned：调用方负责 Decref。
	// 实例方法：新建绑定方法（self 自动绑定）。
	if (obj->type == Type::INSTANCE) {
		InstanceObject* inst = static_cast<InstanceObject*>(obj);
		// 字段优先；其次方法（绑定）。
		Object* field = inst->__getattr__(name);
		if (field != nullptr) {
			Incref(field);
			return field;
		}
		// 字段不存在时，尝试方法。
		Object* bm = inst->get_bound_method(name);
		if (bm != nullptr) return bm; // 已 Owned（refcount=1）
		throw AttributeError("instance has no attribute '" + name + "'");
	}
	// 文件对象的方法：绑定到文件对象（self 自动绑定）。
	if (obj->type == Type::FILE) {
		Object* v = obj->__getattr__(name);
		if (v == nullptr) {
			throw AttributeError("file has no attribute '" + name + "'");
		}
		if (v->type == Type::FUNCTION) {
			// 绑定方法：新建 BoundMethod（Owned）。
			return New<BoundMethod>(obj, static_cast<Function*>(v));
		}
		// 非方法属性：Borrowed 转 Owned。
		Incref(v);
		return v;
	}
	// 其他类型（模块/类）：__getattr__ 返回 Borrowed，转 Owned。
	Object* v = obj->__getattr__(name);
	if (v != nullptr) Incref(v);
	return v;
}

void SetAttr(Object* obj, const std::string& name, Object* value){
	if (obj == nullptr) {
		if (value != nullptr) Decref(value);
		throw AttributeError("cannot set attribute on null object.");
	}
	obj->__setattr__(name, value);
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

	// 仅同类型的 INTEGER / STRING 可参与真正的值比较；
	// 其余组合（含 None、跨类型）与 VM COMPARE_OP 语义一致：
	//   EQ → 0（false）、NE → 1（true）、其余抛 TypeError。
	bool comparable =
		(lhs->type == rhs->type) &&
		(lhs->type == Type::INTEGER || lhs->type == Type::STRING);

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
	if (v->type == Type::NONE) return true;
	if (v->type == Type::INTEGER)
		return static_cast<Integer*>(v)->get_value() == 0;
	return false;
}

Object* Call(Object* callable, Object** argv, std::size_t argc){
	if (callable == nullptr) throw TypeError("Cannot call null object.");
	if (callable->type != Type::FUNCTION){
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
//   纯转发至 namespace Pycp 内无前缀版本。C 端以 void* 操作句柄。
// =============================================================

#ifdef __cplusplus
extern "C" {
#endif

PYCP_C_API void* PYCP_Integer_FromLong_void(long long value){
	return static_cast<void*>(Pycp::Integer_FromLong(value));
}
PYCP_C_API void* PYCP_String_FromString_void(const char* value){
	return static_cast<void*>(Pycp::String_FromString(value));
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
	return static_cast<void*>(Pycp::Class_New(std::string(name)));
}
PYCP_C_API void* PYCP_Instance_New_void(void* cls){
	return static_cast<void*>(Pycp::Instance_New(static_cast<Pycp::ClassObject*>(cls)));
}
PYCP_C_API void PYCP_Class_AddMethod(void* cls, const char* name, void* fn){
	Pycp::Class_AddMethod(static_cast<Pycp::ClassObject*>(cls), std::string(name),
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
