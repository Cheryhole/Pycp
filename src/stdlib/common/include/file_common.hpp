#ifndef PYCP_STDLIB_COMMON_FILE_COMMON_HPP
#define PYCP_STDLIB_COMMON_FILE_COMMON_HPP

// =====================================================================
// stdlib/common：File 类型类的模块绑定样板（io / filesystem 共用）
// ---------------------------------------------------------------------
// File 的「实现」（方法表、构造器、类型类）始终位于运行时
// src/object/PycpFile.* —— 这样新增/既有方法对所有程序（含 AOT 产物）
// 一律可见。common 只统一「把运行时唯一的 File 类型类暴露到某个 stdlib
// 模块命名空间」这段三行样板：
//     io.File 与 filesystem.File 指向同一 Class 对象（别名）。
//
// 同 visibility.hpp：inline（vague linkage），避免静态 AOT 把多份扩展
// 归档链入同一 exe 时出现「多重定义」。
// =====================================================================

#include "object/PycpModule.hpp" // Module 完整定义（set_object）
#include "object/PycpClass.hpp"  // Class 完整定义（Class* -> Object* 隐式转换）
#include "object/PycpFile.hpp"   // FileTypeClass()
#include "object/PycpGC.hpp"     // Incref

namespace Pycp {

// 把运行时唯一的 File 类型类绑定到给定模块的 "File" 名字上。
// FileTypeClass() 首次调用时创建（含类型类登记）并 root，此后返回同一
// 对象（Borrowed）；set_object 接管所有权，并把它登记进运行时类型类注册表
// （typeof / __class__ 可用）。
inline void BindFileType(Module* mod) {
	Object* file_cls = FileTypeClass(); // Borrowed（运行时 root 持有）
	Incref(file_cls);                   // set_object 接管所有权
	mod->set_object("File", file_cls);
}

} // namespace Pycp

#endif // PYCP_STDLIB_COMMON_FILE_COMMON_HPP
