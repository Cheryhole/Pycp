#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"

#include <stdexcept>

namespace Pycp {

Integer* Integer::instances[PYCP_INTEGER_INSTANCES] = {0};

Integer::Integer() : Integer(INT64_C(0)){}

Integer::Integer(int64_t value) : Object(Type::INTEGER){
	this->_value = value;
}

Integer::Integer(const std::string& value) : Object(Type::INTEGER){
	try{
		this->_value = std::stoll(value);  // 使用 64 位解析，避免 stoi 截断
	} catch (const std::invalid_argument&){
		throw ValueError("Invalid literal for integer: \"" + value + "\"");
	} catch (const std::out_of_range&){
		throw ValueError("Integer literal out of range: \"" + value + "\"");
	}
}

Integer::Integer(Integer* value) : Integer(value->get_value()){}

Integer::Integer(Object* obj) : Object(Type::INTEGER){
	if (obj == nullptr){
		throw Exception("Cannot construct Integer from null object.");
	}
	Integer* i = static_cast<Integer*>(obj->__integer__());
	this->_value = i->get_value();
}

Integer::~Integer(){}

int64_t Integer::get_value() const{
	return this->_value;
}

Object* Integer::__integer__(){
	return this;
}

Object* Integer::__string__(){
	return New<String>(std::to_string(this->_value));
}

Object* Integer::__negation__(){
	return New<Integer>(-(this->_value));
}

Object* Integer::__addition__(Object* other){
	if (other == nullptr || other->type != Type::INTEGER){
		throw Exception("Unsupported to add.");
	}
	Integer* i = static_cast<Integer*>(other);
	return New<Integer>(this->_value + i->_value);
}

Object* Integer::__subtraction__(Object* other){
	if (other == nullptr || other->type != Type::INTEGER){
		throw Exception("Unsupported to subtract.");
	}
	Integer* i = static_cast<Integer*>(other);
	return New<Integer>(this->_value - i->_value);
}

Object* Integer::__multiplication__(Object* other){
	if (other == nullptr) throw Exception("Unsupported to multiply.");

	if (other->type == Type::INTEGER){
		Integer* i = static_cast<Integer*>(other);
		return New<Integer>(this->_value * i->_value);
	}
	else if (other->type == Type::STRING){
		String* s = static_cast<String*>(other);
		std::string str = s->get_value();
		std::string res;
		for (int64_t i = 0; i < this->_value; i++){
			res += str;
		}
		return New<String>(res);
	}

	throw Exception("Unsupported to multiply.");
}

Object* Integer::__division__(Object* other){
	if (other == nullptr || other->type != Type::INTEGER){
		throw Exception("Unsupported to divide.");
	}
	Integer* i = static_cast<Integer*>(other);
	if (i->_value == 0){
		throw ValueError("Division by zero.");
	}
	return New<Integer>(this->_value / i->_value);
}

void Integer::Initialize(){
	for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++){
		Integer::instances[i] = New<Integer>(i);
		// 小整数池为常驻对象，永不参与回收
		Integer::instances[i]->_set_gc_flags(
			Integer::instances[i]->_gc_flags() | GCFlag::PERMANENT);
		GC_AddRoot(Integer::instances[i]);
	}
}

void Integer::Finalize(){
	for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++){
		if (Integer::instances[i] != nullptr){
			GC_RemoveRoot(Integer::instances[i]);
			Integer::instances[i]->_set_gc_flags(
				Integer::instances[i]->_gc_flags() | GCFlag::NONE);
			Decref(Integer::instances[i]);
			Integer::instances[i] = nullptr;
		}
	}
}

} // namespace Pycp
