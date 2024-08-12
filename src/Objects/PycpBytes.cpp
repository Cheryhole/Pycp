#include "Objects/PycpBytes.hpp"

namespace Pycp{

PycpBytes::PycpBytes(PycpSize_t input): PycpObject("Bytes"){
	this->container = new std::vector<uint8_t>();
	
	for (PycpSize_t i = UINT64_C(0); i < input; i++){
		this->container->push_back(PycpBytes::NULLCHAR);
	}
}

PycpBytes::PycpBytes(std::string input): PycpObject("Bytes"){
	this->container = new std::vector<uint8_t>(
		input.begin(),
		input.end()
	);
}

PycpBytes::PycpBytes(): PycpObject("Bytes"){
	this->container = new std::vector<uint8_t>();
}

PycpBytes::~PycpBytes(){
	delete this->container;
}

PycpSize_t PycpBytes::hash() const{
	PycpSize_t result = PycpSize_c(0);

	size_t bytesize = this->container->size();
	for (size_t i = PycpSize_c(0); i < bytesize; i++){
		uint8_t byte = this->container->at(i);
		result += PycpBytes::PRIME * result + byte;
	}

	return result;
}

PycpSize_t PycpBytes::length() const{
	return this->container->size();
}

bool PycpBytes::empty() const{
	return this->container->empty();
}


PycpObject* PycpBytes::__string__(){
	if (this->empty()){
		PycpString* strobj = new PycpString();
		return strobj;
	}

	char* strarray = new char[this->length() + 1];
	memcpy(strarray, this->container->data(), this->length());
	PycpString* strobj = new PycpString(strarray);

	delete[] strarray;
	return PycpCastobj(strobj);
}

PycpObject* PycpBytes::__boolean__(){
	bool expression = this->empty();
	PycpBoolean* boolobj = new PycpBoolean(expression ? false : true);
	return PycpCastobj(boolobj);
}

PycpObject* PycpBytes::__bytes__(){
	return PycpCastobj(this);
}

} // namespace Pycp