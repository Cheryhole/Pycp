#include "PycpBoolean.hpp"
#include "PycpGC.hpp"

namespace Pycp {

Boolean* Boolean::g_true = nullptr;
Boolean* Boolean::g_false = nullptr;

// 按对象的 truthiness（__boolean__ 协议）计算 Boolean 构造的目标整数值：
// None → 0；否则取 obj->__boolean__() 返回的 Boolean 的整数值（0/1）。
// 等价于隐式 PYCP.Boolean(obj) 对齐 if 的隐式转换语义。
static int64_t compute_bool_value(Object* obj) {
	if (obj == nullptr || obj->is_type("None")) return 0;
	Object* b = obj->__boolean__();
	if (Integer* ib = dynamic_cast<Integer*>(b)) return ib->get_value();
	return 1; // 非 Integer 子类（理论不发生）按真处理
}

void Boolean::Initialize(){
	// 常驻实例：值 1 为 True，值 0 为 False。
	Boolean::g_true = New<Boolean>(1);
	Boolean::g_false = New<Boolean>(0);
	// 标记 PERMANENT 并登记为 root，永不参与回收。
	Boolean::g_true->_set_gc_flags(Boolean::g_true->_gc_flags() | GCFlag::PERMANENT);
	Boolean::g_false->_set_gc_flags(Boolean::g_false->_gc_flags() | GCFlag::PERMANENT);
	GC_AddRoot(Boolean::g_true);
	GC_AddRoot(Boolean::g_false);
}

void Boolean::Finalize(){
	if (Boolean::g_true != nullptr){
		GC_RemoveRoot(Boolean::g_true);
		Boolean::g_true->_set_gc_flags(Boolean::g_true->_gc_flags() & ~GCFlag::PERMANENT);
		Decref(Boolean::g_true);
		Boolean::g_true = nullptr;
	}
	if (Boolean::g_false != nullptr){
		GC_RemoveRoot(Boolean::g_false);
		Boolean::g_false->_set_gc_flags(Boolean::g_false->_gc_flags() & ~GCFlag::PERMANENT);
		Decref(Boolean::g_false);
		Boolean::g_false = nullptr;
	}
}

Boolean::Boolean(int64_t v) : Integer(v){
	// 覆盖继承来的类型名 "Integer" → "Boolean"（type_name() 非虚，依赖成员）。
	set_type_name("Boolean");
	set_type_info(PycpTypeId::Boolean, PycpTypeFlag::IntegerSubclass | PycpTypeFlag::Hashable);
}

Boolean::Boolean(Object* obj) : Integer(compute_bool_value(obj)){
	// 按 truthiness 初始化（空串/0/空列表/None → 0，其余 → 1），
	// 对齐 __boolean__ 协议与 if 隐式 Boolean 转换。
	set_type_name("Boolean");
	set_type_info(PycpTypeId::Boolean, PycpTypeFlag::IntegerSubclass | PycpTypeFlag::Hashable);
}

Object* Boolean::__boolean__(){
	// 按自身值返回 True/False。
	return get_value() != 0 ? Boolean::True() : Boolean::False();
}

Boolean* Boolean::True(){
	return Boolean::g_true;
}

Boolean* Boolean::False(){
	return Boolean::g_false;
}

Object* Boolean::__string__(){
	// 注意：返回的是静态字符串对象还是新 String？
	// 为保证引用安全，返回新构造的 String（Owned，调用方负责 Decref）。
	// 但作为常驻常量路径（io.print 等）频繁调用，使用缓存的静态 String 更安全。
	return New<String>(get_value() != 0 ? "True" : "False");
}

} // namespace Pycp
