#include "PycpNativeExt.hpp"
#include "PycpException.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpNone.hpp"
#include "PycpGC.hpp"
#include "PycpConfig.hpp"

#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
	#include <windows.h>
#else
	#include <dlfcn.h>
	#include <unistd.h>
#endif

namespace Pycp {

// 原生扩展入口符号约定：每个动态库按其模块名导出符号
//   extern "C" Module* PycpModule_<name>();
// （如 io 库导出 PycpModule_io，Pycp 库导出 PycpModule_Pycp）。LoadNativeModule
// 按导入名 name 拼出 "PycpModule_<name>" 经 dlsym 解析，与 AOT 子模块符号命名统一。
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

// 计算可执行文件所在目录（惰性，仅首次调用时）。
std::string compute_exe_dir() {
#if defined(_WIN32)
	char buf[MAX_PATH];
	DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
	if (len == 0 || len >= MAX_PATH) return "";
	std::string p(buf, len);
#elif defined(__APPLE__)
	// macOS：_NSGetExecutablePath（简化：用 /proc 不可用，退化为空）。
	return "";
#else
	// Linux：readlink /proc/self/exe。
	char buf[4096];
	ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len <= 0) return "";
	buf[len] = '\0';
	std::string p(buf);
#endif
	std::size_t slash = p.find_last_of("/\\");
	if (slash == std::string::npos) return "";
	return p.substr(0, slash);
}
} // anonymous namespace

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
		std::string exe_dir = compute_exe_dir();
		if (!exe_dir.empty()) {
			g_stdlib_dir = exe_dir + "/stdlib";
		}
		g_stdlib_dir_computed = true;
	}
	return g_stdlib_dir;
}

Module* LoadNativeModule(const std::string& name,
                         const std::string& search_dir) {
	const std::string fname = name + native_ext_suffix();

	// 候选路径：优先当前工作目录（cwd），其次 search_dir（脚本目录），
	// 最后 stdlib（可执行文件旁）。取第一个存在的文件。
	std::string path;
	std::error_code ec;
	if (std::filesystem::exists("./" + fname, ec)) {
		path = "./" + fname;
	}
	if (path.empty() && !search_dir.empty()) {
		std::string c = search_dir + "/" + fname;
		if (std::filesystem::exists(c, ec)) path = c;
	}
	if (path.empty()) {
		const std::string& stdlib_dir = GetStdlibDir();
		if (!stdlib_dir.empty()) {
			std::string c = stdlib_dir + "/" + fname;
			if (std::filesystem::exists(c, ec)) path = c;
		}
	}

	// 无任何候选文件：返回 nullptr，交由调用方回退 .pycp 加载。
	if (path.empty()) {
		return nullptr;
	}

	// 文件存在：执行动态加载。
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

	// 解析入口符号 PycpModule_<name>（按导入名拼接，与 AOT 子模块命名统一）。
	const std::string entry_symbol = std::string("PycpModule_") + name;
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
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(handle));
#else
		dlclose(handle);
#endif
		throw ImportError("Extension '" + path + "' has no entry symbol '" +
		                  entry_symbol + "'");
	}

	// 调用入口，跨 ABI 边界保护异常。
	Module* mod = nullptr;
	try {
		mod = init();
	} catch (const Pycp::Exception&) {
		// Pycp 异常直接向上传播。
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(handle));
#else
		dlclose(handle);
#endif
		throw;
	} catch (const std::exception& e) {
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(handle));
#else
		dlclose(handle);
#endif
		throw Pycp::Exception(std::string("native module '") + name +
		                      "' init failed: " + e.what());
	} catch (...) {
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(handle));
#else
		dlclose(handle);
#endif
		throw Pycp::Exception("unknown native exception in module '" + name + "'");
	}

	if (mod == nullptr) {
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(handle));
#else
		dlclose(handle);
#endif
		throw ImportError("Extension '" + path + "' returned null module.");
	}

	// 记录句柄（模块名 -> handle），供 NativeExt_Finalize 统一关闭。
	{
		std::lock_guard<std::mutex> lock(g_handles_mutex);
		if (g_handles == nullptr) {
			g_handles = new std::unordered_map<std::string, void*>();
		}
		// 若同名模块已加载（重复 import 但未走 VM 缓存），关闭旧句柄避免泄漏。
		auto it = g_handles->find(name);
		if (it != g_handles->end()) {
#if defined(_WIN32)
			FreeLibrary(static_cast<HMODULE>(it->second));
#else
			dlclose(it->second);
#endif
		}
		(*g_handles)[name] = handle;
	}

	return mod;
}

void NativeExt_Finalize() {
	std::lock_guard<std::mutex> lock(g_handles_mutex);
	if (g_handles == nullptr) return;
	for (auto& kv : *g_handles) {
		if (kv.second == nullptr) continue;
#if defined(_WIN32)
		FreeLibrary(static_cast<HMODULE>(kv.second));
#else
		dlclose(kv.second);
#endif
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
