#ifndef PYCP_FUNCTION_HPP
#define PYCP_FUNCTION_HPP

#include "PycpObject.hpp"

class PycpFunction : public PycpObject{
	private:
		bool builtin;

	public:
		PycpFunction(const std::string& name, const std::string& code, const std::vector<std::string>& arg_names, const std::string& docstring = "") : PycpObject(name, code, arg_names, docstring) {}
};

#endif // PYCP_FUNCTION_HPP