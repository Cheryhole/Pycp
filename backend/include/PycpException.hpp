#ifndef PYCP_EXCEPTION_HPP
#define PYCP_EXCEPTION_HPP

#include <stdexcept>
#include <string>

class PycpObject;

class PycpException : public std::runtime_error {
	public:
		explicit PycpException(const std::string& msg)
				: std::runtime_error("Exception: " + msg) {}
};

class PycpReturnException : public PycpException {
	public:
		PycpObject* value;

		explicit PycpReturnException(PycpObject* v)
				: PycpException("ReturnException"), value(v){}
};

class PycpTypeError : public PycpException {
	public:
		explicit PycpTypeError(const std::string& msg)
				: PycpException("TypeError: " + msg) {}
};

class PycpValueError : public PycpException {
	public:
		explicit PycpValueError(const std::string& msg)
				: PycpException("ValueError: " + msg) {}
};

#endif // PYCP_EXCEPTION_HPP