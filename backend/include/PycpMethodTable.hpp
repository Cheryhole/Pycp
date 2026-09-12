#ifndef PYCP_METHOD_TABLE_HPP
#define PYCP_METHOD_TABLE_HPP

// =============================================================
// 方法表（MethodTable）——内置类型「全部方法」的唯一权威来源
//
// 每个内置类型（List / Map / String / Integer / Boolean / Object / File）
// 提供一份静态方法表 Xxx_method_table()，列出其全部方法（公开方法 +
// 魔术方法）。类型类的注册（register_object）与实例的 __inspect__
// 均从该方法表派生，消除「注册处」与「__inspect__」双份硬编码不一致。
//
// 方法来源约定（MethodEntry.native）：
//   - 非空：公开方法，直接引用类型 .cpp 内的实现函数（如 _list_length）。
//   - 空  ：魔术方法，注册时经 GetMagicMethodFunction(name) 取得统一
//           分派的 Function（类型无关，见 PycpMagic）。
//
// 本头文件独立自包含（仅依赖 Object 前向声明与 <vector>），以避免
// PycpList.hpp / PycpMap.hpp include PycpFunction.hpp 时经
// PycpFunction.hpp -> PycpABI.hpp -> PycpList.hpp 形成环形包含。
// =============================================================

#include <cstddef>
#include <vector>

namespace Pycp {

class Object;

// 统一原生函数调用签名（与 PycpFunction.hpp 的 PycpCFunction 一致）。
// 相同类型的 using 别名允许重复声明，故此处前置定义与其它头文件不冲突。
using PycpCFunction = Object* (*)(Object* self, Object** argv, std::size_t argc);

// 单个方法描述。
struct MethodEntry {
	const char*        name;    // 方法名
	PycpCFunction native;  // 原生实现；nullptr 表示魔术方法（统一分派）
};

// 方法表访问器类型：返回该对象全部方法的唯一权威清单。
using MethodTableFn = const std::vector<MethodEntry>& (*)();

} // namespace Pycp

#endif // PYCP_METHOD_TABLE_HPP
