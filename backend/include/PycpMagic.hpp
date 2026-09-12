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

// 把一组成员名构造成一个 FixedList（每元素为 String）。
// 供各类型的 __inspect__ 使用（返回 Owned Object*，即 FixedList）。
Object* BuildNameList(const std::vector<std::string>& names);

// 所有内置类型通用的枚举属性名（非方法，仅供 __inspect__ 枚举，
// 如 __class__）。各类型 __inspect__ 在方法表之外补充这些名字。
const std::vector<std::string>& CommonInspectNames();

// 向收集器 out 去重追加名称 name（以 String 存入），供返回 FixedList 的
// __inspect__ 先收集再定型（FixedList 不可变，无法像 List 那样直接 append）。
void CollectUniqueName(std::vector<Object*>& out, const std::string& name);

// 处理 __name__ 属性访问：返回该对象类型名（type_name()）对应的
// String。供各类型的 __get_attribute__ 在判别 name == "__name__" 时
// 调用：GetNameAttribute(receiver) 返回 receiver->type_name() 的 String
// （Owned，由调用方管理）。
Object* GetNameAttribute(Object* receiver);

} // namespace Pycp

#endif // PYCP_MAGIC_HPP