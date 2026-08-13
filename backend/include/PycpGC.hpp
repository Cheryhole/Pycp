#ifndef PYCP_GC_HPP
#define PYCP_GC_HPP

// =============================================================
// Pycp 统一垃圾回收 / 内存管理层
//
// 设计（混合方案，见路线图第 2 / 18 步）：
//   1. 引用计数 (RC) 为主：
//        - 每个对象对象头内含 refcount，创建时 =1（Owned 语义）
//        - Incref / Decref 维护引用，refcount==0 即释放
//        - 释放确定性好、对 Native Compiler 友好
//   2. 标记-清除 (Cycle GC) 兜底：
//        - 维护 root 集合（全局对象、调用栈局部、显式 AddRoot）
//        - GC_Collect() 从 root 出发标记可达对象，清除不可达者
//        - 专门回收循环引用（RC 单独无法处理）
//
// 生命周期三态语义（头注释约定）：
//   Owned    : 调用方需负责 Decref（工厂函数返回值默认为此态）
//   Borrowed : 仅借用，不增加引用，调用方不可跨作用域持有
//   Rooted   : 被登记为 GC root，存活至 RemoveRoot
//
// 命名约定（见 PycpConfig.hpp）：本头位于 namespace Pycp 内部，
// 故符号不加 PYCP 前缀；若需 C 链接请使用 PycpGC_C.hpp 的 PYCP_* 别名。
// =============================================================

#include "PycpConfig.hpp"
#include "PycpObject.hpp"

#include <cstddef>
#include <vector>

namespace Pycp {

// 引用计数操作（唯一合法的引用维护入口）
//   Incref : refcount++（传入 nullptr 安全忽略）
//   Decref : refcount--；归零时递归 Decref 子引用并 delete
PYCP_API void Incref(Object* obj);
PYCP_API void Decref(Object* obj);

// 对象分配登记 / 注销（构造后由工厂函数调用）
PYCP_API void GC_Track(Object* obj);
PYCP_API void GC_Untrack(Object* obj);

// GC root 管理（标记-清除的起始集合）
PYCP_API void GC_AddRoot(Object* obj);
PYCP_API void GC_RemoveRoot(Object* obj);

// 标记-清除兜底回收（仅回收不可达对象，不影响热路径）
PYCP_API void GC_Collect();

// 诊断：返回当前存活对象数量（含 root）
PYCP_API std::size_t GC_LiveCount();

// 统一分配辅助：构造即 refcount=1 并登记（Owned）
//   用法：auto* p = New<Integer>(42);
template <typename T, typename... Args>
T* New(Args&&... args) {
	Object* obj = new T(static_cast<Args&&>(args)...);
	GC_Track(obj);
	return static_cast<T*>(obj);
}

} // namespace Pycp

#endif // PYCP_GC_HPP
