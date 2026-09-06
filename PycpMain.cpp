// =============================================================
// PycpMain.cpp — Pycp 主程序入口
//
// 统一前端（解析）+ 后端（字节码编译 / 执行）的命令行接口。
// 用法对齐 Python 版 PycpMain.py：
//
//   pycp [options] <input_file>
//
// 选项：
//   -h, --help        显示帮助信息
//   -c, --compile     编译 .pycp 源文件为 .cpycp 字节码（不执行）
//   -b, --bytecode    生成 .cpycp 字节码文件（等价于 -c）
//   -i, --interpret   解释执行（默认行为；可直接执行 .pycp 或 .cpycp）
//   -o, --output <f>  指定输出文件路径（与 -c/-b 配合）
//       --emit-cpp    将 .pycp 翻译为独立 C++ 源文件（AOT 预留接口）
//   -d, --dump        查看字节码内容（常量池/符号表/代码对象/指令及行号）
//
// 仅与 --emit-cpp 搭配的链接控制：
//   --compile-runtime=shared|static    运行时库 libPycpRuntime 的链接方式
//   --compile-modules=shared|static    转译 .pycp 模块的全局形态（默认 shared：
//                                      编成动态库加载；static 表示编进主程序）
//   --compile-module:<name>=shared|static
//                                      按模块覆盖（内置扩展 io/Pycp/classtools
//                                      或任一转译依赖模块）
//   --shared / --static                旧别名，等价 --compile-runtime=shared/static
//   --show-imports                     打印编译期 import 解析清单（translated /
//                                      unresolved）与逐模块链接形态决策表
//
// 行为：
//   * 默认（无 -c/-b）执行"解释运行"：若输入为 .pycp 则 解析->编译->执行；
//     若输入为 .cpycp 则 反序列化->执行（无需重新解析）。
//   * -c/-b 仅编译输出 .cpycp，不执行。
//   * .cpycp 可直接执行，无需额外的解释器或依赖（依赖已静态/动态链接的
//     PycpRuntime，但不需要 Python 或其他外部解释器）。
// =============================================================

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "PycpAstNode.hpp"
#include "PycpCodegen.hpp"
#include "aot/PycpAot.hpp"
#include "aot/PycpAotProject.hpp"
#include "aot/PycpAotSdkLocator.hpp" // LocateSdk：过滤内置扩展名的 unresolved 提示
#include "PycpModuleLoader.hpp"
#include "preprocessor/PycpPreprocessor.hpp"

#include "Pycp.hpp"          // 运行时（Object/GC/ABI/Manager）
#include "PycpBytecode.hpp"  // 字节码格式 / 序列化
#include "PycpBytecodeDump.hpp" // 字节码查看（dump）接口
#include "PycpBytecodeVM.hpp"// VM 执行
#include "PycpNativeExt.hpp" // 原生扩展加载 / 源码模块编译器钩子
#include "PycpString.hpp"    // REPL 回显：String::get_value()
#include "PycpException.hpp"
#include "PycpConfig.hpp"    // 集中管理的常量（扩展名/输出命名/版本等）

#include "linenoise.hpp"     // REPL 行编辑（方向键/历史），third_party/cpp-linenoise (C++17, 跨平台)

#include <map>

#if defined(_WIN32)
#include <windows.h>   // SetConsoleOutputCP（仅 Windows 下设置 UTF-8 控制台代码页）
#endif

// 由 Flex/Bison 生成的解析器提供
extern Pycp::Ast::Node* parsef(const std::string& path);
extern int Pycp_parse_error_count;

// =============================================================
// 命令行参数
// =============================================================

namespace {

struct Options {
	std::string input_file;
	// 脚本参数（入口脚本之后的位置参数），传给 pycp.argv，不含 pycp 可执行
	// 文件本身。仅解释执行路径消费；--emit-cpp/--compile 等无需参数。
	std::vector<std::string> script_args;
	std::string output_file;
	bool compile = false;    // -c / -b
	bool interpret = true;   // -i（默认解释执行）
	bool emit_cpp = false;   // --emit-cpp
	bool dump = false;       // -d / --dump
	bool preprocess = false; // -p / --preprocess
	bool show_help = false;

