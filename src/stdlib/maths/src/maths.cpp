// =============================================================
// maths 标准库实现
//
// 设计见 docs/CORE_METHODS.md §13：数值类型的数学能力集中于此，核心类型
// 不新增数值方法。
//
// 语义约定：
//   - 「通用」函数对 Integer/Float/Decimal 生效，结果类型尽量保持；
//   - 与 CPython `math` 同名的超越函数接受任意数值（经 __float__），
//     统一返回 Float；
//   - 不适用于该类型的操作抛 TypeError。
// =============================================================

#include "maths_stdlib.hpp"

#include "object/PycpInteger.hpp"
#include "object/PycpFloat.hpp"
#include "object/PycpDecimal.hpp"
#include "object/PycpString.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpList.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpException.hpp"
#include "object/PycpExtension.hpp"
#include "object/PycpModule.hpp"
#include "object/PycpGC.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Pycp {

namespace {

const char* kModuleName = "maths";

// ---------- 参数/取值辅助 ----------

// 转为 double（经 __float__，接受 Integer/Float/Decimal）。
double _as_double(Object* o) {
	if (o == nullptr) {
		throw TypeError("maths: expected a number, got None.");
	}
	Object* f = o->__float__();
	if (f == nullptr || !IsExact<Float>(f)) {
		if (f != nullptr && f != o) Decref(f);
		throw TypeError("maths: expected a number, got '" + o->type_name() + "'.");
	}
	double v = static_cast<Float*>(f)->get_value();
	if (f != o) Decref(f);
	return v;
}

// 精确整数取值（Boolean 视为 Integer 子类）。
long long _as_int(Object* o, const char* fn, const char* name) {
	if (o == nullptr || !IsInteger(o)) {
		throw TypeError(std::string("maths.") + fn + "(): '" + name +
		                "' must be an Integer.");
	}
	return static_cast<Integer*>(o)->get_value();
}

Object* _mkf(double v) {
	return Float::FromDouble(v);
}

Object* _mkb(bool v) {
	return v ? Boolean::True() : Boolean::False();
}

// 收集任意可迭代对象为 Owned 元素列表。
std::vector<Object*> _iter_vec(Object* it) {
	if (it == nullptr) throw TypeError("maths: object is not iterable.");
	Object* iter = it->__iterator__();
	if (iter == nullptr) throw TypeError("maths: object is not iterable.");
	std::vector<Object*> out;
	for (;;) {
		Object* v = nullptr;
		try {
			v = iter->__next__();
		} catch (const StopIteration&) {
			break;
		}
		out.push_back(v);
	}
	Decref(iter);
	return out;
}

// ---------- 通用：abs / floor / ceil / round / pow / divmod ----------

Object* _m_abs(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"abs", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* x = r["x"];
	if (x != nullptr && IsIntegerExact(x)) {
		long long v = static_cast<Integer*>(x)->get_value();
		return Integer::FromLong(v < 0 ? -v : v);
	}
	return _mkf(std::fabs(_as_double(x)));
}

Object* _m_floor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"floor", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* x = r["x"];
	if (x != nullptr && IsIntegerExact(x)) {
		Incref(x);
		return x;
	}
	Object* iv = x->__integer__();
	if (iv == nullptr || !IsExact<Integer>(iv)) {
		if (iv != nullptr && iv != x) Decref(iv);
		throw TypeError("maths.floor(): 'x' must be a number.");
	}
	if (iv == x) Incref(iv);
	return iv;
}

Object* _m_ceil(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"ceil", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	double d = _as_double(r["x"]);
	return Integer::FromLong(static_cast<long long>(std::ceil(d)));
}

