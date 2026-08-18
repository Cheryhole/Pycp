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
#include "PycpEnvironment.hpp"

#include <string>

namespace Pycp {

class ModuleObject;
class ClassObject;
class InstanceObject;
class FileObject;

// 工厂函数：返回 Owned 引用（refcount = 1），调用方负责 Decref
PYCP_API Object* Integer_FromLong(long long value);
PYCP_API Object* String_FromString(const char* value);

// 模块对象工厂：创建指定名字的模块对象（返回 Owned，refcount=1）。
PYCP_API ModuleObject* Module_New(const std::string& name);

// 类 / 实例 / 文件对象工厂（返回 Owned，refcount=1）。
PYCP_API ClassObject* Class_New(const std::string& name);
PYCP_API InstanceObject* Instance_New(ClassObject* cls);
// 文件对象工厂：由已有流构造（不拥有流）。
PYCP_API FileObject* File_FromStream(const std::string& name,
                                     void* in, void* out);

// 类操作方法：添加成员名 / 方法。
PYCP_API void Class_AddMemberName(ClassObject* cls, const std::string& name);
PYCP_API void Class_AddMethod(ClassObject* cls, const std::string& name, Object* fn);

// 属性访问：从模块对象取属性，返回 Borrowed 引用；未找到抛 AttributeError。
PYCP_API Object* Module_GetAttr(ModuleObject* mod, const std::string& name);

// 通用属性访问/赋值：对任意对象（模块/类/实例/文件）执行。
//   GetAttr 返回 Borrowed 引用；未找到抛 AttributeError。
//   SetAttr 接管 value 所有权（内部按需 Incref）。
PYCP_API Object* GetAttr(Object* obj, const std::string& name);
PYCP_API void SetAttr(Object* obj, const std::string& name, Object* value);

// 运算：内部转调虚函数，返回 Owned 结果
PYCP_API Object* Add(Object* lhs, Object* rhs);
PYCP_API Object* Sub(Object* lhs, Object* rhs);
PYCP_API Object* Mul(Object* lhs, Object* rhs);
PYCP_API Object* Div(Object* lhs, Object* rhs);
PYCP_API Object* Pow(Object* lhs, Object* rhs);

// 比较：op 取值 0~5（LT/LE/EQ/NE/GT/GE），内部按 op 分发到对应比较虚函数。
// 返回小整数池对象 Integer 0/1（PERMANENT，无需 Decref）。
// 异类型时 EQ→0、NE→1、其余抛 TypeError（与 VM COMPARE_OP 语义一致）。
PYCP_API Object* Compare(Object* lhs, Object* rhs, int op);

// 真值判定：nullptr / None / Integer 0 视为假，其余为真。
PYCP_API bool IsFalse(Object* v);

// 统一调用入口（见路线图第 5/6 步）
//   callable : 可调用对象（Function 等）
//   argv     : 参数数组（Borrowed）
//   argc     : 参数数量
// 返回 Owned 结果；参数数量或类型不符时抛异常
PYCP_API Object* Call(Object* callable, Object** argv, std::size_t argc);

// =============================================================
// 执行环境（Environment）接口 —— VM 与 AOT 统一使用
// =============================================================

// 新建一个空环境（globals 为 nullptr，需调用方另行设置）。
// 返回的 Environment 由调用方负责 Environment_Free 释放。
PYCP_API BC::Environment* Environment_New();

// 释放由 Environment_New 创建的环境。
PYCP_API void Environment_Free(BC::Environment* env);

// 按「局部 -> captured 链 -> 全局」查找变量，返回 Borrowed 引用。
// 未找到时返回 nullptr（调用方负责抛出 NameError 并附带位置信息）。
PYCP_API Object* Environment_Lookup(BC::Environment* env, const std::string& name);

// 按「局部 -> captured 链 -> 全局」存储变量（接管 value 所有权，写前
// Decref 旧值）。若局部与捕获链均无此名，则写入 globals（新建或覆盖）。
// globals 为 nullptr 时直接 Decref value（防御，避免泄漏）。
PYCP_API void Environment_Store(BC::Environment* env, const std::string& name,
                                 Object* value);

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

// 比较 / 真值
PYCP_C_API void* PYCP_Compare(void* lhs, void* rhs, int op);
PYCP_C_API int PYCP_IsFalse(void* v);

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
