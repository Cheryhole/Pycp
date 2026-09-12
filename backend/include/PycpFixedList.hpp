#ifndef PYCP_FIXEDLIST_HPP
#define PYCP_FIXEDLIST_HPP

// =============================================================
// Pycp FixedList 对象（FixedList）—— 对应 Python 的 tuple
//
// 不可变、定长序列：
//   - 构造时固定长度，之后不可增删改（无 append / __set_item__ / __delete_item__）
//   - 只读下标 self[key]（__get_item__），支持负索引
//   - length() 方法获取元素个数
//   - "+" 拼接（FixedList + FixedList -> 新 FixedList）
//   - 字符串表示 "(a, b, c)"（单元素 "(a,)"，对齐 Python tuple repr）
//   - 可哈希（元素可哈希时，对齐 Python tuple）
//   - for 迭代（FixedListIterator）
//
// 存储：Object** items_ + std::size_t size_，构造时一次分配（相比
// std::vector 省去容量冗余与增删路径），元素引用由本对象持有。
// =============================================================

#include "PycpObject.hpp"
#include "PycpMethodTable.hpp"   // MethodEntry / MethodTableFn / PycpCFunction

#include <cstddef>
#include <string>
#include <vector>

namespace Pycp {

class Function;

class FixedList : public Object {
private:
	// 元素表：定长数组，构造时一次分配；FixedList 持有各元素引用。
	Object**    items_ = nullptr;
	std::size_t size_  = 0;
	// length 方法对象（懒创建，析构 Decref）。
	Function* length_fn_ = nullptr;

	// 归一化索引：负索引转正，越界抛 IndexError。
	std::size_t normalize_index(Object* key) const;

public:
	// 静态工厂（均返回 Owned，refcount=1）：
	//   New(items, n) —— 接管 items[0..n-1] 的元素引用（内部不做 Incref，
	//                    调用方须为每个元素转移一个 Owned 引用：Borrowed
	//                    对象需先 Incref、Owned 对象直接传入）
	//   New(vector)   —— 同上，自 std::vector<Object*> 接管
	static FixedList* New(Object** items, std::size_t n);
	static FixedList* New(const std::vector<Object*>& items);

	explicit FixedList(std::vector<Object*> items);
	~FixedList() override;

	// 元素访问。
	std::size_t size() const { return size_; }
	Object* at(std::size_t idx) const;

	// 魔术方法（不可变：不提供 __set_item__ / __delete_item__）。
	Object* __get_item__(Object* key) override;
	Object* __boolean__() override;
	Object* __string__() override;
	Object* __iterator__() override;
	Object* __addition__(Object* other) override;
	Object* __hash__() override;
	Object* __map__() override;
	Object* __get_attribute__(const std::string& name) override;
	Object* __inspect__() override;

	// GC 子引用遍历：枚举全部元素（含懒创建的 length_fn_）。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// FixedList 全部方法（公开方法 length + 全部魔术方法）的唯一权威清单。
// 不含 __set_item__ / __delete_item__（不可变）。
const std::vector<MethodEntry>& FixedList_method_table();

// 类型萃取特化：FixedList（序列族，可迭代、可哈希、不可变）。
template <> struct TypeTraits<FixedList> {
	static constexpr PycpTypeId   id            = PycpTypeId::FixedList;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::SequenceSubclass | PycpTypeFlag::Iterable | PycpTypeFlag::Hashable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif // PYCP_FIXEDLIST_HPP
