#ifndef PYCP_MAGIC_HPP
#define PYCP_MAGIC_HPP

// =============================================================
// 魔术方法可调用辅助（PycpMagic）
//
// 目的：让对象的内置魔术方法（__addition__ / __integer__ /
// __iterator__ / __equal__ 等 C++ 虚方法）也能通过属性访问 +
// 调用（obj.__xxx__()）在代码中使用。
//
// 机制：
//   各内置类型的 __get_attribute__ 对 "__xxx__" 魔术方法名回退到
//   GetMagicMethodFunction(name)，返回一个"惰性创建的 Function"，
//   其 native 实现 _magic_fn 依据 Function 名分派到接收者的
//   C++ 虚方法。GetAttr 会把该 Function 包装成 BoundMethod，
//   调用时 self 自动绑定到接收者对象。
// =============================================================

#include "PycpObject.hpp"

#include <string>
#include <vector>

namespace Pycp {

// 返回一个可调用的魔术方法 Function（缓存的，仅创建一次）。
// 若 name 是已识别的魔术方法名则返回其 Function（Borrowed，
// 由调用方决定生命周期）；否则返回 nullptr（表示非魔术方法）。
//
// 注意：返回的 Function 生命周期由内部缓存持有（GC 常驻），
// 调用方只需按普通属性处理（GetAttr 会包装成 BoundMethod）。
Pycp::Object* GetMagicMethodFunction(const std::string& name);

// 判断 name 是否为已识别的魔术方法名（供 __inspect__ 枚举）。
bool IsMagicMethodName(const std::string& name);

// 把一组成员名构造成一个 List（每元素为单字符/字符串 String）。
// 供各类型的 __inspect__ 使用（返回 Owned Object*，即 List）。
Object* BuildNameList(const std::vector<std::string>& names);

// 处理 __name__ 属性访问：返回该对象类型名（type_name()）对应的
// String。供各类型的 __get_attribute__ 在判别 name == "__name__" 时
// 调用：GetNameAttribute(receiver) 返回 receiver->type_name() 的 String
// （Owned，由调用方管理）。
Object* GetNameAttribute(Object* receiver);

} // namespace Pycp

#endif // PYCP_MAGIC_HPP