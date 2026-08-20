#ifndef PYCP_BYTECODE_DUMP_HPP
#define PYCP_BYTECODE_DUMP_HPP

// =============================================================
// Pycp 字节码查看（dump）接口
//
// 将字节码 Module 的内存表示格式化为可读文本，输出到指定流。
// 该能力属于字节码/运行时层的诊断工具，与 Serialize/Deserialize
// 同层，封装在 PycpRuntime 库内；CLI（pycp）链接该库后即可调用。
//
// 仅暴露 DumpModule 一个入口，所有排版细节（操作码名称、常量文本、
// 指令带行号与操作数注释、模块头/常量池/符号表/代码对象汇总）均
// 在 PycpBytecodeDump.cpp 内部实现，调用方无需关心。
// =============================================================

#include "PycpBytecode.hpp"  // Module / Op / CompareOp / Constant / FORMAT_VERSION_*

#include <iostream>          // std::ostream / std::cout（默认输出流）
#include <ostream>

namespace Pycp::BC {

// 将字节码 Module 格式化为可读文本并写入 os（默认 std::cout）。
// 输出内容包含：文件头（格式版本 / 源路径）、常量池、符号表、
// 各代码对象（方法签名、字段、指令流与行号）。
void DumpModule(const Module& module, std::ostream& os = std::cout);

} // namespace Pycp::BC

#endif // PYCP_BYTECODE_DUMP_HPP
