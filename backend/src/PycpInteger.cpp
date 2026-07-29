#include "PycpInteger.hpp"
#include "PycpString.hpp"

namespace Pycp{

Integer* Integer::instances[PYCP_INTEGER_INSTANCES] = {0};

Integer::Integer() : Integer(INT64_C(0)){}

Integer::Integer(int64_t value) : Object(Type::INTEGER){
	this->_value = value;
}

Integer::Integer(const std::string& value) : Integer(std::stoi(value)){}

Integer::Integer(Integer* value) : Integer(value->get_value()){}

Integer::Integer(Object* obj) : Integer(static_cast<Integer*>(obj->__integer__())){}

Integer::~Integer(){}

int64_t Integer::get_value() const{
	return this->_value;
}

Object* Integer::__integer__(){
  return this;
}

Object* Integer::__string__(){
  return new String(std::to_string(this->_value));
}

Object* Integer::__negation__(){
  return new Integer(-(this->_value));
}

Object* Integer::__addition__(Object* other){
	if (other->type != Type::INTEGER){
		throw Exception("Unsupported to add.");
	}
	Integer* i = static_cast<Integer*>(other);
	return new Integer(this->_value + i->_value);
}

Object* Integer::__subtraction__(Object* other){
	if (other->type != Type::INTEGER){
		throw Exception("Unsupported to subtract.");
	}
	Integer* i = static_cast<Integer*>(other);
	return new Integer(this->_value - i->_value);
}

Object* Integer::__multiplication__(Object* other){
	if (other->type == Type::INTEGER){
		Integer* i = static_cast<Integer*>(other);
		return new Integer(this->_value * i->_value);
	} 
	else if (other->type == Type::STRING){
		String* s = static_cast<String*>(other);
		std::string str = s->get_value();
		std::string res;
		for (int64_t i = 0; i < this->_value; i++){
			res += str;
		}
		return new String(res);
	}

	throw Exception("Unsupported to multiply.");
	
}

Object* Integer::__division__(Object* other){
	if (other->type != Type::INTEGER){
		throw Exception("Unsupported to divide.");
	}
	Integer* i = static_cast<Integer*>(other);
	if (i->_value == 0){
		throw ValueError("Division by zero.");
	}
	return new Integer(this->_value / i->_value);
}

void Integer::Initialize(){
	for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++){
		Integer::instances[i] = new Integer(i);
	}
}

void Integer::Finalize(){
	for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++){
		delete Integer::instances[i];
	}  
}

} // namespace Pycp
