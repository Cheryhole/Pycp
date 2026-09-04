#ifndef PYCP_PYCP_STDLIB_HPP
#define PYCP_PYCP_STDLIB_HPP

// =============================================================
// pycp 标准库公开头（动态库 pycp.so / pycp.dll / pycp.dylib）
//
// pycp 库提供数据类型转换等内建功能：
//   - pycp.String(x)：内置类型类，调用时返回字符串表示（语义对齐 Python str(x)）。
//   - pycp.Integer(x)：内置类型类，调用时返回整数（语义对齐 Python int(x)）。
//   - pycp.Object：基类，含默认空 __initialize__（供 super() 调用）。
//   - pycp.public / pycp.private：可见性装饰器函数，功能与 classtools
//     库中的同名函数完全一致（设置被装饰对象的可见性后原样返回）。
//
// 动态库导出入口符号 PycpModule_pycp（extern "C"，按模块名导出），由运行时
// VM::load_module 经 LoadNativeModule 的 dlsym("PycpModule_pycp") 调用。
// =============================================================

namespace Pycp {

class Module; // 前置声明（完整定义见 runtime 的 backend/include/PycpModule.hpp）

// 本库的模块名（import pycp 时匹配）。
constexpr const char* MODULE_NAME = "pycp";

// 动态库入口（extern "C" 定义于 PycpModule.cpp）。
extern "C" Module* PycpModule_pycp();

} // namespace Pycp

#endif // PYCP_PYCP_STDLIB_HPP
