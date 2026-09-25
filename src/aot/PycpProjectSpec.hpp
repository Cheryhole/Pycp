#ifndef PYCP_AOT_PROJECT_SPEC_HPP
#define PYCP_AOT_PROJECT_SPEC_HPP

// =============================================================
// AOT 生成项目的描述数据模型
//
// 职责：以纯数据形式完整描述「待生成的项目」，并做完整性校验。
//   - 不含任何 IO 逻辑（不建目录、不写文件），可独立测试
//   - 所有构建脚本生成器消费同一份 ProjectSpec，保证 CMake 与未来
//     其他构建系统（Makefile / Ninja）产出语义一致
//
// 数据流向：CLI 组装 -> Validate 校验 -> 生成器渲染 -> 编排层落盘
// =============================================================

#include "aot/PycpAotSdkLocator.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Pycp::AOT {

// AOT 产物的链接模式。
enum class LinkMode {
	kShared, // 动态：链接共享运行时，模块/扩展以动态库形态从运行时加载
	kStatic, // 静态：链接静态运行时，产物尽量自包含
};

// 模块编译形态（--compile-modules / --compile-module:<name> 决定）。
// 入口模块恒编进主程序（exe），不进入 modules 列表。
enum class ModuleKind {
	kStatic, // 编译为静态库（.a），链入引用它的链接目标
	kShared, // 编译为动态库（.so/.dll），运行期按模块名加载
};

// 单个依赖模块的构建语义（不含入口模块）。
struct ModuleTarget {
	std::string name;        // Pycp 模块名（import 用的名字）
	std::string source_file; // <name>.gen.cpp（与 spec.sources 的 key 对应）
	ModuleKind kind = ModuleKind::kShared; // 由 ModulePlan 决策
	std::vector<std::string> deps; // 同批转译模块内的直接依赖名
	// 形态决策原因（来自 ModulePlan::reasons）：用户覆盖 / 全局默认 /
	// 强制提升及其宿主明细。供生成器渲染逐模块注释，空串时渲染 <default>。
	std::string reason;
};

struct ProjectSpec {
	// 项目名：同时作为可执行文件名与 CMake project() 名称。
	// 应已是可安全用于文件名与 CMake 标识符的形式。
	std::string name;

	// 项目输出目录（绝对路径或相对 cwd 的路径）。
	std::string output_dir;

	// 原始入口 .pycp 路径，仅用于生成文件头注释，不参与构建。
	std::string source_pycp;

	// 待写入的源文件内容：(相对 output_dir 的文件名, 内容)。
	// 约定入口文件排在首位，使生成脚本中源文件顺序稳定可读。
	// modules / aux_sources 中的源文件也都在此集合内。
	std::vector<std::pair<std::string, std::string>> sources;

	// 运行时 SDK（dist）定位结果。
	SdkInfo sdk;

	// 运行时库（libPycpRuntime）的链接形态（--compile-runtime）。
	// kStatic 要求 sdk.has_static（静态运行时 + libPycpExt_*.a 齐全），
	// 且不允许同时存在任何 kShared 模块 / 运行期加载的内置扩展
	// （否则进程内出现两份运行时状态），由 Validate 拦下。
	LinkMode runtime_link = LinkMode::kShared;

	// 依赖模块（不含入口）的构建语义，由 ModulePlan 决策后填入。
	std::vector<ModuleTarget> modules;

	// 入口模块的直接依赖（仅同批转译模块，stdlib 扩展不在内）。
	// spec.modules 不含入口，生成器据此推导主程序的静态依赖闭包。
	std::vector<std::string> entry_deps;

	// 内置扩展（io / Pycp / classtools）按形态分组：
	//   builtin_static : 以 SDK 静态库（libPycpExt_*.a）链入主程序
	//   builtin_shared : 运行期从 exe 同级 stdlib/ 目录加载（默认）
	// 二者互斥（同一扩展不可同时出现在两组），由组装层保证。
	std::vector<std::string> builtin_static;
	std::vector<std::string> builtin_shared;
	// builtin_static 各扩展对应的 SDK 静态库绝对路径（按模块名匹配
	// sdk.stdlib_static_libs 得到），供 CMake 生成器直接写进链接命令。
	std::vector<std::string> builtin_static_lib_paths;

	// 辅助源文件名（注册/拉入桩等，编进主程序但非 Pycp 模块）。
	// 与 spec.sources 的 key 对应。
	std::vector<std::string> aux_sources;

	// 生成时的 Pycp 版本，写入生成文件供追溯。
	std::string pycp_version;
};

// 校验项目描述是否完整。
//   err : 失败时输出可操作的原因（哪一项缺失、SDK 为何无效）
// 返回 true 表示可安全交由生成器与编排层处理。
bool Validate(const ProjectSpec& spec, std::string* err);

} // namespace Pycp::AOT

#endif // PYCP_AOT_PROJECT_SPEC_HPP
