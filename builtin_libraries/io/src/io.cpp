#include "io.hpp"
#include "PycpFile.hpp"
#include "PycpModule.hpp"   // runtime 的 Module 完整定义
#include "PycpFunction.hpp"
#include "PycpString.hpp"
#include "PycpInteger.hpp"
#include "PycpNone.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"

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

// io.stdout / io.stdin / io.stderr 为 File，
// 支持 .write（仅字符串）、.readline 方法；
// print / input 为模块级函数。
Module* make_io_module() {
	Module* mod = Module::New(MODULE_NAME);
	auto* ns = mod->get_namespace();

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

	return mod;
}

} // anonymous namespace

// 动态库入口（统一符号名 PycpModuleInit，靠文件名区分模块）。
// 由 VM::load_module 经 LoadNativeModule 的 dlsym("PycpModuleInit") 调用。
extern "C" Module* PycpModuleInit() {
	return make_io_module();
}

} // namespace Pycp
