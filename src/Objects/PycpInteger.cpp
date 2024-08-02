#include "Objects/PycpInteger.hpp"

namespace Pycp{
	
PycpInteger::PycpInteger(int64_t input): PycpObject("Integer"){
	this->container = input;
}

PycpInteger::PycpInteger(PycpObject* input): PycpObject("Integer"){
	PycpInteger* intobj = PycpCast(PycpInteger*, input->__integer__());
	this->container = intobj->container;
}

PycpInteger::PycpInteger(): PycpObject("Integer"){
	this->container = INT64_C(0);
}

PycpInteger::~PycpInteger(){
	// nothing;
	;;;;;
}

PycpSize_t PycpInteger::hash() const{
	return uint64_t(this->container);
}

int64_t PycpInteger::raw() const{
	return this->container;
}


PycpObject* PycpInteger::__string__(){
	// 将整数转换为其字符串表示形式
	char* stream;
	sprintf(stream, "%lld", this->container);
	PycpString* strobj = new PycpString(stream);
	return PycpCastobj(strobj);
}

PycpObject* PycpInteger::__integer__(){
	// PycpInteger已经是整数类型，直接返回this
	return PycpCastobj(this);
}

PycpObject* PycpInteger::__decimal__(){
	// 将整数转换为高精度小数
	PycpDecimal* decobj = new PycpDecimal(this->container);
	return ;
}

PycpObject* PycpInteger::__boolean__(){
	// 根据整数的值返回布尔值
	PycpBoolean* boolobj = \
		new PycpBoolean(this->container != UINT64_C(0));
	return PycpCastobj(boolobj);
}

PycpObject* PycpInteger::__bytes__(){
	PycpBytes* byobj = new PycpBytes(this->container);
	return PycpCastobj(byobj);
}

PycpObject* PycpInteger::__addition__(PycpObject* obj){
	// 重载加法运算
	PycpInteger* other = PycpCast(PycpInteger*, obj);
	return new PycpInteger(this->container + other->container);
}

PycpObject* PycpInteger::__subtraction__(PycpObject* obj){
	// 重载减法运算
	PycpInteger* other = PycpCast(PycpInteger*, obj);
	return new PycpInteger(this->container - other->container);
}

PycpObject* PycpInteger::__multiplication__(PycpObject* obj){
	// 重载乘法运算
	PycpInteger* other = PycpCast(PycpInteger*, obj);
	return new PycpInteger(this->container * other->container);
}

PycpObject* PycpInteger::__division__(PycpObject* obj){
	// 重载除法运算
	PycpInteger* other = PycpCast(PycpInteger*, obj);
	if (other->container == 0) {
			PycpValueException("Division by zero!");
			return nullptr; // 抛出异常
	}
	return new PycpInteger(this->container / other->container);
}

} // namespace Pycp
