#include "PycpDecimal.hpp"
#include "PycpInteger.hpp"
#include "PycpFloat.hpp"
#include "PycpBoolean.hpp"
#include "PycpString.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpMagic.hpp"   // CollectUniqueName / CommonInspectNames
#include "PycpFixedList.hpp"

#include <mpdecimal.h>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace Pycp {

namespace {

// ------------------------------------------------------------
// mpdecimal 上下文与生命周期管理
// ------------------------------------------------------------
// 默认上下文：精度 28 位有效数字（对齐 Python decimal.getcontext() 默认值），
// 舍入模式 ROUND_HALF_EVEN（mpd_maxcontext 默认）。加减乘在该精度内精确，
// 除法除不尽按上下文舍入。
// 注意：不能用 mpd_defaultcontext（其 prec=38、HALF_UP），需在 maxcontext
// 基础上覆盖精度。
mpd_context_t& dec_ctx(){
	static mpd_context_t ctx = []{
		mpd_context_t c;
		mpd_maxcontext(&c);
		c.prec = 28;
		return c;
	}();
	return ctx;
}

mpd_t* dec_new(){
	mpd_t* d = mpd_qnew();
	if (d == nullptr){
		throw ValueError("Decimal allocation failed.");
	}
	return d;
}

void dec_free(mpd_t* d){
	if (d != nullptr) mpd_del(d);
}

// 从 int64 构造（精确，无舍入）。
mpd_t* dec_from_i64(int64_t v){
	mpd_t* d = dec_new();
	uint32_t status = 0;
	mpd_qset_i64_exact(d, v, &status);
	// int64 全域可精确表示，理论上不会失败；防御性检查。
	if (status & MPD_Errors){
		dec_free(d);
		throw ValueError("Cannot construct Decimal from integer value.");
	}
	return d;
}

// 从字符串构造；解析失败抛 ValueError（对齐 Integer(const std::string&)）。
mpd_t* dec_from_string(const std::string& s){
	mpd_t* d = dec_new();
	uint32_t status = 0;
	mpd_qset_string(d, s.c_str(), &dec_ctx(), &status);
	if (status & MPD_Errors){
		dec_free(d);
		throw ValueError("Invalid literal for decimal: \"" + s + "\"");
	}
	return d;
}

// 从 double 构造：以 "%.17g" 最短往返表示经字符串导入（libmpdec 的 C API
// 不提供直接的 double 转换；字符串路径可靠且精度充分）。
mpd_t* dec_from_double(double v){
	char buf[64];
	std::snprintf(buf, sizeof(buf), "%.17g", v);
	return dec_from_string(buf);
}

// 科学计数法字符串（调用方负责 mpd_free 释放）。
char* dec_sci(const mpd_t* d){
	return mpd_to_sci(d, 0);
}

// 统一的结果状态检查：除零与运算错误转为 Pycp 异常；舍入（Inexact/Rounded）
// 属正常行为，不报错。
void dec_check_status(uint32_t status, const char* op){
	if (status & MPD_Division_by_zero){
		throw ValueError("Division by zero.");
	}
	if (status & MPD_Errors){
		throw ValueError(std::string("Decimal ") + op + " failed.");
	}
}

// 二元算术辅助：op(a, b) -> result（结果已按上下文 finalize）。
using DecOp = void (*)(mpd_t*, const mpd_t*, const mpd_t*,
                       const mpd_context_t*, uint32_t*);

Object* dec_binop(const mpd_t* a, const mpd_t* b, DecOp op, const char* opname){
	mpd_t* r = dec_new();
	uint32_t status = 0;
	op(r, a, b, &dec_ctx(), &status);
	dec_check_status(status, opname);
	return New<Decimal>(r);
}

// mpd_qcmp 返回 -1/0/1；NaN 参与比较时返回 INT_MAX 并置
// MPD_Invalid_operation（无序）。
constexpr int DEC_CMP_UNORDERED = INT_MAX;

// 三方比较：返回 -1/0/1。无序（NaN）时抛 ValueError（除 EQ/NE 由调用方特判）。
int dec_compare(const mpd_t* a, const mpd_t* b){
	uint32_t status = 0;
	int r = mpd_qcmp(a, b, &status);
	if (r == DEC_CMP_UNORDERED || (status & MPD_Errors)){
		throw ValueError("Cannot compare Decimal with NaN.");
	}
	return r;
}

} // anonymous namespace

