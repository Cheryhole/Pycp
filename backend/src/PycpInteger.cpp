#include "PycpInteger.hpp"
#include "PycpString.hpp"

PycpInteger::PycpInteger() : PycpInteger(0){}

PycpInteger::PycpInteger(int value) : PycpObject(PYCP_INTEGER){
	this->_value = value;
}

PycpInteger::PycpInteger(const std::string& value) : PycpInteger(std::stoi(value)){}

PycpInteger::PycpInteger(PycpString* value) : PycpInteger(value->get_value()){}

PycpInteger::PycpInteger(PycpInteger* value) : PycpInteger(value->get_value()){}

PycpInteger::~PycpInteger(){
  
}

int64_t PycpInteger::get_value() const{
	return this->_value;
}

PycpObject* PycpInteger::__integer__(){
  return this;
}

PycpObject* PycpInteger::__string__(){
  return new PycpString(this);
}

PycpObject* PycpInteger::__addition__(PycpObject* other){
	if (other->type != PYCP_INTEGER){
		throw PycpException("Unsupported to add.");
	}
	PycpInteger* i = static_cast<PycpInteger*>(other);
	return new PycpInteger(this->_value + i->_value);
}

PycpObject* PycpInteger::__subtraction__(PycpObject* other){
	if (other->type != PYCP_INTEGER){
		throw PycpException("Unsupported to subtract.");
	}
	PycpInteger* i = static_cast<PycpInteger*>(other);
	return new PycpInteger(this->_value - i->_value);
}

PycpObject* PycpInteger::__multiplication__(PycpObject* other){
	if (other->type == PYCP_INTEGER){
		PycpInteger* i = static_cast<PycpInteger*>(other);
		return new PycpInteger(this->_value * i->_value);
	} 
	else if (other->type == PYCP_STRING){
		PycpString* s = static_cast<PycpString*>(other);
		std::string str = s->get_value();
		std::string res;
		for (int64_t i = 0; i < this->_value; i++){
			res += str;
		}
		return new PycpString(res);
	}

	throw PycpException("Unsupported to multiply.");
	
}

PycpObject* PycpInteger::__division__(PycpObject* other){
	if (other->type != PYCP_INTEGER){
		throw PycpException("Unsupported to divide.");
	}
	PycpInteger* i = static_cast<PycpInteger*>(other);
	return new PycpInteger(this->_value / i->_value);
}
