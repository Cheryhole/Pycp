#ifndef PYCP_DECIMAL_HPP
#define PYCP_DECIMAL_HPP

#include "object/PycpObject.hpp"
#include "object/PycpMethodTable.hpp"   // MethodEntry / MethodTableFn
#include <cstdint>
#include <string>

// mpdecimal（libmpdec）前向声明：mpd_t 定义于第三方头 mpdecimal.h，
// 公共头只暴露指针，避免把第三方头扩散到全部消费方（stdlib 扩展等）。
struct mpd_t;

namespace Pycp{

// Decimal 类型：精确任意精度小数，基于 mpdecimal（CPython decimal 模块
// 的底层实现，General Decimal Arithmetic Specification）。默认上下文
// 精度 28 位有效数字（对齐 Python decimal 默认值）；加减乘精确，除法
// 除不尽时按上下文舍入（不抛错）。
//
// 混合运算提升规则（用户确认）：
//   Integer op Decimal -> Decimal（精确）
//   Float   op Decimal -> Float（Decimal 先转 double，有精度损失）
class PYCP_API Decimal : public Object{
	private:
		mpd_t* _value;

	public:
		Decimal();
		Decimal(const std::string&);
		Decimal(int64_t);
		Decimal(double);
		Decimal(Decimal*);
		Decimal(Object*);
		// 内部构造：直接接管 mpd_t 所有权（不复制）。仅运算辅助使用，
		// 外部代码请走字符串/数值构造。
		Decimal(mpd_t*);
		virtual ~Decimal();

		// 值访问（只读；运算请走魔术方法，内部管理 mpd 生命周期）。
		const mpd_t* get_value() const;
		// 转双精度浮点（有精度损失，供 Float 提升使用）。
		double to_double() const;
		// 科学计数法字符串（mpd_to_sci）。
		std::string to_string() const;

		// 静态工厂：从字符串构造 Decimal（返回 Owned，refcount=1）。
		static Object* FromString(const std::string& value);

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

		// 比较运算符：支持 Decimal / Integer / Float，返回小整数池
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

// Decimal 全部方法（一元/算术/比较等魔术方法）的唯一权威清单。
const std::vector<MethodEntry>& Decimal_method_table();

// 类型萃取特化：Decimal（无子类）。
template <> struct TypeTraits<Decimal> {
	static constexpr PycpTypeId   id            = PycpTypeId::Decimal;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::Hashable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif //PYCP_DECIMAL_HPP
