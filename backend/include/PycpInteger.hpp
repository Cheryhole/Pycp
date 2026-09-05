#ifndef PYCP_INTEGER_HPP
#define PYCP_INTEGER_HPP

#define PYCP_INTEGER_INSTANCES 100

#include "PycpObject.hpp"
#include <cstdint>
#include <string>

namespace Pycp{

class PYCP_API Integer : public Object{
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
		Object* __boolean__() override;
		Object* __negation__() override;
		Object* __addition__(Object*) override;
		Object* __subtraction__(Object*) override;
		Object* __multiplication__(Object*) override;
		Object* __division__(Object*) override;
		Object* __power__(Object*) override;

		// 比较运算符：仅支持同类型 Integer，返回小整数池 Integer 0/1（PERMANENT）。
		Object* __less_than__(Object*) override;
		Object* __less_equal__(Object*) override;
		Object* __equal__(Object*) override;
		Object* __not_equal__(Object*) override;
		Object* __greater_than__(Object*) override;
		Object* __greater_equal__(Object*) override;
		Object* __inspect__() override;
		Object* __hash__() override;

		static void Initialize();
		static void Finalize();
		// default constructed for commonly used integers, count from 0, [0] -> 0, [1] -> 1, [2] -> 2, ...
		static Integer* instances[PYCP_INTEGER_INSTANCES];
};

} // namespace Pycp

// 对象相等判定辅助：调用 a->__equal__(b)，读取返回的 Integer 0/1 后释放该
// 临时结果（避免每次比较泄漏一个 Integer 对象）。放在 Integer 完整类型可见
// 处定义，调用方无需手动 Decref。
inline bool Pycp::object_equal(Pycp::Object* a, Pycp::Object* b) {
	Pycp::Object* r = a->__equal__(b);
	int64_t v = static_cast<Pycp::Integer*>(r)->get_value();
	Pycp::Decref(r);
	return v != 0;
}

#endif //PYCP_INTEGER_HPP