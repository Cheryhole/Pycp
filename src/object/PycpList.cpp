#include "object/PycpList.hpp"
#include "object/PycpClass.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpString.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpFunction.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpException.hpp"
#include "object/PycpIterator.hpp"
#include "object/PycpMagic.hpp"
#include "object/PycpMap.hpp"
#include "object/PycpExtension.hpp"   // Extension::CompileArgs / Arg 规范框架
#include "abi/PycpABI.hpp"            // Compare / IsFalse（元素相等与排序）

#include <sstream>
#include <algorithm>
#include <vector>

namespace Pycp {

namespace {

// length 方法的原生实现：返回 Integer(元素个数)。
// 容器形态：接收者经 self 传入，实参已收集为 FixedList；个数由规范表校验。
Object* _list_length(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("length", {});
	spec.Bind(args, kwargs);
	List* l = static_cast<List*>(self);
	return Integer::FromLong(static_cast<long long>(l->size()));
}

// append 方法的原生实现：在原对象上追加元素，返回 None。
Object* _list_append(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"append", { Extension::Arg::Required("item") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	List* l = static_cast<List*>(self);
	l->append(r["item"]);
	return None::instance;
}

} // anonymous namespace

namespace {

// 元素相等（用 ABI Compare 的 EQ=2，语义与 `==` 一致）。
bool _list_elem_eq(Object* a, Object* b) {
	Object* r = Compare(a, b, 2);
	bool eq = !IsFalse(r);
	Decref(r);
	return eq;
}

// 通用可迭代对象 -> Owned 元素列表（__iterator__/__next__，StopIteration 收尾）。
std::vector<Object*> _collect_iterable(Object* it) {
	if (it == nullptr) throw TypeError("object is not iterable.");
	Object* iter = it->__iterator__();   // Owned
	if (iter == nullptr) throw TypeError("object is not iterable.");
	std::vector<Object*> out;
	for (;;) {
		Object* v = nullptr;
		try {
			v = iter->__next__();
		} catch (const StopIteration&) {
			break;
		}
		out.push_back(v);
	}
	Decref(iter);
	return out;
}

Object* _list_extend(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"extend", { Extension::Arg::Required("iterable") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	List* l = static_cast<List*>(self);
	std::vector<Object*> elems = _collect_iterable(r["iterable"]);
	for (Object* e : elems) {
		l->append(e);   // Borrowed
		Decref(e);      // 释放本处 Owned
	}
	return None::instance;
}

Object* _list_insert(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"insert", { Extension::Arg::Required("index"),
		            Extension::Arg::Required("item") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* io = r["index"];
	if (io == nullptr || !io->is_type("Integer")) {
		throw TypeError("insert(): 'index' must be an Integer.");
	}
	List* l = static_cast<List*>(self);
	long long n = static_cast<long long>(l->size());
	long long i = static_cast<Integer*>(io)->get_value();
	if (i < 0) i += n;
	if (i < 0) i = 0;
	if (i > n) i = n;
	l->insert_item(static_cast<std::size_t>(i), r["item"]);
	return None::instance;
}

Object* _list_remove(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"remove", { Extension::Arg::Required("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	List* l = static_cast<List*>(self);
	for (std::size_t i = 0; i < l->size(); ++i) {
		if (_list_elem_eq(l->at(i), r["value"])) {
			Object* removed = l->pop_item(i);   // Owned
			if (removed != nullptr) Decref(removed);
			return None::instance;
		}
	}
	throw ValueError("remove(): value not in list.");
}

Object* _list_pop(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"pop", { Extension::Arg::Optional("index") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	List* l = static_cast<List*>(self);
	if (l->size() == 0) throw IndexError("pop from empty list.");
	long long n = static_cast<long long>(l->size());
	long long i = n - 1;
	Object* io = r["index"];
	if (io != nullptr && io->type_id() != PycpTypeId::None) {
		if (!io->is_type("Integer")) {
			throw TypeError("pop(): 'index' must be an Integer.");
		}
		i = static_cast<Integer*>(io)->get_value();
		if (i < 0) i += n;
	}
	if (i < 0 || i >= n) throw IndexError("pop index out of range.");
	return l->pop_item(static_cast<std::size_t>(i));   // Owned
}

Object* _list_index(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"index", { Extension::Arg::Required("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	List* l = static_cast<List*>(self);
	for (std::size_t i = 0; i < l->size(); ++i) {
		if (_list_elem_eq(l->at(i), r["value"])) {
			return Integer::FromLong(static_cast<long long>(i));
		}
	}
	throw ValueError("index(): value not in list.");
}

Object* _list_count(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"count", { Extension::Arg::Required("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	List* l = static_cast<List*>(self);
	long long c = 0;
	for (std::size_t i = 0; i < l->size(); ++i) {
		if (_list_elem_eq(l->at(i), r["value"])) ++c;
	}
	return Integer::FromLong(c);
}

// 成员判定（值语义）：遍历元素，以 == 语义比较。
// 供普通方法 contains 与魔术方法 __contains__ 共用。
bool _list_contains_value(const List* l, Object* value) {
	for (std::size_t i = 0; i < l->size(); ++i) {
		if (_list_elem_eq(l->at(i), value)) return true;
	}
	return false;
}

Object* _list_contains(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"contains", { Extension::Arg::Required("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	List* l = static_cast<List*>(self);
	return _list_contains_value(l, r["value"]) ? Boolean::True()
	                                           : Boolean::False();
}

Object* _list_reverse(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec =
		Extension::CompileArgs("reverse", {});
	spec.Bind(args, kwargs);
	static_cast<List*>(self)->reverse_items();
	return None::instance;
}

Object* _list_sort(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"sort", { Extension::Arg::Optional("reverse") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	bool desc = false;
	Object* ro = r["reverse"];
	if (ro != nullptr && ro->type_id() != PycpTypeId::None) {
		desc = IsFalse(ro->__boolean__()) == false;
	}
	static_cast<List*>(self)->sort_items(desc);
	return None::instance;
}

Object* _list_clear(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("clear", {});
	spec.Bind(args, kwargs);
	static_cast<List*>(self)->clear_items();
	return None::instance;
}

Object* _list_copy(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("copy", {});
	spec.Bind(args, kwargs);
	List* src = static_cast<List*>(self);
	List* out = Pycp::New<List>();
	for (std::size_t i = 0; i < src->size(); ++i) out->append(src->at(i));
	return out;
}

} // anonymous namespace

// List 全部方法的方法表（公开方法 + 全部魔术方法）。
// 公开方法名指向本文件 anonymous namespace 内的实现；魔术方法 native 为
// nullptr，由 GetMagicMethodFunction 统一分派。类型类注册与 __inspect__
// 均以此表为唯一权威来源。
const std::vector<MethodEntry>& List_method_table() {
	static const std::vector<MethodEntry> table = {
		{"length",               _list_length},
		{"append",               _list_append},
		{"extend",               _list_extend},
		{"insert",               _list_insert},
		{"remove",               _list_remove},
		{"pop",                  _list_pop},
		{"index",                _list_index},
		{"count",                _list_count},
		{"contains",             _list_contains},
		{"reverse",              _list_reverse},
		{"sort",                 _list_sort},
		{"clear",                _list_clear},
		{"copy",                 _list_copy},
		{"__iterator__",         nullptr},
		{"__list__",             nullptr},
		{"__boolean__",          nullptr},
		{"__addition__",         nullptr},
		{"__string__",           nullptr},
		{"__raw_string__",       nullptr},
		{"__get_item__",         nullptr},
		{"__set_item__",         nullptr},
		{"__delete_item__",      nullptr},
		{"__contains__",         nullptr},
		{"__map__",              nullptr},
		{"__inspect__",          nullptr},
		{"__get_attribute__",    nullptr},
		{"__set_attribute__",    nullptr},
		{"__delete_attribute__", nullptr},
	};
	return table;
}

// 通用方法分派入口（见 Object::__get_attribute__）。
MethodTableFn List::method_table() const {
	return List_method_table;
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

void List::insert_item(std::size_t idx, Object* item) {
	if (item == nullptr) throw TypeError("cannot insert null into list.");
	if (idx > items_.size()) idx = items_.size();
	items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(idx), item);
	Incref(item);
}

Object* List::pop_item(std::size_t idx) {
	if (idx >= items_.size()) throw IndexError("list index out of range.");
	Object* v = items_[idx];
	// 引用转移：表内那份引用直接交给调用方（列表不再 Decref）。
	items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(idx));
	return v;
}

void List::clear_items() {
	for (Object* o : items_) { if (o != nullptr) Decref(o); }
	items_.clear();
}

void List::reverse_items() {
	std::reverse(items_.begin(), items_.end());
}

void List::sort_items(bool descending) {
	std::stable_sort(items_.begin(), items_.end(),
		[descending](Object* a, Object* b) {
			// 严格弱序：取 x < y；降序用 (b < a) 表达，避免 <= 破坏 weak order。
			Object* x = descending ? b : a;
			Object* y = descending ? a : b;
			Object* r = Compare(x, y, 0);   // 0 == less_than
			bool lt = !IsFalse(r);
			Decref(r);
			return lt;
		});
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

Object* List::__contains__(Object* value) {
	// 成员测试（`value in list`）：遍历元素以 == 语义判定。
	return _list_contains_value(this, value) ? Boolean::True()
	                                         : Boolean::False();
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
	// "[a, b, c]"：元素经各自的 __raw_string__（repr）渲染——是否加引号 /
	// 如何转义由元素自身决定（字符串带引号并转义，数值/None 等无引号）。
	std::ostringstream oss;
	oss << "[";
	for (std::size_t i = 0; i < items_.size(); ++i) {
		if (i > 0) oss << ", ";
		Object* o = items_[i];
		if (o != nullptr) {
			oss << AsRawString(o);
		} else {
			oss << "None";
		}
	}
	oss << "]";
	return String::FromCString(oss.str().c_str());
}

Object* List::__raw_string__() {
	// list 的 repr 与 str 同形（对齐 Python）：嵌套在容器中时显示自身形状。
	return __string__();
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
	// 2.1) 方法表分派：本类型的公开方法（native != nullptr）→ 共享缓存 Function。
	if (Object* method = GetTableMethodFunction(List_method_table(), name)) {
		return method;
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