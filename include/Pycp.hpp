#ifndef __PYCP_HPP__
#define __PYCP_HPP__

#include "PycpDefines.h"
#include "PycpDebug.hpp"
#include "Objects/PycpObject.hpp"
#include "PycpInterpreter.hpp"

namespace Pycp{

struct PycpLastException_t{
	PycpLastException_t(const PycpLastException_t&) = delete;
	PycpLastException_t& operator=(const PycpLastException_t&) = delete;

	PycpLastException_t() = default;
	~PycpLastException_t() = default;

	std::string message;
	PycpSize_t line;
	PycpException* exception;
};

PycpLastException_t* PycpLastException = nullptr;

} // namespace Pycp

#endif // __PYCP_HPP__