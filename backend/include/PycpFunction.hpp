#ifndef PYCP_FUNCTION_HPP
#define PYCP_FUNCTION_HPP

#include "PycpObject.hpp"
#include "PycpNone.hpp"
#include "PycpString.hpp"
#include "PycpEnvironment.hpp"
#include "PycpGC.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <sstream>

namespace Pycp {

class Class;  // 前向声明，避免与 PycpClass.hpp 循环依赖

// 函数种类（见路线图第 6 步：Native / Bytecode 区分）
enum class FunctionKind{
	Native,    // C++ 实现的内建/用户函数
	Bytecode,  // 未来由 VM 执行的字节码函数
};

// 统一调用签名：self + argv/argc（见路线图第 5 步）
//   不再使用单一的 Object* args，避免信息不足
using PycpNativeFunction = Object* (*)(Object* self, Object** argv, std::size_t argc);

Object* _builtin_print(Object* self, Object** argv, std::size_t argc);
struct BuiltinFunction;

class PYCP_API Function : public Object{
	protected:
		FunctionKind kind;
		const char* name;

		// 方法定义所属的类（仅类方法有意义；顶层/模块函数为 nullptr）。
		// 用于 super() 推断"当前方法所属类"以正确解析父类，而非依赖
		// 最派生实例的类（否则继承链上重复调用 super 会无限递归）。
		Class* owner_class_ = nullptr;

		// Native 函数入口（无状态 C 函数指针，避免 C++ lambda [＆] 悬空捕获）
		PycpNativeFunction native;

	public:
		Function();
		Function(const char* name);
		Function(const char* name, PycpNativeFunction func);

		FunctionKind get_kind() const { return kind; }
		const char* get_name() const override { return name; }

		// 方法所属类（供 super() 解析父类）。仅类方法设置，其余为 nullptr。
		Class* get_owner_class() const { return owner_class_; }
		void set_owner_class(Class* cls) { owner_class_ = cls; }

		// 字符串表示：普通函数 "<function \"name\" at 0xADDR>"，
		// 匿名函数（name 为空或 @anonymous）输出 "@anonymous"。
		Object* __string__() override;

		// 内部调用：经统一 argv/argc 形态
		Object* __call__(Object* args) override;
		virtual Object* invoke(Object** argv, std::size_t argc);

		static void Initialize();
		static void Finalize();

};

struct BuiltinFunction{
	static Function* print;
};

} // namespace Pycp

#endif // PYCP_FUNCTION_HPP
