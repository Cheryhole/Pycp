#ifndef __PYCP_TYPE_EXCEPTION_HPP__
#define __PYCP_TYPE_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpTypeException : public PycpException{
	public:
		PycpTypeException(std::string message, PycpSize_t line = UINT64_C(0)): 
			PycpException(message, "TypeException", line){}
};

} // namespace pycp

#endif // __PYCP_TYPE_EXCEPTION_HPP__