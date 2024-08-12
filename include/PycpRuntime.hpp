#ifndef __PYCP_RUNTIME_HPP__
#define __PYCP_RUNTIME_HPP__ 1

#include <functional>
#include "Pycp.hpp"

namespace Pycp{

class PycpThreadState{
	public:
		thread_local static PycpThreadState* current;

		PycpInterpreterState* interp;
		PycpSize_t threadnum;
		PycpException* thrown_exception;

	public:
		PycpThreadState(PycpInterpreterState* interpreter, PycpSize_t threadnumber);
		~PycpThreadState();
		PycpThreadState(PycpThreadState*) = delete;
		PycpThreadState& operator=(PycpThreadState*) = delete;

		PycpException* GetException();
		void SetException(PycpException* exception);
		void ClearException();
		void ThrowException();
};

thread_local PycpThreadState* PycpThreadState::current = nullptr;

inline PycpThreadState* PycpThreadStateGet();
inline void PycpThreadStateSet(PycpThreadState* state);

inline void PycpExit(int state);

} // namespace Pycp

#endif // __PYCP_RUNTIME_HPP__