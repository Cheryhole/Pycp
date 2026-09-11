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
//   - 对象构造经各类型的静态工厂方法（Integer::FromLong /
//     String::FromCString / List::New / Module::New / Class::New /
//     Instance::New / File::FromStream 等）
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
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpManager.hpp"
#include "PycpModule.hpp"
#include "PycpList.hpp"

#include <string>
#include <cstddef>
#include <istream>
#include <ostream>

namespace Pycp {

class Module;
class Class;
class Instance;
class File;

// 仅以指针形式使用的字节码模块，避免本头文件反向依赖 PycpBytecode.hpp
// （PycpBytecode.hpp 经 PycpABI.hpp 与本文件形成环）。
namespace BC { struct Module; }

// 通用属性访问/赋值：对任意对象（模块/类/实例/文件）执行。
//   GetAttr 返回 Borrowed 引用；未找到抛 AttributeError。
//   SetAttr 接管 value 所有权（内部按需 Incref）。
PYCP_API Object* GetAttr(Object* obj, const std::string& name);
PYCP_API void SetAttr(Object* obj, const std::string& name, Object* value);

// 下标运算：内部转调 __get_item__ / __set_item__。
//   GetItem 返回 Owned；SetItem 返回 Owned（通常为 None）。
PYCP_API Object* GetItem(Object* obj, Object* key);
PYCP_API Object* SetItem(Object* obj, Object* key, Object* value);

// 装饰器通用化辅助（VM 与 AOT 产物共用）：
//   ApplyDecorator 把被装饰对象 target 作为参数调用装饰器函数 deco，
//   用返回值替换 target（返回 Owned）。
//   ApplyDecoratorVisibility 用临时占位对象调用 deco 以确定可见性
//   （返回 target 是否私有）。deco 必须可调用，否则抛 TypeError。
PYCP_API Object* ApplyDecorator(Object* deco, Object* target,
                                const std::string& file, int line);
PYCP_API bool ApplyDecoratorVisibility(Object* deco,
                                       const std::string& file, int line);

// 类成员装饰：一次读取可见性 + 只读两种标志（VM 与 AOT 的 MAKE_CLASS 用）。
struct MemberFlags {
	bool priv = false;
	bool readonly = false;
};
PYCP_API MemberFlags ApplyDecoratorMemberFlags(Object* deco,
                                               const std::string& file, int line);

// 叠加装饰器（多个装饰器作用于同一目标）：
//   decos 按源码【自上而下】顺序给出，decos[count-1] 最靠近目标、最先应用。
//   ApplyDecoratorChain 由内向外逐层应用并返回最终对象（Owned）；
//   target 的所有权仍归调用方；任一步装饰器返回非 Function 抛 TypeError。
//   ApplyDecoratorMemberFlagsChain 用同一占位对象串联应用装饰器，读取最终
//   对象的可见性 + 只读标志（= ApplyDecoratorMemberFlags 的多装饰器版）。
//   count 必须 >= 1，调用方保证 decos 至少含 count 个元素。
PYCP_API Object* ApplyDecoratorChain(Object** decos, std::size_t count,
                                     Object* target,
                                     const std::string& file, int line);
PYCP_API MemberFlags ApplyDecoratorMemberFlagsChain(Object** decos, std::size_t count,
                                                    const std::string& file, int line);

// globals map 指针 -> 所属 Module 的进程级注册表：执行模块顶层前登记，
// 使 Environment_Store 覆盖全局名时能查询该模块的只读绑定（常量）。
//   globals 为模块命名空间 map 指针（void* 避免暴露具体类型）。
PYCP_API void BindGlobalsModule(void* globals, Module* mod);
PYCP_API Module* LookupGlobalsModule(void* globals);

// 登记模块绑定级属性：以 globals 中 name 当前值的 is_private()/is_readonly()
// 写入所属 Module 的 binding_attrs_（供 @private/@readonly 声明式访问控制）。
// VM 与 AOT 共用（对应 MARK_BINDING opcode）。
PYCP_API void MarkBinding(void* globals, const std::string& name);

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
// 模块导入（类似 CPython 的 PyImport_ImportModule）
// =============================================================
//
// 按模块名加载并返回 Pycp::Module*（Borrowed，不增引用；调用方负责
// 视需要 Incref）。查找顺序（命中即终止，不再下探）：
//
//   0) 进程级缓存（跨 VM 实例、跨 AOT 调用共享）
//   1) 进程内符号：dlsym(RTLD_DEFAULT, "PycpModule_<name>")；
//      未找到再查 AOT 静态注册表（RegisterAotModule 登记）
//   2) 当前工作目录（cwd）
//   3) 脚本所在目录（SetModuleSearchDir 设置；为 "." 时与 cwd 重合而跳过）
//   4) 可执行文件所在目录的 stdlib/
//
// 文件系统层（2/3/4）采用「本地优先」：离用户最近的候选先探测，故本地
// 同名模块（含源码）优先于 stdlib/ 下的内置扩展。每个目录内部均按
// 「.pycp 源码 → 同名动态库 <name>.so」探测：源码优先使"改源码即生效"，
// 也允许在 stdlib/ 下放置与内置扩展同名的 .pycp 来覆盖它（与 Python
// 的「扩展模块优先」相反，是 Pycp 的刻意选择）。
//
// .pycp 源码需宿主经 SetSourceModuleCompiler 注册编译器钩子，未注册时
// 该候选形式自动禁用（AOT 生成的独立程序仅识别动态库），这正是「转译
// 产物不能调用未转译源码」的实现基础。
//
// 第 1 层命中符号但初始化返回 nullptr 时抛 ImportError：符号存在即表明
// 明确的链接意图，静默下探会掩盖「静态库成员被链接器丢弃」这类问题。
//
// 已知限制：进程级缓存不按 mtime/内容哈希失效，故同一进程内修改 .pycp
// 源码不会重新加载（重启进程后生效）。
//
// 返回值三态：
//   非 nullptr                        : 已初始化完成的模块对象
//   nullptr 且 *out_source 非 nullptr : 命中 .pycp 源码，已编译为字节码
//                                       但尚未执行顶层，由 VM 执行并接管
//   二者皆 nullptr                    : 未命中，调用方应回退 registry_，
//                                       仍无则抛出 ImportError
//
// out_source 默认 nullptr，保证 AOT 已生成的 ImportModule(dep) 调用点
// 零改动（存量 .gen.cpp 无需重新生成即可编译）。
// diagnostics 非空时，逐层记录未命中的候选目录，供调用方拼进 ImportError。
//
// 模块对象经 GC_AddRoot + Incref 常驻进程，跨 VM 实例存活，由 Finalize
// 统一回收。from-import 的属性提取由调用方负责（本函数只管模块对象加载），
// 职责与 PyImport_ImportModule 对齐。
PYCP_API Module* ImportModule(const std::string& name,
                              BC::Module** out_source = nullptr,
                              std::string* diagnostics = nullptr);

// 设置脚本所在目录（第 3 层的第二个候选目录），进程级。
// 解释器在 VM 构造时按入口文件路径设置；AOT 生成的独立程序无需设置。
PYCP_API void SetModuleSearchDir(const std::string& dir);

// AOT 编译的 .pycp 子模块在生成的 .cpp 静态初始化阶段，把自己的初始化函数
// 指针登记进进程级注册表。ImportModule 据此「直接调用」对应 PycpModule_<name>
// （对标「直接调用 PycpModule_Initialize」），不依赖 dlsym/符号导出。
//   name : 模块名（与 ImportModule 的 name 一致，无哈希）
//   init : 形如 Pycp::Module* (*)() 的初始化函数（返回模块对象，懒执行顶层）
using AotModuleInitFn = Module* (*)();
PYCP_API void RegisterAotModule(const std::string& name, AotModuleInitFn init);

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

// 闭包捕获登记（VM 路径）：把代码对象 co_idx 及其创建的嵌套函数所引用的
// 自由变量名加入 env 的保留集合。MAKE_FUNCTION 创建闭包时调用。
PYCP_API void Environment_KeepNamesOfCodeObject(BC::Environment* env,
                                                const BC::Module* module,
                                                std::size_t co_idx);

// 闭包捕获登记（AOT 路径）：按名字列表登记（生成代码内联字符串数组常量）。
PYCP_API void Environment_KeepNames(BC::Environment* env,
                                    const char* const* names, std::size_t count);

// 帧退出（RETURN/HALT）：释放局部变量中未被闭包捕获的槽位；被捕获的槽位保留
// （由 Environment 析构统一释放），避免闭包引用已释放对象 / 越界读取。
PYCP_API void Environment_ReleaseFrame(BC::Environment* env);

} // namespace Pycp

// =============================================================
// C 语言 ABI（extern "C"，保留 PYCP 前缀）
//
// 位置规则：声明在全局作用域并用 extern "C"，故符号保留 PYCP 前缀，
// 经 PYCP_C_API 导出，供 C / FFI 直接链接调用。
// 实现位于 PycpABI.cpp，仅转发至各类型的静态方法 / namespace Pycp 内的
// 同名无前缀版本。
// =============================================================

#ifdef __cplusplus
extern "C" {
#endif

// 工厂（C 端统一以 void* 句柄操作）
PYCP_C_API void* PYCP_Integer_FromLong_void(long long value);
PYCP_C_API void* PYCP_String_FromCString_void(const char* value);

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

// 类 / 实例工厂与操作
PYCP_C_API void* PYCP_Class_New_void(const char* name);
PYCP_C_API void* PYCP_Instance_New_void(void* cls);
PYCP_C_API void PYCP_Class_AddMethod(void* cls, const char* name, void* fn);

// 属性访问
PYCP_C_API void* PYCP_GetAttr(void* obj, const char* name);
PYCP_C_API void PYCP_SetAttr(void* obj, const char* name, void* value);

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