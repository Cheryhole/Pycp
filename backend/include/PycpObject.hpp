#ifndef PYCP_OBJECT_HPP
#define PYCP_OBJECT_HPP

#include "PycpException.hpp"
#include <cstdint>
#include <memory>

enum PycpType{
	PYCP_OBJECT,
	PYCP_NONE,
	PYCP_INTEGER,
	PYCP_STRING,
	PYCP_FUNCTION,
};

class PycpObject{
	private:
		uint32_t ref_cnt;

	public:
		PycpType type;

		PycpObject(PycpType type = PYCP_OBJECT);
		virtual ~PycpObject();

		void inc_ref_cnt();
		void dec_ref_cnt();

		virtual PycpObject* __integer__();
		virtual PycpObject* __string__();
		virtual PycpObject* __call__(PycpObject*);
		virtual PycpObject* __addition__(PycpObject*);
		virtual PycpObject* __subtraction__(PycpObject*);
		virtual PycpObject* __multiplication__(PycpObject*);
		virtual PycpObject* __division__(PycpObject*);

};

class PycpInteger;
class PycpString;

#endif // PYCP_OBJECT_HPP