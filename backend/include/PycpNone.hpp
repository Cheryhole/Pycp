#ifndef PYCP_NONE_HPP
#define PYCP_NONE_HPP

#include "PycpObject.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"

class PycpNone : public PycpObject{
	public:
		static PycpNone* instance;

		PycpNone() : PycpObject(PYCP_TP_NONE){}
		~PycpNone() = default;

		PycpObject* __integer__() override{
			return new PycpInteger(0ll);
		}

		PycpObject* __string__() override{
			return new PycpString("None");
		}
};

PycpNone* PycpNone::instance = nullptr;

#endif // PYCP_NONE_HPP