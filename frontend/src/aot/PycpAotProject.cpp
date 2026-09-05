#include "aot/PycpAotProject.hpp"

#include "aot/PycpAot.hpp"
#include "aot/PycpAotSdkLocator.hpp"
#include "aot/PycpBuildScriptGenerator.hpp"
#include "aot/PycpCMakeGenerator.hpp" // 触发静态实例注册（kCMakeGenerator）
#include "aot/PycpModulePlan.hpp"
#include "aot/PycpProjectSpec.hpp"

#include "PycpConfig.hpp"

#include <algorithm>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace Pycp::AOT {

namespace {

// 写字符串到文件；失败抛异常（由上层 try/catch 转译）。
void write_file(const std::string& path, const std::string& content) {
	std::error_code ec;
	std::filesystem::create_directories(
		std::filesystem::path(path).parent_path(), ec);
	if (ec) throw Pycp::Exception("Failed to create directory for: " + path);

	std::ofstream f(path, std::ios::binary);
	if (!f) throw Pycp::Exception("Failed to write file: " + path);
	f << content;
	if (!f) throw Pycp::Exception("Failed to flush file: " + path);
}

// 从 SDK 静态库路径提取内置扩展名（libPycpExt_<name>.a / PycpExt_<name>.lib
// / PycpExt_<name>.a）。返回空串表示不是 PycpExt 库。
std::string builtin_name_of_lib(const std::string& path) {
	const std::size_t pos = path.find("PycpExt_");
	if (pos == std::string::npos) return "";
	std::string name = path.substr(pos + 8);
	const std::size_t dot = name.find_last_of('.');
	if (dot != std::string::npos) name = name.substr(0, dot);
	return name;
}

// C++ 字符串字面量（转义反斜杠与双引号）。
std::string cpp_string_literal(const std::string& s) {
	std::string out = "\"";
	for (char c : s) {
		switch (c) {
			case '\\': out += "\\\\"; break;
			case '"':  out += "\\\""; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:   out += c;      break;
		}
	}
	return out + "\"";
}

// 静态内置扩展（以 SDK 静态库链入主程序）的「注册 + 链接拉入桩」源文件。
//
// 两个作用：
//   1) 注册：把各内置扩展的初始化函数登记进 Pycp::RegisterAotModule 的
//      进程级注册表。ImportModule 的查找顺序第 ① 层就是「进程内符号 ->
//      注册表」，故静态链接下 import io 能命中，无需 dlsym，也无需
//      -rdynamic（Windows 与 Linux 行为一致）。
//   2) 拉入：静态库链接时链接器只拉入能解析未定义符号的成员。本文件显式
//      引用各 PycpModule_<name>，强制链接器把对应成员保留下来，否则其
//      静态初始化器不会执行（表现为编译链接全通过、运行时 import 失败）。
//
// 仅对「以静态库链入主程序的内置扩展」（builtin_static）生成；运行期从
// stdlib/ 加载的 builtin_shared 不属于本文件（它们不参与链接）。
std::string emit_builtin_registration_stub(
	const std::vector<std::string>& builtins) {
	std::ostringstream os;
	os << "// 自动生成，请勿修改。\n"
	   << "// 静态链入的内置原生扩展：注册并拉入（--compile-module:io=static "
	      "等指定）。\n"
	   << "// 由 Pycp " << Pycp::PYCP_VERSION << " 生成。\n\n"
	   << "#include \"PycpABI.hpp\"\n\n";

	for (const std::string& mod : builtins) {
		os << "extern \"C\" Pycp::Module* " << Pycp::AOT_MODULE_INIT_PREFIX
		   << mod << "();\n";
	}
	os << "\nnamespace {\n"
	   << "struct PycpBuiltinReg {\n"
	   << "  PycpBuiltinReg() {\n";
	for (const std::string& mod : builtins) {
		os << "    Pycp::RegisterAotModule(" << cpp_string_literal(mod) << ", &"
		   << Pycp::AOT_MODULE_INIT_PREFIX << mod << ");\n";
	}
	os << "  }\n"
	   << "};\n"
	   << "PycpBuiltinReg g_pycp_builtin_reg;\n"
	   << "} // namespace\n";
	return os.str();
}

} // anonymous namespace

