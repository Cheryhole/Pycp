#ifndef PYCP_NATIVE_EXT_HPP
#define PYCP_NATIVE_EXT_HPP

// =============================================================
// Pycp 原生扩展加载（类似 Python 的 pyd / C 扩展）
//
// import 语句可导入 C++ 编写的动态库（Linux .so / Windows .dll /
// macOS .dylib），扩展导出一个入口符号：
//
//   extern "C" Pycp::Module* PycpModuleInit_<name>();
//
// 返回一个已构建好的 Module（Owned，refcount=1），其命名空间
// 内可放置 Function（复用 PycpNativeFunction 签名）等对象。
//
// 加载链：VM::load_module 在 FindBuiltinModule（静态内建）之后、
// registry_（.pycp 依赖）之前调用 LoadNativeModule。
// =============================================================

#include "PycpObject.hpp"
#include "PycpModule.hpp"

#include <cstdint>
#include <string>

namespace Pycp {

// 平台原生动态库后缀（纯原生，无特殊命名）：
//   Linux  ".so" / Windows ".dll" / macOS ".dylib"
const char* native_ext_suffix();

// 标准库动态库目录（可执行文件旁的 stdlib/），惰性计算并缓存。
// 如 pycp 可执行文件位于 /usr/local/bin/pycp，则返回 /usr/local/bin/stdlib。
const std::string& GetStdlibDir();

// 尝试加载原生扩展 <name><suffix>，搜索顺序：
//   1) <可执行文件目录>/stdlib/（标准库，优先）
//   2) search_dir（脚本所在目录）
//   3) 当前工作目录
//   - 文件不存在：返回 nullptr（由调用方回退 .pycp 加载）。
//   - 文件存在但 dlopen / dlsym / 入口调用失败：抛 ImportError。
// 命中返回 Module*（Owned，refcount=1，由调用方 GC_AddRoot 并缓存）。
Module* LoadNativeModule(const std::string& name,
                         const std::string& search_dir);

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
