#include "PycpABI.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpFunction.hpp"
#include "PycpGC.hpp"
#include "PycpManager.hpp"

namespace Pycp {

Object* Integer_FromLong(long long value){
	return New<Integer>(static_cast<int64_t>(value));
}

Object* String_FromString(const char* value){
	return New<String>(std::string(value));
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
