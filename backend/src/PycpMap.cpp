#include "PycpMap.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpBoolean.hpp"
#include "PycpNone.hpp"
#include "PycpFunction.hpp"
#include "PycpClass.hpp"
#include "PycpGC.hpp"
#include "PycpABI.hpp"
#include "PycpException.hpp"
#include "PycpMagic.hpp"

#include <sstream>

namespace Pycp {

namespace {

// length 方法的原生实现：返回 Integer(键值对个数)。
Object* _map_length(Object*, Object** argv, std::size_t argc) {
	Map* m = static_cast<Map*>(argv[0]);
	if (argc != 1) {
		throw TypeError("length() expects no arguments.");
	}
	return Integer::FromLong(static_cast<long long>(m->size()));
}

} // anonymous namespace

// ---- 实例方法函数访问器：暴露给 stdlib/Pycp 注册 Map 类型类方法 ----
PycpNativeFunction Map_length_fn() { return _map_length; }

Map* Map::New() {
	return Pycp::New<Map>();
}

Map* Map::NewView(Object* owner) {
	Map* m = Pycp::New<Map>();
	m->owner_ = owner;
	m->is_view_ = true;
	if (owner != nullptr) Incref(owner);
	return m;
}

Map::Map() : Object("Map") {}

Map::~Map() {
	if (length_fn_ != nullptr) Decref(length_fn_);
	length_fn_ = nullptr;
	for (auto& kv : items_) {
		if (kv.first != nullptr) Decref(kv.first);
		if (kv.second != nullptr) Decref(kv.second);
	}
	items_.clear();
	if (owner_ != nullptr) Decref(owner_);
	owner_ = nullptr;
}

// 将视图键转换为字符串属性名（仅支持 String 键）。
static std::string _view_key_name(Object* key) {
	if (key == nullptr) {
		throw TypeError("map view key must be a String.");
	}
	if (key->is_type("String")) {
		return AsString(key);
	}
	throw TypeError("map view only supports String keys.");
}

Object* Map::__set_item__(Object* key, Object* value) {
	if (value == nullptr) {
		throw TypeError("cannot assign null to map value.");
	}
	if (is_view_) {
		// 视图写：转发到 owner 的属性系统（实时同步）。
		owner_->__set_attribute__(_view_key_name(key), value);
		return None::instance;
	}
	if (key == nullptr) {
		throw TypeError("cannot use null as map key.");
	}
	// 可哈希性由 MapKeyHash（调用 __hash__）隐式校验。
	auto it = items_.find(key);
	if (it != items_.end()) {
		// 键已存在：替换旧值，持有新值引用。
		if (it->second != nullptr) Decref(it->second);
		it->second = value;
		Incref(value);
	} else {
		// 新键：同时持有键与值的引用。
		Incref(key);
		Incref(value);
		items_.emplace(key, value);
	}
	return None::instance;
}

Object* Map::__get_item__(Object* key) {
	if (is_view_) {
		// 视图读：转发到 owner 的属性系统。
		return owner_->__get_attribute__(_view_key_name(key));
	}
	if (key == nullptr) {
		throw TypeError("cannot use null as map key.");
	}
	auto it = items_.find(key);
	if (it == items_.end()) {
		throw KeyError("Map key not found.");
	}
	return it->second;
}

Object* Map::__delete_item__(Object* key) {
	if (is_view_) {
		// 视图删：转发到 owner 的 __delete_attribute__。
		owner_->__delete_attribute__(_view_key_name(key));
		return None::instance;
	}
	if (key == nullptr) {
		throw TypeError("cannot use null as map key.");
	}
	auto it = items_.find(key);
	if (it == items_.end()) {
		throw KeyError("Map key not found.");
	}
	if (it->first != nullptr) Decref(it->first);
	if (it->second != nullptr) Decref(it->second);
	items_.erase(it);
	return None::instance;
}

Object* Map::__map__() {
	// 返回成员视图（含属性与方法名），对齐 Object/Instance 的语义。
	return Map::NewView(this);
}

std::size_t Map::size() const {
	if (is_view_) {
		if (owner_ == nullptr) return 0;
		// 合并 owner 数据成员（Instance 合并 fields_+动态成员，其他仅 members_）。
		return owner_->member_pairs().size();
	}
	return items_.size();
}

Object* Map::__boolean__() {
	// 非空映射为 True，空映射为 False（视图含成员即非空）。
	return size() == 0 ? Boolean::False() : Boolean::True();
}

Object* Map::__string__() {
	// "{k: v, ...}"：键/值经 __string__ 转换；字符串元素加引号（repr 风格）。
	std::ostringstream oss;
	oss << "{";
	bool first = true;
	if (is_view_ && owner_ != nullptr) {
		// 合并 owner 数据成员（含动态属性，实时同步）。
		// 不调用 __get_attribute__（避免返回 Borrowed 方法），也不 Decref
		// 值（值由 owner 持有，此处仅只读字符串化）。
		for (const auto& kv : owner_->member_pairs()) {
			const std::string& name = kv.first;
			Object* v = kv.second;
			if (!first) oss << ", ";
			first = false;
			oss << "\"" << name << "\": ";
			// 视图中方法名对应 value 为 nullptr，动态经 __get_attribute__
			// 取真实可调用对象用于显示。注意 __get_attribute__ 对魔术方法
			// 返回 Borrowed（GC 常驻，不可 Decref），对方法返回 Owned；
			// 用 Incref/Decref 包一层保持引用计数平衡（借用一次）。
			if (v == nullptr) {
				Object* dyn = owner_->__get_attribute__(name);
				if (dyn != nullptr) {
					Incref(dyn);
					oss << AsString(dyn);
					Decref(dyn);
				} else {
					oss << "None";
				}
			} else if (v->is_type("String")) {
				oss << "\"" << AsString(v) << "\"";
			} else {
				oss << AsString(v);
			}
		}
	} else {
		for (auto& kv : items_) {
			if (!first) oss << ", ";
			first = false;
			Object* k = kv.first;
			Object* v = kv.second;
			if (k != nullptr && k->is_type("String")) {
				oss << "\"" << AsString(k) << "\"";
			} else if (k != nullptr) {
				oss << AsString(k);
			} else {
				oss << "None";
			}
			oss << ": ";
			if (v != nullptr && v->is_type("String")) {
				oss << "\"" << AsString(v) << "\"";
			} else if (v != nullptr) {
				oss << AsString(v);
			} else {
				oss << "None";
			}
		}
	}
	oss << "}";
	return String::FromCString(oss.str().c_str());
}

Object* Map::__get_attribute__(const std::string& name) {
	// 0) 内建只读属性 __name__：返回类型名（type_name()）对应的 String。
	if (name == "__name__") {
		return GetNameAttribute(this);
	}
	// 1) 先从成员字典中查找（支持动态 set attribute）。
	auto it = members_.find(name);
	if (it != members_.end() && it->second != nullptr) {
		Incref(it->second);
		return it->second;
	}
	// 2) 暴露方法（length）与支持的魔术方法（__set_item__ 等）。
	// 方法返回 Borrowed 的 Function，由 GetAttr 统一包装成 BoundMethod。
	if (name == "length") {
		if (length_fn_ == nullptr) {
			length_fn_ = Pycp::New<Function>("length", _map_length);
		}
		return length_fn_;
	}
	// 3) 魔术方法：回退到通用分派（可调用 C++ 虚方法）。
	if (Object* magic = GetMagicMethodFunction(name)) {
		return magic;
	}
	throw AttributeError("map has no attribute '" + name + "'");
}

Object* Map::__introspect__() {
	// 先收集基类 members_ 中的 key，再添加 map 特有的方法名和魔术方法名。
	List* lst = static_cast<List*>(Object::__introspect__());
	std::vector<std::string> extra = {
		"length",
		"__map__", "__boolean__", "__string__",
		"__get_item__", "__set_item__", "__delete_item__",
		"__get_attribute__", "__set_attribute__", "__introspect__",
	};
	for (const auto& n : extra) {
		bool found = false;
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* elem = lst->at(i);
			if (elem != nullptr && elem->is_type("String") &&
			    static_cast<String*>(elem)->get_value() == n) {
				found = true;
				break;
			}
		}
		if (!found) lst->append(String::FromCString(n.c_str()));
	}
	return lst;
}

void Map::foreach_ref(const std::function<void(Object*)>& visit) {
	if (length_fn_ != nullptr) visit(length_fn_);
	if (is_view_ && owner_ != nullptr) visit(owner_);
	for (auto& kv : items_) {
		if (kv.first != nullptr) visit(kv.first);
		if (kv.second != nullptr) visit(kv.second);
	}
}

} // namespace Pycp
