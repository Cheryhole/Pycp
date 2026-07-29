#ifndef PYCP_MANAGER_HPP
#define PYCP_MANAGER_HPP

// 管理部分常驻对象的创建和释放

#include "PycpNone.hpp"
#include "PycpFunction.hpp"
#include "PycpInteger.hpp"

namespace Pycp{

void Initialize(){
	None::Initialize();
	Function::Initialize();
	Integer::Initialize();
}

void Finalize(){
	Function::Finalize();
	None::Finalize();
	Integer::Finalize();
}

} // namespace Pycp

#endif // PYCP_MANAGER_HPP1