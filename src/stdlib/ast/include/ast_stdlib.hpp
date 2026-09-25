#ifndef PYCP_AST_STDLIB_HPP
#define PYCP_AST_STDLIB_HPP

// =============================================================
// pycp ast 标准库公开头（动态库 ast.so / ast.dll / ast.dylib）
//
// ast 模块提供源码的抽象语法树内省：
//   - ast.parse(source [, filename])：解析源码，返回可遍历的节点树。
//       节点对象的 type_name 即节点类型名（Program / FunctionExpression /
//       AssignmentStatement ...），字段为属性（node.body / node.value /
//       node.target / node.lineno / node.type ...），str(node) 给出该
//       节点的 to_string() 文本。
//   - ast.dump(node)：返回节点的 to_string() 文本。
//   - ast.type_of(node)：返回节点类型名字符串。
//
// 本模块只链接 PycpRuntime；解析能力经宿主注册的 SourceParser 钩子获取
// （AOT 独立程序未注册钩子，故 ast.parse 不可用）。
//
// 动态库导出入口符号 PycpModule_ast（extern "C"，按模块名导出）。
// =============================================================

namespace Pycp {

class Module; // 前置声明（完整定义见 runtime 的 object/PycpModule.hpp）

// 本库的模块名（import ast 时匹配）。
constexpr const char* MODULE_NAME = "ast";

// 动态库入口（extern "C" 定义于 PycpAstModule.cpp）。
extern "C" Module* PycpModule_ast();

} // namespace Pycp

#endif // PYCP_AST_STDLIB_HPP
