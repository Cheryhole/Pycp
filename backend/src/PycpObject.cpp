#include "PycpObject.hpp"

namespace Pycp{

Object::Object(Type type){
	this->type = type;
	this->refcount = 0;
	this->gc_flags = GCFlag::NONE;
}

Object::~Object(){
  
}

Object* Object::__integer__(){
  throw Exception("Unsupported to convert to integer.");
}

Object* Object::__string__(){
  throw Exception("Unsupported to convert to string.");
}

Object* Object::__negation__(){
  throw Exception("Unsupported to negate.");
}

Object* Object::__call__([[maybe_unused]] Object* args){
  throw Exception("Unsupported to call.");
}

Object* Object::__addition__([[maybe_unused]] Object* other){
  throw Exception("Unsupported to add.");
}

Object* Object::__subtraction__([[maybe_unused]] Object* other){
  throw Exception("Unsupported to subtract.");
}

Object* Object::__multiplication__([[maybe_unused]] Object* other){
  throw Exception("Unsupported to multiply.");
}

Object* Object::__division__([[maybe_unused]] Object* other){
  throw Exception("Unsupported to divide.");
}

}
