#ifndef PYCP_MATHS_STDLIB_HPP
#define PYCP_MATHS_STDLIB_HPP

// =============================================================
// Pycp maths 标准库公开头（maths 动态库 / libPycpExt_maths.a）
//
// 数值类型的数学方法独立成库（不进核心类型，见 docs/CORE_METHODS.md §13）：
//   - 通用（Integer/Float/Decimal）：abs/floor/ceil/round/pow/divmod
//   - 与 CPython `math` 同名：trunc/fabs/copysign/fmod/modf/isnan/isinf/
//     isfinite/isclose/sqrt/exp/log/log2/log10/三角与双曲/hypot/degrees/
//     radians/fsum/prod 等
//   - Integer 专属：bit_length/conjugate/gcd/lcm/factorial/isqrt
//   - Float 专属：is_integer/as_integer_ratio
//   - 常量：pi/e/tau/inf/nan
//
// 入口：导出符号 PycpModule_maths（按模块名命名，extern "C"）。
// =============================================================

#include "object/PycpModule.hpp"

namespace Pycp {

// 构造 maths 模块（含全部函数与常量）。
Module* make_maths_module();

} // namespace Pycp

#endif // PYCP_MATHS_STDLIB_HPP
