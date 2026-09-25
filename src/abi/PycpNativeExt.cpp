#include "abi/PycpNativeExt.hpp"
#include "object/PycpException.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpString.hpp"
#include "object/PycpConfig.hpp"
#include "bytecode/PycpBytecode.hpp"   // BC::Module / BC::Deserialize（.cpycp 与包清单）

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
	#include <windows.h>
#elif defined(__APPLE__)
	#include <dlfcn.h>
	#include <mach-o/dyld.h>   // _NSGetExecutablePath
#else
	#include <dlfcn.h>
	#include <unistd.h>
#endif

namespace Pycp {

// 原生扩展入口符号约定：每个动态库按其模块名导出符号
//   extern "C" Module* PycpModule_<name>();
// （如 io 库导出 PycpModule_io，pycp 库导出 PycpModule_pycp）。运行时
// 按导入名 name 拼出 "PycpModule_<name>" 解析，与 AOT 子模块符号命名统一。
namespace {
// 模块初始化入口函数指针类型。
using NativeModuleInitFn = Module* (*)();

// 已加载的动态库句柄缓存（模块名 -> dlopen 句柄），进程级。
std::unordered_map<std::string, void*>* g_handles = nullptr;
std::mutex g_handles_mutex;

// 标准库目录（可执行文件旁的 stdlib/）。惰性计算并缓存。
std::string g_stdlib_dir;
bool g_stdlib_dir_computed = false;
std::mutex g_stdlib_mutex;

// .pycp 源码模块编译器钩子。默认实现返回 nullptr，使「.pycp 源码」这一
// 候选形式整体禁用——AOT 生成的独立程序即为此情形（仅识别原生动态库）。
BC::Module* default_source_compiler(const char*) {
	return nullptr;
}
std::atomic<SourceModuleCompiler> g_source_compiler{&default_source_compiler};

// 可执行文件所在目录（惰性计算并缓存）。
std::string g_exe_dir;
bool g_exe_dir_computed = false;
std::mutex g_exe_dir_mutex;

// 命令行参数（argv）全局状态：宿主启动时 SetArgv 一次性写入，只读访问。
// 默认空 vector => pycp.argv == []（如 REPL 等未注入场景）。
std::vector<std::string> g_argv;

// 关闭动态库句柄（跨平台）。
void close_handle(void* h) {
if (h == nullptr) return;
#if defined(_WIN32)
FreeLibrary(static_cast<HMODULE>(h));
#else
dlclose(h);
#endif
}
} // anonymous namespace

// 计算可执行文件所在目录（跨平台，惰性缓存）。
// 取不到时返回空串（如 /proc 不可用），调用方须按空串处理。
const std::string& GetExeDir() {
std::lock_guard<std::mutex> lock(g_exe_dir_mutex);
if (!g_exe_dir_computed) {
	std::string p;
#if defined(_WIN32)
	char buf[MAX_PATH];
	DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
	if (len != 0 && len < MAX_PATH) p.assign(buf, static_cast<std::size_t>(len));
#elif defined(__APPLE__)
	// macOS 无 /proc，改用 _NSGetExecutablePath：首传 nullptr 取得所需
	// 缓冲区大小，再按该大小重建缓冲区取真实路径。
	uint32_t size = 0;
	_NSGetExecutablePath(nullptr, &size);
	if (size > 0) {
		p.resize(static_cast<std::size_t>(size));
		if (_NSGetExecutablePath(&p[0], &size) == 0) {
			if (!p.empty() && p.back() == '\0') p.pop_back(); // 去掉结尾的 '\0'
		} else {
			p.clear();
		}
	}
#else
	// Linux：readlink /proc/self/exe。
	char buf[4096];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len > 0) {
		buf[len] = '\0';
		p.assign(buf, static_cast<std::size_t>(len));
	}
#endif
	std::size_t slash = p.find_last_of("/\\");
	g_exe_dir = (slash == std::string::npos) ? std::string() : p.substr(0, slash);
	g_exe_dir_computed = true;
}
return g_exe_dir;
}

const char* native_ext_suffix() {
#if defined(_WIN32)
	return ".dll";
#elif defined(__APPLE__)
	return ".dylib";
#else
	return ".so";
#endif
}

