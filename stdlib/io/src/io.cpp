#include "io.hpp"
#include "PycpFile.hpp"
#include "PycpModule.hpp"   // runtime 的 Module 完整定义
#include "PycpClass.hpp"    // 类型对象注册（Module::set_type）
#include "PycpFunction.hpp"
#include "PycpString.hpp"
#include "PycpInteger.hpp"
#include "PycpNone.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"
#include "PycpExtension.hpp" // 扩展唯一对外头（导出宏 + set_* + 参数规范框架）

#include <iostream>

namespace Pycp {

namespace {

// io 模块的标准流对象（懒创建，进程生命周期常驻）。
File* g_io_stdin = nullptr;
File* g_io_stdout = nullptr;
File* g_io_stderr = nullptr;

// 参数拆箱辅助（业务侧自行判型：框架只校验个数与名字，不做类型检查）。
std::string require_string(const char* fn, const char* param, Object* v) {
	if (v == nullptr || !IsString(v)) {
		throw TypeError(std::string(fn) + ": argument '" + param +
		                "' expects a string, got '" +
		                (v != nullptr ? v->type_name() : std::string("None")) + "'.");
	}
	return AsString(v);
}

// =============================================================
// print(value)：单参数，输出内容后自动附加换行符（对齐 Python3 print）。
// 暂不实现可变参数 / sep / end。
// =============================================================
// 保持单参数语义（对齐 Python3 print 的最小实现），不做可变参数。
Object* _builtin_print(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"print", { Extension::Arg::Required("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	// 写入内容（File::write 内部经 __string__ 转字符串并 flush）。
	g_io_stdout->write(r["value"]);
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
Object* _builtin_input(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"input", { Extension::Arg::Required("prompt") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	// 打印提示（不换行）。
	g_io_stdout->write(r["prompt"]);
	// 读取一行（readline 返回 Owned String）。
	return g_io_stdin->readline();
}

// File(path [, mode])：打开文件并返回 File 对象（对齐 Python open）。
// 支持 1 或 2 个参数：path 必填，mode 可选（默认 "r"）。
// 默认值 "r" 在规范表内登记，仅在首次编译规范时构造一次并常驻。
Object* _builtin_file_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"File", { Extension::Arg::Required("path"),
		          Extension::Arg::Optional("mode", String::FromCString("r")) });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::string path = require_string("open", "path", r["path"]);
	std::string mode = require_string("open", "mode", r["mode"]);
	return New<File>(path, mode);
}

// io.stdout / io.stdin / io.stderr 为 File，
// 支持 .write（仅字符串）、.readline 方法；
// print / input 为模块级函数。
Module* make_io_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 规则 2：io 模块命名空间注入 __name__ = 模块名。
	mod->set_variable("__name__", String::FromCString(MODULE_NAME));

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

	mod->set_variable("stdin",  g_io_stdin);
	mod->set_variable("stdout", g_io_stdout);
	mod->set_variable("stderr", g_io_stderr);

	// 模块级函数：print / input（对齐 Python3 单参数语义）。
	mod->set_function("print", _builtin_print);
	mod->set_function("input", _builtin_input);

	// File 类型类：io.File(path [, mode]) 打开文件并返回 File 对象。
	// 注册为 BuiltinTypeClass（而非普通 Function），使 io.File 显示为
	// "<class "File">" 且 io.File.__inspect__() 返回其方法名（write/read/
	// readline/readlines/close/open），而非空结果。
	// File 类型类：一次性完整注册（方法表驱动，含类型类登记）。
	// io.File(path [, mode]) 打开文件并返回 File 对象；方法表使
	// io.File.__inspect__() 与 File 实例 __inspect__() 同源一致。
	// mode 的默认值与函数名 "File" 均由构造器内部的参数规范表登记
	// （构造调用时 self 为 nullptr，故规范表内显式写明函数名以生成可读报错）。
	mod->set_type("File", _builtin_file_ctor, /*initialize=*/nullptr, File_method_table);

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
