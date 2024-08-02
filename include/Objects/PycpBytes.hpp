#ifndef __PYCP_BYTES_HPP__
#define __PYCP_BYTES_HPP__

#include "PycpObject.hpp"
#include <vector>
#include <cstring>
#include <unicode/ucnv.h>

namespace Pycp{

class PycpBytes : public PycpObject{
	private:
		std::vector<uint8_t>* container;
		static const char NULLCHAR;
		static const int PRIME;

	public:
		PycpBytes(const PycpBytes&) = delete;
		PycpBytes& operator=(const PycpBytes&)  = delete;

		PycpBytes(PycpSize_t input); // input is the conut of "\x00"
		PycpBytes(std::string input);
		PycpBytes();
		~PycpBytes();

		virtual PycpSize_t hash() const;
		PycpSize_t length() const;
		bool empty() const;

		virtual PycpObject* __string__();
		virtual PycpObject* __boolean__();
		virtual PycpObject* __bytes__();

}; // class PycpBytes

const char PycpBytes::NULLCHAR = '\x00';
const int PycpBytes::PRIME = 31;

} // namespace Pycp

#endif // __PYCP_BYTES_HPP__