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
//   -m, --module      把位置参数当作【模块文件夹】执行（详见下方）
//   -d, --dump        查看字节码内容（常量池/符号表/代码对象/指令及行号）
//
// AOT（.pycp 翻译为可编译 C++ 项目）由标准库模块 aot 承担：
//   pycp -m aot <entry.pycp | module folder> [out_dir] [--compile-* ...]
//   -m 之后的参数原样交给该包的 main(argv)；用法见 `pycp -m aot --help`。
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

#include "parser/PycpAstNode.hpp"
#include "codegen/PycpCodegen.hpp"
#include "loader/PycpModuleLoader.hpp"
#include "loader/PycpSourceBridge.hpp"  // 向运行时注册源码字符串编译/执行钩子
#include "parser/preprocessor/PycpPreprocessor.hpp"

#include "abi/Pycp.hpp"          // 运行时（Object/GC/ABI/Manager）
#include "bytecode/PycpBytecode.hpp"  // 字节码格式 / 序列化
#include "bytecode/PycpBytecodeDump.hpp" // 字节码查看（dump）接口
#include "vm/PycpBytecodeVM.hpp"// VM 执行
#include "abi/PycpNativeExt.hpp" // 原生扩展加载 / 源码模块编译器钩子
#include "object/PycpString.hpp"    // REPL 回显：String::get_value()
#include "object/PycpFixedList.hpp" // main(argv) 的实参容器
#include "object/PycpException.hpp"
#include "object/PycpConfig.hpp"    // 集中管理的常量（扩展名/输出命名/版本等）

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
	// 文件本身。-m 之后的参数也会原样落入这里，转交包的 main(argv)。

	std::vector<std::string> script_args;
	std::string output_file;
	bool compile = false;    // -c / -b
	bool interpret = true;   // -i（默认解释执行）
	bool dump = false;       // -d / --dump
	bool preprocess = false; // -p / --preprocess
	bool show_help = false;
	// -m / --module：把位置参数当作【模块文件夹】（目录 + package.mpycp 清单）
	// 执行。默认角色为「程序」（解释执行语义）。
	bool module_mode = false;
};

void print_help(const char* prog) {
	std::cout
		<< "Pycp - Python-like language compiler & interpreter\n\n"
		<< "Usage:\n"
		<< "  " << prog << " [options] <input_file>\n\n"
		<< "Options:\n"
		<< "  -m, --module      Treat <input_file> as a module folder (a directory\n"
		<< "                    containing package.mpycp); runs it as a program by\n"
		<< "                    default (its manifest must define func main(argv)).\n"
		<< "                    Arguments after the folder are passed verbatim to\n"
		<< "                    the package's main(argv), not parsed by pycp itself.\n"
		<< "  -h, --help        Show this help message\n"
		<< "  -i, --interpret   Interpret & execute (default); accepts .pycp or .cpycp\n"
		<< "  -c, --compile     Compile <input_file> (.pycp) to bytecode (.cpycp)\n"
		<< "  -b, --bytecode    Generate .cpycp bytecode file (alias of -c)\n"
		<< "  -o, --output <f>  Output path/dir (with -c/-b: .cpycp file; with\n"
		<< "                    -p: .pp.pycp file)\n"
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
		<< "  * AOT (.pycp -> compilable C++ project) is provided by the aot module:\n"
		<< "    `pycp -m aot <entry.pycp | module folder> [out_dir] [--compile-* ...]`;\n"
		<< "    see `pycp -m aot --help` for options and examples.\n\n"
		<< "Examples:\n"
		<< "  " << prog << " hello.pycp              # compile & run\n"
		<< "  " << prog << " hello.cpycp             # run compiled bytecode directly\n"
		<< "  " << prog << " -c hello.pycp -o hello.cpycp\n"
		<< "  " << prog << " -m aot hello.pycp        # AOT: -> hello/ project (CMake)\n";
}

