#ifndef PYCP_EXT_H
#define PYCP_EXT_H

// =============================================================
// Pycp 原生扩展作者头（供 C++ 扩展 include）
//
// 扩展是一个动态库（Linux .so / Windows .dll / macOS .dylib），
// 通过 import <name> 被加载。扩展必须导出一个入口符号：
//
//   extern "C" Pycp::Module* PycpModule_<name>();
//
// （符号名 = "PycpModule_" + 模块名，运行时按导入名 dlsym 查找，与
// AOT 子模块符号命名统一，如 myext 库导出 PycpModule_myext。）
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
//   - Windows 下主程序与扩展均链接 PycpRuntime_shared(DLL)，由 DLL 导出
//     运行时 C++ 符号供扩展解析；POSIX 下主程序以 -rdynamic 导出符号。
// =============================================================

#include "Pycp.hpp"
#include "PycpModule.hpp"
#include "PycpFunction.hpp"
#include "PycpGC.hpp"
#include "PycpNativeExt.hpp"

// 导出模块入口函数签名（extern "C"，避免 name mangling）。
// 符号名 = "PycpModule_" + name（name 拼入符号名），与运行时按导入名
// dlsym 查找的约定一致。Windows 下需显式 __declspec(dllexport) 才能让
// 扩展 DLL 导出该符号，否则 GetProcAddress 找不到 PycpModule_<name>。
#ifdef _WIN32
  #define PYCP_EXPORT_MODULE(name) \
	extern "C" __declspec(dllexport) Pycp::Module* PycpModule_##name()
#else
  #define PYCP_EXPORT_MODULE(name) \
	extern "C" Pycp::Module* PycpModule_##name()
#endif

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