	// ---- --emit-cpp 的模块形态与运行时形态 ----
	// 依赖模块（.pycp 转译产物）的全局默认形态：默认 kShared（编成模块
	// DLL，运行期加载）。要编进主程序请用 --compile-modules=static。
	Pycp::AOT::ModuleKind compile_modules = Pycp::AOT::ModuleKind::kShared;
	// 运行时库（libPycpRuntime）形态：默认 kShared。
	Pycp::AOT::LinkMode compile_runtime = Pycp::AOT::LinkMode::kShared;
	// 是否显式指定过 --compile-modules / --compile-runtime（用于冲突检测）。
	bool compile_modules_set = false;
	bool compile_runtime_set = false;
	// 旧兼容别名 --shared / --static 是否被使用（与 --compile-* 混用报错）。
	bool legacy_link_set = false;
	// --compile-module:<name>=shared|static 的按模块覆盖。
	std::map<std::string, Pycp::AOT::ModuleKind> module_overrides;
	// --show-imports：转译期打印 import 解析清单后继续。
	bool show_imports = false;
};

void print_help(const char* prog) {
	std::cout
		<< "Pycp - Python-like language compiler & interpreter\n\n"
		<< "Usage:\n"
		<< "  " << prog << " [options] <input_file>\n\n"
		<< "Options:\n"
		<< "  -h, --help        Show this help message\n"
		<< "  -i, --interpret   Interpret & execute (default); accepts .pycp or .cpycp\n"
		<< "  -c, --compile     Compile <input_file> (.pycp) to bytecode (.cpycp)\n"
		<< "  -b, --bytecode    Generate .cpycp bytecode file (alias of -c)\n"
		<< "      --emit-cpp    Translate .pycp to a compilable C++ project (CMake)\n"
		<< "      --compile-runtime=shared|static\n"
		<< "                    With --emit-cpp: how to link libPycpRuntime\n"
		<< "                    (default shared). 'static' needs the SDK's static\n"
		<< "                    artifacts and forbids any dynamic module.\n"
		<< "      --compile-modules=shared|static\n"
		<< "                    Global kind for translated .pycp modules (default\n"
		<< "                    shared: compiled to a module DLL loaded at\n"
		<< "                    runtime; static: baked into the main executable).\n"
		<< "      --compile-module:<name>=shared|static\n"
		<< "                    Per-module override (builtin io/Pycp/classtools or\n"
		<< "                    any translated dependency module). Each module\n"
		<< "                    becomes its own CMake target.\n"
		<< "      --shared      Deprecated alias for --compile-runtime=shared.\n"
		<< "      --static      Deprecated alias for --compile-runtime=static\n"
		<< "                    (self-contained when no module is shared).\n"
		<< "      --show-imports\n"
		<< "                    Print the compile-time import resolution manifest\n"
		<< "                    and the per-module link-kind decision table.\n"
		<< "  -o, --output <f>  Output path/dir (with -c/-b: .cpycp file; with\n"
		<< "                    --emit-cpp: project directory; with -p: .pp.pycp file)\n"
		<< "  -d, --dump        Dump bytecode (constant pool, symbols, code objects,\n"
		<< "                    instructions & line numbers) of .pycp or .cpycp\n"
		<< "  -p, --preprocess  Preprocess a .pycp file (no execution): writes\n"
		<< "                    <input-basename>.pp.pycp in the input directory\n"
		<< "                    (or -o <file>). Directives:\n"
		<< "                    # replace NAME with VALUE   (text substitution)\n"
		<< "                    # define NAME [VALUE]      (macro def for #if defined)\n"
		<< "                    # stop replacing NAME / # undefine NAME\n"
		<< "                    # set lineno to N / # set filename to \"PATH\"\n"
		<< "                    # expand NAME             (expand file at NAME path)\n"
		<< "                    # if/#elif/#else/#end      (conditional incl., nestable)\n"
		<< "                    # send error/warning/message \"TEXT\"\n"
		<< "                    code can use #lineno / #filename (line & path)\n\n"
		<< "Import & modules:\n"
		<< "  * Runtime lookup order (first hit wins): in-process symbols /\n"
		<< "    AOT registry -> cwd -> script directory -> <exe>/stdlib/. In\n"
		<< "    every directory, foo.pycp source takes precedence over a same-\n"
		<< "    named native library foo.so/.dll (a .pycp in stdlib/ can even\n"
		<< "    shadow a builtin extension). All misses raise ImportError.\n"
		<< "  * import foo / import foo as bar  loads module foo and binds it in\n"
		<< "    the current scope.\n"
		<< "  * --emit-cpp generates a compilable C++ project directory (default\n"
		<< "    ./<entry-name>/, or -o <dir>): it contains the entry\n"
		<< "    <dir>/__pycp_main.gen.cpp (with main()), one <name>.gen.cpp per\n"
		<< "    imported module, and a CMakeLists.txt. Build & run with:\n"
		<< "      cd <dir> && cmake -S . -B build && cmake --build build && ./build/<entry>\n"
		<< "    The CMake project links the PycpRuntime SDK (auto-located from the\n"
		<< "    pycp install; override with -DPYCP_DIST=<path>), dynamically by\n"
		<< "    default (--shared) or statically with --static. By default each\n"
		<< "    imported module is compiled to its own module DLL; use\n"
		<< "    --compile-modules=static to bake them into the executable.\n\n"
		<< "Examples:\n"
		<< "  " << prog << " hello.pycp              # compile & run\n"
		<< "  " << prog << " hello.cpycp             # run compiled bytecode directly\n"
		<< "  " << prog << " -c hello.pycp -o hello.cpycp\n"
		<< "  " << prog << " --emit-cpp hello.pycp   # -> hello/ project (CMake)\n"
		<< "  " << prog << " --emit-cpp --static hello.pycp -o hello-static\n";
}

bool has_suffix(const std::string& s, const std::string& suffix) {
	if (s.size() < suffix.size()) return false;
	return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool starts_with(const std::string& s, const std::string& prefix) {
	return s.compare(0, prefix.size(), prefix) == 0;
}

// 解析 "shared" / "static" 取值。ok=true 表示 shared，ok=false 表示 static。
bool parse_shared_static(const std::string& v, bool* ok) {
	if (v == "shared") { *ok = true; return true; }
	if (v == "static") { *ok = false; return true; }
	return false;
}

// 解析命令行参数
bool parse_args(int argc, char** argv, Options& opt) {
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "-h" || arg == "--help") {
			opt.show_help = true;
		} else if (arg == "-c" || arg == "--compile") {
			opt.compile = true;
			opt.interpret = false;
		} else if (arg == "-b" || arg == "--bytecode") {
			opt.compile = true;
			opt.interpret = false;
		} else if (arg == "-i" || arg == "--interpret") {
			opt.interpret = true;
		} else if (arg == "-o" || arg == "--output") {
			if (i + 1 >= argc) {
				std::cerr << "Error: -o/--output requires an argument." << std::endl;
				return false;
			}
			opt.output_file = argv[++i];
		} else if (arg == "--emit-cpp") {
			opt.emit_cpp = true;
			opt.compile = false;
			opt.interpret = false;
		} else if (arg == "--shared" || arg == "--static") {
			// 兼容别名：--shared == --compile-runtime=shared（模块形态不变，
			// 仍默认 kStatic）；--static == --compile-runtime=static。
			const bool is_shared = (arg == "--shared");
			if (opt.compile_runtime_set || opt.compile_modules_set ||
			    !opt.module_overrides.empty()) {
				std::cerr << "Error: " << arg
				          << "（旧别名）不能与 --compile-modules / "
				             "--compile-module:<name> / --compile-runtime 混用，"
				             "请使用新的 --compile-* 参数。"
				          << std::endl;
				return false;
			}
			if (opt.legacy_link_set &&
			    opt.compile_runtime != (is_shared ? Pycp::AOT::LinkMode::kShared
			                                      : Pycp::AOT::LinkMode::kStatic)) {
				std::cerr << "Error: --static and --shared are mutually exclusive."
				          << std::endl;
				return false;
			}
			opt.compile_runtime =
				is_shared ? Pycp::AOT::LinkMode::kShared
				          : Pycp::AOT::LinkMode::kStatic;
			opt.legacy_link_set = true;
		} else if (starts_with(arg, "--compile-runtime=")) {
			if (opt.legacy_link_set) {
				std::cerr << "Error: --compile-runtime 不能与 --shared/--static "
				             "（旧别名）混用。" << std::endl;
				return false;
			}
			bool ok = false;
			const std::string v = arg.substr(std::string("--compile-runtime=").size());
			if (!parse_shared_static(v, &ok)) {
				std::cerr << "Error: --compile-runtime 只接受 shared 或 static（得到 '"
				          << v << "'）。" << std::endl;
				return false;
			}
			opt.compile_runtime =
				ok ? Pycp::AOT::LinkMode::kShared : Pycp::AOT::LinkMode::kStatic;
			opt.compile_runtime_set = true;
		} else if (starts_with(arg, "--compile-modules=")) {
			if (opt.legacy_link_set) {
				std::cerr << "Error: --compile-modules 不能与 --shared/--static "
				             "（旧别名）混用。" << std::endl;
				return false;
			}
			bool ok = false;
			const std::string v = arg.substr(std::string("--compile-modules=").size());
			if (!parse_shared_static(v, &ok)) {
				std::cerr << "Error: --compile-modules 只接受 shared 或 static（得到 '"
				          << v << "'）。" << std::endl;
				return false;
			}
			opt.compile_modules =
				ok ? Pycp::AOT::ModuleKind::kShared : Pycp::AOT::ModuleKind::kStatic;
			opt.compile_modules_set = true;
		} else if (starts_with(arg, "--compile-module:") && arg.find('=') != std::string::npos) {
			if (opt.legacy_link_set) {
				std::cerr << "Error: --compile-module:<name> 不能与 --shared/--static "
				             "（旧别名）混用。" << std::endl;
				return false;
			}
			const std::string body = arg.substr(std::string("--compile-module:").size());
			const std::size_t eq = body.find('=');
			const std::string name = body.substr(0, eq);
			const std::string v = body.substr(eq + 1);
			bool ok = false;
			if (name.empty() || !parse_shared_static(v, &ok)) {
				std::cerr << "Error: 用法 --compile-module:<name>=shared|static"
				             "（得到 '" << arg << "'）。" << std::endl;
				return false;
			}
			opt.module_overrides[name] =
				ok ? Pycp::AOT::ModuleKind::kShared : Pycp::AOT::ModuleKind::kStatic;
		} else if (arg == "--show-imports") {
			opt.show_imports = true;
		} else if (arg == "-d" || arg == "--dump") {
			opt.dump = true;
			opt.compile = false;
			opt.interpret = false;
		} else if (arg == "-p" || arg == "--preprocess") {
			opt.preprocess = true;
			opt.compile = false;
			opt.interpret = false;
		} else if (!arg.empty() && arg[0] == '-') {
			std::cerr << "Error: unknown option '" << arg << "'." << std::endl;
			return false;
		} else {
			// 位置参数：首个作为入口脚本（input_file），其余收集为脚本参数
			// 传给 pycp.argv（不含 pycp 可执行文件本身）。
			if (opt.input_file.empty()) {
				opt.input_file = arg;
			} else {
				opt.script_args.push_back(arg);
			}
		}
	}
	return true;
}

