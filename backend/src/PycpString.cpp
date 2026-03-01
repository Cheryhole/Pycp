#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpException.hpp"

PycpString::PycpString() : PycpString(""){}

PycpString::PycpString(const std::string& value) : PycpObject(PYCP_TP_STRING){  
	this->_value = value;
}

PycpString::PycpString(PycpString* value) : PycpString(value->get_value()){}

PycpString::PycpString(PycpObject* obj) : PycpString(static_cast<PycpString*>(obj->__string__())){}

PycpString::~PycpString(){
  
}

std::string PycpString::get_value() const{
  return this->_value;
}

PycpObject* PycpString::__integer__(){
	try{
		int64_t i = std::stoll(this->_value);
		return new PycpInteger(i);
	} catch (const std::invalid_argument& e){
		throw PycpValueError("Invalid literal for integer: \"" + this->_value + "\"");
	}
}

PycpObject* PycpString::__string__(){
  return this;
}

PycpObject* PycpString::__addition__(PycpObject* other){
	if (other->type != PycpType::PYCP_TP_STRING){
	  throw PycpException("Unsupported to add.");
	}
	PycpString* s = static_cast<PycpString*>(other);

  return new PycpString(this->_value + s->get_value());
}

PycpObject* PycpString::__multiplication__(PycpObject* other){
  if (other->type != PycpType::PYCP_TP_INTEGER){
	  throw PycpException("Unsupported to multiply.");
  }
		PycpInteger* i = static_cast<PycpInteger*>(other);
		std::string str = this->_value;
		std::string res;
		for (int64_t j = 0; j < i->get_value(); j++){
			res += str;
		}
		return new PycpString(res);
}