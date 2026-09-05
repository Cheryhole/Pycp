#ifndef PYCP_MAP_HPP
#define PYCP_MAP_HPP

// =============================================================
// Pycp Map 对象（Map）
//
// 对应 Python 的 dict 对象，行为尽可能对齐 Python dict：
//   - Pycp.Map() 构造空映射
//   - Pycp.Map(other) 转换（对齐 Python dict(x)）：
//       * other 为 Map（含 __map__ 视图）-> 独立浅拷贝/材料化
//       * other 提供 keys() 与 __get_item__(k) -> 按键取值构造
//         （自定义类同样适用）
//       * other 为二元组 List（[[k, v], ...]）-> 逐对构造
//       * 其余类型 -> TypeError
//   - keys() 返回含全部键的 List（视图模式返回成员名）
//   - 下标设置 self[key] = value（__set_item__）
//   - 下标获取 self[key]（__get_item__）；键不存在抛 KeyError
//   - length() 方法获取键值对个数
//   - Map 本身不可哈希（未 override __hash__，继承 Object 默认抛
//     "unhashable type: Map"），故 Map 不能作为另一 Map 的键
//   - 字符串表示 "{k: v, ...}"（元素经 __string__ 转换，字符串加引号）
//
// 存储：以 Object* 为键的原生哈希表 std::unordered_map<Object*, Object*,
// MapKeyHash, MapKeyEqual>。键/值对象由 Map 持有引用（插入 Incref，
// 替换/删除/析构 Decref）。支持任意可哈希对象做键（依赖键对象的 __hash__
// / __equal__ 虚方法，如 Integer/String），对齐 Python dict 的哈希语义。
// =============================================================

#include "PycpObject.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpList.hpp"

#include <functional>
#include <unordered_map>

namespace Pycp {

// 统一原生函数调用签名（与 PycpFunction.hpp 中的 PycpNativeFunction 一致）。
using PycpNativeFunction = Object* (*)(Object* self, Object** argv, std::size_t argc);

class Function;

// 键哈希：调用键对象的 __hash__() 取 Integer 值。
struct MapKeyHash {
	std::size_t operator()(Object* k) const {
		Object* h = k->__hash__();
		int64_t v = static_cast<Integer*>(h)->get_value();
		Decref(h);   // __hash__ 返回 Owned Integer，用完释放
		return static_cast<std::size_t>(v);
	}
};

// 键相等：不同类型名直接判不等（对齐 Python：1 == "x" -> False，且不会因
// 类型不匹配而抛异常）；同类型才调用 __equal__ 比较其值。
struct MapKeyEqual {
	bool operator()(Object* a, Object* b) const {
		if (a == nullptr || b == nullptr) return a == b;
		if (a->type_name() != b->type_name()) return false;
		return object_equal(a, b);
	}
};

class Map : public Object {
private:
	// 键值对表：Map 持有键与值的引用。
	std::unordered_map<Object*, Object*, MapKeyHash, MapKeyEqual> items_;
	// length / keys 方法对象（懒创建，析构 Decref）。
	Function* length_fn_ = nullptr;
	Function* keys_fn_ = nullptr;

	// 视图模式：当 is_view_ 为真，本 Map 为某对象 owner_ 的成员字典视图
	// （类似 Python 的 __dict__）。读写/删除操作转发到 owner_ 的属性系统
	// （__get_attribute__ / __set_attribute__ / __delete_attribute__），
	// 遍历/length/字符串化合并 owner_ 的 fields_（仅 Instance）+ members_。
	Object* owner_ = nullptr;
	bool is_view_ = false;

public:
	Map();
	~Map() override;

	// 静态工厂：创建空 Map（返回 Owned，refcount=1）。
	static Map* New();

	// 静态工厂：创建绑定 owner 的成员字典视图（owner 被引用，返回 Owned）。
	static Map* NewView(Object* owner);

	// 元素个数（视图模式返回 owner 合并成员数）。
	std::size_t size() const;

	// 键列表（返回 Owned List，refcount=1）：
	//   - 普通 Map：items_ 的全部键（顺序为哈希表序）
	//   - 视图模式：owner_ 的成员名（String），顺序与 member_pairs() 一致
	List* keys() const;

	// 浅拷贝（返回 Owned Map，refcount=1）：
	//   - 普通 Map：复制 items_ 的键值对（键/值各自 Incref）
	//   - 视图模式：按 owner_ 的成员材料化为独立 Map（不再同步 owner）
	Map* copy_shallow() const;

	// 魔术方法。
	Object* __get_item__(Object* key) override;
	Object* __set_item__(Object* key, Object* value) override;
	Object* __delete_item__(Object* key) override;
	Object* __map__() override;
	Object* __boolean__() override;
	Object* __string__() override;
	Object* __get_attribute__(const std::string& name) override;
	Object* __inspect__() override;

	// GC 子引用遍历：枚举所有键与值，以及视图 owner_。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// Map 实例方法 length / keys 的原生实现函数访问器（PycpNativeFunction 签名），
// 供 stdlib/Pycp 注册进 Map 类型类 BuiltinTypeClass 的 methods_。argv[0] 为 self。
PycpNativeFunction Map_length_fn();
PycpNativeFunction Map_keys_fn();

} // namespace Pycp

#endif // PYCP_MAP_HPP