// 默认输出路径：basename + 目标后缀
std::string default_output(const std::string& input, const std::string& suffix) {
	std::string base = input;
	std::size_t slash = base.find_last_of("/\\");
	if (slash != std::string::npos) base = base.substr(slash + 1);
	std::size_t dot = base.find_last_of('.');
	if (dot != std::string::npos) base = base.substr(0, dot);
	return base + suffix;
}

// 预处理默认输出：输入同目录的 <basename>.pp.pycp（-p 无 -o 时使用）。
std::string preprocess_output(const std::string& input) {
	std::size_t slash = input.find_last_of("/\\");
	const std::string dir = (slash == std::string::npos)
		? std::string() : input.substr(0, slash + 1);
	return dir + default_output(input, Pycp::EXT_PP_PYCP);
}

// 读取整个文件为字节流
std::vector<uint8_t> read_file_bytes(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) throw Pycp::Exception("Cannot open file: " + path);
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
}

void write_file_bytes(const std::string& path, const std::vector<uint8_t>& data) {
	std::ofstream f(path, std::ios::binary);
	if (!f) throw Pycp::Exception("Cannot write file: " + path);
	f.write(reinterpret_cast<const char*>(data.data()),
	        static_cast<std::streamsize>(data.size()));
}

// =============================================================
// 主流程
// =============================================================

