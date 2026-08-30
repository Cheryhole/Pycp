#include "aot/PycpProjectSpec.hpp"

namespace Pycp::AOT {

bool Validate(const ProjectSpec& spec, std::string* err) {
	auto fail = [err](const std::string& msg) -> bool {
		if (err != nullptr) *err = msg;
		return false;
	};

	if (spec.name.empty()) {
		return fail("项目名为空（由入口 .pycp 的 basename 推导，请检查输入文件）。");
	}
	if (spec.output_dir.empty()) {
		return fail("输出目录为空。");
	}
	if (spec.sources.empty()) {
		return fail("源文件列表为空（至少需要入口模块的 .gen.cpp）。");
	}
	for (const auto& kv : spec.sources) {
		if (kv.first.empty()) {
			return fail("存在空文件名的源文件项。");
		}
	}

	if (!spec.sdk.valid) {
		std::string msg = "未找到有效的 Pycp 运行时 SDK（需含 include/、lib/、"
		                  "stdlib/ 三个子目录）。已尝试：";
		if (spec.sdk.tried.empty()) {
			msg += " (无候选路径)";
		} else {
			for (const std::string& t : spec.sdk.tried) {
				msg += "\n  - " + t;
			}
		}
		msg += "\n请检查 pycp 安装是否完整，或设置 PYCP_DIST 环境变量指向 dist 目录。";
		return fail(msg);
	}

	// 静态链接的前置条件：SDK 必须同时提供静态运行时与内置扩展的静态库。
	// 缺少任一项都会在链接期或运行期失败（前者 undefined reference，
	// 后者 import 报 ImportError），故在此提前拦下并给出可操作提示。
	if (spec.link_mode == LinkMode::kStatic && !spec.sdk.has_static) {
		std::string msg = "--static 需要 SDK 提供静态链接产物，但当前 SDK（" +
		                  spec.sdk.root + "）不满足：";
		if (spec.sdk.static_runtime.empty()) {
			msg += "\n  - 缺少静态运行时库：<lib>/libPycpRuntime.a"
			       "（该 SDK 可能以 -DBUILD_RUNTIME_STATIC=OFF 构建）";
		}
		if (spec.sdk.stdlib_static_libs.empty()) {
			msg += "\n  - 缺少内置扩展的静态库：<lib>/libPycpExt_<name>.a"
			       "（io / Pycp / classtools）";
		}
		msg += "\n请重新构建 pycp 以生成静态产物，或改用 --shared（默认）。";
		return fail(msg);
	}

	return true;
}

} // namespace Pycp::AOT
