#ifndef __PYCP_SYNTAX_EXCEPTION_HPP__
#define __PYCP_SYNTAX_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpSyntaxException : public PycpException{
	public:
		PycpSyntaxException(const std::string& message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): 
			PycpException(message, traceback, line, "SyntaxException"){
		}

}; /* class PycpSyntaxException */

} /* namespace Pycp */

#endif /* __PYCP_SYNTAX_EXCEPTION_HPP__ */