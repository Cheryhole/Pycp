#include "aot/PycpModulePlan.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace Pycp::AOT {

namespace {

const char* kind_name(ModuleKind k) {
	return (k == ModuleKind::kStatic) ? "static" : "shared";
}

} // anonymous namespace

ModulePlan PlanModuleKinds(
	const std::vector<std::string>& modules,
	const std::map<std::string, std::vector<std::string>>& deps,
	const std::string& entry,
	ModuleKind default_kind,
	const std::map<std::string, ModuleKind>& overrides,
	std::string* err) {
	ModulePlan plan;
	if (err != nullptr) err->clear();

	// 待决策的依赖模块（不含入口；入口恒编进主程序）。
	std::vector<std::string> dep_names;
	for (const std::string& n : modules) {
		if (n != entry) dep_names.push_back(n);
	}
	if (dep_names.empty()) return plan;

	// ---- 1) 初始形态：用户覆盖 > 全局默认 ----
	for (const std::string& n : dep_names) {
		auto it = overrides.find(n);
		const ModuleKind k =
			(it != overrides.end()) ? it->second : default_kind;
		plan.kinds[n] = k;
		plan.reasons[n] = (it != overrides.end())
			? ("--compile-module:" + n + "=" + kind_name(k))
			: ("--compile-modules=" + std::string(kind_name(default_kind)));
	}

	// ---- 2) 不动点迭代：把「被 ≥2 个链接目标携带」的 static 模块提升 ----
	// 宿主 token：主程序为 "@main"，动态库 M 为 "@shared:M"（M 为模块名，
	// 模块名不含 '@'，不会与主程序 token 冲突）。
	const std::string kMainToken = "@main";

	for (int round = 0; round < 64; ++round) {
		bool promoted_any = false;

		// 构建反向依赖（importers）用于宿主传播。
		std::map<std::string, std::vector<std::string>> importers;
		auto all_names = modules;
		for (const std::string& m : all_names) {
			auto dit = deps.find(m);
			if (dit == deps.end()) continue;
			for (const std::string& d : dit->second) {
				if (d == entry) continue; // 入口不需要宿主计算
				if (plan.kinds.find(d) == plan.kinds.end()) continue; // 非本批模块
				importers[d].push_back(m);
			}
		}

		// host[n] = 会携带 n 的链接目标 token 集合。shared 模块恒为自身；
		// static 模块为「直接 import 它的模块的宿主」的并集，迭代至固定点。
		std::map<std::string, std::set<std::string>> host;
		for (const std::string& n : dep_names) {
			if (plan.kinds.at(n) == ModuleKind::kShared) {
				host[n].insert("@shared:" + n);
			}
		}

		// 传播固定点（static 宿主沿「static import static」链传递）。
		bool changed = true;
		int guard = 0;
		while (changed && guard++ < (int)dep_names.size() * 2 + 8) {
			changed = false;
			for (const std::string& n : dep_names) {
				if (plan.kinds.at(n) != ModuleKind::kStatic) continue;
				std::set<std::string> acc;
				auto iit = importers.find(n);
				if (iit != importers.end()) {
					for (const std::string& p : iit->second) {
						if (p == entry) {
							acc.insert(kMainToken);
						} else if (plan.kinds.at(p) == ModuleKind::kShared) {
							acc.insert("@shared:" + p);
						} else {
							// p 为 static：其宿主全部传给 n（迭代收敛）。
							auto hit = host.find(p);
							if (hit != host.end()) {
								for (const std::string& t : hit->second) {
									acc.insert(t);
								}
							}
						}
					}
				}
				if (acc != host[n]) {
					host[n] = acc;
					changed = true;
				}
			}
		}

		// 提升：static 模块宿主数 ≥ 2 说明会被复制进多个链接目标。
		for (const std::string& n : dep_names) {
			if (plan.kinds.at(n) != ModuleKind::kStatic) continue;
			if (host[n].size() < 2) continue;
			plan.kinds[n] = ModuleKind::kShared;

			std::ostringstream oss;
			oss << "forced shared: referenced by " << host[n].size()
			    << " link targets (";
			bool first = true;
			for (const std::string& t : host[n]) {
				if (!first) oss << ", ";
				oss << t;
				first = false;
			}
			oss << ")";
			plan.reasons[n] = oss.str();
			promoted_any = true;
		}

		if (!promoted_any) break; // 收敛
	}

	return plan;
}

} // namespace Pycp::AOT
