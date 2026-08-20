#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"

#include <iostream>

namespace Pycp {

String::String() : String(""){}

String::String(const std::string& value) : Object("String"){
	this->_value = value;
}

String::String(String* value) : String(value->get_value()){}

String::String(Object* obj) : Object("String"){
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

std::string AsString(Object* obj){
	if (obj == nullptr){
		throw TypeError("Cannot convert null object to string.");
	}
	Object* sobj = obj->__string__();
	if (sobj == nullptr || !sobj->is_type("String")){
		throw TypeError("__string__ did not return a String object.");
	}
	String* s = static_cast<String*>(sobj);
	std::string cppstr = s->get_value();
	// __string__ 返回的临时对象若非常驻需释放（此处仅读取，不接管所有权）
	return cppstr;
}

void String::Initialize(){
	// 预留：字符串驻留表（interning）等全局状态初始化
}

void String::Finalize(){
	// 预留：释放字符串驻留表
}

} // namespace Pycp
