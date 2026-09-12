#ifndef PYCP_NATIVE_EXT_HPP
#define PYCP_NATIVE_EXT_HPP

// =============================================================
// Pycp 原生扩展加载（类似 Python 的 pyd / C 扩展）
//
// import 语句可导入 C++ 编写的动态库（Linux .so / Windows .dll /
// macOS .dylib），扩展导出一个入口符号：
//
//   extern "C" Pycp::Module* PycpModule_<name>();
//
// 返回一个已构建好的 Module（Owned，refcount=1），其命名空间
// 内可放置 Function（复用 PycpCFunction 签名）等对象。
//
// 加载链：VM::load_module 经 Pycp::ImportModule 统一编排，在 registry_
// （.pycp 依赖）之前完成「进程内符号 / stdlib / cwd / 脚本目录」的查找。
// =============================================================

#include "PycpObject.hpp"
#include "PycpModule.hpp"

#include <cstdint>
#include <string>

namespace Pycp {

// 仅以指针形式使用的字节码模块，避免本头文件反向依赖 PycpBytecode.hpp
// （PycpBytecode.hpp 经 PycpABI.hpp 间接依赖本文件，形成环）。
namespace BC { struct Module; }

// 平台原生动态库后缀（纯原生，无特殊命名）：
//   Linux  ".so" / Windows ".dll" / macOS ".dylib"
const char* native_ext_suffix();

// 当前可执行文件所在目录，惰性计算并缓存（跨平台原语）。
// 如 pycp 位于 /usr/local/bin/pycp，则返回 /usr/local/bin；取不到时返回空串。
// 注：GetStdlibDir() 与本函数都基于它，避免各处重复实现平台分支。
PYCP_API const std::string& GetExeDir();

// 标准库动态库目录（可执行文件旁的 stdlib/），惰性计算并缓存。
// 如 pycp 可执行文件位于 /usr/local/bin/pycp，则返回 /usr/local/bin/stdlib。
PYCP_API const std::string& GetStdlibDir();

// =============================================================
// 模块查找原语
//
// ImportModule 按「进程内符号 -> exe 目录/stdlib -> cwd -> 脚本目录」
// 的顺序编排下列原语，每个原语只负责单一来源的探测。
// =============================================================

// .pycp 源码模块编译器钩子：源码路径 -> 字节码 Module。
// 返回堆分配对象，所有权移交运行时。编译失败时抛异常（由调用方转换为
// 带位置信息的 ImportError）。默认的空实现返回 nullptr，使 .pycp 源码
// 层自动禁用（AOT 生成的独立程序即为此情形，仅识别原生动态库）。
//   注：backend 无法解析 .pycp（parser / Codegen 位于 frontend），故由
//   宿主（PycpMain）注册，避免 libPycpRuntime 反向依赖 frontend。
using SourceModuleCompiler = BC::Module* (*)(const char* path);
void SetSourceModuleCompiler(SourceModuleCompiler fn);

// =============================================================
// 命令行参数（argv）注入
//
// 宿主（PycpMain 解释器入口 / AOT 生成的 main）在启动时一次性写入，
// 由 pycp 内置模块在构造时经 GetArgv() 读取并暴露为 pycp.argv
// （语义对齐 Python 的 sys.argv）。默认空 vector => pycp.argv == []。
//   - 解释运行：宿主传入 [脚本名, 脚本参数...]，不含 pycp 可执行文件本身。
//   - AOT 产物：宿主透传完整 argc/argv，argv[0] 为程序路径（对齐 sys.argv[0]）。
// 该状态进程级、写一次读多次，普通全局变量即可（无需原子/锁）。
// =============================================================
void SetArgv(const std::vector<std::string>& args);

// 返回注入的参数列表（只读引用；未注入时为空 vector）。
const std::vector<std::string>& GetArgv();

// 第 1 层（前）：在当前进程已加载的全局符号表中查找 PycpModule_<name>。
//   Linux/macOS : dlsym(RTLD_DEFAULT, ...)
//   Windows     : GetModuleHandle(NULL) + GetProcAddress（仅主程序模块）
// 未找到返回 nullptr（属正常，交由后续层级继续）。
// 找到但入口返回 nullptr 时抛 ImportError——符号存在即表明明确的链接
// 意图，静默下探会掩盖「静态库成员被链接器丢弃」这类问题。
Module* LoadLinkedModule(const std::string& name);

// 单目录探测原语：仅在 dir 指定的单一目录下查找模块，不跨目录回退。
//   dir 为空时表示当前工作目录。
// 先找 <dir>/<name><suffix>（原生动态库，入口符号 PycpModule_<name>），
// 未找到再找 <dir>/<name>.pycp（源码，经 SourceModuleCompiler 编译）。
//   返回非 nullptr          : 已初始化完成的模块对象（Owned）。
//   返回 nullptr 且 *out_source 非 nullptr : 命中 .pycp 源码，已编译为
//                            字节码但尚未执行顶层，交由 VM 完成执行。
//   二者皆 nullptr          : 本目录下不存在该模块的任何形式。
Module* LoadNativeModuleFrom(const std::string& dir, const std::string& name,
                             BC::Module** out_source = nullptr);

// 兼容旧入口：等价于按既有顺序（cwd -> search_dir -> stdlib）依次调用
// LoadNativeModuleFrom。新代码应直接使用 LoadNativeModuleFrom 自行编排，
// 保留本函数仅为不破坏既有外部调用方。
Module* LoadNativeModule(const std::string& name,
                         const std::string& search_dir,
                         BC::Module** out_source = nullptr);

// 统一关闭所有已加载的 dlopen 句柄并清空句柄缓存（进程退出前调用）。
void NativeExt_Finalize();

// =============================================================
// 拆箱辅助（供扩展作者使用，类型不符抛 TypeError，附函数名与参数位置）
// =============================================================

// 取第 i 个参数为 int64_t（须为 Integer）。
int64_t ArgInt(Object** argv, std::size_t i, const char* fn);

// 取第 i 个参数为字符串（须为 String），返回其值拷贝。
std::string ArgString(Object** argv, std::size_t i, const char* fn);

// 取第 i 个参数为 bool（Integer 非零为真）。
bool ArgBool(Object** argv, std::size_t i, const char* fn);

} // namespace Pycp

#endif // PYCP_NATIVE_EXT_HPP
