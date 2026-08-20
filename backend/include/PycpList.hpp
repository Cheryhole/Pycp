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
//   - 字符串表示 "[a, b, c]"（元素经 __string__ 转换，字符串元素加引号）
//   - Pycp.List(obj) 转换（__list__），本版仅 list -> list 幂等
//
// 本阶段不实现 for 迭代协议（见路线图，后续补充）。
// =============================================================

#include "PycpObject.hpp"

#include <string>
#include <vector>

namespace Pycp {

class Function;

class List : public Object {
private:
	// 元素表：List 持有其引用（构造/追加时 Incref，析构 Decref）。
	std::vector<Object*> items_;
	// length 方法对象（懒创建，析构 Decref）。
	Function* length_fn_ = nullptr;

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
	Object* __list__() override;
	Object* __addition__(Object* other) override;
	Object* __string__() override;
	Object* __getattr__(const std::string& name) override;

	// GC 子引用遍历：枚举 items_ 中所有元素。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

} // namespace Pycp

#endif // PYCP_LIST_HPP
