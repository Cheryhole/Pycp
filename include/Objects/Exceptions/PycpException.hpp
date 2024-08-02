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

		PycpException(std::string message): PycpObject("Exception"){
			this->name = "Exception";
			this->message = message;
			this->out();
		}

		PycpException(std::string message, const char* name): PycpObject(name){
			this->name = name;
			this->message = message;
			this->out();
		}

		virtual std::string PycpException::out(PycpSize_t line = UINT64_C(0)) const{
			char* msg;
			sprintf(msg, "%s: %s\n", 
							this->name.c_str(), 
							this->message.c_str());

			PycpLastException->message = msg;
			memcpy(PycpLastException->exception, 
							this, 
							sizeof(*this));

			PycpLastException->line = line;
			return PycpLastException->message;
		}

}; // class PycpException

void PycpThrowLastException(){
	std::string msg = PycpLastException->message;
	PycpSize_t line = PycpLastException->line;
	PycpException* exception = PycpLastException->exception;

}

} // namespace Pycp

#endif // __PYCP_EXCEPTION_HPP__