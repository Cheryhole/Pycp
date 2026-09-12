#include "PycpMagic.hpp"
#include "PycpFunction.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpList.hpp"
#include "PycpFixedList.hpp"
#include "PycpMap.hpp"
#include "PycpString.hpp"
#include "PycpExtension.hpp"

#include <unordered_map>
#include <vector>

namespace Pycp {

namespace {

// =============================================================
// 魔术方法 thunk 表
//
// 旧实现用「native 从 Function::get_name() 反查名字」做运行期分派；容器
// 形态下 self 已是接收者，故改为「每个魔术方法一个具名 thunk」的静态表：
// 名字 -> thunk，零运行期字符串分派。
//
// 参数个数经参数规范表校验（容器形态，参数名统一为 value / key），
// 报错形如 `__addition__() missing required argument: 'value'.`。
// =============================================================

#define PYCP_MAGIC0(fn_name, name_literal, expr)                                \
	Object* fn_name(Object* self, FixedList* args, Map* kwargs) {               \
		static const ::Pycp::Extension::ArgTable spec =                         \
			::Pycp::Extension::CompileArgs(name_literal, {});                   \
		spec.Bind(args, kwargs);                                                \
		return (expr);                                                          \
	}

#define PYCP_MAGIC1(fn_name, name_literal, expr)                                \
	Object* fn_name(Object* self, FixedList* args, Map* kwargs) {               \
		static const ::Pycp::Extension::ArgTable spec =                         \
			::Pycp::Extension::CompileArgs(                                     \
				name_literal, { ::Pycp::Extension::Arg::Required("value") });   \
		::Pycp::Extension::ArgResult r = spec.Bind(args, kwargs);               \
		Object* v0 = r["value"];                                                \
		return (expr);                                                          \
	}

#define PYCP_MAGIC2(fn_name, name_literal, expr)                                \
	Object* fn_name(Object* self, FixedList* args, Map* kwargs) {               \
		static const ::Pycp::Extension::ArgTable spec =                         \
			::Pycp::Extension::CompileArgs(                                     \
				name_literal,                                                   \
				{ ::Pycp::Extension::Arg::Required("key"),                      \
				  ::Pycp::Extension::Arg::Required("value") });                 \
		::Pycp::Extension::ArgResult r = spec.Bind(args, kwargs);               \
		Object* v0 = r["key"];                                                  \
		Object* v1 = r["value"];                                                \
		return (expr);                                                          \
	}

// ---- 0 参 ----
PYCP_MAGIC0(_magic_integer,  "__integer__",  self->__integer__())
PYCP_MAGIC0(_magic_string,   "__string__",   self->__string__())
PYCP_MAGIC0(_magic_boolean,  "__boolean__",  self->__boolean__())
PYCP_MAGIC0(_magic_list,     "__list__",     self->__list__())
PYCP_MAGIC0(_magic_map,      "__map__",      self->__map__())
PYCP_MAGIC0(_magic_hash,     "__hash__",     self->__hash__())
PYCP_MAGIC0(_magic_iterator, "__iterator__", self->__iterator__())
PYCP_MAGIC0(_magic_next,     "__next__",     self->__next__())
PYCP_MAGIC0(_magic_negation, "__negation__", self->__negation__())
PYCP_MAGIC0(_magic_inspect,  "__inspect__",  self->__inspect__())
PYCP_MAGIC0(_magic_delete,   "__delete__",   self->__delete__())

// ---- 1 参（参数名 value）----
PYCP_MAGIC1(_magic_addition,        "__addition__",        self->__addition__(v0))
PYCP_MAGIC1(_magic_subtraction,     "__subtraction__",     self->__subtraction__(v0))
PYCP_MAGIC1(_magic_multiplication,  "__multiplication__",  self->__multiplication__(v0))
PYCP_MAGIC1(_magic_division,        "__division__",        self->__division__(v0))
PYCP_MAGIC1(_magic_power,           "__power__",           self->__power__(v0))
PYCP_MAGIC1(_magic_less_than,       "__less_than__",       self->__less_than__(v0))
PYCP_MAGIC1(_magic_less_equal,      "__less_equal__",      self->__less_equal__(v0))
PYCP_MAGIC1(_magic_equal,           "__equal__",           self->__equal__(v0))
PYCP_MAGIC1(_magic_not_equal,       "__not_equal__",       self->__not_equal__(v0))
PYCP_MAGIC1(_magic_greater_than,    "__greater_than__",    self->__greater_than__(v0))
PYCP_MAGIC1(_magic_greater_equal,   "__greater_equal__",   self->__greater_equal__(v0))
PYCP_MAGIC1(_magic_get_item,        "__get_item__",        self->__get_item__(v0))
PYCP_MAGIC1(_magic_get_attribute,   "__get_attribute__",   self->__get_attribute__(AsString(v0)))
PYCP_MAGIC1(_magic_delete_item,     "__delete_item__",     self->__delete_item__(v0))
PYCP_MAGIC1(_magic_delete_attribute, "__delete_attribute__",
            (self->__delete_attribute__(AsString(v0)), None::instance))

// ---- 2 参（参数名 key / value）----
PYCP_MAGIC2(_magic_set_item, "__set_item__", self->__set_item__(v0, v1))
PYCP_MAGIC2(_magic_set_attribute, "__set_attribute__",
            (self->__set_attribute__(AsString(v0), v1), None::instance))

#undef PYCP_MAGIC0
#undef PYCP_MAGIC1
#undef PYCP_MAGIC2

// 魔术方法名 -> thunk（唯一权威清单）。
struct MagicThunk {
	const char*   name;
	PycpCFunction fn;
};

const std::vector<MagicThunk>& magic_thunks() {
	static const std::vector<MagicThunk> table = {
		{"__integer__",          _magic_integer},
		{"__string__",           _magic_string},
		{"__boolean__",          _magic_boolean},
		{"__list__",             _magic_list},
		{"__map__",              _magic_map},
		{"__hash__",             _magic_hash},
		{"__iterator__",         _magic_iterator},
		{"__next__",             _magic_next},
		{"__negation__",         _magic_negation},
		{"__inspect__",          _magic_inspect},
		{"__delete__",           _magic_delete},
		{"__addition__",         _magic_addition},
		{"__subtraction__",      _magic_subtraction},
		{"__multiplication__",   _magic_multiplication},
		{"__division__",         _magic_division},
		{"__power__",            _magic_power},
		{"__less_than__",        _magic_less_than},
		{"__less_equal__",       _magic_less_equal},
		{"__equal__",            _magic_equal},
		{"__not_equal__",        _magic_not_equal},
		{"__greater_than__",     _magic_greater_than},
		{"__greater_equal__",    _magic_greater_equal},
		{"__get_item__",         _magic_get_item},
		{"__get_attribute__",    _magic_get_attribute},
		{"__delete_attribute__", _magic_delete_attribute},
		{"__delete_item__",      _magic_delete_item},
		{"__set_item__",         _magic_set_item},
		{"__set_attribute__",    _magic_set_attribute},
	};
	return table;
}

const MagicThunk* find_magic_thunk(const std::string& name) {
	for (const MagicThunk& t : magic_thunks()) {
		if (name == t.name) return &t;
	}
	return nullptr;
}

// 惰性创建并缓存各魔术方法对应的 Function（GC 常驻，仅创建一次）。
// key: 魔术方法名；value: Function(name, thunk)。
std::unordered_map<std::string, Function*>& magic_cache() {
	static std::unordered_map<std::string, Function*> cache;
	return cache;
}

} // anonymous namespace

bool IsMagicMethodName(const std::string& name) {
	return find_magic_thunk(name) != nullptr;
}

Object* GetMagicMethodFunction(const std::string& name) {
	const MagicThunk* t = find_magic_thunk(name);
	if (t == nullptr) return nullptr;
	auto& cache = magic_cache();
	auto it = cache.find(name);
	if (it != cache.end()) return it->second;
	Function* f = New<Function>(t->name, t->fn);
	cache[name] = f;
	GC_AddRoot(f); // 常驻缓存，避免被回收
	return f;
}

Object* BuildNameList(const std::vector<std::string>& names) {
	// 返回 FixedList（不可变名称列表）。
	std::vector<Object*> objs;
	objs.reserve(names.size());
	for (const std::string& n : names) {
		objs.push_back(String::FromCString(n.c_str()));
	}
	return FixedList::New(objs);
}

const std::vector<std::string>& CommonInspectNames() {
	// 仅含「非方法」的通用属性名；方法名由各类型方法表提供并已枚举。
	static const std::vector<std::string> names = {
		"__class__",
	};
	return names;
}

void CollectUniqueName(std::vector<Object*>& out, const std::string& name) {
	for (Object* o : out) {
		if (o != nullptr && IsType(o, PycpTypeId::String) &&
		    static_cast<String*>(o)->get_value() == name) {
			return; // 已存在，跳过
		}
	}
	out.push_back(String::FromCString(name.c_str()));
}

Object* GetNameAttribute(Object* receiver) {
	// 属性访问 __name__：返回该对象类型名（type_name()）对应的 String。
	// 返回 Owned 引用（新创建 String），由调用方管理。
	return String::FromCString(receiver->type_name().c_str());
}

} // namespace Pycp
