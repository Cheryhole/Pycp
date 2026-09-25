#include "abi/PycpABI.hpp"

#include "bytecode/PycpBytecode.hpp"
#include "vm/PycpBytecodeVM.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpMap.hpp"
#include "object/PycpString.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpException.hpp"

#include <string>
#include <unordered_map>

namespace Pycp {

// 执行字节码模块的顶层，支持可选的 Map globals（预置 + 写回）。
// 供宿主源码执行钩子与 stdlib compile 模块共用，避免 globals 桥接逻辑重复。
Object* RunBytecodeModule(BC::Module* module, Object* globals) {
	if (module == nullptr) {
		throw TypeError("exec: module is null.");
	}

	Map* gmap = nullptr;
	if (globals != nullptr && !globals->is_type("None")) {
		if (!globals->is_type("Map")) {
			throw TypeError("exec: globals must be a Map or None.");
		}
		gmap = static_cast<Map*>(globals);
	}

	BC::VM vm(module); // 无注册表：模块内 import 在运行时报 ImportError
	std::unordered_map<std::string, Object*>* g = vm.get_globals();

	// ---- 预置：Map 内容 -> VM 全局命名空间 ----
	FixedList* seed_keys = nullptr;
	if (gmap != nullptr) {
		seed_keys = gmap->keys(); // Owned
		if (seed_keys != nullptr) {
			for (std::size_t i = 0; i < seed_keys->size(); ++i) {
				Object* k = seed_keys->at(i);
				if (k == nullptr || !IsString(k)) continue;
				const std::string name = AsString(k);
				Object* v = gmap->__get_item__(k); // Borrowed
				if (v == nullptr) continue;
				Incref(v); // 交由 VM 全局命名空间持有
				(*g)[name] = v;
			}
		}
	}

	// ---- 执行 ----
	Object* result = nullptr;
	try {
		result = vm.run(); // Owned（顶层无返回值时为 nullptr）
	} catch (...) {
		if (seed_keys != nullptr) Decref(seed_keys);
		throw;
	}

	// ---- 写回：VM 全局命名空间 -> Map（新增或覆盖）----
	if (gmap != nullptr) {
		for (auto& kv : *g) {
			if (kv.second == nullptr) continue;
			Object* key = String::FromCString(kv.first.c_str()); // Owned
			Object* old = gmap->__set_item__(key, kv.second);    // 内部 Incref value
			if (old != nullptr) Decref(old);
			Decref(key);
		}
	}
	if (seed_keys != nullptr) Decref(seed_keys);

	if (result != nullptr) return result;
	return None::instance;
}

} // namespace Pycp