// 从源文件编译得到字节码 Module（解析 + Codegen）。
// 复用 ModuleLoader::compile_file 的单文件编译逻辑。
Pycp::BC::Module compile_source(const std::string& path) {
	return Pycp::ModuleLoader::compile_file(path);
}

// 计算文件 basename（去扩展名），用于从入口路径得到入口模块名。
std::string entry_module_name(const std::string& path) {
	std::size_t slash = path.find_last_of("/\\");
	std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
	std::size_t dot = base.find_last_of('.');
	if (dot != std::string::npos) base = base.substr(0, dot);
	return base;
}

// 初始化运行时，并注册 .pycp 源码模块编译器钩子。
//
// 钩子使 import 的第 2/3 层（可执行文件目录的 stdlib/、cwd 与脚本目录）
// 能够直接加载 .pycp 源码文件——backend 无法解析 .pycp（parser / Codegen
// 位于 frontend），故由宿主注入。
//   注：AOT 生成的独立程序不注册钩子，其 stdlib/ 仅识别原生动态库。
//   返回的 Module 为堆分配对象，所有权移交运行时（由 VM 加入
//   owned_modules_，在 ~VM 时清理 runtime_consts 并释放）。
void initialize_runtime() {
	Pycp::Initialize();
	Pycp::SetSourceModuleCompiler([](const char* path) -> Pycp::BC::Module* {
		return new Pycp::BC::Module(Pycp::ModuleLoader::compile_file(path));
	});
}

// 执行入口模块及其 import 依赖（构造带模块注册表的 VM 并 run）。
//   modules : ModuleLoader::load_all 产出的「模块名 -> Module」映射，
//             entry_name 为入口模块名（其 basename）。
int execute_program(std::map<std::string, Pycp::BC::Module>& modules,
                    const std::string& entry_name) {
	// 构建模块注册表（模块名 -> Module*），供 VM 的 LOAD_MODULE 使用。
	std::map<std::string, Pycp::BC::Module*> registry;
	for (auto& kv : modules) {
		registry[kv.first] = &kv.second;
	}

	Pycp::BC::Module* entry = &modules[entry_name];
	Pycp::BC::VM vm(entry, &registry, entry_name);
	Pycp::Object* result = vm.run();
	if (result != nullptr) {
		Pycp::Decref(result);
	}
	return 0;
}

// =============================================================
// 交互式 REPL
// =============================================================

// 判断一段已累积的输入是否尚未完整（需继续读取下一行）。
// 本语言块定界符为花括号 {}（func/if/repeat/class 等），故续行条件：
//   * 括号未闭合（([{ 计数大于 )]}），或
//   * 最后一行以 '{' 结尾（块头未闭合，等待块体）。
// 忽略字符串内的括号与花括号（基于简单转义处理）。
bool repl_needs_continuation(const std::string& buf) {
	int depth = 0;
	bool in_string = false;
	char quote = 0;
	for (std::size_t i = 0; i < buf.size(); ++i) {
		char c = buf[i];
		if (in_string) {
			if (c == '\\') { ++i; continue; } // 跳过转义字符
			if (c == quote) in_string = false;
		} else {
			if (c == '"' || c == '\'') { in_string = true; quote = c; }
			else if (c == '(' || c == '[' || c == '{') depth++;
			else if (c == ')' || c == ']' || c == '}') depth--;
		}
	}
	if (depth > 0) return true;
	// 找最后的非空白字符
	std::size_t end = buf.size();
	while (end > 0) {
		char c = buf[end - 1];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r') --end;
		else break;
	}
	if (end == 0) return false;
	return buf[end - 1] == '{';
}

