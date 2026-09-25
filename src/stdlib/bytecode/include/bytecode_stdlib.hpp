#ifndef PYCP_BYTECODE_STDLIB_HPP
#define PYCP_BYTECODE_STDLIB_HPP

// =============================================================
// pycp bytecode 标准库公开头（动态库 bytecode.so / bytecode.dll / .dylib）
//
// bytecode 模块提供字节码的结构化内省（类似 CPython 的 dis + code object）：
//   - bytecode.compile(source [, filename])：编译源码，返回字节码模块对象。
//       该对象可遍历：source_path / constants / symbols / imports /
//       code_objects / classes；code_objects 的每项含 name / nparams /
//       param_kinds / consts / names / const_refs / name_refs /
//       free_names / instructions（每条含 op / opcode / operand / line）。
//       str(模块对象) 与 bytecode.dump() 返回 DumpModule 文本。
//   - bytecode.dump(x)：x 为字节码模块对象或源码字符串，返回反汇编文本。
//
// 本模块只链接 PycpRuntime；源码 -> 字节码走宿主注册的 SourceStringCompiler
// 钩子（AOT 独立程序未注册钩子，故 bytecode.compile 不可用）。
//
// 动态库导出入口符号 PycpModule_bytecode（extern "C"，按模块名导出）。
// =============================================================

namespace Pycp {

class Module; // 前置声明（完整定义见 runtime 的 object/PycpModule.hpp）

// 本库的模块名（import bytecode 时匹配）。
constexpr const char* MODULE_NAME = "bytecode";

// 动态库入口（extern "C" 定义于 PycpBytecodeModule.cpp）。
extern "C" Module* PycpModule_bytecode();

} // namespace Pycp

#endif // PYCP_BYTECODE_STDLIB_HPP
