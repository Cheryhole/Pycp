#ifndef PYCP_AOT_SDK_LOCATOR_HPP
#define PYCP_AOT_SDK_LOCATOR_HPP

// =============================================================
// Pycp SDK（运行时分发目录）定位模块
//
// 职责：找到 pycp 的运行时 SDK 根目录，并校验其布局完整性。
//   - 只做「定位 + 校验」，不感知「项目」概念，不做任何 IO 写入
//   - 供 PycpProjectSpec 组装项目描述时使用
//
// SDK 即 pycp 自身的 dist 目录（默认 build/dist），布局为：
//   <root>/pycp               主程序（本模块不使用）
//   <root>/include/*.h*       AOT / 原生扩展所需的后端头文件
//   <root>/lib/libPycpRuntime.{a,so}  运行时库
//   <root>/lib/libPycpExt_<name>.a    标准库扩展的静态库（--static 用）
//   <root>/stdlib/*.so        标准库原生扩展（io / pycp / classtools）
//   <root>/{lib,}PycpRuntime.dll  Windows 额外一份（加载 DLL 不搜 lib/）；
//                             文件名随编译器而异：MinGW 带 lib 前缀，MSVC 不带
//
// 定位策略（按序，取第一个通过校验者）：
//   1) 可执行文件所在目录（build/dist/pycp -> build/dist）—— 常规安装
//   2) 可执行文件目录的父目录（build/pycp -> build/dist）—— 构建树
//   3) 环境变量 PYCP_DIST
// =============================================================

#include <string>
#include <vector>

namespace Pycp::AOT {

// SDK 布局信息。
struct SdkInfo {
	std::string root;        // SDK 根目录（绝对路径）；未找到时为空
	bool        valid = false; // include/ + lib/ + stdlib/ 三者齐全才为 true

	std::string include_dir; // <root>/include
	std::string lib_dir;     // <root>/lib
	std::string stdlib_dir;  // <root>/stdlib
	// Windows：SDK 根目录下额外一份运行时 DLL 的绝对路径，其他平台为空。
	// 文件名随构建 SDK 的编译器而异：MinGW -> <root>/libPycpRuntime.dll，
	// MSVC -> <root>/PycpRuntime.dll。非必需（纯静态 SDK 无此文件），
	// 故缺失时为空但不影响 valid。
	std::string root_dll;

	// ---- 静态链接（AOT --static）所需产物 ----
	// 全部由 <root>/lib/ 扫描得到，无需硬编码模块名：新增 stdlib 子库时
	// 只要其静态库按 libPycpExt_<name>.a 命名即自动纳入。
	//
	// 这些产物非必需（SDK 可能只构建了 shared 运行时），缺失时仅置空、
	// 不影响 valid —— 与 root_dll 一致的处理：只有当用户实际使用 --static
	// 时才由 ProjectSpec::Validate 报错，避免给 shared 用户制造噪音。
	std::vector<std::string> builtin_modules;    // {"io", "pycp", "classtools"}
	std::vector<std::string> stdlib_static_libs; // libPycpExt_*.a 绝对路径
	std::string              static_runtime;     // libPycpRuntime.a / PycpRuntime.lib 绝对路径

	// 三者齐全为 true，表示本 SDK 支持 --static。
	bool has_static = false;

	// 尝试过的候选路径（含落选原因），供错误信息展示，避免用户盲猜
	std::vector<std::string> tried;
};

// 定位并校验 SDK。
// 返回 valid=false 时，tried 记录全部候选与原因，调用方可据此给出可操作提示。
SdkInfo LocateSdk();

// 校验指定目录是否为合法 SDK（供 LocateSdk 与测试复用）。
bool ValidateSdkRoot(const std::string& root, SdkInfo* out);

} // namespace Pycp::AOT

#endif // PYCP_AOT_SDK_LOCATOR_HPP
