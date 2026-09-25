#ifndef PYCP_COMPILE_STDLIB_HPP
#define PYCP_COMPILE_STDLIB_HPP

// =============================================================
// pycp compile 标准库公开头（动态库 compile.so / compile.dll / .dylib）
//
// compile 模块提供语言内的编译与执行（对齐 Python 的 compile() / exec()）：
//   - compile.compile(source [, filename])：源码 -> 字节码模块对象
//       （结构化可遍历，见 bytecode 模块的字段说明）。
//   - compile.exec(code [, globals])：执行。
//       code    : 字节码模块对象（compile.compile 的返回值）或源码字符串；
//       globals : 可选的 Map。给定则以其为全局命名空间执行，并把执行后
//                 新增 / 改动的名字写回（对齐 exec(code, globals)）；
//                 省略 / None 则使用全新全局命名空间。
//
// 本模块只链接 PycpRuntime：源码 -> 字节码走宿主 SourceStringCompiler 钩子，
// 字节码 -> 执行走运行时 VM（可执行 compile.compile 返回的模块对象）。
//
// 动态库导出入口符号 PycpModule_compile（extern "C"，按模块名导出）。
// =============================================================

namespace Pycp {

class Module; // 前置声明（完整定义见 runtime 的 object/PycpModule.hpp）

// 本库的模块名（import compile 时匹配）。
constexpr const char* MODULE_NAME = "compile";

// 动态库入口（extern "C" 定义于 PycpCompileModule.cpp）。
extern "C" Module* PycpModule_compile();

} // namespace Pycp

#endif // PYCP_COMPILE_STDLIB_HPP
