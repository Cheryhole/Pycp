#include "classtools.hpp"
#include "PycpModule.hpp"   // runtime 的 Module 完整定义
#include "PycpFunction.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"
#include "PycpClass.hpp"

namespace Pycp {

namespace {

// super()：返回当前类的父类 Class（构造函数/类型本身，而非实例）。
// 通过 thread_local 当前 self 上下文（push_current_self/current_self）取
// 当前方法执行中的接收者实例，再取其所属类 → 父类。语义对齐 Python 的
// super()。返回 Borrowed 引用经 Incref 转为 Owned。
Object* _builtin_super(Object*, Object** argv [[maybe_unused]], std::size_t argc) {
	if (argc != 0) throw TypeError("super() expects 0 arguments.");
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
Object* _builtin_visibility(Object*, Object** argv, std::size_t argc, bool priv) {
	if (argc != 1 || argv == nullptr || argv[0] == nullptr) {
		throw TypeError("visibility decorator expects exactly 1 argument.");
	}
	argv[0]->set_private(priv);
	// 原样返回被装饰对象（装饰器替换逻辑用返回值替换原对象）。
	Incref(argv[0]);
	return argv[0];
}

Object* _builtin_private(Object* self, Object** argv, std::size_t argc) {
	return _builtin_visibility(self, argv, argc, /*priv=*/true);
}

Object* _builtin_public(Object* self, Object** argv, std::size_t argc) {
	return _builtin_visibility(self, argv, argc, /*priv=*/false);
}

// 将原生函数以指定名字放入模块命名空间。
void set_func(Module* mod, const char* name, PycpNativeFunction fn) {
	auto* ns = mod->get_namespace();
	Function* f = New<Function>(name, fn);
	(*ns)[name] = f;
	Incref(f);
	Decref(f); // namespace 持有
}

Module* make_classtools_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 可见性装饰器函数：@private / @public 作为普通函数被装饰器语法糖
	// 调用，设置被装饰对象的可见性。
	set_func(mod, "private", _builtin_private);
	set_func(mod, "public",  _builtin_public);

	// super：运行时函数，返回父类 Class（thread_local self 上下文）。
	set_func(mod, "super", _builtin_super);

	return mod;
}

} // anonymous namespace

// 动态库入口（统一符号名 PycpModuleInit，靠文件名区分模块）。
// 由 VM::load_module 经 LoadNativeModule 的 dlsym("PycpModuleInit") 调用。
extern "C" Module* PycpModuleInit() {
	return make_classtools_module();
}

} // namespace Pycp
