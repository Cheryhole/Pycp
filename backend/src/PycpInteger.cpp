#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpList.hpp"
#include "PycpBoolean.hpp"
#include "PycpFloat.hpp"
#include "PycpDecimal.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpMagic.hpp"   // CollectUniqueName / CommonInspectNames
#include "PycpFixedList.hpp"

#include <cmath>
#include <stdexcept>

namespace Pycp {

Integer* Integer::instances[PYCP_INTEGER_INSTANCES] = {0};

// ------------------------------------------------------------
// 混合运算提升辅助（用户确认的规则）：
//   Integer op Float   -> Float（double 运算）
//   Integer op Decimal -> Decimal（精确运算）
// 左操作数为 Integer 时，把左值提升为对应类型后转调其实现：
//   - Float：直接按 double 计算；
//   - Decimal：构造临时 Decimal（精确承载 int64）后转调其魔术方法，
//     异常路径上同样释放临时对象。
// ------------------------------------------------------------

// Integer op Decimal 的统一转调入口。
static Object* int_op_decimal(int64_t lhs, Object* other,
                              Object* (Decimal::* op)(Object*)){
	Decimal* tmp = static_cast<Decimal*>(New<Decimal>(lhs));
	try {
		Object* r = (tmp->*op)(other);
		Decref(tmp);
		return r;
	} catch (...) {
		Decref(tmp);
		throw;
	}
}

// 右操作数是否为 Float（用于算术提升分支）。
static Float* as_float(Object* other){
	return (other != nullptr) ? dynamic_cast<Float*>(other) : nullptr;
}

// 右操作数是否为 Decimal（用于算术提升分支）。
static Decimal* as_decimal(Object* other){
	return (other != nullptr) ? dynamic_cast<Decimal*>(other) : nullptr;
}

Integer::Integer() : Integer(INT64_C(0)){}

Integer::Integer(int64_t value) : Object("Integer"){
	set_type_info(PycpTypeId::Integer, PycpTypeFlag::IntegerSubclass | PycpTypeFlag::Hashable);
	this->_value = value;
}

Integer::Integer(const std::string& value) : Object("Integer"){
	set_type_info(PycpTypeId::Integer, PycpTypeFlag::IntegerSubclass | PycpTypeFlag::Hashable);
	try{
		this->_value = std::stoll(value);  // 使用 64 位解析，避免 stoi 截断
	} catch (const std::invalid_argument&){
		throw ValueError("Invalid literal for integer: \"" + value + "\"");
	} catch (const std::out_of_range&){
		throw ValueError("Integer literal out of range: \"" + value + "\"");
	}
}

Integer::Integer(Integer* value) : Integer(value->get_value()){}

Integer::Integer(Object* obj) : Object("Integer"){
	set_type_info(PycpTypeId::Integer, PycpTypeFlag::IntegerSubclass | PycpTypeFlag::Hashable);
	if (obj == nullptr){
		throw TypeError("Cannot construct Integer from null object.");
	}
	Integer* i = static_cast<Integer*>(obj->__integer__());
	this->_value = i->get_value();
}

Integer::~Integer(){}

int64_t Integer::get_value() const{
	return this->_value;
}

Object* Integer::FromLong(long long value){
	return New<Integer>(static_cast<int64_t>(value));
}

Object* Integer::__integer__(){
	return this;
}

Object* Integer::__float__(){
	return New<Float>(static_cast<double>(this->_value));
}

Object* Integer::__string__(){
	return New<String>(std::to_string(this->_value));
}

Object* Integer::__raw_string__(){
	// 与 __string__ 保持一致（数值不带引号）。走虚分派，故 Boolean 子类
	// 覆写的 __string__（True / False）在此同样生效，无需另写一份。
	return __string__();
}

Object* Integer::__hash__(){
	// 哈希值即整数本身（对齐 Python：hash(42) == 42）。
	return Integer::FromLong(this->_value);
}

Object* Integer::__boolean__(){
	// 非零为 True，零为 False。
	return this->_value == 0 ? Boolean::False() : Boolean::True();
}

Object* Integer::__negation__(){
	return New<Integer>(-(this->_value));
}

Object* Integer::__addition__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to add.");
	// 数值提升：Integer op Float -> Float。
	if (Float* f = as_float(other)){
		return New<Float>(static_cast<double>(this->_value) + f->get_value());
	}
	// 数值提升：Integer op Decimal -> Decimal（精确）。
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__addition__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to add.");
	}
	Integer* i = static_cast<Integer*>(other);
	return New<Integer>(this->_value + i->_value);
}

Object* Integer::__subtraction__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to subtract.");
	if (Float* f = as_float(other)){
		return New<Float>(static_cast<double>(this->_value) - f->get_value());
	}
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__subtraction__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to subtract.");
	}
	Integer* i = static_cast<Integer*>(other);
	return New<Integer>(this->_value - i->_value);
}

Object* Integer::__multiplication__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to multiply.");

	if (Float* f = as_float(other)){
		return New<Float>(static_cast<double>(this->_value) * f->get_value());
	}
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__multiplication__);
	}
	if (dynamic_cast<Integer*>(other) != nullptr){
		Integer* i = static_cast<Integer*>(other);
		return New<Integer>(this->_value * i->_value);
	}
	else if (other->is_type("String")){
		String* s = static_cast<String*>(other);
		std::string str = s->get_value();
		std::string res;
		for (int64_t i = 0; i < this->_value; i++){
			res += str;
		}
		return New<String>(res);
	}

	throw TypeError("Unsupported to multiply.");
}

