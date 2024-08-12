#ifndef __PYCP_RUNTIME_EXCEPTION_HPP__
#define __PYCP_RUNTIME_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpRuntimeException : public PycpException{
	public:
		PycpRuntimeException(const std::string& message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): 
			PycpException(message, traceback, line, "RuntimeException"){
		}

}; /* class PycpRuntimeException */

} /* namespace Pycp */

#endif /* __PYCP_RUNTIME_EXCEPTION_HPP__ */