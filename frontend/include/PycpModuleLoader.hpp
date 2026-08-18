#ifndef PYCP_MODULE_LOADER_HPP
#define PYCP_MODULE_LOADER_HPP

// =============================================================
// Pycp 模块加载器（编译期）
//
// 职责：解析入口 .pycp 文件，递归收集并编译其 import 依赖，
//   产出「模块名 -> BC::Module」映射，供解释执行与 AOT 共用。
//
// 路径解析规则（第一阶段，暂不支持 sys.path）：
//   仅在【入口文件所在目录】下查找 <modname>.pycp，
//   找不到则抛 ImportError（含导入语句的文件与行号）。
//
// 循环导入处理：
//   编译期以「先占坑」避免重复编译——某个模块第一次被 import 时
//   即登记其路径，若后续再次 import 同一模块则直接复用缓存，
//   不会无限递归（编译本身是幂等的，只关心依赖闭包）。
// =============================================================

#include "PycpBytecode.hpp"

#include <map>
#include <string>

namespace Pycp {

class ModuleLoader {
public:
	// 加载入口文件及其全部 import 依赖。
	//   entry_path : 入口 .pycp 文件路径（相对或绝对均可）
	// 返回按「模块名」索引的编译结果表（含入口模块，入口模块名为 ""）。
	// 任何模块找不到 / 语法错误时抛 Pycp::Exception。
	static std::map<std::string, BC::Module> load_all(const std::string& entry_path);

	// 编译单个源文件为 BC::Module（parsef + Codegen::Compile）。
	// 供 load_all 内部使用，也供 PycpMain 复用。
	static BC::Module compile_file(const std::string& path);

	// 根据模块名在 entry_dir 下解析为绝对文件路径。
	//   成功返回绝对路径；失败返回空字符串。
	static std::string resolve(const std::string& modname,
	                           const std::string& entry_dir);
};

} // namespace Pycp

#endif // PYCP_MODULE_LOADER_HPP