// ------------------------------------------------------------
// 构造 / 析构
// ------------------------------------------------------------
Decimal::Decimal() : Decimal(INT64_C(0)){}

Decimal::Decimal(const std::string& value) : Object("Decimal"){
	set_type_info(PycpTypeId::Decimal, PycpTypeFlag::Hashable);
	this->_value = dec_from_string(value);
}

Decimal::Decimal(int64_t value) : Object("Decimal"){
	set_type_info(PycpTypeId::Decimal, PycpTypeFlag::Hashable);
	this->_value = dec_from_i64(value);
}

Decimal::Decimal(double value) : Object("Decimal"){
	set_type_info(PycpTypeId::Decimal, PycpTypeFlag::Hashable);
	this->_value = dec_from_double(value);
}

Decimal::Decimal(Decimal* value) : Object("Decimal"){
	set_type_info(PycpTypeId::Decimal, PycpTypeFlag::Hashable);
	if (value == nullptr){
		throw TypeError("Cannot construct Decimal from null object.");
	}
	this->_value = dec_new();
	uint32_t status = 0;
	mpd_qcopy(this->_value, value->_value, &status);
	if (status & MPD_Errors){
		dec_free(this->_value);
		this->_value = nullptr;
		throw ValueError("Cannot construct Decimal from Decimal object.");
	}
}

Decimal::Decimal(Object* obj) : Object("Decimal"){
	set_type_info(PycpTypeId::Decimal, PycpTypeFlag::Hashable);
	if (obj == nullptr){
		throw TypeError("Cannot construct Decimal from null object.");
	}
	// 经 __string__ 拿到文本形式再解析（Integer/String/Float 通用，
	// 对齐 Python Decimal(obj) 的 str(obj) 语义）。
	// 注意所有权：String::__string__ 返回 Borrowed（this），其余类型返回
	// Owned（New<String>）。String 入口直接取值，避免误 Decref 常量池
	// 对象（LOAD_CONST 推入的字符串为借用引用）。
	if (IsString(obj)){
		this->_value = dec_from_string(static_cast<String*>(obj)->get_value());
		return;
	}
	Object* s = obj->__string__();
	std::string text = static_cast<String*>(s)->get_value();
	Decref(s);
	this->_value = dec_from_string(text);
}

Decimal::~Decimal(){
	dec_free(this->_value);
	this->_value = nullptr;
}

// 内部构造：接管 mpd_t 所有权（refcount=1，Owned）。
Decimal::Decimal(mpd_t* value) : Object("Decimal"){
	set_type_info(PycpTypeId::Decimal, PycpTypeFlag::Hashable);
	this->_value = value;
}

const mpd_t* Decimal::get_value() const{
	return this->_value;
}

double Decimal::to_double() const{
	char* s = dec_sci(this->_value);
	double v = std::strtod(s, nullptr);
	mpd_free(s);
	return v;
}

std::string Decimal::to_string() const{
	char* s = dec_sci(this->_value);
	std::string out(s);
	mpd_free(s);
	return out;
}

Object* Decimal::FromString(const std::string& value){
	return New<Decimal>(value);
}

// ------------------------------------------------------------
// 转换 / 一元运算
// ------------------------------------------------------------
Object* Decimal::__integer__(){
	uint32_t status = 0;
	int64_t v = mpd_qget_i64(this->_value, &status);
	if (status & MPD_Errors){
		throw ValueError("Cannot convert Decimal to Integer: value out of range.");
	}
	return Integer::FromLong(v);
}

Object* Decimal::__float__(){
	return New<Float>(this->to_double());
}

Object* Decimal::__string__(){
	return New<String>(this->to_string());
}

Object* Decimal::__raw_string__(){
	return __string__();
}

Object* Decimal::__hash__(){
	// 以文本表示哈希（同值同串同哈希；不做 Decimal/Float 交叉一致性）。
	Object* s = New<String>(this->to_string());
	Object* h = s->__hash__();
	Decref(s);
	return h;
}

