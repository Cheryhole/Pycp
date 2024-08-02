#ifndef __PYCP_INDEX_EXCEPTION_HPP__
#define __PYCP_INDEX_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpIndexException : public PycpException{
	public:
		PycpIndexException(const std::string& message): 
			PycpException(message, "IndexException"){
			
		}

}; // class PycpIndexException

} // namespace pycp

#endif // __PYCP_INDEX_EXCEPTION_HPP__