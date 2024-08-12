#ifndef __PYCP_VALUE_EXCEPTION_HPP__
#define __PYCP_VALUE_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpValueException : public PycpException{
	public:
		PycpValueException(const std::string& message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): 
			PycpException(message, traceback, line, "ValueException"){
		}

}; /* class PycpValueException */

} /* namespace Pycp */

#endif /* __PYCP_VALUE_EXCEPTION_HPP__ */