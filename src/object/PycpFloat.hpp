#ifndef PYCP_FLOAT_HPP
#define PYCP_FLOAT_HPP

#include "object/PycpObject.hpp"
#include "object/PycpMethodTable.hpp"   // MethodEntry / MethodTableFn
#include <cstdint>
#include <string>

namespace Pycp{

// Float 类型：IEEE 754 双精度浮点数（C++ double，8 字节）。
// 参照 Integer 的实现模式：魔术方法虚分派 + 方法表 + TypeTraits 特化。
// 混合运算提升规则：Integer 参与 Float 运算自动提升为 Float；
// Decimal 参与 Float 运算时提升为 Float（有精度损失）。
class PYCP_API Float : public Object{
	private:
		double _value;

	public:
		Float();
		Float(double);
		Float(const std::string&);
		Float(Float*);
		Float(Object*);
		virtual ~Float();

		double get_value() const;

		// 静态工厂：从 double 构造 Float（返回 Owned，refcount=1）。
		static Object* FromDouble(double value);

		Object* __integer__() override;
		Object* __float__() override;
		Object* __string__() override;
		// repr：与 __string__ 一致（数值不带引号）。
		Object* __raw_string__() override;
		Object* __boolean__() override;
		Object* __negation__() override;
		Object* __addition__(Object*) override;
		Object* __subtraction__(Object*) override;
		Object* __multiplication__(Object*) override;
		Object* __division__(Object*) override;
		Object* __power__(Object*) override;

		// 比较运算符：支持 Integer 与 Float（自动提升），返回小整数池
		// Integer 0/1（PERMANENT）。
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
};

// Float 全部方法（一元/算术/比较等魔术方法）的唯一权威清单。
const std::vector<MethodEntry>& Float_method_table();

// 类型萃取特化：Float（无子类）。
template <> struct TypeTraits<Float> {
	static constexpr PycpTypeId   id            = PycpTypeId::Float;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::Hashable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif //PYCP_FLOAT_HPP
