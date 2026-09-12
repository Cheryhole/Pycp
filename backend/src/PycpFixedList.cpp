#include "PycpFixedList.hpp"
#include "PycpClass.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpBoolean.hpp"
#include "PycpNone.hpp"
#include "PycpFunction.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpIterator.hpp"
#include "PycpMagic.hpp"
#include "PycpMap.hpp"

#include <sstream>

namespace Pycp {

namespace {

// length 方法的原生实现：返回 Integer(元素个数)。
Object* _fixedlist_length(Object*, Object** argv, std::size_t argc) {
	FixedList* l = static_cast<FixedList*>(argv[0]);
	if (argc != 1) {
		throw TypeError("length() expects no arguments.");
	}
	return Integer::FromLong(static_cast<long long>(l->size()));
}

} // anonymous namespace

// FixedList 全部方法的方法表（公开方法 length + 全部魔术方法）。
// 不可变：不含 __set_item__ / __delete_item__。
const std::vector<MethodEntry>& FixedList_method_table() {
	static const std::vector<MethodEntry> table = {
		{"length",               _fixedlist_length},
		{"__iterator__",         nullptr},
		{"__boolean__",          nullptr},
		{"__addition__",         nullptr},
		{"__string__",           nullptr},
		{"__hash__",             nullptr},
		{"__get_item__",         nullptr},
		{"__map__",              nullptr},
		{"__inspect__",          nullptr},
		{"__get_attribute__",    nullptr},
		{"__set_attribute__",    nullptr},
		{"__delete_attribute__", nullptr},
	};
	return table;
}

FixedList* FixedList::New(Object** items, std::size_t n) {
	std::vector<Object*> v;
	v.reserve(n);
	for (std::size_t i = 0; i < n; ++i) v.push_back(items[i]);
	return New(v);
}

FixedList* FixedList::New(const std::vector<Object*>& items) {
	return Pycp::New<FixedList>(items);
}

FixedList::FixedList(std::vector<Object*> items) : Object("FixedList") {
	set_type_info(PycpTypeId::FixedList,
	              PycpTypeFlag::SequenceSubclass | PycpTypeFlag::Iterable | PycpTypeFlag::Hashable);
	// 接管元素引用（不 Incref）：调用方须为每个元素转移一个 Owned 引用
	// （自有/Borrowed 对象需先 Incref；String::FromCString 等返回的 Owned 直接传入）。
	size_ = items.size();
	if (size_ > 0) {
		items_ = new Object*[size_];
		for (std::size_t i = 0; i < size_; ++i) {
			items_[i] = items[i];
		}
	}
}

FixedList::~FixedList() {
	if (length_fn_ != nullptr) Decref(length_fn_);
	length_fn_ = nullptr;
	if (items_ != nullptr) {
		for (std::size_t i = 0; i < size_; ++i) {
			if (items_[i] != nullptr) Decref(items_[i]);
		}
		delete[] items_;
		items_ = nullptr;
	}
	size_ = 0;
}

Object* FixedList::at(std::size_t idx) const {
	if (idx >= size_) {
		throw IndexError("FixedList index out of range.");
	}
	return items_[idx];
}

std::size_t FixedList::normalize_index(Object* key) const {
	if (key == nullptr || !IsIntegerExact(key)) {
		throw TypeError("FixedList indices must be integers.");
	}
	int64_t i = static_cast<Integer*>(key)->get_value();
	int64_t n = static_cast<int64_t>(size_);
	// 负索引归一化（对齐 Python：-1 为末元素）。
	if (i < 0) i += n;
	if (i < 0 || i >= n) {
		throw IndexError("FixedList index out of range.");
	}
	return static_cast<std::size_t>(i);
}

Object* FixedList::__get_item__(Object* key) {
	return items_[normalize_index(key)];
}

Object* FixedList::__boolean__() {
	// 非空为 True，空为 False。
	return size_ == 0 ? Boolean::False() : Boolean::True();
}

Object* FixedList::__iterator__() {
	// 每次调用返回全新的独立迭代器实例。
	return Pycp::New<FixedListIterator>(this);
}

Object* FixedList::__addition__(Object* other) {
	// FixedList + FixedList：浅拷贝元素，返回新 FixedList。
	if (other == nullptr || !IsType(other, PycpTypeId::FixedList)) {
		throw TypeError("can only concatenate FixedList (not other) to FixedList.");
	}
	FixedList* rhs = static_cast<FixedList*>(other);
	std::vector<Object*> items;
	items.reserve(size_ + rhs->size_);
	for (std::size_t i = 0; i < size_; ++i) {
		Incref(items_[i]);   // 为结果转移一个引用
		items.push_back(items_[i]);
	}
	for (std::size_t i = 0; i < rhs->size_; ++i) {
		Incref(rhs->items_[i]);
		items.push_back(rhs->items_[i]);
	}
	return FixedList::New(items);
}

Object* FixedList::__hash__() {
	// 元素哈希组合（对齐 Python tuple 语义的简化实现：FNV 风格混合）。
	// 任一元素不可哈希时其 __hash__ 抛 TypeError（对齐 Python）。
	uint64_t h = 1469598103934665603ULL;
	for (std::size_t i = 0; i < size_; ++i) {
		Object* hi = items_[i]->__hash__();
		int64_t v = static_cast<Integer*>(hi)->get_value();
		Decref(hi);
		h ^= static_cast<uint64_t>(v);
		h *= 1099511628211ULL;
	}
	return Integer::FromLong(static_cast<long long>(h));
}

Object* FixedList::__map__() {
	// 返回绑定本对象的成员字典视图（仅含动态 members_）。
	return Map::NewView(this);
}

Object* FixedList::__inspect__() {
	// 收集基类 members_ 名 + 方法表方法名 + 通用属性名，定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	for (const MethodEntry& e : FixedList_method_table()) CollectUniqueName(names, e.name);
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

Object* FixedList::__string__() {
	// "(a, b, c)"；单元素 "(a,)"（对齐 Python tuple repr）。
	std::ostringstream oss;
	oss << "(";
	for (std::size_t i = 0; i < size_; ++i) {
		if (i > 0) oss << ", ";
		Object* o = items_[i];
		if (o != nullptr && IsString(o)) {
			oss << "\"" << AsString(o) << "\"";
		} else if (o != nullptr) {
			oss << AsString(o);
		} else {
			oss << "None";
		}
	}
	if (size_ == 1) oss << ",";
	oss << ")";
	return String::FromCString(oss.str().c_str());
}

Object* FixedList::__get_attribute__(const std::string& name) {
	// 0) 内建只读属性 __name__：返回类型名对应的 String。
	if (name == "__name__") {
		return GetNameAttribute(this);
	}
	// 0.1) 只读 __class__：返回类型类对象（Borrowed，注册表持有）。
	if (name == "__class__") {
		return get_type_class();
	}
	// 1) 成员字典。
	auto it = members_.find(name);
	if (it != members_.end() && it->second != nullptr) {
		Incref(it->second);
		return it->second;
	}
	// 2) 暴露方法（length）与支持的魔术方法。
	if (name == "length") {
		if (length_fn_ == nullptr) {
			length_fn_ = Pycp::New<Function>("length", _fixedlist_length);
		}
		return length_fn_;
	}
	// 3) 魔术方法：回退到通用分派。
	if (Object* magic = GetMagicMethodFunction(name)) {
		return magic;
	}
	throw AttributeError("FixedList has no attribute '" + name + "'");
}

void FixedList::foreach_ref(const std::function<void(Object*)>& visit) {
	if (length_fn_ != nullptr) visit(length_fn_);
	for (std::size_t i = 0; i < size_; ++i) {
		if (items_[i] != nullptr) visit(items_[i]);
	}
}

} // namespace Pycp
