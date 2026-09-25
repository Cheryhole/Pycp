#include "object/PycpFloat.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpDecimal.hpp"
#include "object/PycpString.hpp"
#include "object/PycpException.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpMagic.hpp"   // CollectUniqueName / CommonInspectNames
#include "object/PycpFixedList.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace Pycp {

// ------------------------------------------------------------
// 内部辅助：把 double 渲染为最短往返字符串（对齐 Python float 的显示习惯：
// 整数值带 ".0" 后缀，如 1.0；非整数值取最短可往返表示，如 1.23）。
// ------------------------------------------------------------
static std::string float_to_string(double v){
	// 非有限值特殊处理（Python 语义）。
	if (std::isnan(v))   return "nan";
	if (std::isinf(v))   return v > 0 ? "inf" : "-inf";

	// std::to_chars 最短往返表示（MSVC / 新版 libstdc++ 均支持浮点重载）。
	char buf[64];
	auto res = std::to_chars(buf, buf + sizeof(buf), v);
	if (res.ec != std::errc()){
		// 兜底：理论上不会走到（缓冲区足够容纳 double 最短表示）。
		std::snprintf(buf, sizeof(buf), "%g", v);
		return std::string(buf);
	}
	std::string s(buf, res.ptr);
	// 无 '.' 且无 'e'/'E' 时补 ".0"，保持浮点字面量的显示形式。
	if (s.find('.') == std::string::npos &&
	    s.find('e') == std::string::npos &&
	    s.find('E') == std::string::npos){
		s += ".0";
	}
	return s;
}

// 字符串解析辅助（Float(const std::string&) 与 Float(Object*) 共用）。
static double parse_float_string(const std::string& value){
	try{
		size_t pos = 0;
		double v = std::stod(value, &pos);
		// 全串必须被消费（"1.2.3" 之类的输入拒绝）。
		if (pos != value.size()){
			throw ValueError("Invalid literal for float: \"" + value + "\"");
		}
		return v;
	} catch (const std::invalid_argument&){
		throw ValueError("Invalid literal for float: \"" + value + "\"");
	} catch (const std::out_of_range&){
		throw ValueError("Float literal out of range: \"" + value + "\"");
	}
}

Float::Float() : Float(0.0){}

Float::Float(double value) : Object("Float"){
	set_type_info(PycpTypeId::Float, PycpTypeFlag::Hashable);
	this->_value = value;
}

Float::Float(const std::string& value) : Object("Float"){
	set_type_info(PycpTypeId::Float, PycpTypeFlag::Hashable);
	this->_value = parse_float_string(value);
}

Float::Float(Float* value) : Float(value->get_value()){}

Float::Float(Object* obj) : Object("Float"){
	set_type_info(PycpTypeId::Float, PycpTypeFlag::Hashable);
	if (obj == nullptr){
		throw TypeError("Cannot construct Float from null object.");
	}
	// String 入口直接解析（String 无 __float__；对齐 Python float("2.5")）。
	if (IsString(obj)){
		this->_value = parse_float_string(static_cast<String*>(obj)->get_value());
		return;
	}
	// 其余对象经 __float__ 提升为 Float（Integer/Decimal override）。
	Float* f = static_cast<Float*>(obj->__float__());
	this->_value = f->get_value();
}

Float::~Float(){}

double Float::get_value() const{
	return this->_value;
}

Object* Float::FromDouble(double value){
	return New<Float>(value);
}

Object* Float::__integer__(){
	// 截断为 Integer；NaN/Inf 或超出 int64 范围时报错（对齐 Python OverflowError）。
	if (std::isnan(this->_value) || std::isinf(this->_value) ||
	    this->_value < static_cast<double>(INT64_MIN) ||
	    this->_value >= static_cast<double>(INT64_MAX)){
		throw ValueError("Cannot convert Float to Integer: value out of range.");
	}
	return Integer::FromLong(static_cast<int64_t>(this->_value));
}

Object* Float::__float__(){
	return this;  // 幂等：Float 转 Float 返回自身
}

