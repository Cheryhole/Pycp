#include "Pycp.hpp"

PycpObject* a = new PycpInteger(1);
PycpObject* b = new PycpInteger(2);

class f : public PycpFunction{
	public:
		f() : PycpFunction("f") {}

		PycpObject* __call__(PycpObject* x) override{
		  PycpObject* d = new PycpInteger(4);
			return x->__addition__(d);
		}
};

int main(){
	PycpInitialize();

	PycpObject* _func_f = new f();
	PycpObject* c = _func_f->__call__(b);
	delete _func_f;
	PycpBuiltinFunction::print->__call__(a);
	PycpBuiltinFunction::print->__call__(c);

	PycpFinalize();
	return 0;
}