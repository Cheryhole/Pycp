#include <iostream>
#include "Pycp.hpp"

int main(){
	Pycp::Initialize();

	Pycp::ObjectPtr obj = new Pycp::Object();
	Pycp::StringPtr s = new Pycp::String("Hello World");
	Pycp::BuiltinFunction::print->__call__(s);
	try{
		Pycp::ObjectPtr a = obj->__addition__(s);
	}
	catch(Pycp::Exception& e){
		std::cout << e.what() << std::endl;
	}

	Pycp::Finalize();
	return 0;
}