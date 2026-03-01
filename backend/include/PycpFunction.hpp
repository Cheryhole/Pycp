#ifndef PYCP_FUNCTION_HPP
#define PYCP_FUNCTION_HPP

#include "PycpObject.hpp"
#include "PycpNone.hpp"
#include "PycpString.hpp"
#include <functional>
#include <iostream>

typedef std::function<PycpObject*(PycpObject*)> PycpCFunction_t;

class PycpFunction : public PycpObject{
	private:
		PycpCFunction_t func;

	protected:
		const char* name;	

	public:
		PycpFunction(){}
		PycpFunction(const char* name) : PycpObject(PYCP_TP_FUNCTION){
			this->name = name;
			this->func = nullptr;
		}

		PycpFunction(const char* name, PycpCFunction_t func) : PycpObject(PYCP_TP_FUNCTION){
			this->name = name;
			this->func = func;
		}

		PycpObject* __call__(PycpObject* args) override{
			if (this->func != nullptr){
				return this->func(args);
			}
		  return PycpNone::instance;
		}

		const char* get_name() const { return name; }
};

struct PycpBuiltinFunction{
	static PycpFunction* print;
};

PycpFunction* PycpBuiltinFunction::print = nullptr;

PycpObject* _cpp_builtin_print(PycpObject* obj){
	PycpString* s = static_cast<PycpString*>(obj->__string__());
  std::cout << s->get_value() << std::endl;
  return PycpNone::instance;
}

#endif // PYCP_FUNCTION_HPP