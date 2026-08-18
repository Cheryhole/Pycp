#ifndef PYCP_CODEGEN_HPP
#define PYCP_CODEGEN_HPP

// =============================================================
// Pycp 代码生成器（Codegen）
//
// 将前端 AST（PycpAstNode.hpp 的 Program）编译为字节码模块
// （PycpBytecode.hpp 的 BC::Module），供 VM 执行或 AOT 翻译。
// =============================================================

#include "PycpAstNode.hpp"
#include "PycpBytecode.hpp"

namespace Pycp {
namespace Codegen {

// 将 Program AST 编译为字节码模块。
//   program : 顶层程序 AST（由 parser 生成）。
// 返回编译好的 BC::Module（code_objects[0] 为 <module> 顶层代码）。
BC::Module Compile(Ast::Program* program);

} // namespace Codegen
} // namespace Pycp

#endif // PYCP_CODEGEN_HPP
