#ifndef PYCP_CONFIG_HPP
#define PYCP_CONFIG_HPP

// =============================================================
// Pycp 运行时统一配置
//   - 版本号（消除 CMakeLists 与 pybind11 两处硬编码不同步）
//   - 跨平台 ABI 导出宏 PYCP_API（C++ 风格，保留 namespace/重载）
//   - GC 策略开关与命名常量
// =============================================================

#include <cstdint>

namespace Pycp {

// 版本号：单一事实来源，CMake / pybind11 / 运行时均读取此处
constexpr const char* PYCP_VERSION = "1.0.0";

// GC 策略开关
//   PYCP_GC_RC       : 引用计数（默认开启，refcount==0 立即释放）
//   PYCP_GC_CYCLE    : 标记-清除兜底（处理循环引用，由 GC_Collect 触发）
#ifndef PYCP_GC_RC
	#define PYCP_GC_RC 1
#endif
#ifndef PYCP_GC_CYCLE
	#define PYCP_GC_CYCLE 1
#endif

// =============================================================
// 导出符号命名约定（单一、明确规则）
//
// 规则由“声明所在位置”自动决定，与宏无关：
//
//   1) 命名空间内部导出（namespace Pycp { ... }）
//        - 采用 C++ 风格 ABI，保留 namespace 作用域，
//          支持重载、可被 C++ 直接 #include 使用。
//        - 符号【不】添加 PYCP 前缀，仅以 PYCP_API 标记可见性。
//        - 例：Pycp::GC_Track / Pycp::Add / Pycp::Initialize
//
//   2) C 语言方式导出（extern "C" 全局作用域）
//        - 供 C / FFI / 纯 C 链接器使用，无命名空间、无重载。
//        - 符号【保留】PYCP 前缀，以 PYCP_C_API 标记。
//        - 例：PYCP_GC_Track / PYCP_Add / PYCP_Initialize
//
// 两类宏仅控制可见性/链接属性，不改变命名本身；
// 前缀是否添加由上述位置规则决定。
// =============================================================

// C++ 风格导出（命名空间内部，无 PYCP 前缀）
#ifdef _WIN32
	#ifdef PYCP_BUILD_DLL
		#define PYCP_API __declspec(dllexport)
	#else
		#define PYCP_API __declspec(dllimport)
	#endif
#else
	#define PYCP_API __attribute__((visibility("default")))
#endif

// C 语言风格导出（extern "C" 全局，保留 PYCP 前缀）
#ifdef _WIN32
	#ifdef PYCP_BUILD_DLL
		#define PYCP_C_API extern "C" __declspec(dllexport)
	#else
		#define PYCP_C_API extern "C" __declspec(dllimport)
	#endif
#else
	#define PYCP_C_API extern "C" __attribute__((visibility("default")))
#endif

} // namespace Pycp

#endif // PYCP_CONFIG_HPP
