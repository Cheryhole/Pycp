#ifndef __PYCP_EXCEPTION_HPP__
#define __PYCP_EXCEPTION_HPP__ 1

#include <string>
#include "Objects/PycpObject.hpp"
#include "PycpIndexException.hpp"
#include "PycpTypeException.hpp"
#include "PycpValueException.hpp"

namespace Pycp {

class PycpException : public PycpObject{
	private:
		PycpSize_t line;
		PycpException* traceback;

	public:
		char* name;
		std::string message;

		PycpException(std::string message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): PycpObject("Exception"){
			this->name = "Exception";
			this->message = message;
			this->line = line;
			this->traceback = traceback;
			PycpThreadState::current->SetException(this);
		}

		PycpException(std::string message, PycpException* traceback, PycpSize_t line, const char* name): PycpObject(name){
			this->name = strdup(name);
			this->message = message;
			this->line = line;
			this->traceback = traceback;
			PycpThreadState::current->SetException(this);
		}

		~PycpException(){
			free(this->name);

			if (this->traceback != nullptr){
				delete this->traceback;
			}
		}

		PycpSize_t GetLine() const{
			return this->line;
		}

		PycpException* GetTraceback() const{
			return this->traceback;
		}
}; // class PycpException

} // namespace Pycp

#endif // __PYCP_EXCEPTION_HPP__