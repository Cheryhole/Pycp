#include "loader/PycpModuleLoader.hpp"
#include "codegen/PycpCodegen.hpp"
#include "parser/PycpAstNode.hpp"
#include "object/PycpException.hpp"
#include "object/PycpConfig.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include <filesystem>

// 由 Flex/Bison 生成的解析器提供
extern Pycp::Ast::Node* parsef(const std::string& path);
extern Pycp::Ast::Node* parse(const std::string& src);
extern Pycp::Ast::Node* parse_statement(const std::string& src, int initial_line = 1);
extern int Pycp_parse_error_count;
extern std::string g_current_source_path;

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

BC::Module ModuleLoader::compile_string(const std::string& src,
                                         const std::string& name) {
	// 重置上一次 REPL 行的错误计数与源路径，避免沿用旧状态。
	Pycp_parse_error_count = 0;
	g_current_source_path = name;

	Pycp::Ast::Node* ast = parse(src);
	if (ast == nullptr || Pycp_parse_error_count > 0) {
		throw Pycp::Exception("");
	}
	if (ast->get_type() != Pycp::Ast::NodeType::PROGRAM) {
		delete ast;
		throw Pycp::Exception("Expected a program AST.");
	}
	auto* program = static_cast<Pycp::Ast::Program*>(ast);
	Pycp::BC::Module module = Pycp::Codegen::Compile(program, /*repl_eval=*/true);
	module.source_path = name;
	delete ast;
	return module;
}

