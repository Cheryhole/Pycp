#include "Objects/PycpHashmap.hpp"

namespace Pycp{

PycpHashmap::PycpHashmap(void): PycpObject("Hashmap"){
	this->container = new std::unordered_map<PycpObject*, PycpObject*, PycpHashCaller>();
}

PycpHashmap::~PycpHashmap(void){
	delete this->container;
}

PycpObject* PycpHashmap::get(PycpObject* key) const{
	return this->container->at(key);
}

void PycpHashmap::set(PycpObject* key, PycpObject* value){
	std::pair<PycpObject*, PycpObject*> objpair = std::pair<PycpObject*, PycpObject*>(key, value);
	this->container->insert(objpair);
}

void PycpHashmap::remove(PycpObject* key){
	this->container->erase(key);
}

void PycpHashmap::clear(){
	this->container->clear();
}

PycpSize_t PycpHashmap::length(void) const{
	return this->container->size();
}

PycpSize_t PycpHashmap::hash(void) const{
	std::string msg = "Unhashable object ";
	msg += this->name;
	PycpTypeException(msg.c_str());
	return 0ull;
}


PycpObject* PycpHashmap::__string__() const{
	char* result = "{";
	for (std::pair<PycpObject*, PycpObject*> apair : *(this->container)){
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
	//return new PycpString
}

PycpObject* PycpHashmap::__boolean__() const{
	bool expression = this->container->empty();
	expression = expression ? false : true;
	return new PycpBoolean(expression);
}

}