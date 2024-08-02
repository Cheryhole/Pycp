#ifndef __PYCP_STRING_HPP__
#define __PYCP_STRING_HPP__

#include "PycpObject.hpp"
#include <string>

namespace Pycp{

class PycpString : public PycpObject{
	private:
		std::string* container;

	public:
		PycpString(const PycpString&) = delete;
		PycpString& operator=(const PycpString&) = delete;

		PycpString(const std::string& input);
		PycpString(PycpObject* input);
		PycpString();
		~PycpString();

		PycpSize_t length() const;
		virtual PycpSize_t hash() const;
		const char* raw() const;

		// methods

		virtual PycpObject* __string__();
		virtual PycpObject* __integer__();
		virtual PycpObject* __decimal__();
		virtual PycpObject* __boolean__();
		virtual PycpObject* __bytes__();

		virtual PycpObject* __addition__(PycpObject* obj);

}; // class PycpString

} // namespace Pycp

#endif // __PYCP_STRING_HPP__
