#ifndef PYCP_AOT_PROJECT_HPP
#define PYCP_AOT_PROJECT_HPP

// =============================================================
// AOT 项目编排模块（唯一感知「项目目录落盘」的模块）
//
// 职责：把「入口 .pycp + 全部 import 依赖」组装成可编译的 CMake 项目：
//   建目录 -> 写 .gen.cpp 源文件 -> 形态决策（ModulePlan）-> 组装
//   ProjectSpec -> 按 kinds 调生成器 -> 写构建脚本（CMakeLists.txt）。
//
// 不感知：CMake 具体语法（交给 PycpCMakeGenerator）、SDK 定位细节
//   （交给 PycpAotSdkLocator）、字节码翻译细节（交给 PycpAot）。
// 上层依赖下层、无反向依赖，模块间仅通过纯数据 ProjectSpec 通信。
// =============================================================

#include "PycpBytecode.hpp"
#include "aot/PycpProjectSpec.hpp" // LinkMode / ModuleKind

#include <map>
#include <string>
#include <vector>

namespace Pycp::AOT {

// --emit-cpp 的模块形态与运行时形态选项（由 CLI 组装）。
struct AotProjectOptions {
	// 依赖模块（.pycp 转译产物）的全局默认形态（--compile-modules=）。
	// 默认 kStatic：依赖模块编进主程序（与历史行为一致）。
	ModuleKind default_module_kind = ModuleKind::kStatic;
	// 运行时库（libPycpRuntime）的形态（--compile-runtime=；
	// 旧 --shared / --static 为兼容别名）。默认 kShared。
	LinkMode runtime_link = LinkMode::kShared;
	// 按模块覆盖（--compile-module:<name>=）：name 可为同批转译模块
	// 或内置扩展（io / Pycp / classtools）。内置扩展无覆盖时默认跟随
	// runtime_link（static -> SDK 静态库链入；shared -> stdlib/ 加载）。
	std::map<std::string, ModuleKind> overrides;
};

// 编排入口：从「已编译的模块集合 + 入口信息」生成完整项目文件夹。
//   modules      : 入口模块 + 全部 import 依赖（key=模块名, value=字节码 IR）
//   entry_name   : 入口模块名（对应 modules 中的 key），生成含 main 的源码
//   source_pycp  : 原始入口 .pycp 路径（仅用于文件头注释）
//   output_dir   : 项目输出目录（-o 指定，或默认 ./<entry_name>/）
//   options      : 模块/运行时形态与按模块覆盖（见 AotProjectOptions）
//   kinds        : 生成器标识列表，空表示全部已注册生成器（当前仅 "cmake"）
//   written      : 输出参数，返回所有已写入文件的绝对/相对路径
//   err          : 输出参数，失败原因（可操作）
// 返回 true 表示全部成功；任一环节失败即中止，不做部分产出的静默降级。
bool EmitProject(
	const std::map<std::string, Pycp::BC::Module>& modules,
	const std::string& entry_name,
	const std::string& source_pycp,
	const std::string& output_dir,
	const AotProjectOptions& options,
	const std::vector<std::string>& kinds,
	std::vector<std::string>* written,
	std::string* err);

} // namespace Pycp::AOT

#endif // PYCP_AOT_PROJECT_HPP
