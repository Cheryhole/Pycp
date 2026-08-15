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
  throw TypeError("Unsupported to convert to integer.");
}

Object* Object::__string__(){
  throw TypeError("Unsupported to convert to string.");
}

Object* Object::__negation__(){
  throw TypeError("Unsupported to negate.");
}

Object* Object::__call__([[maybe_unused]] Object* args){
  throw TypeError("Unsupported to call.");
}

Object* Object::__addition__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to add.");
}

Object* Object::__subtraction__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to subtract.");
}

Object* Object::__multiplication__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to multiply.");
}

Object* Object::__division__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to divide.");
}

Object* Object::__power__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to power.");
}

Object* Object::__less_than__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__less_equal__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__equal__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__not_equal__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__greater_than__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__greater_equal__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

}