// 返回标准库目录（可执行文件旁的 stdlib/），惰性计算并缓存。
const std::string& GetStdlibDir() {
	std::lock_guard<std::mutex> lock(g_stdlib_mutex);
	if (!g_stdlib_dir_computed) {
		const std::string& exe_dir = GetExeDir();
		if (!exe_dir.empty()) {
			g_stdlib_dir = exe_dir + "/" + Pycp::STDLIB_DIR_NAME;
		}
		g_stdlib_dir_computed = true;
	}
	return g_stdlib_dir;
}

void SetSourceModuleCompiler(SourceModuleCompiler fn) {
	// 传 nullptr 表示注销，回退到默认实现（源码层禁用）。
	g_source_compiler.store(fn != nullptr ? fn : &default_source_compiler);
}

// 写入命令行参数（宿主启动时调用一次）；复制入全局状态，调用方 vector 可释放。
void SetArgv(const std::vector<std::string>& args) {
	g_argv = args;
}

// 返回注入的参数列表（只读引用；未注入时为空 vector）。
const std::vector<std::string>& GetArgv() {
	return g_argv;
}

// 调用模块入口函数，跨 ABI 边界保护异常。
//   symbol : 入口符号名（PycpModule_<name>）
//   source : 来源描述，仅用于错误信息（动态库路径 / "linked symbol"）
Module* call_module_init(NativeModuleInitFn init, const std::string& symbol,
                         const std::string& source) {
	Module* mod = nullptr;
	try {
		mod = init();
	} catch (const Pycp::Exception&) {
		// Pycp 异常已含位置信息，直接向上传播。
		throw;
	} catch (const std::exception& e) {
		throw Pycp::Exception("module entry '" + symbol + "' (" + source +
		                      ") init failed: " + e.what());
	} catch (...) {
		throw Pycp::Exception("unknown native exception in module entry '" +
		                      symbol + "' (" + source + ")");
	}
	if (mod == nullptr) {
		throw ImportError("module entry '" + symbol + "' (" + source +
		                  ") returned null module.");
	}
	return mod;
}

// 从确定的动态库路径加载模块（dlopen + 解析入口 + 调用 + 记录句柄）。
// 调用方须先确认 path 存在。失败时已关闭句柄，不残留。
Module* load_native_from_path(const std::string& path, const std::string& name) {
	// 与 AOT 生成端同源 sanitize（说明见 LoadLinkedModule）。
	const std::string entry_symbol =
		std::string(Pycp::AOT_MODULE_INIT_PREFIX) + Pycp::SanitizeModuleName(name);

	void* handle = nullptr;
#if defined(_WIN32)
	handle = static_cast<void*>(LoadLibraryA(path.c_str()));
	if (handle == nullptr) {
		throw ImportError("Failed to load extension '" + path + "'");
	}
#else
	handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (handle == nullptr) {
		const char* err = dlerror();
		throw ImportError("Failed to load extension '" + path + "': " +
		                  (err != nullptr ? err : "unknown error"));
	}
#endif

	NativeModuleInitFn init = nullptr;
#if defined(_WIN32)
	// GetProcAddress 返回 FARPROC（通用函数指针），先转 void* 再转具体签名，
	// 消除 -Wcast-function-type（Windows 下标准做法）。
	init = reinterpret_cast<NativeModuleInitFn>(
		reinterpret_cast<void*>(
			GetProcAddress(static_cast<HMODULE>(handle), entry_symbol.c_str())));
#else
	init = reinterpret_cast<NativeModuleInitFn>(dlsym(handle, entry_symbol.c_str()));
#endif
	if (init == nullptr) {
		close_handle(handle);
		throw ImportError("Extension '" + path + "' has no entry symbol '" +
		                  entry_symbol + "'");
	}

	Module* mod = nullptr;
	try {
		mod = call_module_init(init, entry_symbol, path);
	} catch (...) {
		close_handle(handle);
		throw;
	}

	// 记录句柄（模块名 -> handle），供 NativeExt_Finalize 统一关闭。
	{
		std::lock_guard<std::mutex> lock(g_handles_mutex);
		if (g_handles == nullptr) {
			g_handles = new std::unordered_map<std::string, void*>();
		}
		// 若同名模块已加载（重复 import 但未走缓存），关闭旧句柄避免泄漏。
		auto it = g_handles->find(name);
		if (it != g_handles->end()) {
			close_handle(it->second);
		}
		(*g_handles)[name] = handle;
	}

	return mod;
}

