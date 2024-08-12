#ifndef __PYCP_INTERPRETER_HPP
#define __PYCP_INTERPRETER_HPP

#include <vector>
#include "PycpRuntime.hpp"
#include "Objects/PycpObject.hpp"

namespace Pycp{

class PycpInterpreterState{
	private:
		PycpSize_t threads_count;
		PycpThreadState** threads;
		PycpSize_t line; // current line

	public:
		static PycpInterpreterState* instance;

		PycpObject* locals;
		PycpObject* globals;
		PycpInterpreterState* __old; // old interpreter state
	public:
		PycpInterpreterState(PycpInterpreterState*) = delete;
		PycpInterpreterState& operator=(PycpInterpreterState*) = delete;

		PycpInterpreterState();
		~PycpInterpreterState();

		void AddThread(PycpThreadState* thread);
};

inline PycpInterpreterState* PycpInterpreterStateGet();
inline void PycpInterpreterStateSet(PycpInterpreterState* state);

void PycpInitialize();
void PycpFinalize();


} // namespace Pycp

#endif // __PYCP_INTERPRETER_HPP