bool has_suffix(const std::string& s, const std::string& suffix) {
	if (s.size() < suffix.size()) return false;
	return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
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
		} else if (arg == "-d" || arg == "--dump") {
			opt.dump = true;
			opt.compile = false;
			opt.interpret = false;
		} else if (arg == "-p" || arg == "--preprocess") {
			opt.preprocess = true;
			opt.compile = false;
			opt.interpret = false;
		} else if (arg == "-m" || arg == "--module") {
			// 模块文件夹模式：位置参数指向目录（内含 package.mpycp 清单），
			// 以程序角色运行。-m 之后的 argv 原样交给包的 main(argv)，
			// 不再被宿主当作自己的选项解析（如 -o / --compile-*）。
			opt.module_mode = true;
			if (i + 1 >= argc) {
				std::cerr << "Error: -m/--module expects a module folder "
				             "(a directory containing "
				          << Pycp::MODULE_MANIFEST_FILENAME << ")." << std::endl;
				return false;
			}
			opt.input_file = argv[++i];          // 模块名（包名或路径）
			while (++i < argc) {                 // 其余参数原样交给包的 main
				opt.script_args.push_back(argv[i]);
			}
			break;
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

// 计算文件 basename（去扩展名），用于从入口路径得到入口模块名（定义见下）。
std::string entry_module_name(const std::string& path);

// =============================================================
// 模块文件夹（package）入口解析
// =============================================================
// 目录（内含 package.mpycp 清单）与清单文件本身都解析为「包入口」：
//   entry_file : 实际要编译执行的源文件（清单路径）
//   entry_name : 入口模块名（包取包名，普通文件取 basename）
struct EntryResolution {
	std::string entry_file;
	std::string entry_name;
	bool is_package = false;   // 入口是否为模块文件夹
	// 命中包所在目录（报错文案定位用）；字面文件入口为空。
	std::string found_dir;
	// `-m <name>` 按名查找未命中：tried_paths 为尝试过的候选清单路径。
	bool name_lookup_miss = false;
	std::vector<std::string> tried_paths;
};

// 去掉路径结尾的目录分隔符（便于取目录自身的名字）。
std::string trim_trailing_separators(const std::string& path) {
	std::string p = path;
	while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
	return p;
}

// 按名查找模块文件夹（包）：候选目录顺序 cwd -> exe 同级 stdlib/。
// 点号名映射为目录层级（a.b -> a/b），与运行期的模块名 -> 路径规则一致。
//   name       : 包名（可含点号，如 a.b）
//   candidates : 非空时回填全部尝试过的候选清单路径（供报错展示）
// 返回命中的清单路径；空字符串表示未找到。
std::string find_package_manifest(const std::string& name,
                                  std::vector<std::string>* candidates) {
	namespace fs = std::filesystem;
	std::string rel = name;
	for (char& c : rel) {
		if (c == Pycp::MODULE_NAME_SEPARATOR) c = '/';
	}
	const std::string tail = rel + "/" + Pycp::MODULE_MANIFEST_FILENAME;

	std::vector<std::string> dirs;
	dirs.push_back(".");   // 当前工作目录（本地优先，可覆盖 stdlib）
	const std::string& stdlib = Pycp::GetStdlibDir();
	if (!stdlib.empty()) dirs.push_back(stdlib);

	std::error_code ec;
	for (const std::string& dir : dirs) {
		const std::string path =
			(dir == ".") ? ("./" + tail) : (dir + "/" + tail);
		if (candidates != nullptr) candidates->push_back(path);
		if (fs::is_regular_file(path, ec)) return path;
	}
	return "";
}

EntryResolution resolve_entry_path(const std::string& input,
                                   bool allow_name_lookup) {
	EntryResolution r;
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::is_directory(input, ec)) {
		const std::string dir = trim_trailing_separators(input);
		r.entry_file = (fs::path(dir) / Pycp::MODULE_MANIFEST_FILENAME).string();
		r.entry_name = fs::path(dir).filename().string();
		r.found_dir = dir;
		r.is_package = true;
		return r;
	}
	const std::string base = fs::path(input).filename().string();
	if (base == Pycp::MODULE_MANIFEST_FILENAME) {
		// 直接指向清单：包名取所在目录名。
		const std::string dir = trim_trailing_separators(input);
		fs::path parent = fs::path(dir).parent_path();
		r.entry_file = dir;
		r.entry_name = parent.empty() ? fs::path(dir).filename().string()
		                              : parent.filename().string();
		r.found_dir = parent.empty() ? std::string(".") : parent.string();
		r.is_package = true;
		return r;
	}
	if (!fs::exists(input, ec) && allow_name_lookup) {
		// `-m <name>`：字面路径不存在时按名查找模块文件夹。
		// 仅 -m 启用（普通用法仍要求写路径，行为零变更）。
		std::vector<std::string> tried;
		const std::string manifest = find_package_manifest(input, &tried);
		if (!manifest.empty()) {
			r.entry_file = manifest;
			// entry_name 用【用户原始名】（点号保留）：包内子模块的限定名
			// （pkg.sub）与 package_names / AOT 目录布局都以此为准。
			r.entry_name = input;
			r.found_dir = fs::path(manifest).parent_path().string();
			r.is_package = true;
			r.name_lookup_miss = false;
			r.tried_paths = tried;
			return r;
		}
		r.tried_paths = tried;
		r.name_lookup_miss = true;
	}
	r.entry_file = input;
	r.entry_name = entry_module_name(input);
	r.is_package = false;
	return r;
}

