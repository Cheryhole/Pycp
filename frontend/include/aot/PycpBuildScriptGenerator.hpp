#ifndef PYCP_AOT_BUILD_SCRIPT_GENERATOR_HPP
#define PYCP_AOT_BUILD_SCRIPT_GENERATOR_HPP

// =============================================================
// 构建脚本生成器抽象接口 + 进程级注册表
//
// 职责：为「可扩展的构建脚本生成」提供统一契约。
//   - 具体构建系统（CMake / Makefile / Ninja）只需实现本接口并在
//     静态初始化阶段注册，编排层（PycpAotProject）便只依赖接口，
//     无需 switch(kind) 或 #ifdef，实现零改造成扩展。
//   - 本模块不感知任何具体构建系统语法，也不做任何 IO。
//
// 数据流：编排层 -> FindGenerator(kind) -> Generate(spec) -> 字符串
// =============================================================

#include "aot/PycpProjectSpec.hpp"

#include <string>
#include <vector>

namespace Pycp::AOT {

// 构建脚本生成器抽象（可扩展点）。
class IBuildScriptGenerator {
public:
	virtual ~IBuildScriptGenerator() = default;

	// 唯一标识，如 "cmake"。编排层据此选取。
	virtual const char* kind() const = 0;

	// 输出文件名，如 "CMakeLists.txt"。
	virtual const char* file_name() const = 0;

	// 由 spec 渲染出完整的构建脚本文本。
	//   spec 已经过 Validate，所有必要字段非空且 SDK 有效。
	virtual std::string Generate(const ProjectSpec& spec) const = 0;
};

// 进程级注册表：各生成器模块在静态初始化阶段调用本函数自注册。
//   gen 须为静态生命周期对象（如命名空间内的全局实例），
//   注册表仅保存裸指针，不持有所有权。
void RegisterGenerator(const IBuildScriptGenerator* gen);

// 按 kind 查找生成器；未找到返回 nullptr。
const IBuildScriptGenerator* FindGenerator(const std::string& kind);

// 返回所有已注册生成器的 kind 列表（按注册顺序）。
std::vector<std::string> AvailableKinds();

} // namespace Pycp::AOT

#endif // PYCP_AOT_BUILD_SCRIPT_GENERATOR_HPP