Module* LoadLinkedModule(const std::string& name) {
	// 与 AOT 生成端（PycpAot.cpp::module_init_symbol）同源 sanitize：
	// 包内子模块名含 '.'（pkg.obj_a），而 C 标识符不允许点号。
	const std::string entry_symbol =
		std::string(Pycp::AOT_MODULE_INIT_PREFIX) + Pycp::SanitizeModuleName(name);

	NativeModuleInitFn init = nullptr;
#if defined(_WIN32)
	// 主程序模块自身导出的符号（GetModuleHandle(NULL) 取 exe 句柄）。
	init = reinterpret_cast<NativeModuleInitFn>(
		reinterpret_cast<void*>(
			GetProcAddress(GetModuleHandleA(nullptr), entry_symbol.c_str())));
#else
	// RTLD_DEFAULT：在整个进程的全局符号表中查找。
	// 注：以 RTLD_LOCAL 加载的 stdlib 扩展（io.so 等）不进全局符号表，
	// 不会在此误命中；仅「明确链接进主程序」的模块才会被找到。
	init = reinterpret_cast<NativeModuleInitFn>(dlsym(RTLD_DEFAULT, entry_symbol.c_str()));
#endif
	if (init == nullptr) return nullptr;

	// 符号存在即表明明确的链接意图。入口返回 nullptr 时直接抛错、不静默
	// 下探，否则「静态库成员被链接器丢弃」会退化为难以排查的 ImportError。
	return call_module_init(init, entry_symbol, "linked symbol");
}

// 模块名 -> 目录相对路径：点号全名（包内子模块 pkg.obj_a）映射为目录层级
// pkg/obj_a，与「目录即包」的磁盘布局一致（对齐 Python 的 pkg/sub.py）。
static std::string module_rel_path(const std::string& name) {
	std::string rel = name;
	for (char& c : rel) {
		if (c == Pycp::MODULE_NAME_SEPARATOR) c = '/';
	}
	return rel;
}

// 命中包清单后，把清单里对【包内兄弟文件】的 import 就地改写为点号全名
// （pkg.sub），使子模块在运行期能经「点号 -> 目录层级」映射按需加载。
// 规则与编译期 ModuleLoader::load_all 完全一致（兄弟优先于 cwd）。
static void rewrite_package_imports(BC::Module* bc, const std::string& pkg,
                                    const std::string& pkg_dir) {
	if (bc == nullptr) return;
	std::error_code ec;
	for (std::string& dep : bc->imports) {
		const std::string sibling = pkg_dir + "/" + dep + Pycp::EXT_PYCP;
		const std::string sibling_bc = pkg_dir + "/" + dep + Pycp::EXT_CPYCP;
		if (std::filesystem::is_regular_file(sibling, ec) ||
		    std::filesystem::is_regular_file(sibling_bc, ec)) {
			dep = pkg + Pycp::MODULE_NAME_SEPARATOR + dep;
		}
	}
}

// .cpycp -> 堆分配的 BC::Module（所有权交 VM，由 owned_modules_ 管理）。
// 反序列化由运行时自身完成，故不依赖宿主的源码编译器钩子。
static BC::Module* load_bytecode_module(const std::string& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return nullptr;
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
	                           std::istreambuf_iterator<char>());
	if (bytes.empty()) return nullptr;
	return new BC::Module(BC::Deserialize(bytes.data(), bytes.size()));
}

