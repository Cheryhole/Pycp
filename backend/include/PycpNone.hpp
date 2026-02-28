#ifndef PYCP_NONE_HPP
#define PYCP_NONE_HPP

#include "PycpObject.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"

class PycpNone : public PycpObject{
	public:
		static PycpNone* instance;

		PycpNone() : PycpObject(PYCP_NONE){}
		~PycpNone() = default;

		PycpObject* __integer__() override{
			return new PycpInteger(0);
		}

		PycpObject* __string__() override{
			return new PycpString("None");
		}
};

PycpNone* PycpNone::instance = new PycpNone();


#endif // PYCP_NONE_HPP