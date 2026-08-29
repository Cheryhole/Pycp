#ifndef PYCP_AOT_CMAKE_GENERATOR_HPP
#define PYCP_AOT_CMAKE_GENERATOR_HPP

// =============================================================
// CMakeLists.txt 渲染器（唯一感知 CMake 语法的模块）
//
// 职责：把 ProjectSpec 渲染成一份可直接 cmake 构建的 CMakeLists.txt。
//   - 只做字符串渲染，不碰文件系统、不感知 CLI。
//   - 满足全部运行时硬约束：动态链接运行时、Linux -rdynamic、
//     rpath 双保险、POST_BUILD 复制 stdlib/ 与 lib/、Windows 复制根 DLL。
//
// 可扩展：新增 Makefile 生成器只需实现 IBuildScriptGenerator 并注册，
//         本模块与编排层均无需改动。
// =============================================================

#include "aot/PycpBuildScriptGenerator.hpp"

namespace Pycp::AOT {

// CMake 生成器（静态实例，静态初始化阶段自注册）。
class CMakeGenerator : public IBuildScriptGenerator {
public:
	const char* kind() const override { return "cmake"; }
	const char* file_name() const override { return "CMakeLists.txt"; }
	std::string Generate(const ProjectSpec& spec) const override;
};

// 全局实例（供静态初始化注册）。
extern const CMakeGenerator kCMakeGenerator;

} // namespace Pycp::AOT

#endif // PYCP_AOT_CMAKE_GENERATOR_HPP
