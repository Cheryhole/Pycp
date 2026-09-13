#ifndef PYCP_LIST_HPP
#define PYCP_LIST_HPP

// =============================================================
// Pycp List 对象（List）
//
// 对应 Python 的 list 对象，行为尽可能对齐 Python list：
//   - 方括号字面量 [a, b, c] 构造
//   - 下标访问 self[key]（__get_item__）与赋值 self[key] = value（__set_item__）
//   - 支持负索引（-1 为末元素）
//   - length() 方法获取元素个数（不使用 len）
//   - "+" 拼接（list + list -> 新 list，浅拷贝元素）
//   - 字符串表示 "[a, b, c]"（元素经各自的 __raw_string__ 渲染：字符串
//     元素带引号并转义，数值/None 等无引号）
//   - Pycp.List(obj) 转换（__list__），本版仅 list -> list 幂等
//
// 本阶段不实现 for 迭代协议（见路线图，后续补充）。
// =============================================================

#include "PycpObject.hpp"
#include "PycpMethodTable.hpp"   // MethodEntry / MethodTableFn / PycpCFunction

#include <string>
#include <vector>

namespace Pycp {

class Function;

class List : public Object {
private:
	// 元素表：List 持有其引用（构造/追加时 Incref，析构 Decref）。
	std::vector<Object*> items_;
	// length / append 方法对象（懒创建，析构 Decref）。
	Function* length_fn_ = nullptr;
	Function* append_fn_ = nullptr;

	// 归一化索引：负索引转正，越界抛 IndexError（附 file/lineno 可选）。
	std::size_t normalize_index(Object* key) const;

public:
	List();
	explicit List(std::vector<Object*> items);
	~List() override;

	// 静态工厂：创建空 list（返回 Owned，refcount=1）。
	static List* New();

	// 元素操作。
	std::size_t size() const { return items_.size(); }
	void append(Object* item);   // 接管 item 所有权（内部 Incref）
	Object* at(std::size_t idx) const;

	// 魔术方法。
	Object* __get_item__(Object* key) override;
	Object* __set_item__(Object* key, Object* value) override;
	Object* __delete_item__(Object* key) override;
	Object* __list__() override;
	Object* __map__() override;  // 返回成员字典视图
	Object* __addition__(Object* other) override;
	Object* __boolean__() override;
	Object* __string__() override;
	// repr：与 __string__ 同形（"[a, b, c]"，元素经各自 __raw_string__ 渲染）,
	// 故嵌套容器显示为自身形状而非默认 <name at 0xADDR>。
	Object* __raw_string__() override;
	Object* __get_attribute__(const std::string& name) override;
	Object* __iterator__() override;
	Object* __inspect__() override;

	// GC 子引用遍历：枚举 items_ 中所有元素。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// List 全部方法（公开方法 length/append + 全部魔术方法）的唯一权威清单。
// 类型类注册（register_object）与实例 __inspect__ 均从它派生，消除
// 「注册处」与「__inspect__」双份维护导致的方法清单不一致。
const std::vector<MethodEntry>& List_method_table();

// 类型萃取特化：List（序列族，可迭代且可变）。
template <> struct TypeTraits<List> {
	static constexpr PycpTypeId   id            = PycpTypeId::List;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::SequenceSubclass | PycpTypeFlag::Iterable | PycpTypeFlag::Mutable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif // PYCP_LIST_HPP