// 校验 `-m/--module` 的入口解析结果，失败时打印可操作错误并返回 false。
//   ctx_hint : 额外的上下文提示（可为 nullptr；解释执行路径恒为 nullptr）。
bool validate_module_entry(const EntryResolution& er, const std::string& input,
                           const char* ctx_hint) {
	auto list_tried = [&]() {
		if (er.tried_paths.empty()) return;
		std::cerr << "  Tried:\n";
		for (const std::string& p : er.tried_paths) {
			std::cerr << "    " << p << "\n";
		}
	};
	if (!er.is_package) {
		std::cerr << "Error: -m/--module expects a module folder (a directory "
		             "containing " << Pycp::MODULE_MANIFEST_FILENAME
		          << "): '" << input << "' not found.\n";
		list_tried();
		std::cerr << "  Hint: -m accepts only module folders; for a plain module "
		             "pass a file path (e.g. " << "pycp foo.pycp" << ").\n";
		if (ctx_hint != nullptr) std::cerr << "  Hint: " << ctx_hint << "\n";
		return false;
	}
	std::error_code ec;
	if (!std::filesystem::is_regular_file(er.entry_file, ec)) {
		std::cerr << "Error: module folder '" << er.found_dir << "' has no "
		          << Pycp::MODULE_MANIFEST_FILENAME << " manifest.\n";
		if (ctx_hint != nullptr) std::cerr << "  Hint: " << ctx_hint << "\n";
		return false;
	}
	return true;
}

// 计算文件 basename（去扩展名），用于从入口路径得到入口模块名。
std::string entry_module_name(const std::string& path) {
	std::size_t slash = path.find_last_of("/\\");
	std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
	std::size_t dot = base.find_last_of('.');
	if (dot != std::string::npos) base = base.substr(0, dot);
	return base;
}

// 初始化运行时，并注册宿主提供的源码钩子。
//
// ① 路径编译钩子：使 import 的第 2/3 层（可执行文件目录的 stdlib/、cwd 与
//    脚本目录）能够直接加载 .pycp 源码文件——运行时无法解析 .pycp（parser /
//    codegen 位于 frontend），故由宿主注入。
// ② 源码字符串钩子（RegisterSourceHooks）：供 stdlib 模块 compile / codebyte /
//    ast 使用（宿主链接了前端，故由前端桥接实现并注册）。
//   注：AOT 生成的独立程序不注册任何钩子，其 stdlib/ 仅识别原生动态库。
//   返回的 Module 为堆分配对象，所有权移交运行时（由 VM 加入
//   owned_modules_，在 ~VM 时清理 runtime_consts 并释放）。
void initialize_runtime() {
	Pycp::Initialize();
	Pycp::SetSourceModuleCompiler([](const char* path) -> Pycp::BC::Module* {
		return new Pycp::BC::Module(Pycp::ModuleLoader::compile_file(path));
	});
	Pycp::RegisterSourceHooks();
}

