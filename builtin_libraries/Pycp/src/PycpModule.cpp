#include "pycp_stdlib.hpp"
#include "PycpModule.hpp"   // runtime 的 Module 完整定义
#include "PycpFunction.hpp"
#include "PycpString.hpp"
#include "PycpInteger.hpp"
#include "PycpNone.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"
#include "PycpClass.hpp"

namespace Pycp {

namespace {

// String(x)：String 类型构造器。调用对象的 __string__ 转换为字符串，
// 返回内置 String 对象。语义对齐 Python 的 str(x)。
Object* _builtin_string_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("String() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("String() argument is null.");
	Object* s = argv[0]->__string__();
	if (s == nullptr) return String::FromCString("");
	// __string__ 可能返回 Borrowed（如 String 返回 this），需 Incref 转为
	// Owned（BuiltinTypeClass::instantiate 期望 Owned 返回值）。
	Incref(s);
	return s;
}

// Integer(x)：Integer 类型构造器。复用 Integer(Object*) 构造：
// Integer 传入返回自身；String 传入解析为整数（String::__integer__，
// 非法抛 ValueError）；其他对象调 __integer__。语义对齐 Python 的 int(x)。
Object* _builtin_integer_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("Integer() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("Integer() argument is null.");
	return New<Integer>(argv[0]);
}

// List(x)：List 类型构造器。调用对象的 __list__ 转换，返回内置 List。
// 本版仅 list -> list 幂等（返回自身）；其他类型抛 TypeError。
Object* _builtin_list_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("List() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("List() argument is null.");
	Object* r = argv[0]->__list__();
	if (r == nullptr) throw TypeError("List() conversion failed.");
	return r;
}

// 将类对象以指定名字放入模块命名空间（构造 BuiltinTypeClass ->
// Incref 进 map -> 释放 Owned）。
void set_type_class(Module* mod, const char* name, PycpNativeFunction ctor) {
	auto* ns = mod->get_namespace();
	BuiltinTypeClass* cls = New<BuiltinTypeClass>(name, ctor);
	(*ns)[name] = cls;
	Incref(cls);
	Decref(cls); // namespace 持有
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
// 注意：本实现与 classtools 库中的 private/public 功能完全一致，
// 两库均导出同名装饰器函数以保证 `from classtools import public`
// 与 `from Pycp import public` 行为一致。
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

// Object 的默认 __initialize__（空实现，接受 self，供子类 super() 调用）。
Object* _object_init(Object*, Object** argv, std::size_t argc) {
	(void)argv; (void)argc;
	return None::instance; // 无操作
}

// 将普通类对象放入命名空间（用于 Pycp.Object 基类）。
void set_plain_class(Module* mod, const char* name) {
	auto* ns = mod->get_namespace();
	Class* cls = New<Class>(name);
	(*ns)[name] = cls;
	Incref(cls);
	Decref(cls); // namespace 持有
}

// 将普通类对象放入命名空间，并附带默认 __initialize__（用于 Pycp.Object）。
void set_object_class(Module* mod, const char* name) {
	auto* ns = mod->get_namespace();
	Class* cls = New<Class>(name);
	// 默认 __initialize__：空实现，供子类 super().__initialize__(self) 调用。
	Function* init = New<Function>("__initialize__", _object_init);
	cls->add_method("__initialize__", init);
	Decref(init); // add_method 已 Incref
	(*ns)[name] = cls;
	Incref(cls);
	Decref(cls); // namespace 持有
}

Module* make_pycp_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 内置类型类：Pycp.String(x) / Pycp.Integer(x)。
	// 调用时走类实例化路径（BuiltinTypeClass::instantiate），把参数
	// 传给构造回调，返回内置 String / Integer 对象。
	set_type_class(mod, "String", _builtin_string_ctor);
	set_type_class(mod, "Integer", _builtin_integer_ctor);
	set_type_class(mod, "List", _builtin_list_ctor);

	// Pycp.Object 基类：类似 Python 的 object，含默认空 __initialize__
	// （供子类 super().__initialize__(self) 调用）。不自动继承；
	// 实例化走默认 instantiate（返回 Instance）。
	set_object_class(mod, "Object");

	// 可见性装饰器函数：@private / @public（与 classtools 库功能一致）。
	set_func(mod, "private", _builtin_private);
	set_func(mod, "public",  _builtin_public);

	return mod;
}

} // anonymous namespace

// 动态库入口（统一符号名 PycpModuleInit，靠文件名区分模块）。
// 由 VM::load_module 经 LoadNativeModule 的 dlsym("PycpModuleInit") 调用。
extern "C" Module* PycpModuleInit() {
	return make_pycp_module();
}

} // namespace Pycp
