#ifndef PYCP_IO_LIBRARY_HPP
#define PYCP_IO_LIBRARY_HPP

// =============================================================
// Pycp io 标准库公开头（动态库 io.so / io.dll / io.dylib）
//
// io 库提供标准输入输出对象与函数：
//   - io.stdin  / io.stdout / io.stderr 为 FileObject，
//     支持 .write（仅字符串）、.readline 方法。
//   - io.print(value)：单参数，输出内容后自动附加换行符（对齐 Python3 print）。
//   - io.input(prompt)：单参数，打印提示（不换行）后读取一行，
//     返回截止至换行符之前的字符串（对齐 Python3 input）。
//
// 动态库导出统一入口符号 PycpModuleInit（extern "C"），由运行时
// VM::load_module 经 LoadNativeModule 的 dlsym("PycpModuleInit") 调用，
// 返回构建好的 ModuleObject。
// =============================================================

#include "PycpFile.hpp"
#include "PycpConfig.hpp"

namespace Pycp {

class ModuleObject; // 前置声明（完整定义见 runtime 的 backend/include/PycpModule.hpp）

// 本库的模块名（import io 时匹配）。
constexpr const char* MODULE_NAME = "io";

// 动态库入口（extern "C" 定义于 io.cpp）。
extern "C" ModuleObject* PycpModuleInit();

} // namespace Pycp

#endif // PYCP_IO_LIBRARY_HPP
