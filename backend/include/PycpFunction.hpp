#ifndef PYCP_FUNCTION_HPP
#define PYCP_FUNCTION_HPP

#include "PycpObject.hpp"
#include "PycpNone.hpp"
#include "PycpString.hpp"
#include <functional>
#include <iostream>

#define PYCP_FUNC(name, ...) \
	new Function(name, \
			[&](Object* args) -> Object* __VA_ARGS__)

namespace Pycp{

typedef std::function<Object*(Object*)> CFunction_t;

Object* _builtin_print(Object*);
struct BuiltinFunction;

class Function : public Object{
	private:
		CFunction_t func;

	protected:
		const char* name;	

	public:
		Function();
		Function(const char*);
		Function(const char*, CFunction_t);

		Object* __call__(Object* args) override;
		const char* get_name() const;
		static void Initialize();
		static void Finalize();

};

struct BuiltinFunction{
	static Function* print;
};

} // namespace Pycp

#endif // PYCP_FUNCTION_HPP