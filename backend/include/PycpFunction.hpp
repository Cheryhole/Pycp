#ifndef PYCP_FUNCTION_HPP
#define PYCP_FUNCTION_HPP

#include "PycpObject.hpp"
#include "PycpNone.hpp"
#include "PycpString.hpp"
#include "PycpEnvironment.hpp"
#include "PycpGC.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"
#include "PycpMethodTable.hpp"   // PycpCFunction / MethodEntry / MethodTableFn

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>

namespace Pycp {

class Class;       // 前向声明，避免与 PycpClass.hpp 循环依赖
class FixedList;
class Map;

// 函数种类（见路线图第 6 步：Native / Bytecode 区分）
enum class FunctionKind{
	Native,    // C++ 实现的内建/用户函数
	Bytecode,  // 未来由 VM 执行的字节码函数
};

// 原生函数统一签名（容器形态）定义在 PycpMethodTable.hpp：
//   Object* (*)(Object* self, FixedList* args, Map* kwargs)
//
// 字节码 / AOT 生成函数体专用签名（数组形态）：
//   AOT 生成的 pycp_fn_N 以 self 作闭包载体（BytecodeFunction*），形参按
//   argv[i] 线性绑定，生成代码内自行管理引用计数。把生成代码改成容器形态
//   收益低、风险高，故保留数组形态，并在 BytecodeFunction::invoke 内完成
//   「容器 <-> 数组」的还原。
using PycpCompiledFunction = Object* (*)(Object* self, Object** argv, std::size_t argc);

// 内建打印：全部实参由调用方收集为 FixedList（容器形态）。
Object* _builtin_print(Object* self, FixedList* args, Map* kwargs);
struct BuiltinFunction;

class PYCP_API Function : public Object{
	protected:
		FunctionKind kind;
		std::string name;

		// 方法定义所属的类（仅类方法有意义；顶层/模块函数为 nullptr）。
		// 用于 super() 推断"当前方法所属类"以正确解析父类，而非依赖
		// 最派生实例的类（否则继承链上重复调用 super 会无限递归）。
		Class* owner_class_ = nullptr;

		// Native 函数入口（无状态 C 函数指针，避免 C++ lambda [＆] 悬空捕获）
		PycpCFunction native;

	public:
		Function();
		Function(const char* name);
		Function(const char* name, PycpCFunction func);

		const char* get_name() const override { return name.c_str(); }

		// 方法所属类（供 super() 解析父类）。仅类方法设置，其余为 nullptr。
		Class* get_owner_class() const { return owner_class_; }
		void set_owner_class(Class* cls) { owner_class_ = cls; }

		// 字符串表示："<function \"name\" at 0xADDR>"。
		// 匿名函数（name 为空或 @anonymous）沿用同一格式，名字位置显示
		// @anonymous，即 <function "@anonymous" at 0xADDR>（不再是裸 @anonymous）。
		Object* __string__() override;
		// repr：与 __string__ 同形（<function "name" at 0xADDR>）。
		Object* __raw_string__() override;
		Object* __inspect__() override;

		// 内部调用：兼容旧 tree-walking 入口（无实参调用）。
		Object* __call__(Object* args) override;

		// =============================================================
		// 唯一调用门（容器形态）
		//
		//   self   : 接收者。模块级函数 / 构造器为 nullptr；实例方法与魔术
		//            方法由 BoundMethod 注入接收者。
		//   args   : 位置参数容器（Borrowed，仅调用期有效）。
		//   kwargs : 关键字参数字典（Borrowed，只读输入）。
		//
		// 默认实现：native != nullptr 时直接调用 native(self, args, kwargs)。
		// =============================================================
		virtual Object* invoke(Object* self, FixedList* args, Map* kwargs);

		// 便捷重载（数组形态位置实参）：供 VM CALL / Call / AOT 兼容入口使用。
		// 内部把 argv 打包为 FixedList 后转调容器形态入口；并处理「未绑定方法
		// 调用」——当 self 为 nullptr 且本函数是某类的方法（owner_class_ 非空）
		// 时，把首个位置实参提升为 self（对齐 `Class.method(obj, ...)` 语义）。
		Object* invoke(Object* self, Object** argv, std::size_t argc);

		static void Initialize();
		static void Finalize();

};

struct BuiltinFunction{
	static Function* print;
};

// 类型萃取特化：Function（可调用）。
template <> struct TypeTraits<Function> {
	static constexpr PycpTypeId   id            = PycpTypeId::Function;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::Callable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif // PYCP_FUNCTION_HPP
