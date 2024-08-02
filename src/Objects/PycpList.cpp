#include "Objects/PycpList.hpp" // 假设这是包含类声明的头文件

namespace Pycp{

PycpList::PycpList(PycpTuple* tuple): PycpObject("List"){
	this->container = new std::vector<PycpObject*>;
	for (PycpSize_t i = PycpSize_c(0); i < tuple->count; i++){
		PycpObject* obj = tuple->container[i];
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
	result[strlength - 2] = ')';
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
    // __tuple__ 方法实现
}

PycpObject* PycpList::__addition__(PycpObject* obj){

}

PycpObject* PycpList::__multiplication__(PycpObject* obj){
    // __multiplication__ 方法实现
}

} // namespace Pycp