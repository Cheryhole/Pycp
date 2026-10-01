#include "object/PycpInteger.hpp"
#include "object/PycpString.hpp"
#include "object/PycpException.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpList.hpp"
#include "object/PycpIterator.hpp"
#include "object/PycpMagic.hpp"   // CollectUniqueName / CommonInspectNames
#include "object/PycpFixedList.hpp"
#include "object/PycpExtension.hpp"   // Extension::CompileArgs / Arg

#include <iostream>
#include <functional>
#include <cctype>
#include <string>
#include <vector>

namespace Pycp {

String::String() : String(""){}

String::String(const std::string& value) : Object("String"){
	set_type_info(PycpTypeId::String, PycpTypeFlag::StringSubclass | PycpTypeFlag::Hashable | PycpTypeFlag::Iterable);
	this->_value = value;
}

String::String(String* value) : String(value->get_value()){}

String::String(Object* obj) : Object("String"){
	set_type_info(PycpTypeId::String, PycpTypeFlag::StringSubclass | PycpTypeFlag::Hashable | PycpTypeFlag::Iterable);
	if (obj == nullptr){
		throw TypeError("Cannot construct String from null object.");
	}
	String* s = static_cast<String*>(obj->__string__());
	this->_value = s->get_value();
}

String::~String(){}

std::string String::get_value() const{
	return this->_value;
}

Object* String::FromCString(const char* value){
	return New<String>(std::string(value));
}

Object* String::__integer__(){
	try{
		int64_t i = std::stoll(this->_value);
		return New<Integer>(i);
	} catch (const std::invalid_argument&){
		throw ValueError("Invalid literal for integer: \"" + this->_value + "\"");
	} catch (const std::out_of_range&){
		throw ValueError("Integer literal out of range: \"" + this->_value + "\"");
	}
}

Object* String::__string__(){
	return this;
}

Object* String::__raw_string__(){
	// repr（对应 Python）：双引号包裹 + 完整转义，返回新的 String（Owned）。
	return String::FromCString(EscapeForRepr(this->_value).c_str());
}

Object* String::__boolean__(){
	// 非空串为 True，空串为 False。
	return _value.empty() ? Boolean::False() : Boolean::True();
}

Object* String::__hash__(){
	// 对底层 std::string 取哈希，转 int64（对齐 Python 字符串哈希语义）。
	std::size_t h = std::hash<std::string>{}(this->_value);
	return Integer::FromLong(static_cast<long long>(h));
}

Object* String::__addition__(Object* other){
	if (other == nullptr || !other->is_type("String")){
		throw TypeError("Unsupported to add.");
	}
	String* s = static_cast<String*>(other);
	return New<String>(this->_value + s->get_value());
}

Object* String::__multiplication__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to multiply.");
	if (!other->is_type("Integer")){
		throw TypeError("Unsupported to multiply.");
	}
	Integer* i = static_cast<Integer*>(other);
	std::string str = this->_value;
	std::string res;
	for (int64_t j = 0; j < i->get_value(); j++){
		res += str;
	}
	return New<String>(res);
}

Object* String::__get_item__(Object* key) {
	if (key == nullptr || !key->is_type("Integer")) {
		throw TypeError("string indices must be integers.");
	}
	int64_t i = static_cast<Integer*>(key)->get_value();
	int64_t n = static_cast<int64_t>(_value.size());
	if (i < 0) i += n; // 负索引归一化
	if (i < 0 || i >= n) {
		throw IndexError("string index out of range.");
	}
	std::string one(1, _value[static_cast<std::size_t>(i)]);
	return String::FromCString(one.c_str());
}

Object* String::__equal__(Object* other) {
	if (other == nullptr || !other->is_type("String")) {
		// 不同类型直接判不等（对齐 Python: "x" == 1 -> False）。
		return Boolean::False();
	}
	return _value == static_cast<String*>(other)->_value
		? Boolean::True() : Boolean::False();
}