// 将对象以可读字符串形式输出到控制台（复用 __string__，String 取 C++ 值）。
void repl_print_value(Pycp::Object* obj) {
	if (obj == nullptr) return;
	Pycp::Object* so = obj->__string__();
	if (so != nullptr && so->is_type("String")) {
		std::cout << static_cast<Pycp::String*>(so)->get_value() << std::endl;
	} else {
		std::cout << "<unprintable>" << std::endl;
	}
	if (so != nullptr) Pycp::Decref(so);
}

void run_repl() {
	initialize_runtime();

	// REPL 使用无入口模块的 VM，全局命名空间独立创建（globals 在 VM 内）。
	Pycp::BC::VM vm(nullptr);
	auto* globals = vm.get_globals();
	using GlobalsMap = std::unordered_map<std::string, Pycp::Object*>;

	std::cout << "Pycp " << Pycp::PYCP_VERSION << " interactive mode. Press Ctrl-D to leave.\n";
	std::cout.flush();

	std::string buffer;
	bool continuation = false;
	// 会话级行号计数：下一条物理输入行将显示的行号（从 1 开始，每读入一行 +1，
	// 含空行、续行、编译失败的行），使 REPL 错误行号在整个会话中连续递增，
	// 对齐 Python 交互模式的计数方式。
	int next_line = 1;

	while (true) {
		std::string line;
		if (linenoise::Readline(continuation ? "... " : ">>> ", line)) {
			// Ctrl-D / Ctrl-C（quit）：退出 REPL，退出码 0。
			std::cout << std::endl;
			break;
		}

		// 历史记录：每读入一行（不含换行）即单独加入历史，供上箭头逐行
		// 回退。不可把含换行的完整多行 buffer 整体入历史——cpp-linenoise 对
		// 含换行的历史项在上箭头调出时会折叠为 "[... N pasted lines ...]"
		// 占位符。空白行（主提示符直接回车）不产生历史项。
		{
			bool line_blank = true;
			for (char c : line) {
				if (c != ' ' && c != '\t' && c != '\r') {
					line_blank = false;
					break;
				}
			}
			if (!line_blank) {
				linenoise::AddHistory(line.c_str());
			}
		}

		buffer += line;
		buffer += '\n';
		++next_line; // 每个物理输入行（含空行/续行/失败行）都推进会话行号。

		// 整段 buffer 全为空白（空格/制表符/换行/回车）时安全忽略，
		// 不进入编译，不打印错误，回到主提示符。覆盖主提示符空行与
		// 「续行中连续空行导致整段仍全空白」的边界情况。
		{
			bool all_blank = true;
			for (char c : buffer) {
				if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
					all_blank = false;
					break;
				}
			}
			if (all_blank) {
				buffer.clear();
				continuation = false;
				continue;
			}
		}

		// 先判断是否需要续行（不调用 parse，避免不完整输入污染解析器全局状态）。
		// 仅当输入「可能已完整」时才编译一次，与文件模式等价。
		if (repl_needs_continuation(buffer)) {
			continuation = true;
			continue; // 保留 buffer，继续读取下一行
		}

		// 尝试编译（此时 buffer 通常已完整：括号/花括号平衡且不以 '{' 结尾）。
		// 采用单语句解析 ABI：每次只编译当前完整 buffer 这一条语句单元，
		// 不携带历史行——实现「逐条独立解析执行」，历史语句不再重编译。
		// 注意：compile_statement 返回的对象生命周期由 VM::exec_module 接管
		//（VM 在析构时统一释放），故此处用裸指针，切勿 delete。
		Pycp::BC::Module* mod = nullptr;
		try {
			// 该输入块首行对应的会话行号 = 已累计的下一条行号 - buffer 内行数。
			int first_line = next_line -
				static_cast<int>(std::count(buffer.begin(), buffer.end(), '\n'));
			mod = new Pycp::BC::Module(
				Pycp::ModuleLoader::compile_statement(
					buffer, Pycp::REPL_SOURCE_NAME, first_line));
		} catch (Pycp::Exception& e) {
			// 编译期异常分两类：
			//   1) 词法/语法错误：lexer/parser 已直接打印 File/line/msg，
			//      抛出的是空消息异常，format() 为空，故此处不重复打印；
			//   2) 解析期语义校验（如形参默认值顺序错误）已自带位置输出，
			//      同样以空消息异常形式由 ModuleLoader 抛出，也不重复打印；
			//   3) Codegen 抛出的带 file/line/msg 的异常必须在此显示，否则
			//      会被静默吞掉（表现为函数未定义且无任何提示）。
			// 统一按 format() 非空才打印，与上述两类天然兼容。
			std::string msg = e.format();
			if (!msg.empty()) std::cerr << msg << std::endl;
			buffer.clear();
			continuation = false;
			continue;
		} catch (const std::exception& e) {
			std::cerr << "Error: " << e.what() << std::endl;
			buffer.clear();
			continuation = false;
			continue;
		}

		// 编译成功：在执行前对全局命名空间做快照（用于失败回滚）。
		GlobalsMap snapshot = *globals;
		for (auto& kv : snapshot) Pycp::Incref(kv.second);

		try {
			Pycp::Object* res = vm.exec_module(mod);
			if (res != nullptr && res->type_name() != "None") {
				repl_print_value(res);
			}
			if (res != nullptr) Pycp::Decref(res);
			// 成功路径释放快照持有的引用（仅成功时执行，避免与 catch 重复 Decref）
			for (auto& kv : snapshot) Pycp::Decref(kv.second);
		} catch (Pycp::Exception& e) {
			// 回退该行产生的全部全局副作用，仅显示错误，不退出。
			// 严格配对引用计数：
			//  1) 释放错误行【新定义】的变量（快照中不存在的键）；
			//  2) 释放错误行【覆盖】的既有变量（当前值 != 快照值）；
			//  3) 用快照整体替换 globals（拷贝，快照值进入 globals）；
			//  4) Decref 快照持有（抵消进入本行时的 Incref）。
			// 注意：未变动的既有变量不能在 1/2 中 Decref，否则会多减一次
			// 导致悬垂，下一行访问即崩溃。
			for (auto& kv : *globals) {
				if (snapshot.find(kv.first) == snapshot.end())
					Pycp::Decref(kv.second); // 错误行新定义的变量
			}
			for (auto& kv : snapshot) {
				auto it = globals->find(kv.first);
				if (it != globals->end() && it->second != kv.second)
					Pycp::Decref(it->second); // 错误行覆盖的既有变量
			}
			*globals = snapshot;
			for (auto& kv : snapshot) Pycp::Decref(kv.second);
			std::string msg = e.format();
			if (!msg.empty()) std::cerr << msg << std::endl;
		} catch (const std::exception& e) {
			for (auto& kv : *globals) {
				if (snapshot.find(kv.first) == snapshot.end())
					Pycp::Decref(kv.second);
			}
			for (auto& kv : snapshot) {
				auto it = globals->find(kv.first);
				if (it != globals->end() && it->second != kv.second)
					Pycp::Decref(it->second);
			}
			*globals = snapshot;
			for (auto& kv : snapshot) Pycp::Decref(kv.second);
			std::cerr << "Error: " << e.what() << std::endl;
		}

		// 历史已在每行读入时逐条记录（见上），不再把含换行的多行 buffer
		// 整体入历史，避免上箭头回退时被 linenoise 折叠为占位符。

		buffer.clear();
		continuation = false;
	}

	Pycp::Finalize();
}

} // anonymous namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
	// 将控制台输出代码页设为 UTF-8，保证后续输出（含中文报错信息）在
	// UTF-8 终端正确显示；必须与终端代码页（如 chcp 65001）配合。
	SetConsoleOutputCP(CP_UTF8);