Object* Float::__string__(){
	return New<String>(float_to_string(this->_value));
}

Object* Float::__raw_string__(){
	return __string__();
}

Object* Float::__hash__(){
	// 哈希取整数值部分（NaN/Inf 退化为 0；仅保证同值同哈希的弱一致性）。
	if (std::isnan(this->_value) || std::isinf(this->_value)){
		return Integer::FromLong(0);
	}
	return Integer::FromLong(static_cast<int64_t>(this->_value));
}

Object* Float::__boolean__(){
	// 非零为 True，零为 False（NaN 为真，与 Python 一致）。
	return this->_value == 0.0 ? Boolean::False() : Boolean::True();
}

Object* Float::__negation__(){
	return New<Float>(-(this->_value));
}

// 右操作数数值提升辅助：Integer / Boolean / Decimal 提升为 double。
// Decimal 转 double 有精度损失（用户确认的提升规则：Float 与 Decimal
// 运算时结果为 Float）。
static bool float_operand(Object* other, double* out){
	if (other == nullptr) return false;
	if (IsInteger(other)){
		*out = static_cast<double>(static_cast<Integer*>(other)->get_value());
		return true;
	}
	if (IsExact<Float>(other)){
		*out = static_cast<Float*>(other)->get_value();
		return true;
	}
	if (IsExact<Decimal>(other)){
		*out = static_cast<Decimal*>(other)->to_double();
		return true;
	}
	return false;
}

Object* Float::__addition__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to add.");
	}
	return New<Float>(this->_value + r);
}

Object* Float::__subtraction__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to subtract.");
	}
	return New<Float>(this->_value - r);
}

Object* Float::__multiplication__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to multiply.");
	}
	return New<Float>(this->_value * r);
}

Object* Float::__division__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to divide.");
	}
	if (r == 0.0){
		throw ValueError("Division by zero.");
	}
	return New<Float>(this->_value / r);
}

Object* Float::__power__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to power.");
	}
	// 0 的负指数次幂：除零语义（对齐 Integer 的除零检查习惯）。
	if (this->_value == 0.0 && r < 0.0){
		throw ValueError("Division by zero.");
	}
	return New<Float>(std::pow(this->_value, r));
}

// 比较运算：仅支持数值族（Integer/Boolean/Float）。返回小整数池
// Integer 0/1（PERMANENT，由 ABI Compare 返回给 VM，栈持有引用但无需
// 额外 Decref——池对象常驻）。
Object* Float::__less_than__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value < r ? Integer::instances[1] : Integer::instances[0];
}

Object* Float::__less_equal__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value <= r ? Integer::instances[1] : Integer::instances[0];
}

Object* Float::__equal__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		// 不同类型直接判不等（对齐 Python: 1.5 == "x" -> False）。
		return Boolean::False();
	}
	return this->_value == r ? Boolean::True() : Boolean::False();
}

Object* Float::__not_equal__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to compare.");
	}
	// NE 与 __equal__ 一致返回 Boolean（旧实现返回 Integer 1/0）。
	return this->_value != r ? Boolean::True() : Boolean::False();
}

Object* Float::__greater_than__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value > r ? Integer::instances[1] : Integer::instances[0];
}

Object* Float::__greater_equal__(Object* other){
	double r;
	if (!float_operand(other, &r)){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value >= r ? Integer::instances[1] : Integer::instances[0];
}

// Float 全部方法的方法表（一元/算术/比较等魔术方法，native 为 nullptr）。
const std::vector<MethodEntry>& Float_method_table() {
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

Object* Float::__inspect__() {
	// 收集基类 members_ 名 + 方法表方法名 + 通用属性名，定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	for (const MethodEntry& e : Float_method_table()) CollectUniqueName(names, e.name);
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

void Float::Initialize(){
	// 无常驻池（浮点值域大，池化无意义）；预留：类型级初始化钩子。
}

void Float::Finalize(){
	// 与 Initialize 对称，当前无资源需释放。
}

} // namespace Pycp
