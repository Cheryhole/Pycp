#include "PycpFunction.hpp"

namespace Pycp{

Function* BuiltinFunction::print = nullptr;

Object* _builtin_print(Object* obj){
  std::cout << AsString(obj) << std::endl;
  return None::instance;
}

Function::Function() : Function(""){}

Function::Function(const char* name) : Object(Type::FUNCTION){
	this->name = name;
	this->func = nullptr;
}

Function::Function(const char* name, CFunction_t func) : Object(Type::FUNCTION){
	this->name = name;
	this->func = func;
}

Object* Function::__call__(Object* args){
	if (this->func != nullptr){
		return this->func(args);
	}
	return None::instance;
}

const char* Function::get_name() const { return name; }

void Function::Initialize(){
	BuiltinFunction::print = new Function("print", _builtin_print);
}

void Function::Finalize(){
	delete BuiltinFunction::print;
}


} // namespace Pycp