Object* Decimal::__boolean__(){
	// 非零为 True，零为 False（NaN 为真，与 Python 一致）。
	return mpd_iszero(this->_value) ? Boolean::False() : Boolean::True();
}

Object* Decimal::__negation__(){
	mpd_t* r = dec_new();
	uint32_t status = 0;
	mpd_qcopy_negate(r, this->_value, &status);
	dec_check_status(status, "negation");
	return New<Decimal>(r);
}

// ------------------------------------------------------------
// 二元算术
// ------------------------------------------------------------
// 右操作数数值提升辅助：
//   Integer / Boolean -> 精确 int64 路径
//   Decimal           -> 本类型路径
//   Float             -> 提升为 Float 结果（Decimal 转 double，有精度损失）
enum class DecRhs { kNone, kDecimal, kInteger, kFloat };

static DecRhs classify_rhs(Object* other, const mpd_t** dec, int64_t* i){
	if (other == nullptr) return DecRhs::kNone;
	if (IsExact<Decimal>(other)){
		*dec = static_cast<Decimal*>(other)->get_value();
		return DecRhs::kDecimal;
	}
	if (IsInteger(other)){
		*i = static_cast<Integer*>(other)->get_value();
		return DecRhs::kInteger;
	}
	if (IsExact<Float>(other)){
		return DecRhs::kFloat;
	}
	return DecRhs::kNone;
}

Object* Decimal::__addition__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_binop(this->_value, d, mpd_qadd, "addition");
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			Object* r = dec_binop(this->_value, b, mpd_qadd, "addition");
			dec_free(b);
			return r;
		}
		case DecRhs::kFloat:
			return New<Float>(this->to_double() + static_cast<Float*>(other)->get_value());
		default:
			throw TypeError("Unsupported to add.");
	}
}

Object* Decimal::__subtraction__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_binop(this->_value, d, mpd_qsub, "subtraction");
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			Object* r = dec_binop(this->_value, b, mpd_qsub, "subtraction");
			dec_free(b);
			return r;
		}
		case DecRhs::kFloat:
			return New<Float>(this->to_double() - static_cast<Float*>(other)->get_value());
		default:
			throw TypeError("Unsupported to subtract.");
	}
}

Object* Decimal::__multiplication__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_binop(this->_value, d, mpd_qmul, "multiplication");
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			Object* r = dec_binop(this->_value, b, mpd_qmul, "multiplication");
			dec_free(b);
			return r;
		}
		case DecRhs::kFloat:
			return New<Float>(this->to_double() * static_cast<Float*>(other)->get_value());
		default:
			throw TypeError("Unsupported to multiply.");
	}
}

Object* Decimal::__division__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_binop(this->_value, d, mpd_qdiv, "division");
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			Object* r = dec_binop(this->_value, b, mpd_qdiv, "division");
			dec_free(b);
			return r;
		}
		case DecRhs::kFloat: {
			double rf = static_cast<Float*>(other)->get_value();
			if (rf == 0.0){
				throw ValueError("Division by zero.");
			}
			return New<Float>(this->to_double() / rf);
		}
		default:
			throw TypeError("Unsupported to divide.");
	}
}

Object* Decimal::__power__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_binop(this->_value, d, mpd_qpow, "power");
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			Object* r = dec_binop(this->_value, b, mpd_qpow, "power");
			dec_free(b);
			return r;
		}
		case DecRhs::kFloat:
			return New<Float>(std::pow(this->to_double(), static_cast<Float*>(other)->get_value()));
		default:
			throw TypeError("Unsupported to power.");
	}
}

// ------------------------------------------------------------
// 比较运算（返回小整数池 Integer 0/1，PERMANENT）
// ------------------------------------------------------------
Object* Decimal::__less_than__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_compare(this->_value, d) < 0 ? Integer::instances[1] : Integer::instances[0];
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			bool r = dec_compare(this->_value, b) < 0;
			dec_free(b);
			return r ? Integer::instances[1] : Integer::instances[0];
		}
		case DecRhs::kFloat:
			return this->to_double() < static_cast<Float*>(other)->get_value()
				? Integer::instances[1] : Integer::instances[0];
		default:
			throw TypeError("Unsupported to compare.");
	}
}

