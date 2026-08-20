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
// 行为：
//   * 默认（无 -c/-b）执行"解释运行"：若输入为 .pycp 则 解析->编译->执行；
//     若输入为 .cpycp 则 反序列化->执行（无需重新解析）。
//   * -c/-b 仅编译输出 .cpycp，不执行。
//   * .cpycp 可直接执行，无需额外的解释器或依赖（依赖已静态/动态链接的
//     PycpRuntime，但不需要 Python 或其他外部解释器）。
// =============================================================

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "PycpAstNode.hpp"
#include "PycpCodegen.hpp"
#include "PycpAot.hpp"
#include "PycpModuleLoader.hpp"

#include "Pycp.hpp"          // 运行时（Object/GC/ABI/Manager）
#include "PycpBytecode.hpp"  // 字节码格式 / 序列化
#include "PycpBytecodeVM.hpp"// VM 执行
#include "PycpException.hpp"
#include "PycpConfig.hpp"    // 集中管理的常量（扩展名/输出命名/版本等）

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
	std::string output_file;
	bool compile = false;    // -c / -b
	bool interpret = true;   // -i（默认解释执行）
	bool emit_cpp = false;   // --emit-cpp
	bool dump = false;       // -d / --dump
	bool show_help = false;
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
		<< "      --emit-cpp    Translate .pycp to C++ source file(s)\n"
		<< "  -o, --output <f>  Output path (used with -c/-b/--emit-cpp)\n"
		<< "  -d, --dump        Dump bytecode (constant pool, symbols, code objects,\n"
		<< "                    instructions & line numbers) of .pycp or .cpycp\n\n"
		<< "Import & modules:\n"
		<< "  * import foo / import foo as bar  loads foo.pycp from the entry\n"
		<< "    file's directory and binds a module object in the current scope.\n"
		<< "  * --emit-cpp generates one .gen.cpp per imported file into a\n"
		<< "    subdirectory named after the entry file: the entry .pycp becomes\n"
		<< "    <name>/__pycp_main.gen.cpp (contains main()), each imported module\n"
		<< "    foo.pycp becomes <name>/foo.gen.cpp.\n\n"
		<< "Examples:\n"
		<< "  " << prog << " hello.pycp              # compile & run\n"
		<< "  " << prog << " hello.cpycp             # run compiled bytecode directly\n"
		<< "  " << prog << " -c hello.pycp -o hello.cpycp\n"
		<< "  " << prog << " --emit-cpp hello.pycp   # -> hello/__pycp_main.gen.cpp (+ deps)\n";
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
		} else if (arg == "--emit-cpp") {
			opt.emit_cpp = true;
			opt.compile = false;
			opt.interpret = false;
		} else if (arg == "-d" || arg == "--dump") {
			opt.dump = true;
			opt.compile = false;
			opt.interpret = false;
		} else if (!arg.empty() && arg[0] == '-') {
			std::cerr << "Error: unknown option '" << arg << "'." << std::endl;
			return false;
		} else {
			opt.input_file = arg;
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
// 字节码查看（dump）
// =============================================================

// 操作码 -> 可读名称
const char* op_name(Pycp::BC::Op op) {
	switch (op) {
		case Pycp::BC::Op::HALT:          return "HALT";
		case Pycp::BC::Op::LOAD_CONST:    return "LOAD_CONST";
		case Pycp::BC::Op::LOAD_VAR:      return "LOAD_VAR";
		case Pycp::BC::Op::STORE_VAR:     return "STORE_VAR";
		case Pycp::BC::Op::LOAD_NONE:     return "LOAD_NONE";
		case Pycp::BC::Op::POP_TOP:       return "POP_TOP";
		case Pycp::BC::Op::DUP_TOP:       return "DUP_TOP";
		case Pycp::BC::Op::BINARY_ADD:    return "BINARY_ADD";
		case Pycp::BC::Op::BINARY_SUB:    return "BINARY_SUB";
		case Pycp::BC::Op::BINARY_MUL:    return "BINARY_MUL";
		case Pycp::BC::Op::BINARY_DIV:    return "BINARY_DIV";
		case Pycp::BC::Op::BINARY_POW:    return "BINARY_POW";
		case Pycp::BC::Op::UNARY_NEG:     return "UNARY_NEG";
		case Pycp::BC::Op::COMPARE_OP:    return "COMPARE_OP";
		case Pycp::BC::Op::JUMP:          return "JUMP";
		case Pycp::BC::Op::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
		case Pycp::BC::Op::JUMP_IF_TRUE:  return "JUMP_IF_TRUE";
		case Pycp::BC::Op::MAKE_FUNCTION: return "MAKE_FUNCTION";
		case Pycp::BC::Op::CALL:          return "CALL";
		case Pycp::BC::Op::RETURN:        return "RETURN";
		case Pycp::BC::Op::RETURN_NONE:   return "RETURN_NONE";
		case Pycp::BC::Op::LOAD_MODULE:   return "LOAD_MODULE";
		case Pycp::BC::Op::GET_ATTR:      return "GET_ATTR";
		case Pycp::BC::Op::MAKE_CLASS:    return "MAKE_CLASS";
		case Pycp::BC::Op::LOAD_ATTR:     return "LOAD_ATTR";
		case Pycp::BC::Op::STORE_ATTR:    return "STORE_ATTR";
		default:                          return "UNKNOWN";
	}
}

// 比较子操作码 -> 可读名称
const char* cmp_op_name(uint8_t op) {
	switch (static_cast<Pycp::BC::CompareOp>(op)) {
		case Pycp::BC::CompareOp::LT: return "LT";
		case Pycp::BC::CompareOp::LE: return "LE";
		case Pycp::BC::CompareOp::EQ: return "EQ";
		case Pycp::BC::CompareOp::NE: return "NE";
		case Pycp::BC::CompareOp::GT: return "GT";
		case Pycp::BC::CompareOp::GE: return "GE";
		default:                      return "?";
	}
}

// 常量池条目 -> 可读文本
std::string const_text(const Pycp::BC::Constant& c) {
	switch (c.kind) {
		case Pycp::BC::ConstKind::INTEGER: return std::to_string(c.int_value);
		case Pycp::BC::ConstKind::STRING:  return "\"" + c.str_value + "\"";
		case Pycp::BC::ConstKind::NONE:    return "None";
		default:                           return "<unknown>";
	}
}

// 打印单个指令（带行号与操作数解释）
void dump_instruction(std::ostream& os, const Pycp::BC::Instruction& ins,
                      int lineno, const Pycp::BC::Module& module) {
	os << "  " << (lineno >= 0 ? std::to_string(lineno) : "?") << "  "
	   << op_name(ins.op);

	// 带操作数注释，便于阅读
	if (ins.op == Pycp::BC::Op::LOAD_CONST) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.const_pool.size()) {
			os << " " << ins.operand << "  # "
			   << const_text(module.const_pool[ins.operand]);
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.op == Pycp::BC::Op::LOAD_VAR ||
	           ins.op == Pycp::BC::Op::STORE_VAR) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.symtab.size()) {
			os << " " << ins.operand << "  # " << module.symtab[ins.operand];
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.op == Pycp::BC::Op::COMPARE_OP) {
		os << " " << ins.operand << "  # " << cmp_op_name(static_cast<uint8_t>(ins.operand));
	} else if (ins.op == Pycp::BC::Op::MAKE_FUNCTION) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.code_objects.size()) {
			os << " " << ins.operand << "  # " << module.code_objects[ins.operand].name;
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.op == Pycp::BC::Op::LOAD_MODULE) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.imports.size()) {
			os << " " << ins.operand << "  # import " << module.imports[ins.operand];
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.op == Pycp::BC::Op::GET_ATTR) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.symtab.size()) {
			os << " " << ins.operand << "  # ." << module.symtab[ins.operand];
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.operand != 0) {
		os << " " << ins.operand;
	}
	os << "\n";
}

