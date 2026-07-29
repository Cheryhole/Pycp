#include "PycpNone.hpp"

namespace Pycp{

None* None::instance = nullptr;

void None::Initialize(){
	None::instance = new None();
}

void None::Finalize(){
	delete None::instance;
}

None::None() : Object(Type::NONE){
	this->none_str = new String("None");
}

None::~None(){
  delete this->none_str;
}

Object* None::__integer__(){
	return Integer::instances[0];
}

Object* None::__string__(){
	return this->none_str;
}

} // namespace Pycp