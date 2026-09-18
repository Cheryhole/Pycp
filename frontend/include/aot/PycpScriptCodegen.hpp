#ifndef PYCP_AOT_SCRIPT_CODEGEN_HPP
#define PYCP_AOT_SCRIPT_CODEGEN_HPP

// =============================================================
// 包清单脚本求值（AOT 转译期）
//
// 职责：在 --emit-cpp 期间，用一个【独立 VM】执行模块文件夹的清单
//   （pycp.mpycp）顶层一趟，读出两类信息：
//     1) 角色：是否显式声明（as_program / as_library）与最终角色；
//     2) __codegen__() 返回的项目描述（子模块链接形态 + 可执行名）。
//
// 设计要点：
//   * 只在入口是模块文件夹时调用；普通单文件入口零开销、零副作用。
//   * 执行前用 BC::Module::symtab 粗筛：清单既未定义 __codegen__ 也未
//     调用角色 API 时，完全不执行顶层（避免为取配置而空跑一次脚本）。
//   * 依赖模块由调用方以注册表形式注入（已解析好的同批模块），故清单
//     里的兄弟 import 不会退化到文件系统查找（cwd 相关、易失败）。
//   * 失败一律填 err 并返回 false，由调用方中止 --emit-cpp，不做静默降级。
// =============================================================

#include "PycpBytecode.hpp"
#include "PycpModule.hpp"   // Pycp::PackageRole
#include "aot/PycpProjectSpec.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Pycp::AOT {

// 清单求值结果（纯数据，供 CLI 合并进 AotProjectOptions 与打印诊断）。
struct ScriptCodegenPlan {
	// 角色是否由清单显式声明（moduletools.as_program / as_library）。
	bool role_declared = false;
	// 最终角色（未声明时为宿主按用途推断的默认值；AOT 默认为库）。
	Pycp::PackageRole role = Pycp::PackageRole::kLibrary;
	// 清单是否定义了 __codegen__。
	bool has_codegen = false;
	// proj.set_executable_name(...) 指定的可执行名（空 = 未指定）。
	std::string executable_name;
	// proj["<模块名>"].static()/shared() 指定的形态。
	std::map<std::string, ModuleKind> kinds;
};

// 求值包清单。
//   modules            : load_all 的产出（入口 + 全部依赖，键为模块名）
//   entry_name         : 入口模块名（包名）
//   package_names      : 包对象名集合（供 VM 标记包模块）
//   translated_modules : 参与转译的模块名（用于校验 proj 里未知名的拼写）
//   out                : 求值结果
//   err                : 失败原因（可操作）
// 返回 false 表示失败（调用方应中止 --emit-cpp）。
bool EvalPackageManifest(
	std::map<std::string, Pycp::BC::Module>& modules,
	const std::string& entry_name,
	const std::set<std::string>& package_names,
	const std::vector<std::string>& translated_modules,
	ScriptCodegenPlan* out, std::string* err);

} // namespace Pycp::AOT

#endif // PYCP_AOT_SCRIPT_CODEGEN_HPP
