#include <iostream>
#include "Pycp.hpp"

int main(){
	Pycp::Initialize();

	// 经统一 GC 入口创建（Owned，refcount = 1）
	Pycp::Object* obj = Pycp::New<Pycp::Object>();
	Pycp::String* s = Pycp::New<Pycp::String>("Hello World");

	// 内建 print（经统一调用入口）
	Pycp::Object* argv[] = { s };
	Pycp::Call(Pycp::BuiltinFunction::print, argv, 1);

	try{
		Pycp::Object* a = obj->__addition__(s);
		Pycp::Decref(a);
	}
	catch(Pycp::Exception& e){
		std::cout << e.what() << std::endl;
	}

	// 诊断信息：当前存活对象数
	std::cout << "GC live objects: " << Pycp::GC_LiveCount() << std::endl;

	// 正确释放（样板演示：业务代码应通过 Decref 而非 delete）
	Pycp::Decref(s);
	Pycp::Decref(obj);

	Pycp::Finalize();
	return 0;
}