#endif
	Options opt;
	if (!parse_args(argc, argv, opt)) {
		print_help(argv[0]);
		return 2;
	}
	if (opt.show_help) {
		print_help(argv[0]);
		return 0;
	}
	if (opt.input_file.empty()) {
		// 无参数启动：进入交互式 REPL（像 Python 解释器）。
		run_repl();
		return 0;
	}
	// --compile-* 与旧别名 --shared/--static 只影响 AOT 生成的 CMake 项目；
	// --show-imports 打印编译期 import 解析清单。它们对解释执行、字节码
	// 编译、dump、预处理都无意义。静默忽略会让用户误以为生效。
	if ((opt.compile_modules_set || opt.compile_runtime_set ||
	     opt.legacy_link_set || !opt.module_overrides.empty() ||
	     opt.show_imports) &&
	    !opt.emit_cpp) {
		std::cerr
			<< "Error: --compile-modules / --compile-module:<name> / "
			   "--compile-runtime / --shared / --static / --show-imports "
			   "can only be used with --emit-cpp." << std::endl;
		return 2;
	}

	int ret = 0;
	try {
		initialize_runtime();

		if (opt.preprocess) {
			// 预处理查看：仅支持 .pycp 源码文本（.cpycp 字节码无法预处理）。
			if (has_suffix(opt.input_file, Pycp::EXT_CPYCP)) {
				std::cerr << "Error: -p/--preprocess only works on .pycp source files "
				             "(got " << opt.input_file << ")." << std::endl;
				ret = 1;
			} else {
				std::ifstream f(opt.input_file);
				if (!f) throw Pycp::Exception("Cannot open file: " + opt.input_file);
				std::string text((std::istreambuf_iterator<char>(f)),
				                 std::istreambuf_iterator<char>());
				f.close();

				std::string processed;
				if (!Pycp::Preprocessor::process(text, opt.input_file, processed)) {
					ret = 1; // 错误已按两行格式输出
				} else {
					// 默认写到输入同目录的 <basename>.pp.pycp（-o 可覆盖），
					const std::string out = opt.output_file.empty()
						? preprocess_output(opt.input_file)
						: opt.output_file;
					std::ofstream of(out, std::ios::binary);
					if (!of) throw Pycp::Exception("Cannot write file: " + out);
					of << processed;
					std::cout << "Preprocessed source: " << out << std::endl;
				}
			}
		}
		else if (opt.dump) {
			// 字节码查看：.pycp（解析+编译）或 .cpycp（反序列化）后输出详情。
			// 加载逻辑（按后缀获取 Module）保留在前端；具体的格式化打印
			// 由后端 Pycp::BC::DumpModule 实现。
			Pycp::BC::Module module;
			if (has_suffix(opt.input_file, Pycp::EXT_CPYCP)) {
				std::vector<uint8_t> bytes = read_file_bytes(opt.input_file);
				module = Pycp::BC::Deserialize(bytes.data(), bytes.size());
			} else {
				module = compile_source(opt.input_file);
			}
			Pycp::BC::DumpModule(module);
		}
		else if (opt.emit_cpp) {
			// AOT 仅支持 .pycp 源码；.cpycp 是已编译字节码，无法再转译。
			if (has_suffix(opt.input_file, Pycp::EXT_CPYCP)) {
				std::cerr << "Error: --emit-cpp only works on .pycp source files "
				          << "(got " << opt.input_file << ")." << std::endl;
				ret = 2;
			}
			else {
			// AOT：收集入口与全部 import 依赖（含解析清单），生成可直接编译的
			// CMake 项目文件夹。编排层（PycpAotProject）负责建目录、写 .gen.cpp、
			// 形态决策（ModulePlan）与渲染构建脚本。
			//   -o <dir> 指定整个项目目录（默认 ./<入口名>/）。
			std::vector<Pycp::ImportResolution> resolutions;
			std::map<std::string, Pycp::BC::Module> modules =
				Pycp::ModuleLoader::load_all(opt.input_file, &resolutions);
			std::string entry_name = entry_module_name(opt.input_file);

			// 未解析 import 的提示：无法 resolve 到 .pycp 的依赖会被转译产物
			// 当作「运行期加载的动态库」处理；若本意是静态链接的源码模块，
			// 说明拼写有误或源码未放置在入口目录/cwd。
			// 内置扩展（io/Pycp/classtools）本就以 stdlib/ 动态库形式加载，
			// 不在此列，避免对正常用法刷警告。
			std::vector<std::string> builtin;
			{
				Pycp::AOT::SdkInfo sdk = Pycp::AOT::LocateSdk();
				builtin = sdk.builtin_modules;
			}
			for (const auto& r : resolutions) {
				if (r.kind != Pycp::ImportKind::kUnresolved) continue;
				const bool is_builtin =
					std::find(builtin.begin(), builtin.end(), r.name) !=
					builtin.end();
				if (is_builtin) continue;
				std::cerr << "warning: import '" << r.name
				          << "' 在入口目录与 cwd 未找到 " << r.name
				          << Pycp::EXT_PYCP << " 源码；转译产物将把它作为运行期"
				          << "加载的动态库解析（若它应是编译进主程序的模块，请检查"
				          << "拼写或源码位置）。\n";
			}
			if (opt.show_imports) {
				std::cout << "Import resolution (" << resolutions.size() << "):\n";
				for (const auto& r : resolutions) {
					const char* k = (r.kind == Pycp::ImportKind::kTranslated)
						? "translated"
						: (r.kind == Pycp::ImportKind::kExternal) ? "external"
						                                          : "unresolved";
					std::cout << "  " << k << "  " << r.name;
					if (!r.path.empty()) std::cout << "  <- " << r.path;
					std::cout << "\n";
				}
			}

			// 输出目录：-o 指定则用之，否则默认 ./<入口名>/（位于当前工作目录）。
			std::string out_dir = opt.output_file.empty()
				? entry_name
				: opt.output_file;

			Pycp::AOT::AotProjectOptions aopt;
			aopt.default_module_kind = opt.compile_modules;
			aopt.runtime_link = opt.compile_runtime;
			aopt.overrides = opt.module_overrides;

			std::vector<std::string> written;
			std::string emsg;
			Pycp::AOT::AotPlanReport report;
			if (!Pycp::AOT::EmitProject(modules, entry_name, opt.input_file,
			                            out_dir, aopt, {}, &written, &emsg,
			                            &report)) {
				throw Pycp::Exception(emsg);
			}

			// 实际形态统计（可能已被「≥2 宿主强制提升」改写，故取自回执
			// 而非命令行参数）。
			std::size_t n_static = 0;
			std::size_t n_shared = 0;
			for (const auto& kv : report.modules.kinds) {
				if (kv.second == Pycp::AOT::ModuleKind::kShared) ++n_shared;
				else ++n_static;
			}

			if (opt.show_imports) {
				// 逐模块形态决策表：打印最终形态与决策来源（全局默认 /
				// 按模块覆盖 / 强制提升及其覆盖的用户指定），使按模块覆盖
				// 不再「看起来没生效」。
				std::cout << "Module kinds (" << report.modules.kinds.size()
				          << "):\n";
				if (report.modules.kinds.empty()) {
					std::cout << "  (no translated dependency module)\n";
				}
				for (const auto& kv : report.modules.kinds) {
					auto rit = report.modules.reasons.find(kv.first);
					std::cout << "  "
					          << (kv.second == Pycp::AOT::ModuleKind::kShared
					                  ? "shared"
					                  : "static")
					          << "  " << kv.first;
					if (rit != report.modules.reasons.end() &&
					    !rit->second.empty()) {
						std::cout << "  <- " << rit->second;
					}
					std::cout << "\n";
				}
				std::cout << "Runtime (1):\n"
				          << "  "
				          << (report.runtime_link ==
				                      Pycp::AOT::LinkMode::kStatic
				                  ? "static"
				                  : "shared")
				          << "  libPycpRuntime  <- --compile-runtime\n";
				std::cout << "Builtin extensions (" << report.builtin_static.size()
				          << " static, " << report.builtin_shared.size()
				          << " shared):\n";
				for (const std::string& b : report.builtin_static) {
					std::cout << "  static  " << b
					          << "  <- linked from SDK (libPycpExt_" << b
					          << ".a)\n";
				}
				for (const std::string& b : report.builtin_shared) {
					std::cout << "  shared  " << b
					          << "  <- loaded from stdlib/ at runtime\n";
				}
			}

			// 覆盖与全局默认形态相同时是空操作（正是「指定 b=static 后
			// 所有模块仍是静态」的成因）：给出「只让个别模块静态」的写法。
			for (const auto& kv : opt.module_overrides) {
				if (kv.second != opt.compile_modules) continue;
				const bool is_static =
					(kv.second == Pycp::AOT::ModuleKind::kStatic);
				std::cerr << "note: --compile-module:" << kv.first << "="
				          << (is_static ? "static" : "shared")
				          << " 与全局默认形态相同，该覆盖为空操作；若要「只有 "
				          << kv.first << (is_static ? " 静态" : " 动态")
				          << "、其余相反」，请追加 --compile-modules="
				          << (is_static ? "shared" : "static") << "\n";
			}

			std::cout << "Generated AOT project in: " << out_dir
			          << "  [runtime: "
			          << (opt.compile_runtime == Pycp::AOT::LinkMode::kStatic
			                  ? "static"
			                  : "shared")
			          << ", modules default: "
			          << (opt.compile_modules == Pycp::AOT::ModuleKind::kStatic
			                  ? "static"
			                  : "shared")
			          << ", modules: " << n_static << " static + " << n_shared
			          << " shared]\n";
			for (const std::string& w : written) {
				std::cout << "  " << w << std::endl;
			}
			std::cout << "Build it with:\n"
			          << "  cd " << out_dir << " && cmake -S . -B build && cmake --build build\n";
			// 自包含判定改用实际形态：runtime 静态且无任何动态模块/内置扩展。
			if (report.runtime_link == Pycp::AOT::LinkMode::kStatic &&
			    n_shared == 0 && report.builtin_shared.empty()) {
				std::cout << "Self-contained executable: " << out_dir
				          << "/build/" << entry_name
				          << " (no stdlib/ or runtime DLL needed)\n";
			}
			}
		}
		else if (opt.compile) {
			// 编译模式：.pycp -> .cpycp
			Pycp::BC::Module module = compile_source(opt.input_file);
			std::vector<uint8_t> bytes = Pycp::BC::Serialize(module);
			std::string out = opt.output_file.empty()
				? default_output(opt.input_file, Pycp::EXT_CPYCP)
				: opt.output_file;
			write_file_bytes(out, bytes);
			std::cout << "Compiled bytecode: " << out
			          << " (" << bytes.size() << " bytes)" << std::endl;
		}
		else {
			// 解释执行：.pycp（递归收集 import 依赖后执行）或
			//          .cpycp（单文件反序列化执行；import 依赖需随源一起编译）。
			// 注入命令行参数到运行时，使 pycp.argv == [脚本名, 脚本参数...]：
			// 脚本名即 input_file（不含 pycp 可执行文件本身），对齐用户预期。
			// 须在 import pycp 触发模块加载前设置（模块构造时读 GetArgv 快照）。
			std::vector<std::string> cli_argv;
			cli_argv.reserve(1 + opt.script_args.size());
			cli_argv.push_back(opt.input_file);
			cli_argv.insert(cli_argv.end(), opt.script_args.begin(),
			                opt.script_args.end());
			Pycp::SetArgv(cli_argv);
			if (has_suffix(opt.input_file, Pycp::EXT_CPYCP)) {
				std::vector<uint8_t> bytes = read_file_bytes(opt.input_file);
				Pycp::BC::Module module = Pycp::BC::Deserialize(bytes.data(), bytes.size());
				Pycp::BC::VM vm(&module); // 无注册表：.cpycp 内 import 会在运行时报 ImportError
				Pycp::Object* result = vm.run();
				if (result != nullptr) Pycp::Decref(result);
			} else {
				std::map<std::string, Pycp::BC::Module> modules =
					Pycp::ModuleLoader::load_all(opt.input_file);
				execute_program(modules, entry_module_name(opt.input_file));
			}
		}

		Pycp::Finalize();
	}
	catch (const Pycp::Exception& e) {
		// 空描述表示错误已由词法/语法层输出，此处仅设置失败码。
		std::string out = e.format();
		if (!out.empty()) {
			std::cerr << out << std::endl;
		}
		ret = 1;
	}
	catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		ret = 1;
	}

	return ret;
}
