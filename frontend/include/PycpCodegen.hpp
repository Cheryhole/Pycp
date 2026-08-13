#ifndef PYCP_CODEGEN_HPP
#define PYCP_CODEGEN_HPP

// =============================================================
// Pycp 代码生成器（AST -> 栈式字节码）
//
// 输入：parsef() 产出的 Pycp::Ast::Program
// 输出：Pycp::BC::Module（常量池 + 符号表 + 代码对象表）
//
// 支持子集：
//   - 赋值、表达式语句
//   - 整数 / 字符串 / None 字面量
//   - 算术（+ - * /）、一元取负
//   - 比较（< <= > >= == !=）
//   - if / elif / else
//   - 函数定义（func name(params){}）与匿名函数（func(params){}）
//   - 函数调用、return
//
// 作用域约定：
//   - 顶层语句的变量为全局变量（走 symtab + globals map）
//   - 函数内变量为局部变量（参数 + 函数体赋值目标），记录在 code.names/nlocals
//   - 闭包：匿名函数捕获外层局部变量（编译期将外层变量名加入其 names，
//     但运行时通过 captured 链查找，见 VM）
// =============================================================

#include "PycpAstNode.hpp"
#include "PycpBytecode.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Pycp::Codegen {

// 编译入口：将 AST Program 编译为字节码 Module。
//   module.code_objects[0] 为顶层 "<module>" 代码。
//   顶层变量全部映射为全局符号（symtab 去重）。
Pycp::BC::Module Compile(Pycp::Ast::Program* program);

} // namespace Pycp::Codegen

#endif // PYCP_CODEGEN_HPP
