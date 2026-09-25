#include "loader/PycpSourceBridge.hpp"

#include "abi/PycpNativeExt.hpp"
#include "bytecode/PycpBytecode.hpp"
#include "loader/PycpModuleLoader.hpp"
#include "vm/PycpBytecodeVM.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpMap.hpp"
#include "object/PycpString.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpException.hpp"

#include <string>
#include <unordered_map>

namespace Pycp {

namespace {

// SourceStringCompiler：源码字符串 -> 字节码 Module（普通模块语义）。
// 使用 repl_eval=false：顶层表达式语句不保留返回值（对齐 Python compile
// 的 "exec" 模式），与 REPL 的 compile_string 默认行为区分开。
BC::Module* bridge_compile_string(const std::string& source,
                                  const std::string& filename) {
	return new BC::Module(
		ModuleLoader::compile_string(source, filename, /*repl_eval=*/false));
}

// SourceExecutor：源码字符串 -> 编译并执行，返回顶层结果（Owned）。
//   globals 为 Map 时：以其内容预置全局命名空间，执行后把新增 / 改动的名字
//   写回该 Map（对齐 Python exec(code, globals) 的读写语义）。
//   globals 为 nullptr / None 时使用全新全局命名空间。
Object* bridge_exec_string(const std::string& source,
                           const std::string& filename, Object* globals) {
	BC::Module module =
		ModuleLoader::compile_string(source, filename, /*repl_eval=*/false);

	Map* gmap = nullptr;
	if (globals != nullptr && !globals->is_type("None")) {
		if (!globals->is_type("Map")) {
			throw TypeError("exec: globals must be a Map or None.");
		}
		gmap = static_cast<Map*>(globals);
	}

	BC::VM vm(&module);
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

} // anonymous namespace

void RegisterSourceHooks() {
	SetSourceStringCompiler(&bridge_compile_string);
	SetSourceExecutor(&bridge_exec_string);
	// SourceParser（ast.parse 用）在前端 AST 反射模块中注册（见 ast 模块阶段）。
}

} // namespace Pycp
