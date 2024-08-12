#ifndef __PYCP_IMPORT_EXCEPTION_HPP__
#define __PYCP_IMPORT_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpImportException : public PycpException{
	public:
		PycpImportException(const std::string& message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): 
			PycpException(message, traceback, line, "ImportException"){
		}

}; /* class PycpImportException */

} /* namespace Pycp */

#endif /* __PYCP_IMPORT_EXCEPTION_HPP__ */