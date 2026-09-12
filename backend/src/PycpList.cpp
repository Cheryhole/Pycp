#include "PycpList.hpp"
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
Object* _list_length(Object*, Object** argv, std::size_t argc) {
	List* l = static_cast<List*>(argv[0]);
	if (argc != 1) {
		throw TypeError("length() expects no arguments.");
	}
	return Integer::FromLong(static_cast<long long>(l->size()));
}

// append 方法的原生实现：在原对象上追加元素，返回 None。
Object* _list_append(Object*, Object** argv, std::size_t argc) {
	List* l = static_cast<List*>(argv[0]);
	if (argc != 2) {
		throw TypeError("append() expects exactly 1 argument.");
	}
	l->append(argv[1]);
	return None::instance;
}

} // anonymous namespace

// List 全部方法的方法表（公开方法 length/append + 全部魔术方法）。
// 方法名指向本文件 anonymous namespace 内的实现（公开方法）或由
// GetMagicMethodFunction 统一分派（魔术方法，native 为 nullptr）。
// 类型类注册（register_object）与 __inspect__ 均以此表为唯一权威来源。
const std::vector<MethodEntry>& List_method_table() {
	static const std::vector<MethodEntry> table = {
		{"length",               _list_length},
		{"append",               _list_append},
		{"__iterator__",         nullptr},
		{"__list__",             nullptr},
		{"__boolean__",          nullptr},
		{"__addition__",         nullptr},
		{"__string__",           nullptr},
		{"__get_item__",         nullptr},
		{"__set_item__",         nullptr},
		{"__delete_item__",      nullptr},
		{"__map__",              nullptr},
		{"__inspect__",          nullptr},
		{"__get_attribute__",    nullptr},
		{"__set_attribute__",    nullptr},
		{"__delete_attribute__", nullptr},
	};
	return table;
}

List* List::New() {
	return Pycp::New<List>();
}

List::List() : Object("List") {
	set_type_info(PycpTypeId::List, PycpTypeFlag::SequenceSubclass | PycpTypeFlag::Iterable | PycpTypeFlag::Mutable);
}

List::List(std::vector<Object*> items) : Object("List") {
	set_type_info(PycpTypeId::List, PycpTypeFlag::SequenceSubclass | PycpTypeFlag::Iterable | PycpTypeFlag::Mutable);
	items_.reserve(items.size());
	for (Object* o : items) {
		if (o != nullptr) {
			items_.push_back(o);
			Incref(o);
		}
	}
}

List::~List() {
	if (length_fn_ != nullptr) Decref(length_fn_);
	length_fn_ = nullptr;
	if (append_fn_ != nullptr) Decref(append_fn_);
	append_fn_ = nullptr;
	for (Object* o : items_) {
		if (o != nullptr) Decref(o);
	}
	items_.clear();
}

void List::append(Object* item) {
	if (item == nullptr) {
		throw TypeError("cannot append null to list.");
	}
	items_.push_back(item);
	Incref(item);
}

Object* List::at(std::size_t idx) const {
	if (idx >= items_.size()) {
		throw IndexError("list index out of range.");
	}
	return items_[idx];
}

std::size_t List::normalize_index(Object* key) const {
	if (key == nullptr || !key->is_type("Integer")) {
		throw TypeError("list indices must be integers.");
	}
	int64_t i = static_cast<Integer*>(key)->get_value();
	int64_t n = static_cast<int64_t>(items_.size());
	// 负索引归一化（对齐 Python：-1 为末元素）。
	if (i < 0) i += n;
	if (i < 0 || i >= n) {
		throw IndexError("list index out of range.");
	}
	return static_cast<std::size_t>(i);
}

Object* List::__get_item__(Object* key) {
	return items_[normalize_index(key)];
}

Object* List::__set_item__(Object* key, Object* value) {
	std::size_t idx = normalize_index(key);
	if (value == nullptr) {
		throw TypeError("cannot assign null to list element.");
	}
	// 替换元素：释放旧引用，持有新引用。
	if (items_[idx] != nullptr) Decref(items_[idx]);
	items_[idx] = value;
	Incref(value);
	return None::instance;
}

