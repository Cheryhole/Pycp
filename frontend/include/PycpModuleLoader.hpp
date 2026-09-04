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
#include <vector>

namespace Pycp {

// 编译期 import 解析结果分类（转译期判定「模块来源」，供 AOT 闭包校验
// 与 --show-imports 诊断）。kExternal 在本层不产生（由上层把 kUnresolved
// 中「显式声明为外部动态库」的名字提升而来）。
enum class ImportKind {
	kTranslated, // 已 resolve 到 .pycp 并纳入转译
	kExternal,   // 显式声明的外部动态库（--external:<name>）
	kUnresolved, // 既无源码也未声明：默认警告；--strict-imports 下报错
};

struct ImportResolution {
	std::string name;
	ImportKind kind = ImportKind::kUnresolved;
	std::string path; // kTranslated 时为源文件路径，其余为空
};

class ModuleLoader {
public:
	// 加载入口文件及其全部 import 依赖。
	//   entry_path  : 入口 .pycp 文件路径（相对或绝对均可）
	//   resolutions : 可选输出；按 import 语句出现的模块名逐条记录
	//                 kTranslated / kUnresolved（按名去重，稳定顺序），
	//                 供 AOT 闭包校验与 --show-imports 诊断使用。
	// 返回按「模块名」索引的编译结果表（含入口模块，入口模块名为 ""）。
	// 任何模块找不到 / 语法错误时抛 Pycp::Exception。
	static std::map<std::string, BC::Module> load_all(
		const std::string& entry_path,
		std::vector<ImportResolution>* resolutions = nullptr);

	// 编译单个源文件为 BC::Module（parsef + Codegen::Compile）。
	// 供 load_all 内部使用，也供 PycpMain 复用。
	static BC::Module compile_file(const std::string& path);

	// 编译内存中的源码字符串为 BC::Module（parse + Codegen::Compile）。
	// 供 REPL 逐行/逐块求值使用；返回的 Module 已置 repl_eval=true，
	// 使顶层表达式语句保留返回值供 REPL 回显。
	//   src  : 源码文本
	//   name : 用于报错显示的源名称（默认 REPL_SOURCE_NAME，即 <stdin>）
	static BC::Module compile_string(const std::string& src,
	                                 const std::string& name = REPL_SOURCE_NAME);

	// 单语句解析 ABI：编译【一段完整语句】为独立 BC::Module（parse_statement
	// + Codegen::Compile）。语义对标 Python 的 "single" 解析模式——解析一条
	// 完整语句单元（可跨多行，如类/函数/列表定义），整体编译、整体执行。
	// 供 REPL 逐条输入模型使用：每次只传入当前完整 buffer（不携带历史行），
	// 与 compile_string 不同之处在于调用方语义为「一条语句」而非「整段程序」，
	// 二者底层均置 repl_eval=true 以支持顶层表达式回显。
	//   src         : 单条语句源码文本（可跨多行）
	//   name        : 用于报错显示的源名称（默认 REPL_SOURCE_NAME，即 <stdin>）
	//   initial_line: 本次解析的起始行号（默认 1）。REPL 传入会话累计行号，
	//                 使错误显示的行号在整个会话中连续递增。
	static BC::Module compile_statement(const std::string& src,
	                                    const std::string& name = REPL_SOURCE_NAME,
	                                    int initial_line = 1);

	// 根据模块名在 entry_dir 下解析为绝对文件路径。
	//   成功返回绝对路径；失败返回空字符串。
	static std::string resolve(const std::string& modname,
	                           const std::string& entry_dir);
};

} // namespace Pycp

#endif // PYCP_MODULE_LOADER_HPP
