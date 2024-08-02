#ifndef __PYCP_TYPE_EXCEPTION_HPP__
#define __PYCP_TYPE_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{

class PycpTypeException : public PycpException{
	public:
		PycpTypeException(std::string message): 
			PycpException(message, "TypeException"){

		}
};

} // namespace pycp

#endif // __PYCP_TYPE_EXCEPTION_HPP__