Object* List::__list__() {
	// 幂等：返回自身（转 Owned）。
	Incref(this);
	return this;
}

Object* List::__boolean__() {
	// 非空列表为 True，空列表为 False。
	return items_.empty() ? Boolean::False() : Boolean::True();
}

Object* List::__iterator__() {
	// 每次调用返回全新的独立迭代器实例。
	return Pycp::New<ListIterator>(this);
}

Object* List::__inspect__() {
	// 收集基类 members_ 名 + 方法表方法名 + 通用属性名，定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	for (const MethodEntry& e : List_method_table()) CollectUniqueName(names, e.name);
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

Object* List::__map__() {
	// 返回绑定本列表的成员字典视图（仅含动态 members_）。
	return Map::NewView(this);
}

Object* List::__delete_item__(Object* key) {
	// 按下标删除元素（支持负索引）。
	std::size_t idx = normalize_index(key);
	Object* removed = items_[idx];
	items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
	if (removed != nullptr) Decref(removed);
	return None::instance;
}

Object* List::__addition__(Object* other) {
	// list + list：浅拷贝元素，返回新 list（对齐 Python）。
	if (other == nullptr || !other->is_type("List")) {
		throw TypeError("can only concatenate list (not other) to list.");
	}
	List* rhs = static_cast<List*>(other);
	List* result = Pycp::New<List>();
	for (Object* o : items_) result->append(o);
	for (Object* o : rhs->items_) result->append(o);
	return result;
}

Object* List::__string__() {
	// "[a, b, c]"：元素经 __string__ 转换；字符串元素加引号（对齐 repr 风格）。
	std::ostringstream oss;
	oss << "[";
	for (std::size_t i = 0; i < items_.size(); ++i) {
		if (i > 0) oss << ", ";
		Object* o = items_[i];
		if (o != nullptr && o->is_type("String")) {
			oss << "\"" << AsString(o) << "\"";
		} else if (o != nullptr) {
			oss << AsString(o);
		} else {
			oss << "None";
		}
	}
	oss << "]";
	return String::FromCString(oss.str().c_str());
}

Object* List::__get_attribute__(const std::string& name) {
	// 0) 内建只读属性 __name__：返回类型名（type_name()）对应的 String。
	if (name == "__name__") {
		return GetNameAttribute(this);
	}
	// 0.1) 只读 __class__：返回类型类对象（Borrowed，注册表持有）。
	if (name == "__class__") {
		return get_type_class();
	}
	// 1) 先从成员字典中查找（支持动态 set attribute）。
	auto it = members_.find(name);
	if (it != members_.end() && it->second != nullptr) {
		Incref(it->second);
		return it->second;
	}
	// 2) 暴露方法（length / append）与支持的魔术方法（__iterator__ 等）。
	// 方法返回 Borrowed 的 Function，由 GetAttr 统一包装成 BoundMethod。
	if (name == "length") {
		if (length_fn_ == nullptr) {
			length_fn_ = Pycp::New<Function>("length", _list_length);
		}
		return length_fn_;
	}
	if (name == "append") {
		if (append_fn_ == nullptr) {
			append_fn_ = Pycp::New<Function>("append", _list_append);
		}
		return append_fn_;
	}
	// 3) 魔术方法：回退到通用分派（可调用 C++ 虚方法）。
	if (Object* magic = GetMagicMethodFunction(name)) {
		return magic;
	}
	throw AttributeError("list has no attribute '" + name + "'");
}

void List::foreach_ref(const std::function<void(Object*)>& visit) {
	if (length_fn_ != nullptr) visit(length_fn_);
	if (append_fn_ != nullptr) visit(append_fn_);
	for (Object* o : items_) {
		if (o != nullptr) visit(o);
	}
}

} // namespace Pycp