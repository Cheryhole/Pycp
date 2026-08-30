#include "aot/PycpAotSdkLocator.hpp"

#include "PycpConfig.hpp"
#include "PycpNativeExt.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace Pycp::AOT {

namespace {

// 目录存在且为目录（跨平台，用 error_code 避免异常）。
bool is_dir(const std::string& path) {
	if (path.empty()) return false;
	std::error_code ec;
	return std::filesystem::is_directory(path, ec);
}

// 文件存在且为常规文件。
bool is_file(const std::string& path) {
	if (path.empty()) return false;
	std::error_code ec;
	return std::filesystem::is_regular_file(path, ec);
}

// 拼接路径：dir 为空或已带分隔符时不再重复添加。
std::string join(const std::string& dir, const std::string& name) {
	if (dir.empty()) return name;
	if (dir.back() == '/' || dir.back() == '\\') return dir + name;
	return dir + "/" + name;
}

// 取父目录；无分隔符或已是根时返回空串。
std::string parent_dir(const std::string& path) {
	if (path.empty()) return std::string();
	std::size_t slash = path.find_last_of("/\\");
	if (slash == std::string::npos || slash == 0) return std::string();
	return path.substr(0, slash);
}

// 取环境变量值；未设置时返回空串。
std::string env_var(const char* name) {
	const char* v = std::getenv(name);
	return (v != nullptr) ? std::string(v) : std::string();
}

// 取路径最后一段（文件名）；无分隔符时返回原串。
std::string file_name(const std::string& path) {
	const std::size_t pos = path.find_last_of("/\\");
	return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// 静态库文件所属的模块名：libPycpExt_io.a / PycpExt_io.lib -> "io"。
// 即剥掉 lib 前缀与静态库扩展名（.a / .lib）后的剩余部分，
// 再剥掉固定的 PycpExt_ 前缀。非该命名规则的文件返回空串（表示跳过）。
std::string static_ext_module_name(const std::string& file) {
	std::string name = file_name(file);

	// 剥静态库扩展名
	for (const char* ext : {".a", ".lib"}) {
		const std::string e(ext);
		if (name.size() > e.size() &&
		    name.compare(name.size() - e.size(), e.size(), e) == 0) {
			name = name.substr(0, name.size() - e.size());
			break;
		}
	}
	// 剥 GNU 的 lib 前缀
	const std::string lib = "lib";
	if (name.size() > lib.size() && name.compare(0, lib.size(), lib) == 0) {
		name = name.substr(lib.size());
	}

	const std::string prefix = "PycpExt_";
	if (name.size() > prefix.size() &&
	    name.compare(0, prefix.size(), prefix) == 0) {
		return name.substr(prefix.size());
	}
	return std::string(); // 不是内置扩展的静态库
}

// 扫描 <lib_dir> 下全部 libPycpExt_<name>.a，推导内置扩展清单。
// 结果按模块名排序，保证生成文件内容稳定（避免每次生成顺序不同导致
// 不必要的重编译）。
void scan_static_extensions(const std::string& lib_dir, SdkInfo* info) {
	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(lib_dir, ec)) {
		if (!entry.is_regular_file(ec)) continue;
		const std::string path = entry.path().string();
		const std::string mod = static_ext_module_name(path);
		if (mod.empty()) continue;
		info->stdlib_static_libs.push_back(path);
		info->builtin_modules.push_back(mod);
	}
	std::sort(info->stdlib_static_libs.begin(), info->stdlib_static_libs.end());
	std::sort(info->builtin_modules.begin(), info->builtin_modules.end());
}

} // anonymous namespace

