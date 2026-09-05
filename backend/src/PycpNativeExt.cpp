#include "PycpNativeExt.hpp"
#include "PycpException.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpConfig.hpp"

#include <atomic>
#include <filesystem>
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
	const std::string entry_symbol = std::string(Pycp::AOT_MODULE_INIT_PREFIX) + name;

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
	const std::string entry_symbol = std::string(Pycp::AOT_MODULE_INIT_PREFIX) + name;

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

Module* LoadNativeModuleFrom(const std::string& dir, const std::string& name,
                             BC::Module** out_source) {
	if (out_source != nullptr) *out_source = nullptr;

	const std::string fname = name + native_ext_suffix();
	std::error_code ec;

	// 源码优先：同层同时存在 <name>.pycp 与 <name>.so 时，优先按源码模块
	// 加载（解释态语义）。与 Python 的「扩展模块优先」相反——Pycp 让
	// 「所见即所得」的源码优先于同名动态库：改源码即生效，不被同名 .so
	// 悄悄遮蔽；也允许在 stdlib/ 下放置与内置扩展同名的 .pycp 来覆盖它。
	//
	// 源码形式需调用方能接收 BC::Module（由其执行顶层），且宿主必须注册
	// 了源码编译器钩子；否则本候选形式不可用（旧接口 LoadNativeModule
	// 与 AOT 生成的独立程序即属此情形，自动降级到动态库）。
	if (out_source != nullptr) {
		const std::string src_path = dir.empty()
			? ("./" + name + Pycp::EXT_PYCP)
			: (dir + "/" + name + Pycp::EXT_PYCP);
		if (std::filesystem::exists(src_path, ec)) {
			// 未注册编译器钩子（AOT 生成的独立程序）：跳过源码形式。
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
	}

	// 其次同名动态库（原生扩展，类似 Python 的 .pyd）。
	const std::string so_path = dir.empty()
		? ("./" + fname) : (dir + "/" + fname);
	if (std::filesystem::exists(so_path, ec)) {
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

// =============================================================
// 拆箱辅助
// =============================================================

int64_t ArgInt(Object** argv, std::size_t i, const char* fn) {
	if (argv == nullptr || argv[i] == nullptr || !argv[i]->is_type("Integer")) {
		throw TypeError(std::string(fn) + "(): argument " + std::to_string(i + 1) +
		                " expects an integer.");
	}
	return static_cast<Integer*>(argv[i])->get_value();
}

std::string ArgString(Object** argv, std::size_t i, const char* fn) {
	if (argv == nullptr || argv[i] == nullptr || !argv[i]->is_type("String")) {
		throw TypeError(std::string(fn) + "(): argument " + std::to_string(i + 1) +
		                " expects a string.");
	}
	return static_cast<String*>(argv[i])->get_value();
}

bool ArgBool(Object** argv, std::size_t i, const char* fn) {
	return ArgInt(argv, i, fn) != 0;
}

} // namespace Pycp
