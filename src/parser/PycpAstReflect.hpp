#ifndef PYCP_AST_REFLECT_HPP
#define PYCP_AST_REFLECT_HPP

// =============================================================
// AST 反射（前端）：把 parser 产出的 C++ AST 映射为可遍历的 pycp 对象
// -------------------------------------------------------------
// 每个节点映射为一个 pycp 对象：
//   - type_name = 节点类型名（如 "Program" / "FunctionExpression"）；
//   - 字段写入成员字典，故 node.body / node.value / node.target 等可直接
//     属性访问（null 字段以 None 呈现）；
//   - 通用属性：node.type（节点类型名字符串）、node.lineno（行号）；
//   - __string__() 返回该节点 to_string() 的文本（惰性计算）。
//
// 节点对象经 std::shared_ptr 共同持有底层 C++ AST，故即使只保留子树中的
// 某个节点、丢弃根节点，其 to_string() 依旧安全（AST 生命周期随引用延续）。
//
// 供宿主经 SourceParser 钩子注册（ast.parse）。
// =============================================================

#include "object/PycpObject.hpp"

#include <string>

namespace Pycp {
namespace Ast {

// 解析源码字符串并映射为 pycp AST 节点树（返回 Owned 根节点）。
// 解析失败时抛 Pycp::Exception（词法/语法错误已由 parser 打印，异常消息为空）。
Object* ParseToObjects(const std::string& source, const std::string& filename);

} // namespace Ast
} // namespace Pycp

#endif // PYCP_AST_REFLECT_HPP
