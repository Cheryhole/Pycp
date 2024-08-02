#ifndef __PYCP_NONE_HPP__
#define __PYCP_NONE_HPP__

#include "PycpObject.hpp"

namespace Pycp{

class PycpNone : public PycpObject{
	private:
		static const char* STRING;
		static const bool  BOOLEAN;
		static const int64_t INTEGER;
		static const long double DECIMAL;

	public:
		PycpNone(const PycpNone&) = delete;
		PycpNone& operator=(const PycpNone&) = delete;

		PycpNone(): PycpObject(PycpNone::STRING){}
		~PycpNone(){}
		
		virtual PycpSize_t hash() const{
			return UINT64_C(0);
		}

		// methods 
		virtual PycpObject* __string__() const{
			return new PycpString(PycpNone::STRING);
		}
		virtual PycpObject* __integer__() const{
			return new PycpInteger(PycpNone::INTEGER);
		}
		virtual PycpObject* __decimal__() const{
			return new PycpDecimal(PycpNone::DECIMAL);
		}

		virtual PycpObject* __boolean__() const{
			return new PycpBoolean(PycpNone::BOOLEAN);
		}

}; // class PycpNone

const char* PycpNone::STRING = "None";
const bool  PycpNone::BOOLEAN = false;
const int64_t PycpNone::INTEGER = INT64_C(0);
const long double PycpNone::DECIMAL = 0.0l;

} // namespace Pycp

#endif // __PYCP_NONE_HPP__