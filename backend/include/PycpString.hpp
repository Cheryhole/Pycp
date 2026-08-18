#ifndef PYCP_STRING_HPP
#define PYCP_STRING_HPP

#include "PycpObject.hpp"
#include <string>

namespace Pycp{

class String : public Object{
	private:
		std::string _value;

	public:
		String();
		String(const std::string&);
		String(String*);
		String(Object*);
		virtual ~String();

		std::string get_value() const;

		Object* __integer__();
		Object* __string__();
		Object* __addition__(Object*);
		Object* __multiplication__(Object*);

		static void Initialize();
		static void Finalize();

};

std::string AsString(Object*);

} // namespace Pycp

#endif // PYCP_STRING_HPP