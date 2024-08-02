#ifndef __PYCP_TUPLE_HPP__
#define __PYCP_TUPLE_HPP__

#include "PycpObject.hpp"
#include "xxhash.h"

#include <cstdarg>
#include <cstring>
#include <cstdlib>

namespace Pycp{

class PycpTuple : public PycpObject{
	private:
		PycpObject** container;
		PycpSize_t count;

	public:
		PycpTuple(const PycpTuple&) = delete;
		PycpTuple& operator=(const PycpTuple&) = delete;

		PycpTuple(PycpSize_t size, ...);
		PycpTuple(PycpSize_t size, PycpObject* items[]);
		PycpTuple(PycpSize_t size);
		~PycpTuple();

		PycpObject* get(PycpSize_t index);
		PycpExceptions set(PycpSize_t index, PycpObject* obj);

		/*
		the parse of PycpTuple is like PyTupleParse.
		the format is for parsing the tuple.
		each charactor represents a type:
			"i" represents integer (to int64_t)
			"s" represents string (to char*)
			"d" represents decimal (to long double)
			"o" represents object (to PycpObject*)
		the characters after the colon are considered as resolved names
		*/
		PycpExceptions parse(const char* format, ...);

		PycpSize_t length() const;
		virtual PycpSize_t hash() const;

		virtual PycpObject* __string__();
		virtual PycpObject* __boolean__();
		virtual PycpObject* __list__();
		virtual PycpObject* __tuple__();

		virtual PycpObject* __addition__(PycpObject* obj);
		virtual PycpObject* __multiplication__(PycpObject* obj);

};

} // namespace Pycp

#endif // __PYCP_TUPLE_HPP__