Module* LoadNativeModuleFrom(const std::string& dir, const std::string& name,
                             BC::Module** out_source,
                             bool* out_source_is_package) {
	if (out_source != nullptr) *out_source = nullptr;
	if (out_source_is_package != nullptr) *out_source_is_package = false;

	const std::string fname = name + native_ext_suffix();
	std::error_code ec;

	const std::string rel = module_rel_path(name);
	auto dir_path = [&dir](const std::string& p) {
		return dir.empty() ? ("./" + p) : (dir + "/" + p);
	};

	// ---- ① 模块文件夹：<dir>/<rel>/pycp.mpycp ----
	// 命中即编译清单（需宿主编译器钩子；AOT 独立程序无钩子则继续下探），
	// 并把清单里对兄弟文件的 import 改写为点号全名。
	if (out_source != nullptr) {
		const std::string manifest =
			dir_path(rel + "/" + Pycp::MODULE_MANIFEST_FILENAME);
		if (std::filesystem::is_regular_file(manifest, ec)) {
			SourceModuleCompiler compiler = g_source_compiler.load();
			if (compiler != nullptr) {
				BC::Module* bc = compiler(manifest.c_str());
				if (bc != nullptr) {
					rewrite_package_imports(bc, name, dir_path(rel));
					if (out_source_is_package != nullptr) {
						*out_source_is_package = true;
					}
					*out_source = bc;
					return nullptr;
				}
			}
		}
	}

	// ---- ②/③ 源码形态优先于原生扩展 ----
	// 同层同时存在 <name>.pycp 与 <name>.so 时优先按源码加载（解释态语义）：
	// 与 Python 的「扩展模块优先」相反——Pycp 让「所见即所得」的源码优先于
	// 同名动态库，改源码即生效，也允许在 stdlib/ 下放同名 .pycp 覆盖内置扩展。
	// 顺序：.pycp（源码，需钩子）→ .cpycp（字节码，无需钩子）→ .so（原生）。
	if (out_source != nullptr) {
		const std::string src_path = dir_path(rel + Pycp::EXT_PYCP);
		if (std::filesystem::is_regular_file(src_path, ec)) {
			SourceModuleCompiler compiler = g_source_compiler.load();
			if (compiler != nullptr) {
				BC::Module* bc = compiler(src_path.c_str());
				if (bc != nullptr) {
					// 已编译为字节码但尚未执行顶层，交由 VM 完成
					// （与 registry 路径一致）。
					*out_source = bc;
					return nullptr;
				}
			}
		}

		// .cpycp：与 .pycp 语义完全相同，只是省去「源码 -> 字节码」这一步。
		const std::string bc_path = dir_path(rel + Pycp::EXT_CPYCP);
		if (std::filesystem::is_regular_file(bc_path, ec)) {
			BC::Module* bc = load_bytecode_module(bc_path);
			if (bc != nullptr) {
				*out_source = bc;
				return nullptr;
			}
		}
	}

	// ---- ④ 同名动态库（原生扩展，类似 Python 的 .pyd）----
	const std::string so_path = dir_path(fname);
	if (std::filesystem::is_regular_file(so_path, ec)) {
		return load_native_from_path(so_path, name);
	}

	return nullptr;
}

Module* LoadNativeModule(const std::string& name,
                         const std::string& search_dir,
                         BC::Module** out_source) {
	// 兼容旧入口：保持既有顺序（cwd -> search_dir -> stdlib）。
	Module* m = LoadNativeModuleFrom(std::string(), name, out_source);
	if (m != nullptr || (out_source != nullptr && *out_source != nullptr)) {
		return m;
	}
	if (!search_dir.empty()) {
		m = LoadNativeModuleFrom(search_dir, name, out_source);
		if (m != nullptr || (out_source != nullptr && *out_source != nullptr)) {
			return m;
		}
	}
	const std::string& stdlib_dir = GetStdlibDir();
	if (!stdlib_dir.empty()) {
		m = LoadNativeModuleFrom(stdlib_dir, name, out_source);
		if (m != nullptr || (out_source != nullptr && *out_source != nullptr)) {
			return m;
		}
	}
	return nullptr;
}

void NativeExt_Finalize() {
	std::lock_guard<std::mutex> lock(g_handles_mutex);
	if (g_handles == nullptr) return;
	for (auto& kv : *g_handles) {
		close_handle(kv.second);
	}
	g_handles->clear();
	delete g_handles;
	g_handles = nullptr;
}

// 注：拆箱辅助（旧 ArgInt/ArgString/ArgBool）已随旧参数框架一并删除；
// 参数声明与个数校验改用 PycpExtension.hpp 的参数规范表，类型判断由
// 业务函数自行完成。

} // namespace Pycp
