#include "Objects/PycpList.hpp" // 假设这是包含类声明的头文件

namespace Pycp{

PycpList::PycpList(PycpTuple* tuple): PycpObject("List"){
	this->container = new std::vector<PycpObject*>;
	for (PycpSize_t i = PycpSize_c(0); i < tuple->length(); i++){
		PycpObject* obj = tuple->get(i);
		obj->incref();
		this->container->push_back(obj);
	}
}

PycpList::PycpList(std::vector<PycpObject*> items): PycpObject("List"){
	this->container = new std::vector<PycpObject*>;
	for (PycpObject* obj : items){
		obj->incref();
		this->container->push_back(obj);
	}
}

PycpList::PycpList(): PycpObject("List"){
	this->container = new std::vector<PycpObject*>;
}

PycpList::~PycpList(){
	for (PycpObject* obj : *this->container){
		obj->decref();
	}
	delete this->container;
}

PycpObject* PycpList::get(int index){
	return this->container->at(index);
}

void PycpList::set(int index, PycpObject* obj){
	this->container->at(index)->decref();
	obj->incref();
	this->container->at(index) = obj;
}

void PycpList::insert(int index, PycpObject* obj){
	obj->incref();
	this->container->insert(this->container->begin() + index, obj);
}

void PycpList::append(PycpObject* obj){
	obj->incref();
	this->container->push_back(obj);
}

PycpSize_t PycpList::length(){
	return this->container->size();
}

void PycpList::clear(){
	this->container->clear();
}


PycpObject* PycpList::__string__(){
	char* result = "[";
	for (PycpSize_t i = PycpSize_c(0); i < this->length(); i++){
		PycpObject* obj = this->container->at(i);
		PycpString* strobj = PycpCast(PycpString*, obj->__string__());
		strcat(result, strobj->raw());
		strcat(result, ", ");
	}
	PycpSize_t strlength = strlen(result);
	result[strlength - 2] = ']';
	result[strlength - 1] = '\0';

	PycpString* strobj = new PycpString(result);
	return PycpCastobj(strobj);
}

PycpObject* PycpList::__boolean__(){
	bool expression = this->length() == PycpSize_c(0) ? false : true;
	return PycpCastobj(new PycpBoolean(expression));
}

PycpObject* PycpList::__list__(){
	return PycpCastobj(this);
}

PycpObject* PycpList::__tuple__(){
	return PycpCastobj(new PycpTuple(this));
}

PycpObject* PycpList::__addition__(PycpObject* obj){
	if (!this->IsItThisType(obj)){
		std::string msg;
		msg += "Can't concatenate ";
		msg += this->name;
		msg += " with";
		msg += obj->name;
		msg += ".";
		PycpTypeException(msg.c_str());
	}
	PycpList* other = PycpCast(PycpList*, obj);
	PycpList* result = new PycpList();

	result->container->insert(result->container->end(), this->container->begin(), this->container->end());
	result->container->insert(result->container->end(), other->container->begin(), other->container->end());
	
	return PycpCastobj(result);
}

PycpObject* PycpList::__multiplication__(PycpObject* obj){
	if (!PycpCheckType(PycpInteger*, obj)){
		std::string msg;
		msg += "Can't multiply ";
		msg += this->name;
		msg += " with";
		msg += obj->name;
		msg += "(non-integer).";
		PycpTypeException(msg.c_str());
	}

	PycpInteger* other = PycpCast(PycpInteger*, obj);
	PycpList* result = new PycpList();
	for (int64_t i = INT64_C(0); i < other->raw(); i++){
		result->container->insert(result->container->end(), this->container->begin(), this->container->end());
	}
	return PycpCastobj(result);
}


} // namespace Pycp