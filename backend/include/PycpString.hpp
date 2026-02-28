#ifndef PYCP_STRING_HPP
#define PYCP_STRING_HPP

#include "PycpObject.hpp"
#include <string>

class PycpString : public PycpObject{
	private:
		std::string _value;

	public:
		PycpString();
		PycpString(const std::string&);
		PycpString(PycpString*);
		PycpString(PycpInteger*);
		virtual ~PycpString();

		std::string get_value() const;

		PycpObject* __integer__();
		PycpObject* __string__();
		PycpObject* __addition__(PycpObject*);
		PycpObject* __multiplication__(PycpObject*);

};

#endif // PYCP_STRING_HPP