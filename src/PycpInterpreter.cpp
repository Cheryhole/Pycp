#include "PycpInterpreter.hpp"
#include <fstream>

namespace Pycp{

void PycpInitialize(){
	PycpNoneRef = new PycpNone();
	PycpTrueRef = new PycpBoolean(true);
	PycpFalseRef = new PycpBoolean(false);
	
	PycpInterpreterState::instance = new PycpInterpreterState();
}

void PycpFinalize(){
	delete PycpNoneRef;
	delete PycpTrueRef;
	delete PycpFalseRef;

	delete PycpInterpreterState::instance;
}

PycpInterpreterState::PycpInterpreterState(){
  this->globals = new PycpHashmap();
  this->locals = new PycpHashmap();
	this->threads = nullptr;
	this->threads_count = PycpSize_c(1);

	PycpThreadState* main_thread = new PycpThreadState(this, 0);
	PycpThreadState::current = main_thread;
	this->AddThread(main_thread);
}

PycpInterpreterState::~PycpInterpreterState(){
	delete PycpCast(PycpHashmap*, this->globals);
	delete PycpCast(PycpHashmap*, this->locals);
	for(PycpSize_t i = PycpSize_c(0); i < this->threads_count; i++) delete this->threads[i];
}

void PycpInterpreterState::AddThread(PycpThreadState* thread){
	if (this->threads == nullptr){
		this->threads = (PycpThreadState**)malloc(sizeof(PycpThreadState*));
		memset(this->threads, 0, sizeof(PycpThreadState*));
		this->threads[0] = thread;
		return;
	}

	PycpThreadState** old = this->threads;
	this->threads = (PycpThreadState**)calloc(this->threads_count + PycpSize_c(1), sizeof(PycpThreadState*));
	memcpy(this->threads, old, this->threads_count * sizeof(PycpThreadState*));
	this->threads[this->threads_count] = thread;
	this->threads_count++;
}	


PycpInterpreterState* PycpInterpreterStateGet(){
	return PycpInterpreterState::instance;
}

void PycpInterpreterStateSet(PycpInterpreterState* state){
	PycpInterpreterState* old = PycpInterpreterState::instance;
	state->__old = old;
	PycpInterpreterState::instance = state;
}


} // namespace Pycp