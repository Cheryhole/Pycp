#ifndef PYCP_MANAGER_HPP
#define PYCP_MANAGER_HPP

// 管理部分常驻对象的创建和释放

#include "PycpNone.hpp"
#include "PycpFunction.hpp"

void PycpInitialize(){
	PycpNone::instance = new PycpNone();
	PycpBuiltinFunction::print = new PycpFunction(
			"print",	
			_cpp_builtin_print
		);
}

void PycpFinalize(){
	delete PycpNone::instance;
	delete PycpBuiltinFunction::print;
}

#endif // PYCP_MANAGER_HPP1