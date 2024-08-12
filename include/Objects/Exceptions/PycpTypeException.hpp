#ifndef __PYCP_TYPE_EXCEPTION_HPP__
#define __PYCP_TYPE_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpTypeException : public PycpException{
	public:
		PycpTypeException(const std::string& message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): 
			PycpException(message, traceback, line, "TypeException"){
		}

}; /* class PycpTypeException */

} /* namespace Pycp */

#endif /* __PYCP_TYPE_EXCEPTION_HPP__ */