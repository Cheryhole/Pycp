#ifndef __PYCP_VALUE_EXCEPTION_HPP__
#define __PYCP_VALUE_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp {

class PycpValueException : public PycpException{
	public:
		PycpValueException(std::string message, PycpSize_t line = UINT64_C(0)):
			PycpException(message, "ValueException", line){}
};

} // namespace pycp

#endif // __PYCP_VALUE_EXCEPTION_HPP__