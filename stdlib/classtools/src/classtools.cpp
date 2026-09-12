#include "classtools.hpp"
#include "PycpModule.hpp"   // runtime 的 Module 完整定义
#include "PycpFunction.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"
#include "PycpClass.hpp"
#include "PycpExtension.hpp" // 扩展唯一对外头（导出宏 + set_* + 参数规范框架）

namespace Pycp {

namespace {

// super()：返回当前类的父类 Class（构造函数/类型本身，而非实例）。
// 通过 thread_local 当前 self 上下文（push_current_self/current_self）取
// 当前方法执行中的接收者实例，再取其所属类 → 父类。语义对齐 Python 的
// super()。返回 Borrowed 引用经 Incref 转为 Owned。
// 无参：个数校验由参数规范表完成。
Object* _builtin_super(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("super", {});
	spec.Bind(args, kwargs);
	Instance* self = current_self();
	if (self == nullptr) {
		throw TypeError("super() used outside a method.");
	}
	// 基于"当前方法所属类"解析父类，而非最派生实例的类，避免继承链上
	// 重复调用 super 时无限递归到自身（如 o1.__initialize__ 内 super 应
	// 取 o1 的父类而非 o2 的父类）。
	Class* cls = current_class();
	if (cls == nullptr) {
		// 退化：无方法上下文时用最派生实例类（保持旧行为）。
		cls = self->get_class();
	}
	Class* parent = (cls != nullptr) ? cls->get_parent() : nullptr;
	if (parent == nullptr) {
		throw TypeError("super(): class '" +
		                std::string(cls ? cls->get_name() : "?") + "' has no parent.");
	}
	Incref(parent);
	return parent;
}

// =============================================================
// private / public：可见性装饰器函数
//
// 作为通用装饰器语法糖：@private / @public 把被装饰对象（函数或任意
// 对象）作为参数传给本函数，本函数设置该对象的可见性后原样返回，由
// 装饰器替换逻辑用返回值替换原对象。可见性经对象通用的
// is_private()/set_private() 属性（C++ ABI 底层）设置。
//
//   - 类内成员：控制该成员在类外的访问可见性（公开/私有）。
//   - 模块顶层符号：控制其他文件 import 时是否可访问。
//
// 注意：本实现与 Pycp 库中的 private/public 功能完全一致，两库均导出
// 同名装饰器函数以保证 `from classtools import public` 与
// `from Pycp import public` 行为一致。
// =============================================================
// 内部辅助（非注册函数）：参数个数与类型已由 private/public 的框架 thunk 校验。
Object* _builtin_visibility(Object* target, bool priv) {
	if (target == nullptr) {
		throw TypeError("visibility decorator expects exactly 1 argument.");
	}
	target->set_private(priv);
	// 原样返回被装饰对象（装饰器替换逻辑用返回值替换原对象）。
	Incref(target);
	return target;
}

Object* _builtin_private(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"private", { Extension::Arg::Required("target") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return _builtin_visibility(r["target"], /*priv=*/true);
}

Object* _builtin_public(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"public", { Extension::Arg::Required("target") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return _builtin_visibility(r["target"], /*priv=*/false);
}

// =============================================================
// readonly：只读装饰器函数
//
// @readonly 把被装饰对象（变量/函数/类/实例/任意对象）设为只读后
// 原样返回。只读语义（C++ ABI 底层标志 readonly_）：
//   - 任意对象冻结：拒绝 obj.attr = v / delete obj.attr；
//   - 模块顶层常量绑定：该名字不可被再次赋值覆盖（由存储层检查）；
//   - 类成员字段（经 MAKE_CLASS 读取返回对象 is_readonly）只读。
// 与 Pycp 库 readonly 行为一致，两库均导出同名函数。
// =============================================================
Object* _builtin_readonly(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"readonly", { Extension::Arg::Required("target") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* target = r["target"];
	if (target == nullptr) {
		throw TypeError("readonly decorator expects exactly 1 argument.");
	}
	target->set_readonly(true);
	// 原样返回被装饰对象（装饰器替换逻辑用返回值替换原对象）。
	Incref(target);
	return target;
}

Module* make_classtools_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 规则 2：classtools 模块命名空间注入 __name__ = 模块名。
	mod->set_variable("__name__", String::FromCString(MODULE_NAME));

	// 可见性装饰器函数：@private / @public 作为普通函数被装饰器语法糖
	// 调用，设置被装饰对象的可见性。
	mod->set_function("private", _builtin_private);
	mod->set_function("public",  _builtin_public);

	// 只读装饰器：@readonly（对象冻结 / 模块常量绑定 / 只读成员）。
	mod->set_function("readonly", _builtin_readonly);

	// super：运行时函数，返回父类 Class（thread_local self 上下文）。
	mod->set_function("super", _builtin_super);

	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_classtools，按模块名导出）。
// 由 VM::load_module 经 LoadNativeModule 的 dlsym("PycpModule_classtools") 调用。
// 必须走 PYCP_EXPORT_MODULE：Windows 下没有 __declspec(dllexport) 时，
// 只有"整库无任何显式导出"才会被 MinGW 自动全导出，一旦本 TU 出现任何
// 其它导出符号，GetProcAddress 就找不到入口，import classtools 会静默失效。
PYCP_EXPORT_MODULE(classtools) {
	return make_classtools_module();
}

} // namespace Pycp
