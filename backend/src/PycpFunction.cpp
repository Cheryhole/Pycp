#include "PycpFunction.hpp"
#include "PycpGC.hpp"

namespace Pycp {

Function* BuiltinFunction::print = nullptr;

Object* _builtin_print([[maybe_unused]] Object* self, Object** argv, std::size_t argc){
	// 支持多参数：以空格分隔打印全部实参，末尾换行
	if (argv != nullptr){
		for (std::size_t i = 0; i < argc; ++i){
			if (i > 0) std::cout << " ";
			std::cout << AsString(argv[i]);
		}
	}
	std::cout << std::endl;
	return None::instance;
}

Function::Function() : Function("", nullptr){}

Function::Function(const char* name)
		: Object(Type::FUNCTION), kind(FunctionKind::Native), name(name), native(nullptr){}

Function::Function(const char* name, PycpNativeFunction func)
		: Object(Type::FUNCTION), kind(FunctionKind::Native), name(name), native(func){}

Object* Function::invoke(Object** argv, std::size_t argc){
	if (this->native != nullptr){
		return this->native(this, argv, argc);
	}
	return None::instance;
}

// 兼容旧 tree-walking 解释器：单参数形态转调统一入口
Object* Function::__call__([[maybe_unused]] Object* args){
	return this->invoke(nullptr, 0);
}

void Function::Initialize(){
	BuiltinFunction::print = New<Function>("print", _builtin_print);
	GC_AddRoot(BuiltinFunction::print);
}

void Function::Finalize(){
	if (BuiltinFunction::print != nullptr){
		GC_RemoveRoot(BuiltinFunction::print);
		Decref(BuiltinFunction::print);
		BuiltinFunction::print = nullptr;
	}
}

} // namespace Pycp
