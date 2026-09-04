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

// AOT 转译模块（.gen.cpp）统一使用的导出宏：把模块初始化函数 PycpModule_<name>
// 声明/定义为可被「编译为 DLL 时导出 / 编译进主程序或静态库时按普通符号
// 链接」的形态。
//
//   Windows：
//     - PYCP_BUILDING_MODULE 已定义（本 TU 正在构建自身的模块 DLL）
//         -> __declspec(dllexport)，使 GetProcAddress 能解析入口符号
//     - 未定义（本 TU 编译进主程序 / 静态模块库）
//         -> 展开为 extern "C"：符号在本镜像内定义、由同镜像内其它 TU 的
//            拉入桩直接链接，不经过 DLL 边界，故既不需要也不应带
//            __declspec(dllimport)（否则 MSVC 会把静态库内符号误当作
//            DLL 导入，要求 __imp_ 符号而链接失败）。
//   POSIX：默认符号可见，展开为 extern "C"。
//
// 注意：本宏只控制「模块入口符号」自身的导入/导出形态；「运行时 API」
// （PYCP_API）的 dllimport 仍由各 TU 是否定义 PYCP_STATIC 决定（仅当
// 静态链接 libPycpRuntime 时需要定义，由生成的 CMake 按 runtime 形态设置）。
// 此外本宏仅用于声明/定义本 TU 自己的模块入口；对被依赖模块的 extern
// 前向声明（AOT 拉入桩等）必须继续用裸 extern "C"。
#ifdef _WIN32
  #ifdef PYCP_BUILDING_MODULE
    #define PYCP_MODULE_EXPORT extern "C" __declspec(dllexport)
  #else
    #define PYCP_MODULE_EXPORT extern "C"
  #endif
#else
  #define PYCP_MODULE_EXPORT extern "C"
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
