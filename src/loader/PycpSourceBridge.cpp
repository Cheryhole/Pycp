#include "loader/PycpSourceBridge.hpp"

#include "abi/PycpNativeExt.hpp"
#include "abi/PycpABI.hpp"          // RunBytecodeModule
#include "bytecode/PycpBytecode.hpp"
#include "loader/PycpModuleLoader.hpp"
#include "object/PycpConfig.hpp"    // MODULE_MANIFEST_FILENAME
#include "parser/PycpAstReflect.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace Pycp {

namespace {

// SourceStringCompiler：源码字符串 -> 字节码 Module（普通模块语义）。
// 使用 repl_eval=false：顶层表达式语句不保留返回值（对齐 Python compile
// 的 "exec" 模式），与 REPL 的 compile_string 默认行为区分开。
BC::Module* bridge_compile_string(const std::string& source,
                                  const std::string& filename) {
	return new BC::Module(
		ModuleLoader::compile_string(source, filename, /*repl_eval=*/false));
}

// SourceExecutor：源码字符串 -> 编译并执行，返回顶层结果（Owned）。
// globals 语义（预置 + 写回）由 RunBytecodeModule 统一处理。
Object* bridge_exec_string(const std::string& source,
                           const std::string& filename, Object* globals) {
	BC::Module module =
		ModuleLoader::compile_string(source, filename, /*repl_eval=*/false);
	return RunBytecodeModule(&module, globals);
}

// SourceParser：源码字符串 -> AST 节点树（已映射为 pycp 对象），供 ast.parse。
Object* bridge_parse_string(const std::string& source, const std::string& filename) {
	return Ast::ParseToObjects(source, filename);
}

// SourceModuleSetLoader：递归加载入口及其 import 依赖（ModuleLoader::load_all）。
// 入口名推导与 CLI 的 resolve_entry_path 同源（此处不做「按名查找」，
// 名称解析由 CLI 负责；本钩子接收的是已定位的路径）。
void bridge_load_module_set(const std::string& entry_path,
                            std::string* out_entry_name,
                            std::map<std::string, BC::Module*>* out_modules,
                            std::set<std::string>* out_packages) {
	namespace fs = std::filesystem;
	std::error_code ec;

	std::string entry_file = entry_path;
	std::string entry_name;

	if (fs::is_directory(entry_path, ec)) {
		// 模块文件夹：入口即其清单。
		entry_file = (fs::path(entry_path) / MODULE_MANIFEST_FILENAME).string();
		entry_name = fs::path(entry_path).filename().string();
	} else if (fs::path(entry_path).filename().string() ==
	           MODULE_MANIFEST_FILENAME) {
		// 直接指向清单：包名取所在目录名。
		const fs::path p(entry_path);
		const fs::path parent = p.parent_path();
		entry_name = parent.empty() ? p.filename().string()
		                            : parent.filename().string();
	} else {
		// 普通源文件：basename 去扩展名。
		std::string base = fs::path(entry_path).filename().string();
		const std::size_t dot = base.rfind('.');
		if (dot != std::string::npos) base = base.substr(0, dot);
		entry_name = base;
	}

	std::set<std::string> pkgs;
	std::map<std::string, BC::Module> mods =
		ModuleLoader::load_all(entry_file, nullptr, &pkgs);

	if (out_entry_name != nullptr) *out_entry_name = entry_name;
	if (out_packages != nullptr) *out_packages = pkgs;
	if (out_modules != nullptr) {
		// 每个模块转为堆分配对象，所有权经钩子移交调用方（bytecode 模块）。
		for (auto& kv : mods) {
			(*out_modules)[kv.first] = new BC::Module(std::move(kv.second));
		}
	}
}

} // anonymous namespace

void RegisterSourceHooks() {
	SetSourceStringCompiler(&bridge_compile_string);
	SetSourceExecutor(&bridge_exec_string);
	SetSourceParser(&bridge_parse_string);
	SetSourceModuleSetLoader(&bridge_load_module_set);
}

} // namespace Pycp
