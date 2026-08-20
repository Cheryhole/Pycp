#ifndef PYCP_INTEGER_HPP
#define PYCP_INTEGER_HPP

#define PYCP_INTEGER_INSTANCES 100

#include "PycpObject.hpp"
#include <cstdint>
#include <string>

namespace Pycp{

class Integer : public Object{
	private:
		int64_t _value;

	public:
		Integer();
		Integer(int64_t);
		Integer(const std::string&);
		Integer(Integer*);
		Integer(Object*);
		virtual ~Integer();

		int64_t get_value() const;

		// 静态工厂：从 long long 构造 Integer（返回 Owned，refcount=1）。
		static Object* FromLong(long long value);

		Object* __integer__() override;
		Object* __string__() override;
		Object* __negation__() override;
		Object* __addition__(Object*) override;
		Object* __subtraction__(Object*) override;
		Object* __multiplication__(Object*) override;
		Object* __division__(Object*) override;
		Object* __power__(Object*) override;

		static void Initialize();
		static void Finalize();
		// default constructed for commonly used integers, count from 0, [0] -> 0, [1] -> 1, [2] -> 2, ...
		static Integer* instances[PYCP_INTEGER_INSTANCES];
};

} // namespace Pycp

#endif //PYCP_INTEGER_HPP