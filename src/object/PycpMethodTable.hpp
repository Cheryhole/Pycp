#ifndef PYCP_METHOD_TABLE_HPP
#define PYCP_METHOD_TABLE_HPP

// =============================================================
// 原生函数签名与「方法表」最小类型头（内部头，非扩展作者入口）
//
// 扩展作者唯一需要 include 的是 PycpExtension.hpp；本头只提供三个
// 互相独立的最小类型：
//   PycpCFunction  —— 原生函数统一签名（容器形态，见下）
//   MethodEntry    —— 方法表条目
//   MethodTableFn  —— 方法表访问器
//
// 之所以单独成文件：PycpList.hpp / PycpMap.hpp 等类型头需要
// MethodEntry，而它们经 PycpABI.hpp 参与环形包含
// （PycpFunction.hpp -> PycpABI.hpp -> PycpList.hpp -> ...）。
// 本头只依赖 Object 前向声明与 <vector>，可安全地出现在任何一层。
// =============================================================

#include <cstddef>
#include <vector>

namespace Pycp {

class Object;
class FixedList;   // 位置参数容器（tuple 语义）
class Map;         // 关键字参数字典

// =============================================================
// 原生函数统一调用签名（容器形态）
//
//   Object* (*)(Object* self, FixedList* args, Map* kwargs)
//
// 语义：
//   self   : 接收者。实例方法 / 魔术方法由 BoundMethod 注入；模块级函数、
//            自由函数与构造器为 nullptr（未绑定方法调用 `Class.m(obj, ...)`
//            由 Function::invoke 的数组重载把首个实参提升为 self）。
//   args   : 位置参数容器（FixedList，只读输入；不可变故可安全共享）。
//   kwargs : 关键字参数字典（Map，只读输入；当前语言层无关键字实参
//            来源，恒为 Extension::EmptyKwargs()）。
//
// 参数个数 / 名字的校验由 Pycp::Extension 的参数规范表在业务函数内完成
// （见 PycpExtension.hpp），本层不做任何检查。
// =============================================================
using PycpCFunction = Object* (*)(Object* self, FixedList* args, Map* kwargs);

// 单个方法描述。
struct MethodEntry {
	const char*   name;    // 方法名
	PycpCFunction native;  // 原生实现；nullptr 表示魔术方法（统一分派）
};

// 方法表访问器类型：返回该对象全部方法的唯一权威清单。
using MethodTableFn = const std::vector<MethodEntry>& (*)();

} // namespace Pycp

#endif // PYCP_METHOD_TABLE_HPP
