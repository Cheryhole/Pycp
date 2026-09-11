#include "io.hpp"
#include "PycpFile.hpp"
#include "PycpModule.hpp"   // runtime 的 Module 完整定义
#include "PycpClass.hpp"    // BuiltinTypeClass / Class::add_method
#include "PycpMagic.hpp"    // GetMagicMethodFunction
#include "PycpFunction.hpp"
#include "PycpString.hpp"
#include "PycpInteger.hpp"
#include "PycpNone.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"
#include "PycpExt.h"       // PYCP_EXPORT_MODULE（Windows 下带 dllexport）

#include <iostream>

namespace Pycp {

namespace {

// io 模块的标准流对象（懒创建，进程生命周期常驻）。
File* g_io_stdin = nullptr;
File* g_io_stdout = nullptr;
File* g_io_stderr = nullptr;

// =============================================================
// print(value)：单参数，输出内容后自动附加换行符（对齐 Python3 print）。
// 暂不实现可变参数 / sep / end。
// =============================================================
Object* _builtin_print(Object*, Object** argv, std::size_t argc) {
	if (argc != 1 || argv == nullptr || argv[0] == nullptr) {
		throw TypeError("print() expects exactly 1 argument.");
	}
	// 写入内容（File::write 内部经 __string__ 转字符串并 flush）。
	g_io_stdout->write(argv[0]);
	// 追加换行符。
	Object* nl = String::FromCString("\n");
	g_io_stdout->write(nl);
	Decref(nl);
	return None::instance;
}

// =============================================================
// input(prompt)：单参数，打印 prompt（不换行）后从 stdin 读取一行，
// 返回截止至换行符之前的字符串（对齐 Python3 input）。
// =============================================================
Object* _builtin_input(Object*, Object** argv, std::size_t argc) {
	if (argc != 1 || argv == nullptr || argv[0] == nullptr) {
		throw TypeError("input() expects exactly 1 argument.");
	}
	// 打印提示（不换行）。
	g_io_stdout->write(argv[0]);
	// 读取一行（readline 返回 Owned String）。
	return g_io_stdin->readline();
}

// 将原生函数以指定名字放入模块命名空间。
void set_func(Module* mod, const char* name, PycpNativeFunction fn) {
	auto* ns = mod->get_namespace();
	Function* f = New<Function>(name, fn);
	(*ns)[name] = f;
	Incref(f);
	Decref(f); // namespace 持有
}

// File(path [, mode])：打开文件并返回 File 对象（对齐 Python open）。
// 支持 1 或 2 个参数：path 必填，mode 可选（默认 "r"）。
Object* _builtin_file_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc < 1 || argc > 2) {
		throw TypeError("File() expects 1 or 2 arguments (path [, mode]).");
	}
	Object* path_obj = argv[0];
	if (path_obj == nullptr || !path_obj->is_type("String")) {
		throw TypeError("File() path must be a string.");
	}
	std::string path = static_cast<String*>(path_obj)->get_value();
	std::string mode = "r";
	if (argc == 2) {
		Object* mode_obj = argv[1];
		if (mode_obj == nullptr || !mode_obj->is_type("String")) {
			throw TypeError("File() mode must be a string.");
		}
		mode = static_cast<String*>(mode_obj)->get_value();
	}
	return New<File>(path, mode);
}

