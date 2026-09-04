#ifndef PYCP_AOT_HPP
#define PYCP_AOT_HPP

// =============================================================
// Pycp 原生编译（AOT）扩展接口 —— 预留骨架
//
// 目标：将 .pycp（或已编译的 .cpycp）翻译为独立的 C++ 源文件 (.cpp)，
//   使程序可脱离 Pycp 运行时解释器独立编译、链接、执行。
//
// 设计要点：
//   1. 接口与 Codegen/VM 解耦：EmitCpp 仅依赖 BC::Module（字节码 IR），
//      与 VM 执行共享同一份 IR，保证行为一致。
//   2. 生成的 .cpp 仅依赖稳定的 PycpABI.hpp（Integer::FromLong / Add / Call 等），
//      通过 `extern "C"` 的 PYCP_* 符号链接到 PycpRuntime 库。
//   3. 常量池在生成代码中以静态数组 + 初始化函数一次性构造，
//      并 GC_AddRoot 保护，与 VM 反序列化路径等价。
//
// 当前为骨架：仅定义接口签名与代码结构约定，供后续实现填充。
// =============================================================

#include "PycpBytecode.hpp"
#include "aot/PycpProjectSpec.hpp" // ModuleKind（桩生成按形态过滤）

#include <map>
#include <string>

namespace Pycp::AOT {

// 将字节码 Module 翻译为独立 C++ 源文件内容（含 main，单模块模式）。
//   返回生成的 C++ 源码字符串。
// 参数：
//   module     : 已编译的字节码 IR（常量池 + 符号表 + 代码对象）
//   entry_name : 生成文件中入口函数的符号名（默认 "pycp_main"）
std::string EmitCpp(const Pycp::BC::Module& module,
                    const std::string& entry_name = "pycp_main");

// 多文件输出：为「模块名 -> Module」集合生成各自独立的 C++ 源码。
//   entry_name : 入口模块名（对应 modules 中的 key），其生成的源码含 main()，
//                负责按需调用被导入模块。
//   kinds      : 模块形态表（可选）。kShared 的依赖为运行期加载的动态模块，
//                对其不生成「链接拉入桩」（符号在独立 DLL 中，extern 引用会
//                制造无法解析的外部符号）；nullptr 表示全部按静态处理。
//   被导入模块生成的源码仅含初始化函数（无 main），供入口链接调用。
//   返回 map<模块名, 源码>；每个值的文件名由调用方决定（如 <name>.gen.cpp）。
std::map<std::string, std::string> EmitCppAll(
    const std::map<std::string, Pycp::BC::Module>& modules,
    const std::string& entry_name,
    const std::map<std::string, ModuleKind>* kinds = nullptr);

// 将生成结果写入磁盘文件（path 为 .cpp 输出路径）。
//   返回 true 表示写入成功。
bool EmitCppToFile(const Pycp::BC::Module& module, const std::string& path);

} // namespace Pycp::AOT

#endif // PYCP_AOT_HPP