// 查看字节码内容：常量池 / 符号表 / 代码对象（方法签名、字段、指令与行号）
void dump_module(const Pycp::BC::Module& module) {
	using namespace Pycp::BC;

	std::cout << "==============================================\n"
	          << "Pycp Bytecode Dump\n"
	          << "==============================================\n";

	// ---- 文件头 ----
	std::cout << "\n[Header]\n"
	          << "  format version : " << FORMAT_VERSION_MAJOR << "."
	          << FORMAT_VERSION_MINOR << "\n"
	          << "  source path    : " << module.source_path << "\n";

	// ---- 常量池 ----
	std::cout << "\n[Constant Pool] (" << module.const_pool.size() << " entries)\n";
	for (size_t i = 0; i < module.const_pool.size(); ++i) {
		std::cout << "  " << i << ": " << const_text(module.const_pool[i]) << "\n";
	}

	// ---- 符号表 ----
	std::cout << "\n[Symbol Table] (" << module.symtab.size() << " entries)\n";
	for (size_t i = 0; i < module.symtab.size(); ++i) {
		std::cout << "  " << i << ": " << module.symtab[i] << "\n";
	}

	// ---- 代码对象（含方法签名 / 字段 / 指令与行号）----
	std::cout << "\n[Code Objects] (" << module.code_objects.size() << ")\n";
	for (size_t ci = 0; ci < module.code_objects.size(); ++ci) {
		const CodeObject& co = module.code_objects[ci];
		std::cout << "\n--- CodeObject[" << ci << "] ---\n"
		          << "  name    : " << co.name << "\n"
		          << "  nparams : " << co.nparams << "\n"
		          << "  nlocals : " << co.nlocals << "\n";

		// 局部变量名表（字段信息）
		if (!co.names.empty()) {
			std::cout << "  locals  : ";
			for (size_t k = 0; k < co.names.size(); ++k) {
				if (k) std::cout << ", ";
				std::cout << co.names[k];
			}
			std::cout << "\n";
		}

		// 常量引用
		if (!co.const_refs.empty()) {
			std::cout << "  const_refs:";
			for (size_t k : co.const_refs) std::cout << " " << k;
			std::cout << "\n";
		}

		// 指令流（含行号）
		std::cout << "  code (" << co.code.size() << " instrs):\n";
		for (size_t k = 0; k < co.code.size(); ++k) {
			int lineno = (k < co.linenos.size()) ? co.linenos[k] : -1;
			dump_instruction(std::cout, co.code[k], lineno, module);
		}
	}
	std::cout << "\n==============================================\n";
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
	if (opt.show_help || opt.input_file.empty()) {
		print_help(argv[0]);
		return opt.show_help ? 0 : 2;
	}

	int ret = 0;
	try {
		Pycp::Initialize();

		if (opt.dump) {
			// 字节码查看：.pycp（解析+编译）或 .cpycp（反序列化）后输出详情
			Pycp::BC::Module module;
			if (has_suffix(opt.input_file, Pycp::EXT_CPYCP)) {
				std::vector<uint8_t> bytes = read_file_bytes(opt.input_file);
				module = Pycp::BC::Deserialize(bytes.data(), bytes.size());
			} else {
				module = compile_source(opt.input_file);
			}
			dump_module(module);
		}
		else if (opt.emit_cpp) {
			// AOT：收集入口与全部 import 依赖，逐文件生成 C++ 源码。
			//   入口模块 -> <name>/__pycp_main.gen.cpp（含 main）；
			//   被导入模块 foo -> <name>/foo.gen.cpp。
			std::map<std::string, Pycp::BC::Module> modules =
				Pycp::ModuleLoader::load_all(opt.input_file);
			std::string entry_name = entry_module_name(opt.input_file);
			auto sources = Pycp::AOT::EmitCppAll(modules, entry_name);

			// 输出目录：以入口文件名（去扩展名）命名的子目录，位于当前工作目录。
			//   如 ../tests/import_test.pycp -> ./import_test/。
			std::string out_dir = entry_name;

			// 入口输出路径：<out_dir>/__pycp_main.gen.cpp（-o 可覆盖为指定路径）。
			std::string entry_out = opt.output_file.empty()
				? out_dir + "/" + Pycp::AOT_ENTRY_CPP_FILENAME
				: opt.output_file;

			// 确保输出目录存在（std::ofstream 不会自动创建目录）。
			std::error_code ec;
			std::filesystem::create_directories(out_dir, ec);
			if (ec) throw Pycp::Exception("Failed to create output directory: " + out_dir);

			// 写入口 .cpp
			{
				std::ofstream f(entry_out, std::ios::binary);
				if (!f) throw Pycp::Exception("Failed to write AOT output: " + entry_out);
				f << sources[entry_name];
			}
			std::cout << "Generated C++ source: " << entry_out << std::endl;

			// 写各依赖模块 .cpp（原始模块名 + .gen.cpp，写到同一输出目录）
			for (const auto& kv : sources) {
				if (kv.first == entry_name) continue; // 跳过入口
				std::string dep_out = out_dir + "/" + kv.first + Pycp::AOT_CPP_SUFFIX;
				std::ofstream f(dep_out, std::ios::binary);
				if (!f) throw Pycp::Exception("Failed to write AOT output: " + dep_out);
				f << kv.second;
				std::cout << "Generated C++ source: " << dep_out << std::endl;
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
