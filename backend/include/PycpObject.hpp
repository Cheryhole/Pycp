#ifndef PYCP_OBJECT_HPP
#define PYCP_OBJECT_HPP

#include "PycpException.hpp"

namespace Pycp{

enum class Type{
	OBJECT,
	NONE,
	INTEGER,
	STRING,
	FUNCTION,
};

class Object{
	private:

	public:
		Type type;

		Object(Type type = Type::OBJECT);
		virtual ~Object();

		virtual Object* __integer__();
		virtual Object* __string__();
		virtual Object* __negation__();
		virtual Object* __call__(Object*);
		virtual Object* __addition__(Object*);
		virtual Object* __subtraction__(Object*);
		virtual Object* __multiplication__(Object*);
		virtual Object* __division__(Object*);

};

class Integer;
class String;

} // namespace Pycp

#endif // PYCP_OBJECT_HPP