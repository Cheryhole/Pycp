#include "Objects/PycpTuple.hpp"

namespace Pycp{

PycpTuple::PycpTuple(PycpSize_t size, ...): PycpObject("Tuple"){
	this->container = (PycpObject**)calloc(size, sizeof(PycpObject*));
	va_list items;
	va_start(items, size);
	for (PycpSize_t i = PycpSize_c(0); i < size; i++){
		PycpObject* obj = va_arg(items, PycpObject*);
		obj->incref();
		this->container[i] = obj;
	}
	this->count = size;
	va_end(items);
}

PycpTuple::PycpTuple(PycpSize_t size, PycpObject* items[]): PycpObject("Tuple"){
	this->container = (PycpObject**)calloc(size, sizeof(PycpObject*));
	for (PycpSize_t i = PycpSize_c(0); i < size; i++){
		PycpObject* obj = items[i];
		obj->incref();
		this->container[i] = obj;
	}
	this->count = size;
}

PycpTuple::PycpTuple(PycpSize_t size): PycpObject("Tuple"){
	this->container = (PycpObject**)calloc(size, sizeof(PycpObject*));
	this->count = size;
}

PycpTuple::~PycpTuple(){
	for (PycpSize_t i = PycpSize_c(0); i < this->count; i++){
		PycpObject* obj = this->container[i];
		obj->decref();
	}

	free(this->container);
}

PycpObject* PycpTuple::get(PycpSize_t index){
	if (index >= this->count){
		PycpIndexException("Tuple index out of range");
		return nullptr;
	}
	return this->container[index];
}

PycpExceptions PycpTuple::set(PycpSize_t index, PycpObject* obj){
	if (index >= this->count){
		PycpIndexException("Tuple index out of range");
		return PycpExceptions::IndexError;
	}
	PycpObject* old = this->container[index];
	old->decref();
	obj->incref();
	this->container[index] = obj;
	return PycpExceptions::NoException;
}


PycpExceptions PycpTuple::parse(const char* format, ...){
	va_list items; // the pointers
	int count = 0; // pointers count

	while (format[count] != ':' && format[count] != '\0') count++;
	PycpDebugBlock({
		char* msg;
		sprintf(msg, "Tuple parsed items: %d", count);
		PycpLog(msg);
	})

	if (count >= this->count){
		PycpIndexException("Tuple index out of range");
		return PycpExceptions::IndexError;
	}


	va_start(items, count);

	for (int i = 0; i < count; i++){
		switch (format[i]){
			case 'i':
				int64_t* ip = va_arg(items, int64_t*);
				*ip = this->parse2int64(i);
				break;
			case 'd':
				long double* dp = va_arg(items, long double*);
				*dp = this->parse2decimal(i);
				break;
			case 's':
				char** cp = va_arg(items, char**);
				*cp = this->parse2char(i);
				break;
			case 'o':
				PycpObject** op =va_arg(items, PycpObject**);
				*op = this-> parse2object(i);
				break;
			default:
				break;
		}
	}

	va_end(items);
	return PycpExceptions::NoException;
}

PycpSize_t PycpTuple::length() const{
	return this->count;
}

PycpSize_t PycpTuple::hash() const{
	XXH64_state_t* xxstate = XXH64_createState();
	XXH64_reset(xxstate, 0);
	XXH64_update(xxstate, (const void*)this->container, this->count * sizeof(PycpObject*));
	PycpSize_t result = XXH64_digest(xxstate);
	XXH64_freeState(xxstate);
	return result;
}

PycpObject* PycpTuple::__string__(){
	char* result = "(";
	for (PycpSize_t i = PycpSize_c(0); i < this->count; i++){
		PycpObject* obj = this->container[i];
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

PycpObject* PycpTuple::__boolean__(){
	bool expression = this->count == PycpSize_c(0) ? false : true;
	return PycpCastobj(new PycpBoolean(expression));
}

PycpObject* PycpTuple::__list__(){
	return PycpCastobj();
}

PycpObject* PycpTuple::__tuple__(){
	return PycpCastobj(this);
}

PycpObject* PycpTuple::__addition__(PycpObject* obj){
	if (!this->IsItThisType(obj)){
		std::string msg;
		msg += "Can't concatenate ";
		msg += this->name;
		msg += " with";
		msg += obj->name;
		msg += ".";
		PycpTypeException(msg.c_str());
	}
	PycpTuple* other = PycpCast(PycpTuple*, obj);
	PycpSize_t total_size = this->count + other->length();
	PycpObject** temp = (PycpObject**)calloc(total_size, sizeof(PycpObject*));
	// copy this tuple's items to new memory
	memcpy(temp, 
				 this->container, 
				 this->count * sizeof(PycpObject*));
	// copy the other tuple's items to new memory
	memcpy(temp + this->count, 
				 other->container, 
				 other->length() * sizeof(PycpObject*));

	PycpTuple* result = new PycpTuple(total_size, temp);
	return PycpCastobj(result);
}

PycpObject* PycpTuple::__multiplication__(PycpObject* obj){
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
	int64_t times = other->raw();

	PycpSize_t total_size = this->count * times;
	PycpObject** temp = (PycpObject**)calloc(total_size, sizeof(PycpObject*));
	// copy this tuple's items to new memory (times)
	for (int64_t i = INT64_C(0); i < times; i++){
		memcpy(temp + i * this->count, 
					 this->container, 
					 this->count * sizeof(PycpObject*));
	}
	temp -= total_size;
	
	PycpTuple* result = new PycpTuple(total_size, temp);
	return PycpCastobj(result);
}


} // namespace Pycp