Object* _m_round(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"round", { Extension::Arg::Required("x"),
		           Extension::Arg::Optional("ndigits") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* xo = r["x"];
	Object* no = r["ndigits"];
	if (xo != nullptr && IsIntegerExact(xo) &&
	    (no == nullptr || no->type_id() == PycpTypeId::None)) {
		Incref(xo);
		return xo;   // 整数四舍五入即自身
	}
	double d = _as_double(xo);
	double nd = 0;
	bool has_nd = (no != nullptr && no->type_id() != PycpTypeId::None);
	if (has_nd) nd = static_cast<double>(_as_int(no, "round", "ndigits"));
	double scale = std::pow(10.0, nd);
	double v = std::nearbyint(d * scale) / scale;   // 银行家舍入（FE_TONEAREST）
	if (xo != nullptr && IsIntegerExact(xo) && has_nd) {
		return Integer::FromLong(static_cast<long long>(v));
	}
	return _mkf(v);
}

Object* _m_pow(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"pow", { Extension::Arg::Required("x"),
		         Extension::Arg::Required("y") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* xo = r["x"];
	Object* yo = r["y"];
	if (xo != nullptr && yo != nullptr && IsIntegerExact(xo) &&
	    IsIntegerExact(yo)) {
		long long b = static_cast<Integer*>(xo)->get_value();
		long long e = static_cast<Integer*>(yo)->get_value();
		if (e >= 0) {
			long long acc = 1;
			for (long long i = 0; i < e; ++i) acc *= b;
			return Integer::FromLong(acc);
		}
	}
	return _mkf(std::pow(_as_double(xo), _as_double(yo)));
}

Object* _m_divmod(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"divmod", { Extension::Arg::Required("x"),
		            Extension::Arg::Required("y") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* xo = r["x"];
	Object* yo = r["y"];
	if (xo != nullptr && yo != nullptr && IsIntegerExact(xo) &&
	    IsIntegerExact(yo)) {
		long long a = static_cast<Integer*>(xo)->get_value();
		long long b = static_cast<Integer*>(yo)->get_value();
		if (b == 0) throw ValueError("integer division or modulo by zero.");
		long long q = a / b;
		long long m = a % b;
		// Python 语义：商向负无穷取整。
		if (m != 0 && ((m < 0) != (b < 0))) { --q; m += b; }
		std::vector<Object*> pair;
		pair.push_back(Integer::FromLong(q));
		pair.push_back(Integer::FromLong(m));
		return FixedList::New(pair);
	}
	double a = _as_double(xo), b = _as_double(yo);
	if (b == 0.0) throw ValueError("float divmod() by zero.");
	double q = std::floor(a / b);
	std::vector<Object*> pair;
	pair.push_back(_mkf(q));
	pair.push_back(_mkf(a - q * b));
	return FixedList::New(pair);
}

// ---------- 与 CPython math 同名的函数 ----------

Object* _m_trunc(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"trunc", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* x = r["x"];
	if (x != nullptr && IsIntegerExact(x)) { Incref(x); return x; }
	return Integer::FromLong(static_cast<long long>(std::trunc(_as_double(x))));
}

Object* _m_fabs(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"fabs", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return _mkf(std::fabs(_as_double(r["x"])));
}

Object* _m_copysign(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"copysign", { Extension::Arg::Required("x"),
		              Extension::Arg::Required("y") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return _mkf(std::copysign(_as_double(r["x"]), _as_double(r["y"])));
}

Object* _m_fmod(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"fmod", { Extension::Arg::Required("x"), Extension::Arg::Required("y") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return _mkf(std::fmod(_as_double(r["x"]), _as_double(r["y"])));
}

Object* _m_modf(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"modf", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	double ip = 0;
	double fp = std::modf(_as_double(r["x"]), &ip);
	std::vector<Object*> pair;
	pair.push_back(_mkf(fp));
	pair.push_back(_mkf(ip));
	return FixedList::New(pair);
}

Object* _m_frexp(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"frexp", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	int e = 0;
	double m = std::frexp(_as_double(r["x"]), &e);
	std::vector<Object*> pair;
	pair.push_back(_mkf(m));
	pair.push_back(Integer::FromLong(e));
	return FixedList::New(pair);
}

Object* _m_ldexp(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"ldexp", { Extension::Arg::Required("x"),
		           Extension::Arg::Required("i") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	int i = static_cast<int>(_as_int(r["i"], "ldexp", "i"));
	return _mkf(std::ldexp(_as_double(r["x"]), i));
}

Object* _m_isnan(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"isnan", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* x = r["x"];
	if (x != nullptr && IsInteger(x)) return Boolean::False();
	return _mkb(std::isnan(_as_double(x)));
}

Object* _m_isinf(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"isinf", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* x = r["x"];
	if (x != nullptr && IsInteger(x)) return Boolean::False();
	return _mkb(std::isinf(_as_double(x)));
}

Object* _m_isfinite(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"isfinite", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* x = r["x"];
	if (x != nullptr && IsInteger(x)) return Boolean::True();
	return _mkb(std::isfinite(_as_double(x)));
}

Object* _m_isclose(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"isclose", { Extension::Arg::Required("a"),
		             Extension::Arg::Required("b"),
		             Extension::Arg::Optional("rel_tol"),
		             Extension::Arg::Optional("abs_tol") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	double a = _as_double(r["a"]);
	double b = _as_double(r["b"]);
	double rel = 1e-09, abs_ = 0.0;
	Object* ro = r["rel_tol"];
	Object* ao = r["abs_tol"];
	if (ro != nullptr && ro->type_id() != PycpTypeId::None) rel = _as_double(ro);
	if (ao != nullptr && ao->type_id() != PycpTypeId::None) abs_ = _as_double(ao);
	double diff = std::fabs(a - b);
	if (diff <= abs_) return Boolean::True();
	return _mkb(diff <= rel * std::fabs(b));
}

// 一元超越函数（double -> double），统一返回 Float。
#define PYCP_MATHS_UNARY(pyname, cfname, expr)                                 \
	Object* cfname(Object*, FixedList* args, Map* kwargs) {                     \
		static const Extension::ArgTable spec = Extension::CompileArgs(         \
			pyname, { Extension::Arg::Required("x") });                          \
		Extension::ArgResult r = spec.Bind(args, kwargs);                        \
		double x = _as_double(r["x"]);                                           \
		return _mkf(expr);                                                       \
	}

PYCP_MATHS_UNARY("sqrt", _m_sqrt, std::sqrt(x))
PYCP_MATHS_UNARY("exp", _m_exp, std::exp(x))
PYCP_MATHS_UNARY("log2", _m_log2, std::log2(x))
PYCP_MATHS_UNARY("log10", _m_log10, std::log10(x))
PYCP_MATHS_UNARY("sin", _m_sin, std::sin(x))
PYCP_MATHS_UNARY("cos", _m_cos, std::cos(x))
PYCP_MATHS_UNARY("tan", _m_tan, std::tan(x))
PYCP_MATHS_UNARY("asin", _m_asin, std::asin(x))
PYCP_MATHS_UNARY("acos", _m_acos, std::acos(x))
PYCP_MATHS_UNARY("atan", _m_atan, std::atan(x))
PYCP_MATHS_UNARY("sinh", _m_sinh, std::sinh(x))
PYCP_MATHS_UNARY("cosh", _m_cosh, std::cosh(x))
PYCP_MATHS_UNARY("tanh", _m_tanh, std::tanh(x))
PYCP_MATHS_UNARY("degrees", _m_degrees, x * (180.0 / 3.14159265358979323846))
PYCP_MATHS_UNARY("radians", _m_radians, x * (3.14159265358979323846 / 180.0))

#undef PYCP_MATHS_UNARY

Object* _m_log(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"log", { Extension::Arg::Required("x"),
		         Extension::Arg::Optional("base") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	double x = _as_double(r["x"]);
	Object* b = r["base"];
	if (b != nullptr && b->type_id() != PycpTypeId::None) {
		return _mkf(std::log(x) / std::log(_as_double(b)));
	}
	return _mkf(std::log(x));
}

Object* _m_atan2(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"atan2", { Extension::Arg::Required("y"), Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return _mkf(std::atan2(_as_double(r["y"]), _as_double(r["x"])));
}

Object* _m_hypot(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"hypot", { Extension::Arg::Required("x"), Extension::Arg::Required("y") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return _mkf(std::hypot(_as_double(r["x"]), _as_double(r["y"])));
}

Object* _m_fsum(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"fsum", { Extension::Arg::Required("iterable") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::vector<Object*> elems = _iter_vec(r["iterable"]);
	double sum = 0.0;
	for (Object* e : elems) { sum += _as_double(e); Decref(e); }
	return _mkf(sum);
}

Object* _m_prod(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"prod", { Extension::Arg::Required("iterable") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::vector<Object*> elems = _iter_vec(r["iterable"]);
	bool all_int = true;
	for (Object* e : elems) { if (e == nullptr || !IsIntegerExact(e)) { all_int = false; } }
	if (all_int) {
		long long acc = 1;
		for (Object* e : elems) { acc *= static_cast<Integer*>(e)->get_value(); Decref(e); }
		return Integer::FromLong(acc);
	}
	double acc = 1.0;
	for (Object* e : elems) { acc *= _as_double(e); Decref(e); }
	return _mkf(acc);
}

// ---------- Integer 专属 ----------

Object* _m_bit_length(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"bit_length", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	long long v = _as_int(r["x"], "bit_length", "x");
	unsigned long long u = (v < 0) ? static_cast<unsigned long long>(-(v + 1)) + 1ULL
	                               : static_cast<unsigned long long>(v);
	long long bits = 0;
	while (u > 0) { ++bits; u >>= 1; }
	return Integer::FromLong(bits);
}

Object* _m_conjugate(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"conjugate", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	(void)_as_int(r["x"], "conjugate", "x");
	Object* x = r["x"];
	Incref(x);
	return x;
}

long long _gcd2(long long a, long long b) {
	if (a < 0) a = -a;
	if (b < 0) b = -b;
	while (b != 0) { long long t = a % b; a = b; b = t; }
	return a;
}

Object* _m_gcd(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"gcd", { Extension::Arg::Required("a"), Extension::Arg::Required("b") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return Integer::FromLong(_gcd2(_as_int(r["a"], "gcd", "a"),
	                               _as_int(r["b"], "gcd", "b")));
}

Object* _m_lcm(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"lcm", { Extension::Arg::Required("a"), Extension::Arg::Required("b") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	long long a = _as_int(r["a"], "lcm", "a");
	long long b = _as_int(r["b"], "lcm", "b");
	if (a == 0 || b == 0) return Integer::FromLong(0);
	long long g = _gcd2(a, b);
	long long aa = a < 0 ? -a : a;
	long long bb = b < 0 ? -b : b;
	return Integer::FromLong((aa / g) * bb);
}

Object* _m_isqrt(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"isqrt", { Extension::Arg::Required("n") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	long long n = _as_int(r["n"], "isqrt", "n");
	if (n < 0) throw ValueError("isqrt() argument must be nonnegative.");
	long long x = static_cast<long long>(std::sqrt(static_cast<double>(n)));
	while ((x + 1) * (x + 1) <= n) ++x;
	while (x * x > n) --x;
	return Integer::FromLong(x);
}

Object* _m_factorial(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"factorial", { Extension::Arg::Required("n") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	long long n = _as_int(r["n"], "factorial", "n");
	if (n < 0) throw ValueError("factorial() not defined for negative values.");
	long long acc = 1;
	for (long long i = 2; i <= n; ++i) acc *= i;
	return Integer::FromLong(acc);
}

// ---------- Float 专属 ----------

Object* _m_is_integer(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"is_integer", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	double d = _as_double(r["x"]);
	return _mkb(std::isfinite(d) && std::floor(d) == d);
}

Object* _m_as_integer_ratio(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"as_integer_ratio", { Extension::Arg::Required("x") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	double d = _as_double(r["x"]);
	if (!std::isfinite(d)) {
		throw ValueError("as_integer_ratio(): cannot convert NaN/Infinity.");
	}
	// 以 2^52 为分母近似（与 CPython float 的二进制表示一致）。
	long long den = 1LL << 52;
	long long num = static_cast<long long>(std::ldexp(d, 52));
	long long g = _gcd2(num, den);
	if (g > 0) { num /= g; den /= g; }
	std::vector<Object*> pair;
	pair.push_back(Integer::FromLong(num));
	pair.push_back(Integer::FromLong(den));
	return FixedList::New(pair);
}

} // anonymous namespace

Module* make_maths_module() {
	Module* mod = Module::New(kModuleName);
	mod->set_variable("__name__", String::FromCString(kModuleName));

	// 常量
	const double kPi = 3.14159265358979323846;
	mod->set_variable("pi", Float::FromDouble(kPi));
	mod->set_variable("e", Float::FromDouble(2.71828182845904523536));
	mod->set_variable("tau", Float::FromDouble(2.0 * kPi));
	mod->set_variable("inf", Float::FromDouble(HUGE_VAL));
	mod->set_variable("nan", Float::FromDouble(NAN));

	// 通用
	mod->set_function("abs", _m_abs, /*with_keywords=*/true);
	mod->set_function("floor", _m_floor, true);
	mod->set_function("ceil", _m_ceil, true);
	mod->set_function("round", _m_round, true);
	mod->set_function("pow", _m_pow, true);
	mod->set_function("divmod", _m_divmod, true);

	// CPython math 同名
	mod->set_function("trunc", _m_trunc, true);
	mod->set_function("fabs", _m_fabs, true);
	mod->set_function("copysign", _m_copysign, true);
	mod->set_function("fmod", _m_fmod, true);
	mod->set_function("modf", _m_modf, true);
	mod->set_function("frexp", _m_frexp, true);
	mod->set_function("ldexp", _m_ldexp, true);
	mod->set_function("isnan", _m_isnan, true);
	mod->set_function("isinf", _m_isinf, true);
	mod->set_function("isfinite", _m_isfinite, true);
	mod->set_function("isclose", _m_isclose, true);
	mod->set_function("sqrt", _m_sqrt, true);
	mod->set_function("exp", _m_exp, true);
	mod->set_function("log", _m_log, true);
	mod->set_function("log2", _m_log2, true);
	mod->set_function("log10", _m_log10, true);
	mod->set_function("sin", _m_sin, true);
	mod->set_function("cos", _m_cos, true);
	mod->set_function("tan", _m_tan, true);
	mod->set_function("asin", _m_asin, true);
	mod->set_function("acos", _m_acos, true);
	mod->set_function("atan", _m_atan, true);
	mod->set_function("atan2", _m_atan2, true);
	mod->set_function("sinh", _m_sinh, true);
	mod->set_function("cosh", _m_cosh, true);
	mod->set_function("tanh", _m_tanh, true);
	mod->set_function("hypot", _m_hypot, true);
	mod->set_function("degrees", _m_degrees, true);
	mod->set_function("radians", _m_radians, true);
	mod->set_function("fsum", _m_fsum, true);
	mod->set_function("prod", _m_prod, true);

	// Integer 专属
	mod->set_function("bit_length", _m_bit_length, true);
	mod->set_function("conjugate", _m_conjugate, true);
	mod->set_function("gcd", _m_gcd, true);
	mod->set_function("lcm", _m_lcm, true);
	mod->set_function("isqrt", _m_isqrt, true);
	mod->set_function("factorial", _m_factorial, true);

	// Float 专属
	mod->set_function("is_integer", _m_is_integer, true);
	mod->set_function("as_integer_ratio", _m_as_integer_ratio, true);

	return mod;
}

// 动态库入口（符号名 PycpModule_maths，按模块名导出）。
PYCP_EXPORT_MODULE(maths) {
	return make_maths_module();
}

} // namespace Pycp
