#ifndef PYCP_ABI_HPP
#define PYCP_ABI_HPP

// =============================================================
// Pycp 公共 ABI（C++ 风格导出，供未来 Native Compiler 依赖）
//
// 该头文件是 Runtime 对外的稳定边界：
//   - 不直接暴露内部 class 布局 / vtable 细节
//   - 所有对象经 PycpObject* 句柄操作
//   - 运算经 Add / Sub ... 自由函数封装，
//     内部转调现有虚函数（__addition__ 等）
//   - 调用统一为 Call(callable, argv, argc) 形态
//
// 未来 Native Compiler 只需依赖 pycp.h（本头 umbrellized 后）。
//
// 命名约定（见 PycpConfig.hpp）：
//   - 下方 namespace Pycp 内的自由函数【不】带 PYCP 前缀，
//     由 PYCP_API 提供 C++ 风格导出（保留 namespace，可重载）。
//   - 文件末尾 extern "C" 区块提供带 PYCP 前缀的 C 语言 ABI 别名，
//     仅作转发，供 C / FFI 使用。
// =============================================================

#include "PycpConfig.hpp"
#include "PycpObject.hpp"
#include "PycpGC.hpp"

namespace Pycp {

// 工厂函数：返回 Owned 引用（refcount = 1），调用方负责 Decref
PYCP_API Object* Integer_FromLong(long long value);
PYCP_API Object* String_FromString(const char* value);

// 运算：内部转调虚函数，返回 Owned 结果
PYCP_API Object* Add(Object* lhs, Object* rhs);
PYCP_API Object* Sub(Object* lhs, Object* rhs);
PYCP_API Object* Mul(Object* lhs, Object* rhs);
PYCP_API Object* Div(Object* lhs, Object* rhs);
PYCP_API Object* Pow(Object* lhs, Object* rhs);

// 统一调用入口（见路线图第 5/6 步）
//   callable : 可调用对象（Function 等）
//   argv     : 参数数组（Borrowed）
//   argc     : 参数数量
// 返回 Owned 结果；参数数量或类型不符时抛异常
PYCP_API Object* Call(Object* callable, Object** argv, std::size_t argc);

} // namespace Pycp

// =============================================================
// C 语言 ABI（extern "C"，保留 PYCP 前缀）
//
// 位置规则：声明在全局作用域并用 extern "C"，故符号保留 PYCP 前缀，
// 经 PYCP_C_API 导出，供 C / FFI 直接链接调用。
// 实现位于 PycpABI.cpp，仅转发至 namespace Pycp 内的同名无前缀版本。
// =============================================================

#ifdef __cplusplus
extern "C" {
#endif

// 工厂（C 端统一以 void* 句柄操作）
PYCP_C_API void* PYCP_Integer_FromLong_void(long long value);
PYCP_C_API void* PYCP_String_FromString_void(const char* value);

// 运算
PYCP_C_API void* PYCP_Add(void* lhs, void* rhs);
PYCP_C_API void* PYCP_Sub(void* lhs, void* rhs);
PYCP_C_API void* PYCP_Mul(void* lhs, void* rhs);
PYCP_C_API void* PYCP_Div(void* lhs, void* rhs);
PYCP_C_API void* PYCP_Pow(void* lhs, void* rhs);

// 统一调用
PYCP_C_API void* PYCP_Call(void* callable, void** argv, std::size_t argc);

// GC 控制
PYCP_C_API void PYCP_Incref(void* obj);
PYCP_C_API void PYCP_Decref(void* obj);
PYCP_C_API void PYCP_GC_Track(void* obj);
PYCP_C_API void PYCP_GC_Untrack(void* obj);
PYCP_C_API void PYCP_GC_AddRoot(void* obj);
PYCP_C_API void PYCP_GC_RemoveRoot(void* obj);
PYCP_C_API void PYCP_GC_Collect();
PYCP_C_API std::size_t PYCP_GC_LiveCount();

// 运行时初始化
PYCP_C_API void PYCP_Initialize();
PYCP_C_API void PYCP_Finalize();

#ifdef __cplusplus
}
#endif

#endif // PYCP_ABI_HPP
