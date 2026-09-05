#ifndef PYCP_ITERATOR_HPP
#define PYCP_ITERATOR_HPP

#include "PycpObject.hpp"
#include "PycpList.hpp"
#include "PycpString.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpMagic.hpp"
#include <cstddef>

namespace Pycp {

class List;
class String;

// =============================================================
// List 迭代器（ListIterator）
//
// 独立继承 Object（不与 StringIterator 共享基类）。
// 持有 List 源引用（Incref）与游标 index，__next__ 依次返回元素，
// 越界抛 StopIteration。一次性语义：index 只增不减，耗尽后任何
// __next__ 均抛 StopIteration。
// =============================================================
class PYCP_API ListIterator : public Object {
	private:
		List* source_;        // 被迭代的 List（Incref 持有）
		std::size_t index_;   // 当前游标

	public:
		explicit ListIterator(List* source);
		~ListIterator() override;

		const char* get_name() const override { return "ListIterator"; }

		Object* __next__() override;
		Object* __iterator__() override { Incref(this); return this; }
		Object* __inspect__() override;

		// GC 子引用：遍历持有的 source。
		void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// =============================================================
// String 迭代器（StringIterator）
//
// 独立继承 Object。持有 String 源引用（Incref）与游标 index，
// __next__ 依次返回单字符 String，越界抛 StopIteration。
// 一次性语义同 ListIterator。
// =============================================================
class PYCP_API StringIterator : public Object {
	private:
		String* source_;      // 被迭代的 String（Incref 持有）
		std::size_t index_;   // 当前游标

	public:
		explicit StringIterator(String* source);
		~StringIterator() override;

		const char* get_name() const override { return "StringIterator"; }

		Object* __next__() override;
		Object* __iterator__() override { Incref(this); return this; }
		Object* __inspect__() override;

		void foreach_ref(const std::function<void(Object*)>& visit) override;
};

} // namespace Pycp

#endif // PYCP_ITERATOR_HPP