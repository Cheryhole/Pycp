#include "aot/PycpAotProject.hpp"

#include "aot/PycpAot.hpp"
#include "aot/PycpAotSdkLocator.hpp"
#include "aot/PycpBuildScriptGenerator.hpp"
#include "aot/PycpCMakeGenerator.hpp" // 触发静态实例注册（kCMakeGenerator）
#include "aot/PycpProjectSpec.hpp"

#include "PycpConfig.hpp"

#include <fstream>
#include <filesystem>

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

} // anonymous namespace

bool EmitProject(
	const std::map<std::string, Pycp::BC::Module>& modules,
	const std::string& entry_name,
	const std::string& source_pycp,
	const std::string& output_dir,
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

		// 4) 校验（名称/输出目录/源文件/SDK 有效性）。
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
			const std::string path =
				std::filesystem::path(output_dir) / kv.first;
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
			const std::string path =
				std::filesystem::path(output_dir) / g->file_name();
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
