#include "Objects/PycpBoolean.hpp"

namespace Pycp{

PycpBoolean::PycpBoolean(bool value) : PycpObject("Boolean"){
	this->value = value;
}

PycpBoolean::~PycpBoolean(){
  // nothing
	;;;;;
}

PycpSize_t PycpBoolean::hash() const{
	// true -> 1ul(l)
	// false -> 0ul(l)
	return this->value ? UINT64_C(1) : UINT64_C(0);
}

PycpObject* PycpBoolean::__string__(){
	return new PycpString(this->value ? "True" : "False");
}

PycpObject* PycpBoolean::__integer__(){
	return new PycpInteger(this->value ? 1 : 0);
}

PycpObject* PycpBoolean::__decimal__(){
	return new PycpDecimal(this->value ? 1.0l : 0.0l);
}

PycpObject* PycpBoolean::__boolean__(){
	return this;
}

PycpObject* PycpBoolean::__bytes__(){
	PycpBytes* byobj = new PycpBytes(this->value ? UINT64_C(1) : UINT64_C(0));
	return PycpCastobj(byobj);
}

PycpObject* PycpBoolean::__addition__(PycpObject* obj){
	PycpInteger* intobj = PycpCast(PycpInteger*, this->__integer__());
	PycpObject* result = intobj->__addition__(obj);
	delete intobj;
	return result;
}

PycpObject* PycpBoolean::__subtraction__(PycpObject* obj){
	PycpInteger* intobj = PycpCast(PycpInteger*, this->__integer__());
	PycpObject* result = intobj->__subtraction__(obj);
	delete intobj;
	return result;
}

PycpObject* PycpBoolean::__multiplication__(PycpObject* obj){
	PycpInteger* intobj = PycpCast(PycpInteger*, this->__integer__());
	PycpObject* result = intobj->__multiplication__(obj);
	delete intobj;
	return result;
}

PycpObject* PycpBoolean::__division__(PycpObject* obj){
	PycpInteger* intobj = PycpCast(PycpInteger*, this->__integer__());
	PycpObject* result = intobj->__division__(obj);
	delete intobj;
	return result;
}

} // namespace Pycp