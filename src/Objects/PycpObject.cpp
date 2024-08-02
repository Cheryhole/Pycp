#include "Objects/PycpObject.hpp"

namespace Pycp{

PycpObject::PycpObject(){
	this->ReferecenConut = 0ull;
	this->name = strdup("Object");
	this->document = "";
	this->obmap = std::unordered_map<std::string, PycpObject*>();
}

PycpObject::PycpObject(const char* name){
	this->ReferecenConut = 0ull;
	this->name = strdup(name);
	this->document = "";
	this->obmap = std::unordered_map<std::string, PycpObject*>();
}

PycpObject::~PycpObject(){
	free(this->name);
}

void PycpObject::incref(){
	if (this->ReferecenConut >= UINT64_MAX){
		printf("PycpObject::incref: %s's reference count overflowed.\n",
						this->name);
		return;
	}
	this->ReferecenConut++;
}

void PycpObject::decref(){
	if (this->ReferecenConut == 0){
		delete this;
		return;
	}
	this->ReferecenConut--;
}

PycpSize_t PycpObject::hash() const {
	// 返回对象地址uint64
	return ((PycpSize_t)this);
}

PycpSize_t PycpObject::length() const{
	return PycpSize_c(0);
}

PycpObject* PycpObject::call(PycpObject* args){
	return nullptr;
}

bool PycpObject::IsItThisType(PycpObject* obj){
	bool expression = \
		dynamic_cast<decltype(this)>(obj) == nullptr;
	return expression ? false : true;
}

void PycpObject::buildmap(){
}



PycpObject* PycpObject::__string__(){
	char* result;
	sprintf(result, "<object %s at %p>",
		this->name,
		this);
	PycpString* strobj = new PycpString(result);
	return PycpCastobj(strobj);
}

PycpObject* PycpObject::__integer__(){
	return nullptr;
}

PycpObject* PycpObject::__decimal__(){
	return nullptr;
}

PycpObject* PycpObject::__boolean__(){
	PycpBoolean* boolobj = new PycpBoolean(true);
	return PycpCastobj(boolobj);
}

PycpObject* PycpObject::__tuple__(){
	return nullptr;
}

PycpObject* PycpObject::__list__(){
	return nullptr;
}

PycpObject* PycpObject::__bytes__(){
	return nullptr;
}

PycpObject* PycpObject::__addition__(PycpObject* obj){
	return nullptr;
}

PycpObject* PycpObject::__subtraction__(PycpObject* obj){
	return nullptr;
}

PycpObject* PycpObject::__multiplication__(PycpObject* obj){
	return nullptr;
}

PycpObject* PycpObject::__division__(PycpObject* obj){
	return nullptr;
}

PycpObject* PycpObject::operator+(PycpObject* obj){
	PycpObject* result = this->__addition__(obj);
	if (result == nullptr){
		char* msg;
		sprintf(msg, "unsupported operand for +: \"%s\" and \"%s\"",
						this->name,
						obj->name);

		PycpTypeException(std::string(msg));
		return nullptr;
	}
	return result;
}

PycpObject* PycpObject::operator-(PycpObject* obj){
	PycpObject* result = this->__subtraction__(obj);
	if (result == nullptr){
		char* msg;
		sprintf(msg, "unsupported operand for -: \"%s\" and \"%s\"",
						this->name,
						obj->name);

		PycpTypeException(std::string(msg));
		return nullptr;
	}
	return result;
}

PycpObject* PycpObject::operator*(PycpObject* obj){
	PycpObject* result = this->__multiplication__(obj);
	if (result == nullptr){
		char* msg;
		sprintf(msg, "unsupported operand for *: \"%s\" and \"%s\"",
						this->name,
						obj->name);

		PycpTypeException(std::string(msg));
		return nullptr;
	}
	return result;
}

PycpObject* PycpObject::operator/(PycpObject* obj){
	PycpObject* result = this->__division__(obj);
	if (result == nullptr){
		char* msg;
		sprintf(msg, "unsupported operand for /: \"%s\" and \"%s\"",
						this->name,
						obj->name);

		PycpTypeException(std::string(msg));
		return nullptr;
	}
	return result;
}

PycpObject* PycpObject::GetAttr(const char* name){
	return this->obmap.at(name);
}

int PycpObject::SetAttr(const char* name, PycpObject* obj){
	std::pair<std::string, PycpObject*> apair;
	apair.first = name;
	apair.second = obj;
	this->obmap.insert(apair);
	return 0;
}


} // namespace Pycp