Object* String::__list__() {
	// Pycp.List("abc") -> ["a", "b", "c"]：逐字符转 List。
	List* lst = Pycp::New<List>();
	for (char ch : _value) {
		std::string one(1, ch);
		lst->append(String::FromCString(one.c_str()));
	}
	return lst;
}

Object* String::__iterator__() {
	// 每次调用返回全新的独立迭代器实例。
	return Pycp::New<StringIterator>(this);
}

namespace {

// =============================================================
// String 公开方法（Python 同名同语义；见 docs/CORE_METHODS.md §1）
// 说明：Pycp 的 String 是字节串（__get_item__/__list__/迭代均按字节），
//       故 length/find 等下标与 Python 的「码点」语义在纯 ASCII 下一致，
//       多字节 UTF-8 下按字节计（与既有语义保持一致）。
// =============================================================

// 校验并取字符串参数（借用其底层 std::string 值）。
std::string _arg_str(const char* fn, const char* name, Object* o) {
	if (o == nullptr || !o->is_type("String")) {
		throw TypeError(std::string(fn) + "(): '" + name +
		                "' must be a String.");
	}
	return static_cast<String*>(o)->get_value();
}

// 子串判定：v 是否包含 sub。供普通方法 contains 与魔术方法 __contains__ 共用。
bool _str_has_substring(const std::string& v, const std::string& sub) {
	return v.find(sub) != std::string::npos;
}

// 可选字符串参数：未给出或 None -> false；类型不符 -> TypeError。
bool _opt_str(const char* fn, const char* name, Object* o, std::string* out) {
	if (o == nullptr || o->type_id() == PycpTypeId::None) return false;
	if (!o->is_type("String")) {
		throw TypeError(std::string(fn) + "(): '" + name +
		                "' must be a String or None.");
	}
	*out = static_cast<String*>(o)->get_value();
	return true;
}

bool _is_ws(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
	       c == '\v' || c == '\f';
}

bool _in_char_set(char c, const std::string& set) {
	for (char s : set) { if (s == c) return true; }
	return false;
}

// strip/lstrip/rstrip 共用内核：mode 0=两端 1=左 2=右。
std::string _strip_core(const std::string& v, const std::string& set,
                        bool has_set, int mode) {
	std::size_t b = 0, e = v.size();
	auto match = [&](char c) {
		return has_set ? _in_char_set(c, set) : _is_ws(c);
	};
	if (mode != 2) { while (b < e && match(v[b])) ++b; }
	if (mode != 1) { while (e > b && match(v[e - 1])) --e; }
	return v.substr(b, e - b);
}

Object* _str_length(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("length", {});
	spec.Bind(args, kwargs);
	return Integer::FromLong(static_cast<long long>(
		static_cast<String*>(self)->get_value().size()));
}

Object* _str_upper(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("upper", {});
	spec.Bind(args, kwargs);
	std::string v = static_cast<String*>(self)->get_value();
	for (char& c : v) {
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	return String::FromCString(v.c_str());
}

Object* _str_lower(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("lower", {});
	spec.Bind(args, kwargs);
	std::string v = static_cast<String*>(self)->get_value();
	for (char& c : v) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return String::FromCString(v.c_str());
}

Object* _str_strip(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"strip", { Extension::Arg::Optional("chars") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::string set;
	bool has = _opt_str("strip", "chars", r["chars"], &set);
	return String::FromCString(_strip_core(
		static_cast<String*>(self)->get_value(), set, has, 0).c_str());
}

Object* _str_lstrip(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"lstrip", { Extension::Arg::Optional("chars") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::string set;
	bool has = _opt_str("lstrip", "chars", r["chars"], &set);
	return String::FromCString(_strip_core(
		static_cast<String*>(self)->get_value(), set, has, 1).c_str());
}

Object* _str_rstrip(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"rstrip", { Extension::Arg::Optional("chars") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::string set;
	bool has = _opt_str("rstrip", "chars", r["chars"], &set);
	return String::FromCString(_strip_core(
		static_cast<String*>(self)->get_value(), set, has, 2).c_str());
}

Object* _str_split(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"split", { Extension::Arg::Optional("sep"),
		           Extension::Arg::Optional("maxsplit") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string v = static_cast<String*>(self)->get_value();
	long long maxsplit = -1;
	Object* mo = r["maxsplit"];
	if (mo != nullptr && mo->type_id() != PycpTypeId::None) {
		if (!mo->is_type("Integer")) {
			throw TypeError("split(): 'maxsplit' must be an Integer.");
		}
		maxsplit = static_cast<Integer*>(mo)->get_value();
	}
	std::string sep;
	bool has_sep = _opt_str("split", "sep", r["sep"], &sep);
	if (has_sep && sep.empty()) {
		throw ValueError("split(): empty separator.");
	}
	List* out = Pycp::New<List>();
	auto push = [&](const std::string& part) {
		Object* s = String::FromCString(part.c_str());
		out->append(s);   // append 内部 Incref（Borrowed 语义）
		Decref(s);        // 释放本处 Owned
	};
	if (!has_sep) {
		std::size_t i = 0;
		long long cnt = 0;
		while (i < v.size()) {
			while (i < v.size() && _is_ws(v[i])) ++i;
			if (i >= v.size()) break;
			if (maxsplit >= 0 && cnt >= maxsplit) {
				push(v.substr(i));
				break;
			}
			std::size_t j = i;
			while (j < v.size() && !_is_ws(v[j])) ++j;
			push(v.substr(i, j - i));
			i = j;
			++cnt;
		}
	} else {
		std::size_t i = 0;
		long long cnt = 0;
		for (;;) {
			if (maxsplit >= 0 && cnt >= maxsplit) {
				push(v.substr(i));
				break;
			}
			std::size_t p = v.find(sep, i);
			if (p == std::string::npos) {
				push(v.substr(i));
				break;
			}
			push(v.substr(i, p - i));
			i = p + sep.size();
			++cnt;
		}
	}
	return out;
}

Object* _str_join(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"join", { Extension::Arg::Required("iterable") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* it = r["iterable"];
	const bool is_list = (it != nullptr && it->is_type("List"));
	const bool is_fixed = (it != nullptr && it->is_type("FixedList"));
	if (!is_list && !is_fixed) {
		throw TypeError("join(): 'iterable' must be a List or FixedList.");
	}
	const std::string sep = static_cast<String*>(self)->get_value();
	const std::size_t n = is_list ? static_cast<List*>(it)->size()
	                              : static_cast<FixedList*>(it)->size();
	std::string out;
	for (std::size_t i = 0; i < n; ++i) {
		Object* elem = is_list ? static_cast<List*>(it)->at(i)
		                       : static_cast<FixedList*>(it)->at(i);
		if (i > 0) out += sep;
		out += _arg_str("join", "element", elem);
	}
	return String::FromCString(out.c_str());
}

Object* _str_replace(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"replace", { Extension::Arg::Required("old"),
		             Extension::Arg::Required("new"),
		             Extension::Arg::Optional("count") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string old_s = _arg_str("replace", "old", r["old"]);
	const std::string new_s = _arg_str("replace", "new", r["new"]);
	long long count = -1;
	Object* co = r["count"];
	if (co != nullptr && co->type_id() != PycpTypeId::None) {
		if (!co->is_type("Integer")) {
			throw TypeError("replace(): 'count' must be an Integer.");
		}
		count = static_cast<Integer*>(co)->get_value();
	}
	const std::string v = static_cast<String*>(self)->get_value();
	std::string res;
	if (old_s.empty()) {
		// 对齐 Python：空 old 在每个字符边界插入（受 count 限制）。
		res = new_s;
		for (char c : v) {
			res += c;
			res += new_s;
		}
		return String::FromCString(res.c_str());
	}
	std::size_t i = 0;
	long long cnt = 0;
	for (;;) {
		if (count >= 0 && cnt >= count) {
			res += v.substr(i);
			break;
		}
		std::size_t p = v.find(old_s, i);
		if (p == std::string::npos) {
			res += v.substr(i);
			break;
		}
		res += v.substr(i, p - i);
		res += new_s;
		i = p + old_s.size();
		++cnt;
	}
	return String::FromCString(res.c_str());
}

Object* _str_find(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"find", { Extension::Arg::Required("sub"),
		          Extension::Arg::Optional("start"),
		          Extension::Arg::Optional("end") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string sub = _arg_str("find", "sub", r["sub"]);
	const std::string v = static_cast<String*>(self)->get_value();
	const long long n = static_cast<long long>(v.size());
	long long start = 0, end = n;
	Object* so = r["start"];
	Object* eo = r["end"];
	if (so != nullptr && so->type_id() != PycpTypeId::None) {
		if (!so->is_type("Integer")) {
			throw TypeError("find(): 'start' must be an Integer.");
		}
		start = static_cast<Integer*>(so)->get_value();
	}
	if (eo != nullptr && eo->type_id() != PycpTypeId::None) {
		if (!eo->is_type("Integer")) {
			throw TypeError("find(): 'end' must be an Integer.");
		}
		end = static_cast<Integer*>(eo)->get_value();
	}
	if (start < 0) start = 0;
	if (end > n) end = n;
	if (end < start) return Integer::FromLong(-1);
	std::size_t p = v.find(sub, static_cast<std::size_t>(start));
	if (p == std::string::npos) return Integer::FromLong(-1);
	if (static_cast<long long>(p) + static_cast<long long>(sub.size()) > end) {
		return Integer::FromLong(-1);
	}
	return Integer::FromLong(static_cast<long long>(p));
}

Object* _str_count(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"count", { Extension::Arg::Required("sub") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string sub = _arg_str("count", "sub", r["sub"]);
	const std::string v = static_cast<String*>(self)->get_value();
	long long cnt = 0;
	if (sub.empty()) {
		return Integer::FromLong(static_cast<long long>(v.size()) + 1);
	}
	std::size_t i = 0;
	for (;;) {
		std::size_t p = v.find(sub, i);
		if (p == std::string::npos) break;
		++cnt;
		i = p + sub.size();
	}
	return Integer::FromLong(cnt);
}

Object* _str_startswith(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"startswith", { Extension::Arg::Required("prefix") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string pre = _arg_str("startswith", "prefix", r["prefix"]);
	const std::string v = static_cast<String*>(self)->get_value();
	const bool ok = v.size() >= pre.size() && v.compare(0, pre.size(), pre) == 0;
	return ok ? Boolean::True() : Boolean::False();
}

Object* _str_endswith(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"endswith", { Extension::Arg::Required("suffix") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string suf = _arg_str("endswith", "suffix", r["suffix"]);
	const std::string v = static_cast<String*>(self)->get_value();
	const bool ok = v.size() >= suf.size() &&
	                v.compare(v.size() - suf.size(), suf.size(), suf) == 0;
	return ok ? Boolean::True() : Boolean::False();
}

Object* _str_contains(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"contains", { Extension::Arg::Required("sub") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string sub = _arg_str("contains", "sub", r["sub"]);
	const std::string v = static_cast<String*>(self)->get_value();
	return _str_has_substring(v, sub) ? Boolean::True() : Boolean::False();
}

} // anonymous namespace

// String 全部方法的方法表（公开方法 + 全部魔术方法）。
// 公开方法名指向本文件 anonymous namespace 内的实现；魔术方法 native 为
// nullptr，由 GetMagicMethodFunction 统一分派。类型类注册与 __inspect__
// 均以此表为唯一权威来源。
const std::vector<MethodEntry>& String_method_table() {
	static const std::vector<MethodEntry> table = {
		{"length",               _str_length},
		{"upper",                _str_upper},
		{"lower",                _str_lower},
		{"strip",                _str_strip},
		{"lstrip",               _str_lstrip},
		{"rstrip",               _str_rstrip},
		{"split",                _str_split},
		{"join",                 _str_join},
		{"replace",              _str_replace},
		{"find",                 _str_find},
		{"count",                _str_count},
		{"startswith",           _str_startswith},
		{"endswith",             _str_endswith},
		{"contains",             _str_contains},
		{"__contains__",         nullptr},
		{"__integer__",          nullptr},
		{"__string__",           nullptr},
		{"__raw_string__",       nullptr},
		{"__boolean__",          nullptr},
		{"__addition__",         nullptr},
		{"__multiplication__",   nullptr},
		{"__equal__",            nullptr},
		{"__get_item__",         nullptr},
		{"__list__",             nullptr},
		{"__iterator__",         nullptr},
		{"__map__",              nullptr},
		{"__hash__",             nullptr},
		{"__get_attribute__",    nullptr},
		{"__set_attribute__",    nullptr},
		{"__delete_attribute__", nullptr},
		{"__inspect__",          nullptr},
	};
	return table;
}

// 通用方法分派入口（见 Object::__get_attribute__）。
MethodTableFn String::method_table() const {
	return String_method_table;
}

Object* String::__inspect__() {
	// 收集基类 members_ 名 + 方法表方法名 + 通用属性名，定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	for (const MethodEntry& e : String_method_table()) CollectUniqueName(names, e.name);
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

Object* String::__contains__(Object* value) {
	// 成员测试（`sub in s`）：子串语义，左操作数须为 String
	// （非 String 时 _arg_str 抛 TypeError）。
	const std::string sub = _arg_str("__contains__", "value", value);
	return _str_has_substring(get_value(), sub) ? Boolean::True()
	                                            : Boolean::False();
}

std::string EscapeForRepr(const std::string& value) {
	// CPython repr 风格：双引号包裹，转义 \\ 与 \"，\n / \r / \t 转义为
	// 可读形式，其余控制字节（< 0x20 与 0x7f）写作 \xHH；>= 0x80 的字节
	// 原样保留（UTF-8 直通，避免破坏中文等多字节字符）。
	static const char* kHex = "0123456789abcdef";
	std::string out;
	out.reserve(value.size() + 2);
	out.push_back('"');
	for (unsigned char c : value) {
		switch (c) {
			case '\\': out += "\\\\"; break;
			case '"':  out += "\\\""; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if (c < 0x20 || c == 0x7f) {
					out += "\\x";
					out.push_back(kHex[(c >> 4) & 0x0f]);
					out.push_back(kHex[c & 0x0f]);
				} else {
					out.push_back(static_cast<char>(c));
				}
				break;
		}
	}
	out.push_back('"');
	return out;
}

std::string AsString(Object* obj){
	// 借用语义：__string__ 可能返回 Borrowed（String 返回自身）或 Owned
	// （Integer 等返回新对象），故统一以 Incref/Decref 包围读取，
	// 既不接管所有权，也不泄漏临时对象。
	if (obj == nullptr){
		throw TypeError("Cannot convert null object to string.");
	}
	Object* sobj = obj->__string__();
	if (sobj == nullptr || !sobj->is_type("String")){
		throw TypeError("__string__ did not return a String object.");
	}
	Incref(sobj);
	std::string cppstr = static_cast<String*>(sobj)->get_value();
	Decref(sobj);
	return cppstr;
}

std::string AsRawString(Object* obj){
	// 同 AsString，但走 __raw_string__（repr）：容器渲染元素/键值时使用。
	if (obj == nullptr){
		throw TypeError("Cannot convert null object to raw string.");
	}
	Object* sobj = obj->__raw_string__();
	if (sobj == nullptr || !sobj->is_type("String")){
		throw TypeError("__raw_string__ did not return a String object.");
	}
	Incref(sobj);
	std::string cppstr = static_cast<String*>(sobj)->get_value();
	Decref(sobj);
	return cppstr;
}

void String::Initialize(){
	// 预留：字符串驻留表（interning）等全局状态初始化
}

void String::Finalize(){
	// 预留：释放字符串驻留表
}

} // namespace Pycp