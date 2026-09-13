#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpException.hpp"
#include "PycpBoolean.hpp"
#include "PycpGC.hpp"
#include "PycpList.hpp"
#include "PycpIterator.hpp"
#include "PycpMagic.hpp"   // CollectUniqueName / CommonInspectNames

#include <iostream>
#include <functional>

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

// String 全部方法的方法表（全部为魔术方法，native 为 nullptr）。
const std::vector<MethodEntry>& String_method_table() {
	static const std::vector<MethodEntry> table = {
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

Object* String::__inspect__() {
	// 收集基类 members_ 名 + 方法表方法名 + 通用属性名，定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	for (const MethodEntry& e : String_method_table()) CollectUniqueName(names, e.name);
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
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