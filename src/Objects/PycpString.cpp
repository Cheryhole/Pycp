#include "Objects/PycpString.hpp"

namespace Pycp{

PycpString::PycpString(const std::string& input): PycpObject("String"){
	this->container = new std::string(input);
}

PycpString::PycpString(PycpObject* input): PycpObject("String"){
	PycpString* strobj = PycpCast(PycpString*, input->__string__());
	
	this->container = \
		new std::string(strobj->container->data(), 
										strobj->container->size());
}

PycpString::PycpString(): PycpObject("String"){
	this->container = new std::string("");
}

PycpString::~PycpString(){
	delete this->container;
}

PycpSize_t PycpString::length() const{
	return this->container->length();
}

PycpSize_t PycpString::hash() const{
	return std::hash<std::string>() \
		(*(this->container));
}

const char* PycpString::raw() const{
	return this->container->c_str();
}

PycpObject* PycpString::__string__(){
	return this; // 字符串对象本身就是其字符串表示
}

PycpObject* PycpString::__integer__(){
	// 实现将字符串转换为整数的逻辑
	try{
		long long num = std::stoll(*(this->container));
		return new PycpInteger(num);
	}
	catch (...){
		return nullptr; // 转换失败时返回nullptr
	}
}

PycpObject* PycpString::__decimal__(){
    // 实现将字符串转换为小数的逻辑
	try{
		long double num = std::stold(*(this->container));
		return new PycpDecimal(num);
	}
	catch (...){
		return nullptr; // 转换失败时返回nullptr
	}
}

PycpObject* PycpString::__boolean__() {
	bool expression = this->container->empty() ? false : true;
	return new PycpBoolean(expression); // 根据字符串是否为空返回布尔值
}

PycpObject* PycpString::__bytes__(){
	// 字符串已经是字节序列，可以返回自身的副本
	return new PycpString(*(this->container));
}

PycpObject* PycpString::__addition__(PycpObject* obj){
	// 实现字符串连接的逻辑
	PycpString* other = PycpCast(PycpString*, obj);
	if (!other) return nullptr;
	std::string new_str = *(this->container) + *(other->container);
	return new PycpString(new_str);
}

} // namespace Pycp

