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
	std::vector<std::pair<std::string, std::string>> sources;

	// 运行时 SDK（dist）定位结果。
	SdkInfo sdk;

	// 生成时的 Pycp 版本，写入生成文件供追溯。
	std::string pycp_version;
};

// 校验项目描述是否完整。
//   err : 失败时输出可操作的原因（哪一项缺失、SDK 为何无效）
// 返回 true 表示可安全交由生成器与编排层处理。
bool Validate(const ProjectSpec& spec, std::string* err);

} // namespace Pycp::AOT

#endif // PYCP_AOT_PROJECT_SPEC_HPP
