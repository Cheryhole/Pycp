#include "PycpModuleLoader.hpp"
#include "PycpCodegen.hpp"
#include "PycpAstNode.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include <filesystem>

// 由 Flex/Bison 生成的解析器提供
extern Pycp::Ast::Node* parsef(const std::string& path);
extern int Pycp_parse_error_count;

namespace Pycp {

namespace {

// 判断文件是否存在且为常规文件（跨平台：用 std::filesystem 替代 POSIX stat/S_ISREG）
bool file_exists(const std::string& path) {
	std::error_code ec;
	return std::filesystem::is_regular_file(path, ec);
}

// 提取路径中的目录部分（不含文件名）；无 '/' 时返回 "."。
std::string dir_of(const std::string& path) {
	std::size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos) return ".";
	return path.substr(0, slash);
}

// 拼接目录与文件名
std::string join_path(const std::string& dir, const std::string& name) {
	if (dir.empty() || dir == ".") return name;
	if (dir.back() == '/' || dir.back() == '\\') return dir + name;
	return dir + "/" + name;
}

// 提取文件 basename 并去掉扩展名（foo.pycp -> foo）。
std::string base_name_no_ext(const std::string& path) {
	std::size_t slash = path.find_last_of("/\\");
	std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
	std::size_t dot = base.find_last_of('.');
	if (dot != std::string::npos) base = base.substr(0, dot);
	return base;
}

} // anonymous namespace

BC::Module ModuleLoader::compile_file(const std::string& path) {
	Pycp::Ast::Node* ast = parsef(path);
	if (ast == nullptr || Pycp_parse_error_count > 0) {
		throw Pycp::Exception("");
	}
	if (ast->get_type() != Pycp::Ast::NodeType::PROGRAM) {
		delete ast;
		throw Pycp::Exception("Expected a program AST.");
	}
	auto* program = static_cast<Pycp::Ast::Program*>(ast);
	Pycp::BC::Module module = Pycp::Codegen::Compile(program);
	module.source_path = path;
	delete ast;
	return module;
}

std::string ModuleLoader::resolve(const std::string& modname,
                                  const std::string& entry_dir) {
	// 优先当前工作目录（cwd），未命中再查脚本所在目录。
	std::string cwd_candidate = join_path(".", modname + Pycp::EXT_PYCP);
	if (file_exists(cwd_candidate)) return cwd_candidate;

	std::string candidate = join_path(entry_dir, modname + Pycp::EXT_PYCP);
	if (file_exists(candidate)) return candidate;
	return "";
}

std::map<std::string, BC::Module> ModuleLoader::load_all(const std::string& entry_path) {
	std::map<std::string, BC::Module> modules;
	// 入口文件所在目录（所有 import 均在此目录查找）
	const std::string entry_dir = dir_of(entry_path);

	// 递归编译依赖闭包（模块名 -> 文件路径）。
	// 用「待处理队列 + 已登记集合」做广度优先收集，避免重复编译。
	std::vector<std::pair<std::string, std::string>> queue; // (模块名, 文件路径)
	std::vector<std::string> seen_paths;

	// 入口模块：模块名 = basename（去扩展名），便于被依赖模块反向 import。
	const std::string entry_name = base_name_no_ext(entry_path);
	queue.push_back({entry_name, entry_path});
	seen_paths.push_back(entry_path);

	for (std::size_t qi = 0; qi < queue.size(); ++qi) {
		const std::string modname = queue[qi].first;
		const std::string path = queue[qi].second;

		// 编译该文件
		BC::Module m = compile_file(path);

		// 收集其 import 依赖（Module.imports 已在 Codegen 填充模块名）
		for (std::size_t ii = 0; ii < m.imports.size(); ++ii) {
			const std::string& dep = m.imports[ii];

			// 无法 resolve 为 .pycp 的模块名统一跳过（可能是动态库扩展，
			// 交由运行时 VM::load_module 的 LoadNativeModule 处理；命中原生
			// 扩展则加载，否则运行时抛 ImportError）。
			std::string dep_path = resolve(dep, entry_dir);
			if (dep_path.empty()) {
				continue;
			}
			// 去重（按路径）
			bool already = false;
			for (const std::string& sp : seen_paths) {
				if (sp == dep_path) { already = true; break; }
			}
			if (already) continue;
			seen_paths.push_back(dep_path);
			queue.push_back({dep, dep_path});
		}

		modules[modname] = std::move(m);
	}

	return modules;
}

} // namespace Pycp
