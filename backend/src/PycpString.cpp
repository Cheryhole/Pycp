#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpException.hpp"
#include <iostream>

namespace Pycp{

String::String() : String(""){}

String::String(const std::string& value) : Object(Type::STRING){  
	this->_value = value;
}

String::String(String* value) : String(value->get_value()){}

String::String(Object* obj) : String(static_cast<String*>(obj->__string__())){}

String::~String(){
  
}

std::string String::get_value() const{
  return this->_value;
}

Object* String::__integer__(){
	try{
		int64_t i = std::stoll(this->_value);
		return new Integer(i);
	} catch (const std::invalid_argument&){
		throw ValueError("Invalid literal for integer: \"" + this->_value + "\"");
	}
}

Object* String::__string__(){
  return this;
}

Object* String::__addition__(Object* other){
	if (other->type != Type::STRING){
	  throw Exception("Unsupported to add.");
	}
	String* s = static_cast<String*>(other);

  return new String(this->_value + s->get_value());
}

Object* String::__multiplication__(Object* other){
  if (other->type != Type::INTEGER){
	  throw Exception("Unsupported to multiply.");
  }
		Integer* i = static_cast<Integer*>(other);
		std::string str = this->_value;
		std::string res;
		for (int64_t j = 0; j < i->get_value(); j++){
			res += str;
		}
		return new String(res);
}

std::string AsString(Object* obj){
	String* sobj = static_cast<String*>(obj->__string__());
	std::string cppstr = sobj->get_value();
	return cppstr;
}

} // namespace Pycp