Object* Decimal::__less_equal__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_compare(this->_value, d) <= 0 ? Integer::instances[1] : Integer::instances[0];
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			bool r = dec_compare(this->_value, b) <= 0;
			dec_free(b);
			return r ? Integer::instances[1] : Integer::instances[0];
		}
		case DecRhs::kFloat:
			return this->to_double() <= static_cast<Float*>(other)->get_value()
				? Integer::instances[1] : Integer::instances[0];
		default:
			throw TypeError("Unsupported to compare.");
	}
}

Object* Decimal::__equal__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: {
			uint32_t status = 0;
			return mpd_qcmp(this->_value, d, &status) == 0
				? Boolean::True() : Boolean::False();
		}
		case DecRhs::kInteger: {
			uint32_t status = 0;
			mpd_t* b = dec_from_i64(i);
			bool r = (mpd_qcmp(this->_value, b, &status) == 0);
			dec_free(b);
			return r ? Boolean::True() : Boolean::False();
		}
		case DecRhs::kFloat:
			return this->to_double() == static_cast<Float*>(other)->get_value()
				? Boolean::True() : Boolean::False();
		default:
			// 不同类型直接判不等（对齐 Python: Decimal("1") == "x" -> False）。
			return Boolean::False();
	}
}

Object* Decimal::__not_equal__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: {
			uint32_t status = 0;
			return mpd_qcmp(this->_value, d, &status) != 0 ? Integer::instances[1] : Integer::instances[0];
		}
		case DecRhs::kInteger: {
			uint32_t status = 0;
			mpd_t* b = dec_from_i64(i);
			bool r = (mpd_qcmp(this->_value, b, &status) != 0);
			dec_free(b);
			return r ? Integer::instances[1] : Integer::instances[0];
		}
		case DecRhs::kFloat:
			return this->to_double() != static_cast<Float*>(other)->get_value()
				? Integer::instances[1] : Integer::instances[0];
		default:
			throw TypeError("Unsupported to compare.");
	}
}

Object* Decimal::__greater_than__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_compare(this->_value, d) > 0 ? Integer::instances[1] : Integer::instances[0];
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			bool r = dec_compare(this->_value, b) > 0;
			dec_free(b);
			return r ? Integer::instances[1] : Integer::instances[0];
		}
		case DecRhs::kFloat:
			return this->to_double() > static_cast<Float*>(other)->get_value()
				? Integer::instances[1] : Integer::instances[0];
		default:
			throw TypeError("Unsupported to compare.");
	}
}

Object* Decimal::__greater_equal__(Object* other){
	const mpd_t* d; int64_t i;
	switch (classify_rhs(other, &d, &i)){
		case DecRhs::kDecimal: return dec_compare(this->_value, d) >= 0 ? Integer::instances[1] : Integer::instances[0];
		case DecRhs::kInteger: {
			mpd_t* b = dec_from_i64(i);
			bool r = dec_compare(this->_value, b) >= 0;
			dec_free(b);
			return r ? Integer::instances[1] : Integer::instances[0];
		}
		case DecRhs::kFloat:
			return this->to_double() >= static_cast<Float*>(other)->get_value()
				? Integer::instances[1] : Integer::instances[0];
		default:
			throw TypeError("Unsupported to compare.");
	}
}

// Decimal 全部方法的方法表（一元/算术/比较等魔术方法，native 为 nullptr）。
const std::vector<MethodEntry>& Decimal_method_table() {
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

Object* Decimal::__inspect__() {
	// 收集基类 members_ 名 + 方法表方法名 + 通用属性名，定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	for (const MethodEntry& e : Decimal_method_table()) CollectUniqueName(names, e.name);
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

void Decimal::Initialize(){
	// mpdecimal 无需全局初始化；预留：上下文/陷阱配置钩子。
}

void Decimal::Finalize(){
	// 与 Initialize 对称，当前无资源需释放。
}

} // namespace Pycp
