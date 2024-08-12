#ifndef __PYCP_SYSTEM_EXCEPTION_HPP__
#define __PYCP_SYSTEM_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpSystemException : public PycpException{
	public:
		PycpSystemException(const std::string& message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): 
			PycpException(message, traceback, line, "SystemException"){
		}

}; /* class PycpSystemException */

} /* namespace Pycp */

#endif /* __PYCP_SYSTEM_EXCEPTION_HPP__ */