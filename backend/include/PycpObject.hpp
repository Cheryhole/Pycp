#ifndef PYCP_OBJECT_HPP
#define PYCP_OBJECT_HPP

#include "PycpException.hpp"

enum PycpType{
	PYCP_TP_OBJECT,
	PYCP_TP_NONE,
	PYCP_TP_INTEGER,
	PYCP_TP_STRING,
	PYCP_TP_FUNCTION,
};

class PycpObject{
	private:

	public:
		PycpType type;

		PycpObject(PycpType type = PYCP_TP_OBJECT);
		virtual ~PycpObject();

		virtual PycpObject* __integer__();
		virtual PycpObject* __string__();
		virtual PycpObject* __negation__();
		virtual PycpObject* __call__(PycpObject*);
		virtual PycpObject* __addition__(PycpObject*);
		virtual PycpObject* __subtraction__(PycpObject*);
		virtual PycpObject* __multiplication__(PycpObject*);
		virtual PycpObject* __division__(PycpObject*);

};

class PycpInteger;
class PycpString;

#endif // PYCP_OBJECT_HPP