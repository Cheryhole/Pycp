#ifndef PYCP_CLASSTOOLS_HPP
#define PYCP_CLASSTOOLS_HPP

// =============================================================
// Pycp classtools 标准库公开头
//
// classtools 提供类工具运行时对象（均非语言关键字）：
//   - private / public：可见性装饰器函数（Function），经 C++ ABI 导出。
//     作为通用装饰器语法糖：@private / @public 把被装饰对象作为参数传给
//     本函数，设置该对象的可见性（公开/私有）后原样返回。类内成员控制
//     类外访问可见性；模块顶层符号控制其他文件 import 时的可访问性。
//     注意：本库的 public/private 与 Pycp 库中的同名函数功能完全一致，
//     保留本库导出以兼容 `from classtools import public, private` 用法。
//   - super：原生函数，返回当前类的父类 Class（经 thread_local 当前
//     self 上下文），供子类方法中 super().__initialize__(self, ...) 调用。
//
// `from classtools import super, public, private` 将这些运行时对象绑定到
// 当前命名空间，之后即可作为普通标识符使用。
//
// 动态库导出统一入口符号 PycpModuleInit（extern "C"），由运行时
// VM::load_module 经 LoadNativeModule 的 dlsym("PycpModuleInit") 调用。
// =============================================================

namespace Pycp {

class Module; // 前置声明（完整定义见 backend/include/PycpModule.hpp）

// 本库的模块名（import classtools 时匹配）。
constexpr const char* MODULE_NAME = "classtools";

// 动态库入口（extern "C" 定义于 classtools.cpp）。
extern "C" Module* PycpModuleInit();

} // namespace Pycp

#endif // PYCP_CLASSTOOLS_HPP
