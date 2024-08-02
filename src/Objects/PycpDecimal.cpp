#include "Objects/PycpDecimal.hpp"

namespace Pycp{

PycpDecimal::PycpDecimal(long double input): PycpObject("Decimal"){
	this->container = new std::decimal::decimal128(input);
}

PycpDecimal::PycpDecimal(int64_t input): PycpObject("Decimal"){
	this->container = new std::decimal::decimal128(input);
}

PycpDecimal::PycpDecimal(std::decimal::decimal128 input): PycpObject("Decimal"){
	this->container = new std::decimal::decimal128(input);
}

PycpDecimal::PycpDecimal(std::string input): PycpObject("Decimal"){
	this->container = new std::decimal::decimal128(input);
}

PycpDecimal::PycpDecimal(PycpObject* input): PycpObject("Decimal"){
	PycpDecimal* decobj = PycpCast(PycpDecimal*, input->__decimal__());
	this->container = new std::decimal::decimal128(decobj->container);
}

PycpDecimal::~PycpDecimal(){
	delete this->container;
}

long double PycpDecimal::ToLongDouble() const{
	long double ldb = std::decimal::decimal128_to_long_double(*(this->container));
	return ldb;
}

int64_t PycpDecimal::ToInt64() const{
	int64_t lli = std::decimal::decimal128_to_long_long(*(this->container));
	return lli;
}

PycpSize_t PycpDecimal::hash() const{
	long double ldb = this->ToLongDouble();
	return std::hash<long double>()(ldb);
}

long double PycpDecimal::raw() const{
	return this->ToLongDouble();
}


PycpObject* PycpDecimal::__string__(){
	long double ldb = this->ToLongDouble();
	std::string tstr = std::to_string(ldb); // temporate string
	return new PycpString(tstr);
}

PycpObject* PycpDecimal::__integer__(){
	int64_t lint = this->ToInt64();
	return new PycpInteger(lint);
}

PycpObject* PycpDecimal::__decimal__(){
	return PycpCastobj(this);
}

PycpObject* PycpDecimal::__boolean__(){
	long double ldb = this->ToLongDouble();
	return new PycpBoolean(ldb == 0.00l ? false : true);
}

PycpObject* PycpDecimal::__addition__(PycpObject* obj){
	PycpDecimal* decobj = PycpCast(PycpDecimal*, obj);
	std::decimal::decimal128 decnum = *(this->container) + *(decobj->container);
	return new PycpDecimal(decnum);
}	

PycpObject* PycpDecimal::__subtraction__(PycpObject* obj){
	PycpDecimal* decobj = PycpCast(PycpDecimal*, obj);
	std::decimal::decimal128 decnum = *(this->container) - *(decobj->container);
	return new PycpDecimal(decnum);
}

PycpObject* PycpDecimal::__multiplication__(PycpObject* obj){
	PycpDecimal* decobj = PycpCast(PycpDecimal*, obj);
	std::decimal::decimal128 decnum = *(this->container) * *(decobj->container);
	return new PycpDecimal(decnum);
}

PycpObject* PycpDecimal::__division__(PycpObject* obj){
	PycpDecimal* decobj = PycpCast(PycpDecimal*, obj);
	std::decimal::decimal128 decnum = *(this->container) / *(decobj->container);
	return new PycpDecimal(decnum);
}

}