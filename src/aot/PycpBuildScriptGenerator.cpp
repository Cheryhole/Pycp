#include "aot/PycpBuildScriptGenerator.hpp"

#include <mutex>
#include <vector>

namespace Pycp::AOT {

namespace {

// 注册表：进程内唯一的生成器集合。
// 用裸指针 + 静态初始化注册，不持有所有权（生成器实例为全局对象）。
std::vector<const IBuildScriptGenerator*>& registry() {
	static std::vector<const IBuildScriptGenerator*> inst;
	return inst;
}

// 静态初始化在多编译单元间存在竞态风险，用互斥锁保护注册表写入。
std::mutex& registry_mutex() {
	static std::mutex m;
	return m;
}

} // anonymous namespace

void RegisterGenerator(const IBuildScriptGenerator* gen) {
	if (gen == nullptr) return;
	std::lock_guard<std::mutex> lock(registry_mutex());
	// 去重：同名 kind 仅保留首次注册（多数构建系统单次注册，防御性处理）。
	for (const auto* g : registry()) {
		if (g->kind() == gen->kind()) return;
	}
	registry().push_back(gen);
}

const IBuildScriptGenerator* FindGenerator(const std::string& kind) {
	for (const auto* g : registry()) {
		if (g->kind() == kind) return g;
	}
	return nullptr;
}

std::vector<std::string> AvailableKinds() {
	std::vector<std::string> kinds;
	for (const auto* g : registry()) kinds.push_back(g->kind());
	return kinds;
}

} // namespace Pycp::AOT