bool ValidateSdkRoot(const std::string& root, SdkInfo* out) {
	SdkInfo info;
	info.root = root;

	if (root.empty()) {
		if (out != nullptr) *out = info;
		return false;
	}
	if (!is_dir(root)) {
		info.tried.push_back(root + "  (不是目录)");
		if (out != nullptr) *out = info;
		return false;
	}

	info.include_dir = join(root, "include");
	info.lib_dir     = join(root, "lib");
	info.stdlib_dir  = join(root, Pycp::STDLIB_DIR_NAME);

	// 三项必需：头文件、运行时库、标准库原生扩展。
	// 任一项缺失都会让生成的 CMake 项目在链接期或运行期失败，故在此拦下。
	if (!is_dir(info.include_dir)) {
		info.tried.push_back(root + "  (缺少 include/)");
		if (out != nullptr) *out = info;
		return false;
	}
	if (!is_dir(info.lib_dir)) {
		info.tried.push_back(root + "  (缺少 lib/)");
		if (out != nullptr) *out = info;
		return false;
	}
	if (!is_dir(info.stdlib_dir)) {
		info.tried.push_back(root + "  (缺少 " + Pycp::STDLIB_DIR_NAME + "/)");
		if (out != nullptr) *out = info;
		return false;
	}

	// Windows：加载 DLL 只搜 exe 同级目录、不搜 lib/，故 SDK 根目录会额外
	// 放一份运行时 DLL（见顶层 CMakeLists 的 PYCP_DIST_ROOT_FILES）。
	// 文件名随构建 SDK 的编译器而异：MinGW/GNU 产出 libPycpRuntime.dll，
	// MSVC 产出 PycpRuntime.dll，故两者都要探测（此前只探测后者，MinGW 下
	// 恒不命中，导致 root_dll 永远为空）。
	// 该文件非必需（静态链接构建下没有），缺失时仅置空、不影响校验。
	for (const char* dll_name : {"PycpRuntime.dll", "libPycpRuntime.dll"}) {
		const std::string dll = join(root, dll_name);
		if (is_file(dll)) {
			info.root_dll = dll;
			break;
		}
	}

	// 静态链接（--static）所需产物：静态运行时 + 各内置扩展的静态库。
	// 同样非必需：shared-only 的 SDK（如 -DBUILD_RUNTIME_STATIC=OFF 构建的
	// dist）没有这些文件，此时置空且不影响 valid，仅在使用 --static 时报错。
	for (const char* a_name : {"libPycpRuntime.a", "PycpRuntime.lib",
	                           "libPycpRuntime.lib"}) {
		const std::string a = join(info.lib_dir, a_name);
		if (is_file(a)) {
			info.static_runtime = a;
			break;
		}
	}
	scan_static_extensions(info.lib_dir, &info);
	info.has_static = !info.static_runtime.empty() &&
	                  !info.stdlib_static_libs.empty();

	info.valid = true;
	if (out != nullptr) *out = info;
	return true;
}

SdkInfo LocateSdk() {
	std::vector<std::string> candidates;

	// 1) 可执行文件所在目录：常规安装 / dist 布局下，pycp 与 include/、
	//    lib/、stdlib/ 同级。
	const std::string& exe_dir = Pycp::GetExeDir();
	if (!exe_dir.empty()) candidates.push_back(exe_dir);

	// 2) exe 同级 dist：部分安装布局（如通过 pycp-dist 收集到 build/dist，
	//    而 pycp 自身在 build/pycp 或 build/）时，SDK 就在 exe 目录的 dist/ 子目录。
	if (!exe_dir.empty()) candidates.push_back(join(exe_dir, "dist"));

	// 3) 其父目录的 dist：构建树内 pycp 位于 build/pycp，而 SDK 在 build/dist
	//    （pycp 直接在 build/ 根时也覆盖：此时父目录 dist = 仓库/dist，无效则跳过）。
	if (!exe_dir.empty()) {
		std::string parent = parent_dir(exe_dir);
		if (!parent.empty()) candidates.push_back(join(parent, "dist"));
	}

	// 4) 当前工作目录同级 dist（从仓库根运行时：./dist）。
	{
		std::error_code ec;
		std::string cwd = std::filesystem::current_path(ec).string();
		if (!cwd.empty()) candidates.push_back(join(cwd, "dist"));
	}

	// 5) 环境变量兜底（CI / 非标准安装布局）。
	candidates.push_back(env_var("PYCP_DIST"));

	SdkInfo result;
	for (const std::string& c : candidates) {
		if (c.empty()) continue;
		SdkInfo info;
		if (ValidateSdkRoot(c, &info)) {
			return info; // 命中即返回
		}
		// 落选原因并入最终结果，供调用方统一展示
		for (const std::string& t : info.tried) result.tried.push_back(t);
	}

	// 全部落选：保留候选清单便于报错
	if (result.tried.empty()) {
		result.tried.push_back("(无候选路径：无法定位可执行文件，且 PYCP_DIST 未设置)");
	}
	return result;
}

} // namespace Pycp::AOT
