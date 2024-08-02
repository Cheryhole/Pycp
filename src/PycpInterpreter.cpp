#include "PycpInterpreter.hpp"
#include <fstream>

namespace Pycp{

void PycpInitialize(){
	PycpNoneRef = new PycpNone();
	PycpTrueRef = new PycpBoolean(true);
	PycpFalseRef = new PycpBoolean(false);
	
	PycpGlobalInterpreterFrame = new PycpInterpreterFrame();
}

void PycpFinalize(){
	delete PycpNoneRef;
	delete PycpTrueRef;
	delete PycpFalseRef;

	delete PycpGlobalInterpreterFrame;
}

PycpInterpreterFrame::PycpInterpreterFrame(){
  this->globals = new PycpHashmap();
  this->locals = new PycpHashmap();
	this->PycpThrownException = nullptr;
}

PycpInterpreterFrame::~PycpInterpreterFrame(){
	delete PycpCast(PycpHashmap*, this->globals);
	delete PycpCast(PycpHashmap*, this->locals);
}

void PycpInterpreterFrame::ThrowException(){
  if (this->PycpThrownException == nullptr){
		return;
	}

	if (this->PycpThrownException->line != PycpSize_c(0)){
		printf("At line %llu\n", this->PycpThrownException->line);
	}
	printf("%s: %s\n",this->PycpThrownException->name, this->PycpThrownException->message);
	exit(EXIT_FAILURE);
}


} // namespace Pycp