bool EmitProject(
	const std::map<std::string, Pycp::BC::Module>& modules,
	const std::string& entry_name,
	const std::string& source_pycp,
	const std::string& output_dir,
	const AotProjectOptions& options,
	const std::vector<std::string>& kinds,
	std::vector<std::string>* written,
	std::string* err,
	AotPlanReport* report_out) {

	auto fail = [err](const std::string& msg) -> bool {
		if (err != nullptr) *err = msg;
		return false;
	};
	// 决策回执：成功路径统一回填，失败时保持未定义（调用方不使用）。
	if (report_out != nullptr) *report_out = AotPlanReport();

	try {
		// 组装项目描述（纯数据）。
		ProjectSpec spec;
		spec.name         = entry_name;
		spec.output_dir   = output_dir;
		spec.source_pycp  = source_pycp;
		spec.pycp_version = Pycp::PYCP_VERSION;
		spec.runtime_link = options.runtime_link;

		// 3) 定位 SDK（内置扩展名单在 SDK 定位结果中，override 归属依赖它）。
		spec.sdk = LocateSdk();

		// 4) 同批模块依赖图（仅同批转译模块；stdlib 扩展不在内，运行时加载）。
		std::vector<std::string> all_names; // 含入口
		for (const auto& kv : modules) all_names.push_back(kv.first);
		std::map<std::string, std::vector<std::string>> deps;
		for (const auto& kv : modules) {
			for (const std::string& dep : kv.second.imports) {
				if (modules.find(dep) != modules.end()) {
					deps[kv.first].push_back(dep);
				}
			}
		}

		// 5) 拆 override：内置扩展名 vs 同批转译模块名；两者皆非则报错。
		std::map<std::string, ModuleKind> module_overrides;
		std::map<std::string, ModuleKind> builtin_overrides;
		for (const auto& kv : options.overrides) {
			const std::string& n = kv.first;
			const bool is_builtin =
				std::find(spec.sdk.builtin_modules.begin(),
				          spec.sdk.builtin_modules.end(), n) !=
				spec.sdk.builtin_modules.end();
			const bool is_module =
				(modules.find(n) != modules.end()) && (n != entry_name);
			if (is_builtin) {
				builtin_overrides[n] = kv.second;
			} else if (is_module) {
				module_overrides[n] = kv.second;
			} else {
				return fail("--compile-module:<name>= 指定了未知模块 '" + n +
				            "'：它既不是同批转译的依赖模块，也不是内置扩展"
				            "（io / Pycp / classtools）。");
			}
		}

		// 6) 模块形态决策（不动点：被 ≥2 个链接目标引用的 static 提升 shared）。
		ModulePlan plan = PlanModuleKinds(
			all_names, deps, entry_name, options.default_module_kind,
			module_overrides, err);

		// 6b) 翻译字节码 -> C++ 源码（全模块）。需在形态决策之后进行：
		//     对 kShared 依赖不生成链接拉入桩（其符号在独立 DLL 中）。
		auto sources = Pycp::AOT::EmitCppAll(modules, entry_name, &plan.kinds);

		// 7) 内置扩展分组：无覆盖时默认跟随 runtime_link。
		for (const std::string& b : spec.sdk.builtin_modules) {
			auto it = builtin_overrides.find(b);
			const ModuleKind k = (it != builtin_overrides.end())
				? it->second
				: (spec.runtime_link == LinkMode::kStatic ? ModuleKind::kStatic
				                                          : ModuleKind::kShared);
			if (k == ModuleKind::kStatic) {
				spec.builtin_static.push_back(b);
			} else {
				spec.builtin_shared.push_back(b);
			}
		}
		// 7b) 为 builtin_static 收集对应 SDK 静态库绝对路径（CMake 链接用）。
		for (const std::string& lib : spec.sdk.stdlib_static_libs) {
			const std::string b = builtin_name_of_lib(lib);
			if (!b.empty() &&
			    std::find(spec.builtin_static.begin(),
			              spec.builtin_static.end(), b) !=
			        spec.builtin_static.end()) {
				spec.builtin_static_lib_paths.push_back(lib);
			}
		}

		// 8) 源文件与模块清单：入口源码排首，随后依赖模块。
		spec.sources.emplace_back(
			std::string(Pycp::AOT_ENTRY_CPP_FILENAME), sources.at(entry_name));
		for (const auto& kv : sources) {
			if (kv.first == entry_name) continue;
			const std::string fname = kv.first + Pycp::AOT_CPP_SUFFIX;
			spec.sources.emplace_back(fname, kv.second);

			ModuleTarget mt;
			mt.name = kv.first;
			mt.source_file = fname;
			mt.kind = plan.kinds.at(kv.first);
			mt.deps = deps[kv.first];
			// 决策原因随纯数据下传，生成器可渲染逐模块注释（CLI 亦据此打印）。
			auto rit = plan.reasons.find(kv.first);
			mt.reason = (rit != plan.reasons.end()) ? rit->second : std::string();
			spec.modules.push_back(std::move(mt));
		}
		// 8b) 入口的直接依赖：生成器据此推导主程序的静态依赖闭包
		//     （spec.modules 不含入口，否则无从得知 exe 需要链哪些静态库）。
		{
			auto eit = deps.find(entry_name);
			if (eit != deps.end()) spec.entry_deps = eit->second;
		}

		// 9) 静态链入主程序的内置扩展：生成注册/拉入桩（编进主程序）。
		//    必须早于 Validate —— Validate 会据此校验 SDK 静态产物齐全。
		if (!spec.builtin_static.empty()) {
			const std::string stub_file(Pycp::AOT_BUILTIN_REG_CPP_FILENAME);
			spec.sources.emplace_back(stub_file,
			                          emit_builtin_registration_stub(
			                              spec.builtin_static));
			spec.aux_sources.push_back(stub_file);
		}

		// 10) 校验（名称/输出目录/源文件/SDK/形态组合/静态产物齐全）。
		{
			std::string verr;
			if (!Validate(spec, &verr)) return fail(verr);
		}

		// 11) 建目录并写源文件。
		std::error_code ec;
		std::filesystem::create_directories(output_dir, ec);
		if (ec) return fail("Failed to create output directory: " + output_dir);

		if (written != nullptr) written->clear();
		for (const auto& kv : spec.sources) {
			const std::string path =
				(std::filesystem::path(output_dir) / kv.first).string();
			write_file(path, kv.second);
			if (written != nullptr) written->push_back(path);
		}

		// 12) 选生成器：kinds 为空 -> 全部已注册；否则按 kind 取。
		std::vector<const IBuildScriptGenerator*> gens;
		if (kinds.empty()) {
			for (const std::string& k : AvailableKinds()) {
				const IBuildScriptGenerator* g = FindGenerator(k);
				if (g != nullptr) gens.push_back(g);
			}
		} else {
			for (const std::string& k : kinds) {
				const IBuildScriptGenerator* g = FindGenerator(k);
				if (g == nullptr) {
					std::string avail;
					for (const std::string& a : AvailableKinds()) {
						if (!avail.empty()) avail += ", ";
						avail += a;
					}
					return fail("未知的生成器类型: " + k +
					            "（可用: " + (avail.empty() ? "(无)" : avail) + ")");
				}
				gens.push_back(g);
			}
		}
		if (gens.empty()) {
			return fail("未注册任何构建脚本生成器（CMake 生成器应静态自注册）。");
		}

		// 13) 调生成器渲染并写构建脚本。
		for (const IBuildScriptGenerator* g : gens) {
			const std::string content = g->Generate(spec);
			const std::string path =
				(std::filesystem::path(output_dir) / g->file_name()).string();
			write_file(path, content);
			if (written != nullptr) written->push_back(path);
		}

		// 14) 回填决策回执（CLI 可观测输出用）。必须在成功路径末尾，
		//     确保只有真正落盘的项目才返回形态决策。
		if (report_out != nullptr) {
			report_out->modules = plan;
			report_out->runtime_link = spec.runtime_link;
			report_out->builtin_static = spec.builtin_static;
			report_out->builtin_shared = spec.builtin_shared;
		}

		return true;
	}
	catch (const Pycp::Exception& e) {
		return fail(e.format());
	}
	catch (const std::exception& e) {
		return fail(std::string("EmitProject failed: ") + e.what());
	}
}

} // namespace Pycp::AOT
