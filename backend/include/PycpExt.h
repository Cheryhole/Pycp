#ifndef PYCP_EXT_H
#define PYCP_EXT_H

// =============================================================
// Pycp 原生扩展作者头（供 C++ 扩展 include）
//
// 扩展是一个动态库（Linux .so / Windows .dll / macOS .dylib），
// 通过 import <name> 被加载。扩展必须导出一个入口符号：
//
//   extern "C" Pycp::Module* PycpModuleInit();
//
// （所有动态库统一使用符号名 PycpModuleInit，运行时靠文件名区分模块。）
// 该函数返回一个已构建好的 Module（Owned，refcount=1），其
// 命名空间内放置导出的函数（Function，复用 PycpNativeFunction 签名）。
//
// 写法示例（myext.cpp）：
//
//   #include "PycpExt.h"
//
//   static Pycp::Object* my_add(Pycp::Object*, Pycp::Object** argv, std::size_t argc) {
//       int64_t a = Pycp::ArgInt(argv, 0, "my_add");
//       int64_t b = Pycp::ArgInt(argv, 1, "my_add");
//       return Pycp::Integer::FromLong(a + b);
//   }
//
//   PYCP_EXPORT_MODULE(myext) {
//       auto* m = Pycp::Module::New("myext");
//       auto* ns = m->get_namespace();
//       PYCP_SET_FUNC(ns, "add", my_add);
//       return m;
//   }
//
// 注意：
//   - 扩展与主程序必须使用相同编译器、相同 C++ 标准（C++17）、相同 ABI。
//   - 主程序以 -rdynamic（POSIX）导出符号，供扩展解析运行时 C++ 符号。
// =============================================================

#include "Pycp.hpp"
#include "PycpModule.hpp"
#include "PycpFunction.hpp"
#include "PycpGC.hpp"
#include "PycpNativeExt.hpp"

// 导出模块入口函数签名（extern "C"，避免 name mangling）。
// 统一符号名 PycpModuleInit（靠文件名区分模块），name 参数仅用于
// 模块对象命名，不再拼入符号名。
#define PYCP_EXPORT_MODULE(name) \
	extern "C" Pycp::Module* PycpModuleInit()

// 将函数 fn 以名字 fname 放入模块命名空间 ns。
//   构造 Function（Owned）-> 写入 map（Incref）-> 释放本地 Owned。
#define PYCP_SET_FUNC(ns, fname, fn)                                       \
	do {                                                                  \
		Pycp::Function* _f = Pycp::New<Pycp::Function>((fname), (fn));    \
		(*(ns))[(fname)] = _f;                                            \
		Pycp::Incref(_f);                                                 \
		Pycp::Decref(_f);                                                 \
	} while (0)

#endif // PYCP_EXT_H