Object* Integer::__division__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to divide.");
	if (Float* f = as_float(other)){
		if (f->get_value() == 0.0){
			throw ValueError("Division by zero.");
		}
		return New<Float>(static_cast<double>(this->_value) / f->get_value());
	}
	if (Decimal* d = as_decimal(other)){
		// 除零检查在 Decimal::__division__ 内部完成。
		return int_op_decimal(this->_value, d, &Decimal::__division__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to divide.");
	}
	Integer* i = static_cast<Integer*>(other);
	if (i->_value == 0){
		throw ValueError("Division by zero.");
	}
	return New<Integer>(this->_value / i->_value);
}

Object* Integer::__power__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to power.");
	if (Float* f = as_float(other)){
		if (this->_value == 0 && f->get_value() < 0.0){
			throw ValueError("Division by zero.");
		}
		return New<Float>(std::pow(static_cast<double>(this->_value), f->get_value()));
	}
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__power__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to power.");
	}
	Integer* i = static_cast<Integer*>(other);
	if (i->_value < 0){
		throw ValueError("Negative exponent is not supported.");
	}
	return New<Integer>(static_cast<int64_t>(
		std::pow(static_cast<double>(this->_value), static_cast<double>(i->_value))));
}

// 比较运算符：支持数值族（Integer/Boolean/Float/Decimal）。返回小整数池
// Integer 0/1（PERMANENT，由 ABI Compare 返回给 VM，栈持有引用但无需
// 额外 Decref——池对象常驻）。
Object* Integer::__less_than__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to compare.");
	if (Float* f = as_float(other)){
		return static_cast<double>(this->_value) < f->get_value()
			? instances[1] : instances[0];
	}
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__less_than__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value < static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

Object* Integer::__less_equal__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to compare.");
	if (Float* f = as_float(other)){
		return static_cast<double>(this->_value) <= f->get_value()
			? instances[1] : instances[0];
	}
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__less_equal__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value <= static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

Object* Integer::__equal__(Object* other){
	if (other == nullptr){
		// 不同类型直接判不等（对齐 Python: 42 == "x" -> False）。
		return Boolean::False();
	}
	if (Float* f = as_float(other)){
		return static_cast<double>(this->_value) == f->get_value()
			? Boolean::True() : Boolean::False();
	}
	if (Decimal* d = as_decimal(other)){
		// __equal__ 永不抛错，临时对象无需 try/catch。
		Decimal* tmp = static_cast<Decimal*>(New<Decimal>(this->_value));
		Object* r = tmp->__equal__(d);
		Decref(tmp);
		return r;
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		return Boolean::False();
	}
	return this->_value == static_cast<Integer*>(other)->_value
		? Boolean::True() : Boolean::False();
}

Object* Integer::__not_equal__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to compare.");
	if (Float* f = as_float(other)){
		// EQ / NE 统一返回 Boolean（与 __equal__ 一致）：旧实现返回小整数
		// 池的 1/0，导致同一类型内 == 得 False、!= 得 1 的混搭结果。
		return static_cast<double>(this->_value) != f->get_value()
			? Boolean::True() : Boolean::False();
	}
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__not_equal__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value != static_cast<Integer*>(other)->_value
		? Boolean::True() : Boolean::False();
}

Object* Integer::__greater_than__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to compare.");
	if (Float* f = as_float(other)){
		return static_cast<double>(this->_value) > f->get_value()
			? instances[1] : instances[0];
	}
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__greater_than__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value > static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

Object* Integer::__greater_equal__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to compare.");
	if (Float* f = as_float(other)){
		return static_cast<double>(this->_value) >= f->get_value()
			? instances[1] : instances[0];
	}
	if (Decimal* d = as_decimal(other)){
		return int_op_decimal(this->_value, d, &Decimal::__greater_equal__);
	}
	if (dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value >= static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

// Integer 全部方法的方法表（一元/算术/比较等魔术方法，native 为 nullptr）。
// Boolean 继承 Integer，复用本表。
const std::vector<MethodEntry>& Integer_method_table() {
	static const std::vector<MethodEntry> table = {
		{"__integer__",          nullptr},
		{"__string__",           nullptr},
		{"__raw_string__",       nullptr},
		{"__boolean__",          nullptr},
		{"__negation__",         nullptr},
		{"__addition__",         nullptr},
		{"__subtraction__",      nullptr},
		{"__multiplication__",   nullptr},
		{"__division__",         nullptr},
		{"__power__",            nullptr},
		{"__less_than__",        nullptr},
		{"__less_equal__",       nullptr},
		{"__equal__",            nullptr},
		{"__not_equal__",        nullptr},
		{"__greater_than__",     nullptr},
		{"__greater_equal__",    nullptr},
		{"__map__",              nullptr},
		{"__hash__",             nullptr},
		{"__get_attribute__",    nullptr},
		{"__set_attribute__",    nullptr},
		{"__delete_attribute__", nullptr},
		{"__inspect__",          nullptr},
	};
	return table;
}

Object* Integer::__inspect__() {
	// 收集基类 members_ 名 + 方法表方法名 + 通用属性名，定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	for (const MethodEntry& e : Integer_method_table()) CollectUniqueName(names, e.name);
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

void Integer::Initialize(){
	for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++){
		Integer::instances[i] = New<Integer>(i);
		// 小整数池为常驻对象，永不参与回收
		Integer::instances[i]->_set_gc_flags(
			Integer::instances[i]->_gc_flags() | GCFlag::PERMANENT);
		GC_AddRoot(Integer::instances[i]);
	}
}

void Integer::Finalize(){
	for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++){
		if (Integer::instances[i] != nullptr){
			GC_RemoveRoot(Integer::instances[i]);
			Integer::instances[i]->_set_gc_flags(
				Integer::instances[i]->_gc_flags() | GCFlag::NONE);
			Decref(Integer::instances[i]);
			Integer::instances[i] = nullptr;
		}
	}
}

} // namespace Pycp