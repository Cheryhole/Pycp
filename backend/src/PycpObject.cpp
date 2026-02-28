#include "PycpObject.hpp"

PycpObject::PycpObject(PycpType type){
	this->ref_cnt = 0;
	this->type = type;
}

PycpObject::~PycpObject(){
  
}

void PycpObject::inc_ref_cnt(){
	this->ref_cnt++;
}

void PycpObject::dec_ref_cnt(){
	this->ref_cnt--;
	if(this->ref_cnt == 0){
		delete this;
	}
}

PycpObject* PycpObject::__integer__(){
  throw PycpException("Unsupported to convert to integer.");
}

PycpObject* PycpObject::__string__(){
  throw PycpException("Unsupported to convert to string.");
}

PycpObject* PycpObject::__call__([[maybe_unused]] PycpObject* args){
  throw PycpException("Unsupported to call.");
}

PycpObject* PycpObject::__addition__([[maybe_unused]] PycpObject* other){
  throw PycpException("Unsupported to add.");
}

PycpObject* PycpObject::__subtraction__([[maybe_unused]] PycpObject* other){
  throw PycpException("Unsupported to subtract.");
}

PycpObject* PycpObject::__multiplication__([[maybe_unused]] PycpObject* other){
  throw PycpException("Unsupported to multiply.");
}

PycpObject* PycpObject::__division__([[maybe_unused]] PycpObject* other){
  throw PycpException("Unsupported to divide.");
}
