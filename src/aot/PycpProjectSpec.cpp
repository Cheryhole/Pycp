#include "aot/PycpProjectSpec.hpp"

namespace Pycp::AOT {

namespace {

// 是否存在任一动态形态的模块或运行期加载的内置扩展。
bool has_any_dynamic(const ProjectSpec& spec) {
	if (!spec.builtin_shared.empty()) return true;
	for (const ModuleTarget& mt : spec.modules) {
		if (mt.kind == ModuleKind::kShared) return true;
	}
	return false;
}

} // anonymous namespace

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

	// 内置扩展分组一致性：同一扩展不可同时被声明为静态与动态。
	for (const std::string& s : spec.builtin_static) {
		for (const std::string& d : spec.builtin_shared) {
			if (s == d) {
				return fail("内置扩展 '" + s +
				            "' 同时出现在 builtin_static 与 builtin_shared 中，"
				            "请检查模块形态参数。");
			}
		}
	}
	// 模块名与内置扩展名不可冲突（用户 .pycp 模块不应叫 io / Pycp / classtools）。
	for (const ModuleTarget& mt : spec.modules) {
		for (const std::string& b : spec.sdk.builtin_modules) {
			if (mt.name == b) {
				return fail("模块名 '" + mt.name +
				            "' 与内置扩展同名，将被 stdlib/ 下的同名内置库遮蔽，"
				            "请改名。");
			}
		}
	}

	// 运行时组合校验：动态形态（shared 模块 / 运行期加载的内置扩展）要求
	// runtime 也是 shared，否则进程内出现两份运行时（GC 池 / 小整数池 /
	// 句柄缓存），跨模块对象操作直接崩溃。这里给出明确报错而非静默降级。
	if (spec.runtime_link == LinkMode::kStatic && has_any_dynamic(spec)) {
		return fail("--compile-runtime=static 与动态形态冲突：存在 kShared 模块"
		            "或运行期加载的内置扩展，若把运行时静态链接会生成两份"
		            "运行时状态（GC 池 / 小整数池 / 句柄缓存）。\n请改用 "
		            "--compile-runtime=shared，或把这些模块置为静态"
		            "（--compile-module:<name>=static）。");
	}

	// 静态链接的前置条件：SDK 必须同时提供静态运行时与内置扩展的静态库。
	// 缺少任一项都会在链接期或运行期失败（前者 undefined reference，
	// 后者 import 报 ImportError），故在此提前拦下并给出可操作提示。
	if (spec.runtime_link == LinkMode::kStatic && !spec.sdk.has_static) {
		std::string msg = "--compile-runtime=static 需要 SDK 提供静态链接产物，"
		                  "但当前 SDK（" +
		                  spec.sdk.root + "）不满足：";
		if (spec.sdk.static_runtime.empty()) {
			msg += "\n  - 缺少静态运行时库：<lib>/libPycpRuntime.a"
			       "（该 SDK 可能以 -DBUILD_RUNTIME_STATIC=OFF 构建）";
		}
		if (spec.sdk.stdlib_static_libs.empty()) {
			msg += "\n  - 缺少内置扩展的静态库：<lib>/libPycpExt_<name>.a"
			       "（io / Pycp / classtools）";
		}
		msg += "\n请重新构建 pycp 以生成静态产物，或改用 --compile-runtime=shared"
		       "（默认）。";
		return fail(msg);
	}

	// 静态模块需要链入 SDK 静态库的情况：仅当 builtin_static 非空或
	// runtime_link 为 static 时要求 has_static（此时 libPycpExt_*.a 与
	// 静态运行时都要存在，上述分支已拦 runtime；这里补 builtin_static）。
	if (!spec.builtin_static.empty() && !spec.sdk.has_static) {
		return fail("指定了以静态库链入的内置扩展（builtin_static），但当前 SDK"
		            "缺少静态链接产物（libPycpExt_*.a / libPycpRuntime.a）。"
		            "\n请改用 --compile-module:io=shared（默认运行期加载）或重新"
		            "构建 SDK。");
	}

	return true;
}

} // namespace Pycp::AOT
