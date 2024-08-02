#ifndef __PYCP_INTEGER_HPP__
#define __PYCP_INTEGER_HPP__

#include "PycpObject.hpp"
#include <stdint.h>

namespace Pycp{

class PycpInteger : public PycpObject {
	private:
		int64_t container;

	public:
		PycpInteger(const PycpInteger&) = delete;
		PycpInteger& operator=(const PycpInteger&) = delete;

		PycpInteger(int64_t input);
		PycpInteger(PycpObject* input);
		PycpInteger();
		~PycpInteger();

		virtual PycpSize_t hash() const;
		virtual int64_t raw() const;

		// methods
		virtual PycpObject* __string__();
		virtual PycpObject* __integer__();
		virtual PycpObject* __decimal__();
		virtual PycpObject* __boolean__();
		virtual PycpObject* __bytes__();

		virtual PycpObject* __addition__(PycpObject* obj);
		virtual PycpObject* __subtraction__(PycpObject* obj);
		virtual PycpObject* __multiplication__(PycpObject* obj);
		virtual PycpObject* __division__(PycpObject* obj);


};


}


#endif // __PYCP_INTEGER_HPP__