// io.stdout / io.stdin / io.stderr 为 File，
// 支持 .write（仅字符串）、.readline 方法；
// print / input 为模块级函数。
Module* make_io_module() {
	Module* mod = Module::New(MODULE_NAME);
	auto* ns = mod->get_namespace();

	// 规则 2：io 模块命名空间注入 __name__ = 模块名。
	(*ns)["__name__"] = String::FromCString(MODULE_NAME);
	Incref((*ns)["__name__"]);
	Decref((*ns)["__name__"]); // namespace 持有

	// 标准流对象（懒创建，常驻）。
	if (g_io_stdin == nullptr) {
		g_io_stdin = File::FromStream("<stdin>",
		                             static_cast<void*>(&std::cin),
		                             nullptr);
		GC_AddRoot(g_io_stdin);
	}
	if (g_io_stdout == nullptr) {
		g_io_stdout = File::FromStream("<stdout>", nullptr,
		                              static_cast<void*>(&std::cout));
		GC_AddRoot(g_io_stdout);
	}
	if (g_io_stderr == nullptr) {
		g_io_stderr = File::FromStream("<stderr>", nullptr,
		                              static_cast<void*>(&std::cerr));
		GC_AddRoot(g_io_stderr);
	}

	(*ns)["stdin"] = g_io_stdin;
	Incref(g_io_stdin);
	(*ns)["stdout"] = g_io_stdout;
	Incref(g_io_stdout);
	(*ns)["stderr"] = g_io_stderr;
	Incref(g_io_stderr);

	// 模块级函数：print / input（对齐 Python3 单参数语义）。
	set_func(mod, "print", _builtin_print);
	set_func(mod, "input", _builtin_input);

	// File 类型类：io.File(path [, mode]) 打开文件并返回 File 对象。
	// 注册为 BuiltinTypeClass（而非普通 Function），使 io.File 显示为
	// "<class "File">" 且 io.File.__inspect__() 返回其方法名（write/read/
	// readline/readlines/close/open），而非空结果。
	{
		BuiltinTypeClass* file_cls = New<BuiltinTypeClass>("File", _builtin_file_ctor);
		// 把实例方法注册进类型类 methods_，使 __inspect__() 能枚举到。
		// 这些 Function 经由 File 实例的 __get_attribute__ 被懒创建并绑定，
		// 此处仅用于类级成员枚举与类方法调用（如 io.File.open 静态风格）。
		file_cls->add_method("write",     New<Function>("write",     File_write_fn()));
		file_cls->add_method("read",      New<Function>("read",      File_read_fn()));
		file_cls->add_method("readline",  New<Function>("readline",  File_readline_fn()));
		file_cls->add_method("readlines", New<Function>("readlines", File_readlines_fn()));
		file_cls->add_method("close",     New<Function>("close",     File_close_fn()));
		file_cls->add_method("open",      New<Function>("open",      File_open_fn()));
		// 注册 io.File 类级支持的魔术方法，使其 __inspect__ 能枚举
		// （与 pycp 内置类型 add_magic_methods 一致）。
		file_cls->add_method("__string__",         static_cast<Function*>(GetMagicMethodFunction("__string__")));
		file_cls->add_method("__inspect__",        static_cast<Function*>(GetMagicMethodFunction("__inspect__")));
		file_cls->add_method("__get_attribute__",  static_cast<Function*>(GetMagicMethodFunction("__get_attribute__")));
		file_cls->add_method("__set_attribute__",  static_cast<Function*>(GetMagicMethodFunction("__set_attribute__")));
		file_cls->add_method("__delete_attribute__", static_cast<Function*>(GetMagicMethodFunction("__delete_attribute__")));
		file_cls->add_method("__map__",            static_cast<Function*>(GetMagicMethodFunction("__map__")));
		file_cls->add_method("__boolean__",        static_cast<Function*>(GetMagicMethodFunction("__boolean__")));
		// 登记 File 到运行时类型类注册表：File 对象经 typeof/__class__ 解析到 io.File。
		RegisterTypeClass("File", file_cls);
		(*ns)["File"] = file_cls;
		Incref(file_cls);
		Decref(file_cls); // namespace 持有
	}

	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_io，按模块名导出）。
// 由 VM::load_module 经 LoadNativeModule 的 dlsym("PycpModule_io") 调用。
// 必须走 PYCP_EXPORT_MODULE：Windows 下没有 __declspec(dllexport) 时，
// 只有"整库无任何显式导出"才会被 MinGW 自动全导出，一旦本 TU 出现任何
// 其它导出符号，GetProcAddress 就找不到入口，import io 会静默失效。
PYCP_EXPORT_MODULE(io) {
	return make_io_module();
}

} // namespace Pycp
