#ifndef __PYCP_DECIMAL_HPP__
#define __PYCP_DECIMAL_HPP__

#include "PycpObject.hpp"
#include <decimal/decimal>

namespace Pycp{

class PycpDecimal: public PycpObject{
	private:
		std::decimal::decimal128* container;

		inline long double ToLongDouble() const;
		inline int64_t ToInt64() const;

	public:
		PycpDecimal(long double input);
		PycpDecimal(int64_t input);
		PycpDecimal(std::decimal::decimal128 input);
		PycpDecimal(std::string input);
		PycpDecimal(PycpObject* input);
		~PycpDecimal();

		virtual PycpSize_t hash() const;
		virtual long double raw() const;

		// methods
		virtual PycpObject* __string__();
		virtual PycpObject* __integer__();
		virtual PycpObject* __decimal__();
		virtual PycpObject* __boolean__();

		virtual PycpObject* __addition__(PycpObject* obj);
		virtual PycpObject* __subtraction__(PycpObject* obj);
		virtual PycpObject* __multiplication__(PycpObject* obj);
		virtual PycpObject* __division__(PycpObject* obj);

};


} // namespace Pycp



#endif // __PYCP_DECIMAL_HPP__