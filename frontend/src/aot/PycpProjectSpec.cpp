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

	return true;
}

} // namespace Pycp::AOT
