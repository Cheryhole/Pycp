#include "aot/PycpAotProject.hpp"

#include "aot/PycpAot.hpp"
#include "aot/PycpAotSdkLocator.hpp"
#include "aot/PycpBuildScriptGenerator.hpp"
#include "aot/PycpCMakeGenerator.hpp" // 触发静态实例注册（kCMakeGenerator）
#include "aot/PycpProjectSpec.hpp"

#include "PycpConfig.hpp"

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

// 内置扩展的「注册 + 链接拉入桩」源文件（仅 --static 生成）。
//
// 两个作用：
//   1) 注册：把各内置扩展的初始化函数登记进 Pycp::RegisterAotModule 的
//      进程级注册表。ImportModule 的查找顺序第 ① 层就是「进程内符号 ->
//      注册表」，故静态链接下 import io 能命中，无需 dlsym，也无需
//      -rdynamic（Windows 与 Linux 行为一致）。
//   2) 拉入：静态库链接时链接器只拉入能解析未定义符号的成员。本文件显式
//      引用各 PycpModule_<name>，强制链接器把对应成员保留下来，否则其
//      静态初始化器不会执行（表现为编译链接全通过、运行时 import 失败）。
//      与 PycpAot.cpp 对 .pycp 依赖模块的处理同范式，比
//      -Wl,--whole-archive 精确且不依赖 GNU 专有选项。
std::string emit_builtin_registration_stub(const SdkInfo& sdk) {
	std::ostringstream os;
	os << "// 自动生成，请勿修改。\n"
	   << "// 静态链接模式（--static）专用：注册并拉入内置原生扩展。\n"
	   << "// 由 Pycp " << Pycp::PYCP_VERSION << " 生成。\n\n"
	   << "#include \"PycpABI.hpp\"\n\n";

	for (const std::string& mod : sdk.builtin_modules) {
		os << "extern \"C\" Pycp::Module* " << Pycp::AOT_MODULE_INIT_PREFIX
		   << mod << "();\n";
	}
	os << "\nnamespace {\n"
	   << "struct PycpBuiltinReg {\n"
	   << "  PycpBuiltinReg() {\n";
	for (const std::string& mod : sdk.builtin_modules) {
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
	LinkMode link_mode,
	const std::vector<std::string>& kinds,
	std::vector<std::string>* written,
	std::string* err) {

	auto fail = [err](const std::string& msg) -> bool {
		if (err != nullptr) *err = msg;
		return false;
	};

	try {
		// 1) 翻译字节码 -> C++ 源码（全模块）。
		auto sources = Pycp::AOT::EmitCppAll(modules, entry_name);

		// 2) 组装项目描述（纯数据）。
		ProjectSpec spec;
		spec.name        = entry_name;
		spec.output_dir  = output_dir;
		spec.source_pycp = source_pycp;
		spec.pycp_version = Pycp::PYCP_VERSION;

		// 入口源码排在首位：文件名固定为 AOT_ENTRY_CPP_FILENAME（含 main）。
		spec.sources.emplace_back(
			std::string(Pycp::AOT_ENTRY_CPP_FILENAME), sources.at(entry_name));

		// 依赖模块源码：原始模块名 + AOT_CPP_SUFFIX。
		for (const auto& kv : sources) {
			if (kv.first == entry_name) continue;
			spec.sources.emplace_back(kv.first + Pycp::AOT_CPP_SUFFIX, kv.second);
		}

		// 3) 定位 SDK（运行时硬约束：需含 include/lib/stdlib）。
		spec.sdk = LocateSdk();
		spec.link_mode = link_mode;

		// 3b) 静态模式：追加内置扩展的注册/拉入桩。
		// 必须早于 Validate —— Validate 会据此校验 SDK 的静态产物是否齐全。
		if (link_mode == LinkMode::kStatic && !spec.sdk.builtin_modules.empty()) {
			spec.sources.emplace_back(
				std::string(Pycp::AOT_BUILTIN_REG_CPP_FILENAME),
				emit_builtin_registration_stub(spec.sdk));
		}

		// 4) 校验（名称/输出目录/源文件/SDK 有效性/静态可行性）。
		{
			std::string verr;
			if (!Validate(spec, &verr)) return fail(verr);
		}

		// 5) 建目录并写源文件。
		std::error_code ec;
		std::filesystem::create_directories(output_dir, ec);
		if (ec) return fail("Failed to create output directory: " + output_dir);

		if (written != nullptr) written->clear();
		for (const auto& kv : spec.sources) {
			// 显式 .string()：Windows 下 path::value_type 为 wchar_t，
			// path -> std::string 无隐式转换（Linux 下为 char 故可隐式转换）。
			const std::string path =
				(std::filesystem::path(output_dir) / kv.first).string();
			write_file(path, kv.second);
			if (written != nullptr) written->push_back(path);
		}

		// 6) 选生成器：kinds 为空 -> 全部已注册；否则按 kind 取。
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

		// 7) 调生成器渲染并写构建脚本。
		for (const IBuildScriptGenerator* g : gens) {
			const std::string content = g->Generate(spec);
			// 同上：显式转换，避免 Windows 下 path -> std::string 编译失败。
			const std::string path =
				(std::filesystem::path(output_dir) / g->file_name()).string();
			write_file(path, content);
			if (written != nullptr) written->push_back(path);
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