// 执行入口模块及其 import 依赖（构造带模块注册表的 VM 并 run）。
//   modules       : ModuleLoader::load_all 产出的「模块名 -> Module」映射
//   entry_name    : 入口模块名（包为包名，普通文件为 basename）
//   package_names : 包对象名集合（供 VM 标记包模块）
//   module_mode   : 是否以 `-m`（程序角色）运行
//   cli_argv      : 传给程序入口 main(argv) 的参数（同 pycp.argv）
// 返回进程退出码：非程序角色恒为 0；程序角色取 main(argv) 返回值
// （经 __integer__ 宽松转换，None 转换为 0）。
int execute_program(std::map<std::string, Pycp::BC::Module>& modules,
                    const std::string& entry_name,
                    const std::set<std::string>& package_names,
                    bool module_mode,
                    const std::vector<std::string>& cli_argv,
                    const std::string& entry_dir = std::string()) {
	// 构建模块注册表（模块名 -> Module*），供 VM 的 LOAD_MODULE 使用。
	std::map<std::string, Pycp::BC::Module*> registry;
	for (auto& kv : modules) {
		registry[kv.first] = &kv.second;
	}

	Pycp::BC::Module* entry = &modules[entry_name];
	Pycp::BC::VM vm(entry, &registry, entry_name);
	vm.set_package_names(package_names);

	// 程序角色（`-m` 直接执行一个模块文件夹）：入口 __name__ 固定
	// "__main__" 且只读，角色默认 program（清单可用 as_library() 覆盖）。
	const bool as_program = module_mode;
	if (as_program) {
		Pycp::ResetPackageRole();
		Pycp::SetPackageRole(Pycp::PackageRole::kProgram, /*declared=*/false);
		vm.set_entry_name_main();
	}

	Pycp::Object* result = vm.run();
	if (result != nullptr) {
		Pycp::Decref(result);
	}

	if (!as_program) return 0;

	// ---- 角色校验 ----
	// 报错统一说明「这是库 / 没有程序入口，不可运行」，并给出两条出路：
	// 用 import 引用，或定义 main(argv) + as_program() 使其可运行。
	const std::string where =
		entry_dir.empty() ? std::string()
		                  : ("\n  Package dir: " + entry_dir);
	if (Pycp::GetPackageRole() == Pycp::PackageRole::kLibrary) {
		throw Pycp::RuntimeError(
			"'" + entry_name + "' is a library (the manifest calls "
			"moduletools.as_library()) and cannot be run as a program." + where +
			"\n  - Use it as a library: import " + entry_name +
			"\n  - Make it runnable: define func main(argv) in " +
			Pycp::MODULE_MANIFEST_FILENAME +
			" and call moduletools.as_program().");
	}

	// ---- 调用程序入口 main(argv) ----
	auto* ns = vm.get_entry_module()->get_namespace();
	auto it = ns->find("main");
	if (it == ns->end() || it->second == nullptr ||
	    !it->second->is_type("Function")) {
		throw Pycp::RuntimeError(
			"'" + entry_name + "' has no program entry: the manifest defines no "
			"func main(argv), so it cannot be run as a program." + where +
			"\n  - Use it as a library: import " + entry_name +
			"\n  - Make it runnable: define func main(argv) in " +
			Pycp::MODULE_MANIFEST_FILENAME +
			" and call moduletools.as_program().");
	}

	// main 只接收【一个】argv（内容同 pycp.argv），故把命令行参数打包成
	// 一个 FixedList 再作为唯一实参传入。FixedList::New 接管元素所有权
	// （构造不 Incref），故元素用后只需释放容器本身。
	std::vector<Pycp::Object*> argv_objs;
	argv_objs.reserve(cli_argv.size());
	for (const std::string& a : cli_argv) {
		argv_objs.push_back(Pycp::String::FromCString(a.c_str())); // Owned
	}
	Pycp::Object* argv_list = Pycp::FixedList::New(argv_objs);      // Owned
	Pycp::Object* r = Pycp::Call(it->second, &argv_list, 1);
	Pycp::Decref(argv_list);

	long long code = 0;
	if (r != nullptr) {
		Pycp::Object* iv = r->__integer__();   // Owned；不支持转换时抛 TypeError
		if (iv != nullptr) {
			if (Pycp::Integer* in = dynamic_cast<Pycp::Integer*>(iv)) {
				code = in->get_value();
			}
			Pycp::Decref(iv);
		}
		Pycp::Decref(r);
	}
	return static_cast<int>(code);
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
			// 入口解析：模块文件夹（目录 / 清单 / `-m <name>` 按名查找）或
			// 普通源文件。
			const EntryResolution er =
				resolve_entry_path(opt.input_file, opt.module_mode);
			if (opt.module_mode &&
			    !validate_module_entry(er, opt.input_file, nullptr)) {
				ret = 2;
				Pycp::Finalize();
				return ret;
			}
			if (has_suffix(er.entry_file, Pycp::EXT_CPYCP)) {
				std::vector<uint8_t> bytes = read_file_bytes(er.entry_file);
				Pycp::BC::Module module = Pycp::BC::Deserialize(bytes.data(), bytes.size());
				Pycp::BC::VM vm(&module); // 无注册表：.cpycp 内 import 会在运行时报 ImportError
				Pycp::Object* result = vm.run();
				if (result != nullptr) Pycp::Decref(result);
			} else {
				std::set<std::string> package_names;
				std::map<std::string, Pycp::BC::Module> modules =
					Pycp::ModuleLoader::load_all(er.entry_file, nullptr, &package_names);
				ret = execute_program(modules, er.entry_name, package_names,
				                      opt.module_mode, cli_argv, er.found_dir);
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
