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
//   <root>/stdlib/*.so        标准库原生扩展（io / Pycp / classtools）
//   <root>/PycpRuntime.dll    Windows 额外一份（加载 DLL 不搜 lib/）
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
	std::string root_dll;    // Windows：<root>/PycpRuntime.dll，其他平台为空

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