BC::Module ModuleLoader::compile_statement(const std::string& src,
                                           const std::string& name,
                                           int initial_line) {
	// 单语句解析 ABI：解析【一段完整语句】（可跨多行，如类/函数/列表定义），
	// 整体作为独立 Program 编译并执行。REPL 逐条输入模型下，每次只传入当前
	// 完整 buffer（不携带历史行），由调用方保证 buffer 已由续行启发式判定为
	// 完整的单条语句单元。
	// 重置上一次 REPL 行的错误计数与源路径，避免沿用旧状态。
	Pycp_parse_error_count = 0;
	g_current_source_path = name;

	Pycp::Ast::Node* ast = parse_statement(src, initial_line);
	if (ast == nullptr || Pycp_parse_error_count > 0) {
		throw Pycp::Exception("");
	}
	if (ast->get_type() != Pycp::Ast::NodeType::PROGRAM) {
		delete ast;
		throw Pycp::Exception("Expected a program AST.");
	}
	auto* program = static_cast<Pycp::Ast::Program*>(ast);
	// 顶层表达式语句按 REPL 模式 RETURN 回显，与 compile_string 一致。
	Pycp::BC::Module module = Pycp::Codegen::Compile(program, /*repl_eval=*/true);
	module.source_path = name;
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

// =============================================================
// 模块文件夹（package）解析
// =============================================================
namespace {

// 一次解析的完整结果：模块名 + 源文件路径 + 所属包上下文。
struct ModuleRef {
	std::string name;     // 模块名（包内为点号全名 pkg.sub）
	std::string path;     // 源文件路径（包为清单路径）
	std::string pkg;      // 所属包名（空 = 不在包内）
	std::string pkg_dir;  // 包目录（pkg 非空时有效）
};

bool is_directory(const std::string& path) {
	std::error_code ec;
	return std::filesystem::is_directory(path, ec);
}

// 取路径的最后一段（目录则为其自身名字）。
std::string last_segment(const std::string& path) {
	std::string p = path;
	// 去掉结尾的斜杠，便于取目录名。
	while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
	std::size_t slash = p.find_last_of("/\\");
	return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

// 该路径是否为包清单（pycp.mpycp）。
bool is_manifest(const std::string& path) {
	return last_segment(path) == Pycp::MODULE_MANIFEST_FILENAME;
}

// 在单一目录下按「包优先、其次源码、再次字节码」解析模块名。
//   <dir>/<name>/pycp.mpycp  -> 包（模块名不变，pkg = 限定名）
//   <dir>/<name>.pycp        -> 普通源码模块
//   <dir>/<name>.cpycp       -> 已编译字节码模块（与 .pycp 等价，只是省去
//                               编译步骤；加载时反序列化而非解析源码）
bool resolve_in_dir(const std::string& dir, const std::string& name,
                    ModuleRef* out) {
	const std::string pkg_dir = join_path(dir, name);
	const std::string manifest = join_path(pkg_dir, Pycp::MODULE_MANIFEST_FILENAME);
	if (file_exists(manifest)) {
		out->name = name;
		out->path = manifest;
		out->pkg = name;
		out->pkg_dir = pkg_dir;
		return true;
	}
	const std::string src = join_path(dir, name + Pycp::EXT_PYCP);
	if (file_exists(src)) {
		out->name = name;
		out->path = src;
		out->pkg.clear();
		out->pkg_dir.clear();
		return true;
	}
	const std::string bc = join_path(dir, name + Pycp::EXT_CPYCP);
	if (file_exists(bc)) {
		out->name = name;
		out->path = bc;
		out->pkg.clear();
		out->pkg_dir.clear();
		return true;
	}
	return false;
}

// 按扩展名加载模块：.pycp 走「解析 + Codegen」，.cpycp 走反序列化。
// 两者产出等价的 BC::Module（.cpycp 只是省去源码 -> 字节码这一步）。
BC::Module load_module_file(const std::string& path) {
	if (path.size() >= std::string(Pycp::EXT_CPYCP).size() &&
	    path.compare(path.size() - std::string(Pycp::EXT_CPYCP).size(),
	                 std::string(Pycp::EXT_CPYCP).size(),
	                 Pycp::EXT_CPYCP) == 0) {
		std::ifstream f(path, std::ios::binary);
		if (!f) throw Pycp::Exception("Cannot open file: " + path);
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
		                           std::istreambuf_iterator<char>());
		BC::Module m = Pycp::BC::Deserialize(bytes.data(), bytes.size());
		m.source_path = path;
		return m;
	}
	return Pycp::ModuleLoader::compile_file(path);
}

} // anonymous namespace

std::map<std::string, BC::Module> ModuleLoader::load_all(
	const std::string& entry_path,
	std::vector<ImportResolution>* resolutions,
	std::set<std::string>* package_names) {
	std::map<std::string, BC::Module> modules;
	// 入口文件所在目录（所有 import 均在此目录查找）
	const std::string entry_dir = dir_of(entry_path);

	// ---- 入口解析：目录（包）/ 清单文件 / 普通源文件 ----
	std::string entry_file = entry_path;
	std::string entry_pkg;      // 非空表示入口本身是包
	std::string entry_pkg_dir;
	if (is_directory(entry_path)) {
		entry_file = join_path(entry_path, Pycp::MODULE_MANIFEST_FILENAME);
		entry_pkg = last_segment(entry_path);
		entry_pkg_dir = entry_path;
	} else if (is_manifest(entry_path)) {
		entry_pkg = last_segment(entry_dir);
		entry_pkg_dir = entry_dir;
	}
	const std::string entry_name =
		entry_pkg.empty() ? base_name_no_ext(entry_file) : entry_pkg;

	// 递归编译依赖闭包（模块名 -> 文件路径）。
	// 用「待处理队列 + 已登记集合」做广度优先收集，避免重复编译。
	std::vector<ModuleRef> queue;
	std::vector<std::string> seen_paths;

	// 解析清单去重：按模块名只记第一条（不同模块重复 import 同一名字
	// 时，结论一致，无需重复记录）。
	auto record = [&resolutions](const std::string& dep_name, ImportKind kind,
	                             const std::string& dep_path) {
		if (resolutions == nullptr) return;
		for (const auto& r : *resolutions) {
			if (r.name == dep_name) return;
		}
		ImportResolution r;
		r.name = dep_name;
		r.kind = kind;
		r.path = dep_path;
		resolutions->push_back(std::move(r));
	};

	// 入口模块：包入口取包名，普通文件取 basename（便于反向 import）。
	queue.push_back(ModuleRef{entry_name, entry_file, entry_pkg, entry_pkg_dir});
	seen_paths.push_back(entry_file);
	if (package_names != nullptr && is_manifest(entry_file)) {
		package_names->insert(entry_name);
	}

	for (std::size_t qi = 0; qi < queue.size(); ++qi) {
		const ModuleRef item = queue[qi];

		// 加载该文件（.pycp 编译 / .cpycp 反序列化）
		BC::Module m = load_module_file(item.path);

		// 收集其 import 依赖（Module.imports 已在 Codegen 填充模块名）
		for (std::size_t ii = 0; ii < m.imports.size(); ++ii) {
			const std::string dep = m.imports[ii];
			ModuleRef r;
			bool found = false;

			// ① 包内兄弟优先：包内 import 同名兄弟时解析为限定名
			//    pkg.sub（对齐 Python 的子模块语义），且不会被 cwd 下的
			//    同名文件抢走。
			if (!item.pkg.empty()) {
				ModuleRef sib;
				if (resolve_in_dir(item.pkg_dir, dep, &sib)) {
					found = true;
					r = sib;
					if (r.pkg.empty()) {
						// 普通兄弟源码：升级为限定名，继承包上下文。
						r.name = item.pkg + Pycp::MODULE_NAME_SEPARATOR + dep;
						r.pkg = item.pkg;
						r.pkg_dir = item.pkg_dir;
					} else {
						// 嵌套子包：限定名即其包名。
						r.name = item.pkg + Pycp::MODULE_NAME_SEPARATOR + dep;
						r.pkg = r.name;
						r.pkg_dir = sib.pkg_dir;
					}
				}
			}
			// ② 常规顺序：cwd -> 入口目录（含「目录即包」的探测）。
			if (!found) {
				found = resolve_in_dir(std::string(), dep, &r) ||
				        resolve_in_dir(entry_dir, dep, &r);
			}
			if (!found) {
				// 解析失败：可能是动态库扩展（io/pycp 等），也可能是拼写
				// 错误。为让 AOT 闭包可判定，如实记录 kUnresolved，由上层
				// 做警告或 --external 提升。
				record(dep, ImportKind::kUnresolved, "");
				continue;
			}

			// 把 import 名字改写为最终模块名（限定名），使 VM 的
			// LOAD_MODULE 与 AOT 模块表按同一套键寻址。
			m.imports[ii] = r.name;

			// 去重（按路径）
			bool already = false;
			for (const std::string& sp : seen_paths) {
				if (sp == r.path) { already = true; break; }
			}
			if (!already) {
				seen_paths.push_back(r.path);
				queue.push_back(r);
				if (package_names != nullptr && is_manifest(r.path)) {
					package_names->insert(r.name);
				}
			}
			record(r.name, ImportKind::kTranslated, r.path);
		}

		modules[item.name] = std::move(m);
	}

	return modules;
}

} // namespace Pycp
