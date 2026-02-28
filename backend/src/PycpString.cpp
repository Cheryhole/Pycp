#include "PycpInteger.hpp"
#include "PycpString.hpp"

PycpString::PycpString() : PycpObject(){
	this->_value = "";
}

PycpString::PycpString(const std::string& value){
  this->_value = value;
}

PycpString::PycpString(PycpString* value){
  this->_value = std::string(value->get_value());
}

PycpString::PycpString(PycpInteger* value){
  this->_value = std::to_string(value->get_value());
}

PycpString::~PycpString(){
  
}

std::string PycpString::get_value() const{
  return this->_value;
}

PycpObject* PycpString::__integer__(){
  return new PycpInteger(this);
}

PycpObject* PycpString::__string__(){
  return this;
}

PycpObject* PycpString::__addition__(PycpObject* other){
	if (other->type != PycpType::PYCP_STRING){
	  throw PycpException("Unsupported to add.");
	}
	PycpString* s = static_cast<PycpString*>(other);

  return new PycpString(this->_value + s->get_value());
}

PycpObject* PycpString::__multiplication__(PycpObject* other){
  if (other->type != PycpType::PYCP_INTEGER){
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