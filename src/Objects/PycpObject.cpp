#include "Objects/PycpObject.hpp"

namespace Pycp{

struct PycpHashCaller{
	template <PycpObject*>
	PycpSize_t operator()(const PycpObject* obj) const {
		return obj->hash();
	}
};

PycpObject::PycpObject(){
	this->ReferecenConut = PycpSize_0;
	this->name = strdup("Object");
	this->document = "";
	this->obmap = PycpObjectMap();
}

PycpObject::PycpObject(const char* name){
	this->ReferecenConut = PycpSize_0;
	this->name = strdup(name);
	this->document = "";
	this->obmap = PycpObjectMap();
}

PycpObject::~PycpObject(){
	free(this->name);
}

void PycpObject::incref(){
	if (this->ReferecenConut >= UINT64_MAX){
		PycpLog::error("PycpObject::incref: %s's reference count overflowed.\n",
						this->name);
		return;
	}
	this->ReferecenConut++;
}

void PycpObject::decref(){
	if (this->ReferecenConut == PycpSize_0){
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
	return PycpSize_0;
}

PycpObject* PycpObject::call(PycpObject* args){
	return nullptr;
}


PycpObject* PycpObject::__string__() const{
	char* result = "{";
	for (std::pair<PycpObject*, PycpObject*> apair : this->obmap){
		PycpString* key = PycpCast(PycpString*, apair.first->__string__());
		PycpString* value = PycpCast(PycpString*, apair.second->__string__());
		const char* ckey = key->raw();
		const char* cvalue = value->raw();
		strcat(result, ckey);
		strcat(result, ": ");
		strcat(result, cvalue);
		strcat(result, ", ");
	}
	PycpSize_t strlength = strlen(result);
	result[strlength - 2] = '}';
	result[strlength - 1] = '\0';
	return PycpCastobj(new PycpString(result));
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
	PycpObject* index = new PycpString(name);

	PycpObjectMap::iterator obj = this->obmap.find(index);
	if (obj == this->obmap.end()){
		PycpIndexException(std::string("\"").append(name).append("\""));
		delete index;
		return nullptr;
	}
	delete index;
	return obj->second;
}

void PycpObject::DelAttr(const char* name){
	PycpObject* index = new PycpString(name);

	PycpObjectMap::iterator obj = this->obmap.find(index);
	if (obj == this->obmap.end()){
		PycpIndexException(std::string("\"").append(name).append("\""));
		delete index;
		return;
	}
	obj->first->decref(); // key
	obj->second->decref(); // value

	this->obmap.erase(obj);
	delete index;
}

void PycpObject::SetAttr(const char* name, PycpObject* obj){
	PycpObject* index = new PycpString(name);
	index->incref();
	obj->incref();

	PycpObjectMap::iterator old = this->obmap.find(index);
	if (old != this->obmap.end()){
		this->DelAttr(name);
	}

	std::pair<PycpObject*, PycpObject*> apair;
	apair.first = index;
	apair.second = obj;
	this->obmap.insert(apair);
}


} // namespace Pycp

