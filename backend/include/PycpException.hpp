#ifndef PYCP_EXCEPTION_HPP
#define PYCP_EXCEPTION_HPP

#include <stdexcept>
#include <string>

namespace Pycp{

class Object;

class Exception : public std::runtime_error{
	public:
		explicit Exception(const std::string& msg)
				: std::runtime_error("Exception: " + msg) {}
};

class TypeError : public Exception {
	public:
		explicit TypeError(const std::string& msg)
				: Exception("TypeError: " + msg) {}
};

class ValueError : public Exception {
	public:
		explicit ValueError(const std::string& msg)
				: Exception("ValueError: " + msg) {}
};

} // namespace Pycp

#endif // PYCP_EXCEPTION_HPP