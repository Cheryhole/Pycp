#ifndef PYCP_PYCP_STDLIB_HPP
#define PYCP_PYCP_STDLIB_HPP

// =============================================================
// Pycp 标准库公开头（动态库 Pycp.so / Pycp.dll / Pycp.dylib）
//
// Pycp 库提供数据类型转换等内建功能：
//   - Pycp.String(x)：内置类型类，调用时返回字符串表示（语义对齐 Python str(x)）。
//   - Pycp.Integer(x)：内置类型类，调用时返回整数（语义对齐 Python int(x)）。
//   - Pycp.Object：基类，含默认空 __initialize__（供 super() 调用）。
//   - Pycp.public / Pycp.private：可见性装饰器函数，功能与 classtools
//     库中的同名函数完全一致（设置被装饰对象的可见性后原样返回）。
//
// 动态库导出统一入口符号 PycpModuleInit（extern "C"），由运行时
// VM::load_module 经 LoadNativeModule 的 dlsym("PycpModuleInit") 调用。
// =============================================================

namespace Pycp {

class Module; // 前置声明（完整定义见 runtime 的 backend/include/PycpModule.hpp）

// 本库的模块名（import Pycp 时匹配）。
constexpr const char* MODULE_NAME = "Pycp";

// 动态库入口（extern "C" 定义于 PycpModule.cpp）。
extern "C" Module* PycpModuleInit();

} // namespace Pycp

#endif // PYCP_PYCP_STDLIB_HPP
