#ifndef PYCP_AOT_MODULE_PLAN_HPP
#define PYCP_AOT_MODULE_PLAN_HPP

// =============================================================
// AOT 模块形态决策器（ModulePlan）
//
// 输入：同批转译的全部模块依赖图 + 用户覆盖 + 全局默认形态。
// 输出：每个依赖模块（不含入口）的最终形态（kStatic / kShared）与决策原因。
//
// 决策规则（用户拍板）：
//   1) 初始形态 = 用户覆盖（--compile-module:<name>=）> 全局默认
//      （--compile-modules=）。
//   2) 任何被 ≥2 个「链接目标」（主程序 / 多个动态库）直接或间接引用的
//      static 模块，强制提升为 kShared——否则同一模块会被复制进多个
//      链接目标，出现两份模块对象（两份 PycpModule_<name> 定义、
//      注册表同名覆盖、done 守卫各一份）。
//   3) 仅当全部模块静态打包进主程序（无任何 shared 模块）时，才不会出现
//      跨目标共享，此时无需任何提升。
//
// 计算用「不动点迭代」：某模块被提升为 shared 会引入新的「宿主」动态库
// token，可能使其下游 static 依赖的宿主数增加，需要重算直至收敛。
// 宿主（host）集合语义：模块最终会被哪些链接目标携带。
//   - 入口模块恒为主程序，token "@main"；
//   - kShared 模块 M 的宿主恒为自身 token "@shared:M"；
//   - kStatic 模块 M 的宿主 = ∪ 其直接 import 者 p 的宿主
//     （p 为入口给 "@main"，p 为 shared 给 "@shared:p"，p 为 static 时
//     传递 p 的宿主；import 关系按同批模块依赖图）。
//   static 模块宿主数 ≥ 2 即冲突，强制提升。
// =============================================================

#include "aot/PycpProjectSpec.hpp" // ModuleKind

#include <map>
#include <set>
#include <string>
#include <vector>

namespace Pycp::AOT {

struct ModulePlan {
	// 依赖模块名 -> 最终形态（不含入口；入口恒编进主程序）。
	std::map<std::string, ModuleKind> kinds;
	// 模块名 -> 决策原因（用户覆盖 / 全局默认 / 强制提升与宿主明细）。
	std::map<std::string, std::string> reasons;
};

// 计算每个依赖模块的最终形态。
//   modules      : 同批转译的全部模块名（含入口）
//   deps         : 模块名 -> 其直接依赖（仅同批转译模块；stdlib 扩展不在内）
//   entry        : 入口模块名（恒编进主程序，从不提升为 shared）
//   default_kind : 来自 --compile-modules 的全局默认
//   overrides    : 来自 --compile-module:<name> 的按模块覆盖
//                  （含 __codegen__ 指定的形态，命令行优先）
//   strict_names : 由包清单 __codegen__ 显式指定过形态的模块名。这些模块
//                  若因「被 ≥2 个链接目标引用」必须提升为 shared，则【报错
//                  中止】而非静默提升（用户约定：脚本的显式意图不得被
//                  静默改写；命令行指定的覆盖仍按既有行为提升并标注原因）。
//   err          : 输出参数（冲突中止时填可操作原因，plan 内容无效）
ModulePlan PlanModuleKinds(
	const std::vector<std::string>& modules,
	const std::map<std::string, std::vector<std::string>>& deps,
	const std::string& entry,
	ModuleKind default_kind,
	const std::map<std::string, ModuleKind>& overrides,
	const std::set<std::string>& strict_names,
	std::string* err);

} // namespace Pycp::AOT

#endif // PYCP_AOT_MODULE_PLAN_HPP
