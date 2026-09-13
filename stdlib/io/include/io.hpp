#ifndef PYCP_IO_LIBRARY_HPP
#define PYCP_IO_LIBRARY_HPP

// =============================================================
// Pycp io 标准库公开头（动态库 io.so / io.dll / io.dylib）
//
// io 库提供标准输入输出对象与函数：
//   - io.stdin  / io.stdout / io.stderr 为 File，
//     支持 .write（仅字符串）、.flush、.readline 方法。
//   - io.print(*args, sep=" ", end="\n", file=io.stdout, flush=False)：
//     对齐 Python 内建 print（sep/end 接受 String 或 None；file 为任何有
//     write 方法的对象，None 回退 io.stdout；flush 经 __boolean__ 真值化，
//     为真时调 file.flush() 强制刷出）。
//   - io.input(prompt)：单参数，打印提示（不换行）后读取一行，
//     返回截止至换行符之前的字符串（对齐 Python3 input）。
//
// 动态库导出入口符号 PycpModule_io（extern "C"，按模块名导出），由运行时
// VM::load_module 经 LoadNativeModule 的 dlsym("PycpModule_io") 调用，
// 返回构建好的 Module。
// =============================================================

#include "PycpFile.hpp"
#include "PycpConfig.hpp"

namespace Pycp {

class Module; // 前置声明（完整定义见 runtime 的 backend/include/PycpModule.hpp）

// 本库的模块名（import io 时匹配）。
constexpr const char* MODULE_NAME = "io";

// 动态库入口（extern "C" 定义于 io.cpp）。
extern "C" Module* PycpModule_io();

} // namespace Pycp

#endif // PYCP_IO_LIBRARY_HPP
