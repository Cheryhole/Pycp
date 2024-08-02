#include "PycpInterpreter.hpp"
#include <fstream>

namespace Pycp{

void PycpInitialize(){
	PycpNoneRef = new PycpNone();
	PycpTrueRef = new PycpBoolean(true);
	PycpFalseRef = new PycpBoolean(false);
	
	PycpLastException = new PycpLastException_t();
}

void PycpFinalize(){
	delete PycpNoneRef;
	delete PycpTrueRef;
	delete PycpFalseRef;

	delete PycpLastException;
}


} // namespace Pycp