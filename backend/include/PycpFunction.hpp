#ifndef PYCP_FUNCTION_HPP
#define PYCP_FUNCTION_HPP

#include "PycpObject.hpp"
#include "PycpNone.hpp"
#include "PycpString.hpp"
#include "PycpEnvironment.hpp"
#include <functional>
#include <iostream>
#include <memory>

namespace Pycp {

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

class Function : public Object{
	protected:
		FunctionKind kind;
		const char* name;

		// Native 函数入口（无状态 C 函数指针，避免 C++ lambda [＆] 悬空捕获）
		PycpNativeFunction native;

	public:
		Function();
		Function(const char* name);
		Function(const char* name, PycpNativeFunction func);

		FunctionKind get_kind() const { return kind; }
		const char* get_name() const { return name; }

		// 内部调用：经统一 argv/argc 形态
		Object* __call__(Object* args) override;
		virtual Object* invoke(Object** argv, std::size_t argc);

		static void Initialize();
		static void Finalize();

};

struct BuiltinFunction{
	static Function* print;
};

// =============================================================
// 闭包（Closure）：携带捕获环境的 native 函数对象
//
// AOT 生成的函数 pycp_fn_N 通过 self（本对象）取捕获环境，
// 从而访问外层作用域的局部变量，实现与 VM 的 BytecodeFunction
// 一致的闭包语义。捕获环境以 shared_ptr 持有，保证闭包作为
// 参数 / 返回值传递时被捕获变量不被提前释放。
// =============================================================
class Closure : public Function{
	private:
		std::shared_ptr<BC::Environment> captured_;

	public:
		Closure(const char* name, PycpNativeFunction func,
		        std::shared_ptr<BC::Environment> captured)
			: Function(name, func), captured_(std::move(captured)) {}

		std::shared_ptr<BC::Environment> get_captured() const { return captured_; }
};

} // namespace Pycp

#endif // PYCP_FUNCTION_HPP
