#include "PycpRuntime.hpp"

namespace Pycp {

PycpThreadState::PycpThreadState(PycpInterpreterState* interpreter, PycpSize_t threadnumber){
	this->interp = interpreter;
	this->threadnum = threadnumber;
	this->thrown_exception = nullptr;
}

PycpThreadState::~PycpThreadState(){
	this->ClearException();
}

PycpException* PycpThreadState::GetException(){
	return this->thrown_exception;
}

void PycpThreadState::SetException(PycpException* exception){
	this->thrown_exception = exception;
}

void PycpThreadState::ClearException(){
	if (this->thrown_exception != nullptr){
		delete this->thrown_exception;
		this->thrown_exception = nullptr;
	}
}

void PycpThreadState::ThrowException(){
	if (this->thrown_exception == nullptr){
		return;
	}
	std::function<void(PycpException*)> RecursionThrow = \
	[RecursionThrow](PycpException* exception) -> void {
		PycpException* tb = exception->GetTraceback();
		if (tb == nullptr) goto print;
		RecursionThrow(tb);

		print:
		(exception->GetLine() == PycpSize_0) || printf("At line %zu\n", exception->GetLine());
		printf("\033[31m%s:\033[0m \033[33m%s\033[0m\n\n", exception->name, exception->message);
		return;
	};

	RecursionThrow(this->thrown_exception);
	this->ClearException();

	if (threadnum == 0){
		PycpExit(1);
	}
}

PycpThreadState* PycpThreadStateGet(){
	return PycpThreadState::current;
}

void PycpThreadStateSet(PycpThreadState* state){
	PycpThreadState* old = PycpThreadState::current;
	PycpThreadState::current = state;
	delete old;
}

void PycpExit(int state){
	PycpFinalize();
	exit(state);
}

} // namespace Pycp