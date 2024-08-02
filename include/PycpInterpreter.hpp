#ifndef __PYCP_INTERPRETER_HPP
#define __PYCP_INTERPRETER_HPP

#include "Objects/PycpObject.hpp"

namespace Pycp{

class PycpInterpreterFrame{
	public:
		PycpObject* locals;
		PycpObject* globals;

		PycpException* PycpThrownException;

	public:
		PycpInterpreterFrame(PycpInterpreterFrame*) = delete;
		PycpInterpreterFrame& operator=(PycpInterpreterFrame*) = delete;

		PycpInterpreterFrame();
		~PycpInterpreterFrame();

		void ThrowException();
};

PycpInterpreterFrame* PycpGlobalInterpreterFrame;

void PycpInitialize();
void PycpFinalize();


} // namespace Pycp

#endif // __PYCP_INTERPRETER_HPP