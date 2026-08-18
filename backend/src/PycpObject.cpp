#include "PycpObject.hpp"
#include "PycpString.hpp"
#include "PycpABI.hpp"

#include <sstream>

namespace Pycp{

// 将指针格式化为十六进制地址字符串（0x...）。
static std::string ptr_address(const void* p) {
	std::ostringstream oss;
	oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(p);
	return oss.str();
}

Object::Object(Type type){
	this->type = type;
	this->refcount = 0;
	this->gc_flags = GCFlag::NONE;
	this->private_ = false;
}

Object::~Object(){
  
}

Object* Object::__integer__(){
  throw TypeError("Unsupported to convert to integer.");
}

const char* Object::get_name() const {
  return ANONYMOUS_FUNCTION;
}

Object* Object::__string__(){
  // 默认表示："<name at 0xADDR>"（作为所有未显式定义 __string__ 的
  // 对象的兜底输出；匿名对象 name 为 @anonymous）。
  return String_FromString(("<" + std::string(get_name()) +
                            " at " + ptr_address(this) + ">").c_str());
}

Object* Object::__negation__(){
  throw TypeError("Unsupported to negate.");
}

Object* Object::__getattr__([[maybe_unused]] const std::string& name){
  throw AttributeError("Unsupported attribute access.");
}

void Object::__setattr__([[maybe_unused]] const std::string& name,
                         [[maybe_unused]] Object* value){
  throw AttributeError("Unsupported attribute assignment.");
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
