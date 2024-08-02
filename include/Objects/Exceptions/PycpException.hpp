#ifndef __PYCP_EXCEPTION_HPP__
#define __PYCP_EXCEPTION_HPP__

#include <string>
#include "Objects/PycpObject.hpp"
#include "PycpIndexException.hpp"
#include "PycpTypeException.hpp"
#include "PycpValueException.hpp"

namespace Pycp {

enum class PycpExceptions : PycpSize_t{
	NoException = PycpSize_c(0),
	Exception,
	IndexError,
	TypeError,
	ValueError,
};

class PycpException : public PycpObject{
	public:
		std::string name;
		std::string message;
		PycpSize_t line;

		PycpException(std::string message, PycpSize_t line = UINT64_C(0)): PycpObject("Exception"){
			this->name = "Exception";
			this->message = message;
			this->line = line;
		}

		PycpException(std::string message, const char* name, PycpSize_t line): PycpObject(name){
			this->name = name;
			this->message = message;
			this->line = line;
		}
}; // class PycpException

} // namespace Pycp

#endif // __PYCP_EXCEPTION_HPP__