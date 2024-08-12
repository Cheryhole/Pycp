#ifndef __PYCP_INDEX_EXCEPTION_HPP__
#define __PYCP_INDEX_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpIndexException : public PycpException{
	public:
		PycpIndexException(const std::string& message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): 
			PycpException(message, traceback, line, "IndexException"){
		}

}; /* class PycpIndexException */

} /* namespace Pycp */

#endif /* __PYCP_INDEX_EXCEPTION_HPP__ */