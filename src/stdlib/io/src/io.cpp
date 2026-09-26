#include "io.hpp"
#include "file_common.hpp"       // common：File 类型类模块绑定样板
#include "object/PycpFile.hpp"   // File 实现已上提至运行时
#include "object/PycpModule.hpp"   // runtime 的 Module 完整定义
#include "object/PycpClass.hpp"    // 类型对象注册（Module::set_type）
#include "object/PycpFunction.hpp"
#include "object/PycpString.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpException.hpp"
#include "object/PycpConfig.hpp"
#include "abi/PycpABI.hpp"
#include "object/PycpExtension.hpp" // 扩展唯一对外头（导出宏 + set_* + 参数规范框架）

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
// print(*args, sep=" ", end="\n", file=io.stdout, flush=False)
// 对齐 Python 内建 print：
//   - 位置实参逐个经 __string__ 转字符串（对齐 str()），sep 连接、end 结尾；
//     允许 0 参（输出 end 本身）。
//   - sep / end：String 或 None（None = 恢复默认 " " / "\n"）。
//   - file：鸭子类型——任何有 write 方法的对象均可（经属性查找调其 write）；
//     显式传 None 时回退 io.stdout（对齐 CPython 哨兵语义）。
//   - flush：经 __boolean__ 真值化；print 走「不自动 flush 的写入」路径，
//     仅 flush 为真时调 file 的 flush（鸭子对象无 flush 属性则 AttributeError，
//     对齐 CPython）。
// sep/end/file/flush 写在 Arg::Rest 之后 => 位置隐含为可选关键字-only
// （只能按关键字传参，与 Python 签名一致）。
// =============================================================
Object* _builtin_print(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("print", {
		Extension::Arg::Rest("args"),
		Extension::Arg::Optional("sep", String::FromCString(" ")),
		Extension::Arg::Optional("end", String::FromCString("\n")),
		Extension::Arg::Optional("file", g_io_stdout),
		Extension::Arg::Optional("flush", New<Boolean>(0)),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);

	// sep / end：String 直用；None 恢复默认；其他类型报 TypeError。
	std::string sep = " ";
	Object* sep_o = r["sep"];
	if (sep_o != nullptr && sep_o != None::instance) {
		if (!IsString(sep_o)) {
			throw TypeError("print(): argument 'sep' expects a string, got '" +
			                sep_o->type_name() + "'.");
		}
		sep = static_cast<String*>(sep_o)->get_value();
	}
	std::string end = "\n";
	Object* end_o = r["end"];
	if (end_o != nullptr && end_o != None::instance) {
		if (!IsString(end_o)) {
			throw TypeError("print(): argument 'end' expects a string, got '" +
			                end_o->type_name() + "'.");
		}
		end = static_cast<String*>(end_o)->get_value();
	}

	// flush：经 __boolean__ 真值化（对齐 Python 任意 truthy）。
	bool do_flush = false;
	Object* flush_o = r["flush"];
	if (flush_o != nullptr && flush_o != None::instance) {
		Object* bt = flush_o->__boolean__();
		if (bt == nullptr || !bt->is_type("Boolean")) {
			if (bt != flush_o) Decref(bt);
			throw TypeError("print(): argument 'flush' expects a boolean-compatible value, got '" +
			                flush_o->type_name() + "'.");
		}
		do_flush = static_cast<Boolean*>(bt)->get_value() != 0;
		if (bt != flush_o) Decref(bt);
	}

	// file：None 哨兵回退 io.stdout（对齐 CPython）。
	Object* file = r["file"];
	if (file == nullptr || file == None::instance) {
		file = g_io_stdout;
	}

	// 组装完整输出（sep 连接 + end 结尾），一次写入。
	FixedList* values = static_cast<FixedList*>(r["args"]);
	std::string out;
	const std::size_t n = values->size();
	for (std::size_t i = 0; i < n; ++i) {
		if (i > 0) out += sep;
		Object* v = values->at(i);
		Object* s = v->__string__();
		if (s == nullptr || !s->is_type("String")) {
			if (s != v) Decref(s);
			throw TypeError("print(): __string__ did not return a String.");
		}
		out += static_cast<String*>(s)->get_value();
		if (s != v) Decref(s);
	}
	out += end;

	Object* content = String::FromCString(out.c_str());   // Owned
	if (IsExact<File>(file)) {
		// 快速路径：io.File 走「不自动 flush 的写入」，按 flush 参数刷出。
		File* f = static_cast<File*>(file);
		f->write(content, /*flush_after=*/false);
		if (do_flush) f->flush();
	} else {
		// 鸭子类型：经属性查找调其 write（无 write 属性则 AttributeError，
		// 对齐 Python）。
		Object* wfn = GetAttr(file, "write");
		Object* rc = Call(wfn, &content, 1);
		Decref(rc);
		if (do_flush) {
			Object* ffn = GetAttr(file, "flush");
			Object* empty[1] = {nullptr};
			Object* rf = Call(ffn, empty, 0);
			Decref(rf);
		}
	}
	Decref(content);
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

// io.stdout / io.stdin / io.stderr 为 File，
// 支持 .write（仅字符串）、.flush、.readline 方法；
// print（对齐 Python print(*args, sep, end, file, flush)）/ input 为模块级函数。
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

	// 模块级函数：print（Python 签名）/ input。
	mod->set_function("print", _builtin_print, /*with_keywords=*/true);
	mod->set_function("input", _builtin_input);

	// File 类型类：io.File(path [, mode]) 打开文件并返回 File 对象。
	// 绑定运行时唯一的 File 类型类——与 filesystem.File 指向同一对象
	// （别名）；该样板已上提到 stdlib/common/file_common.hpp。
	BindFileType(mod);

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
