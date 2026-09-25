#include "bytecode_stdlib.hpp"

#include "object/PycpModule.hpp"
#include "object/PycpFunction.hpp"
#include "object/PycpString.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpExtension.hpp"        // 参数规范框架 / PYCP_EXPORT_MODULE

#include "abi/PycpNativeExt.hpp"           // CompileSourceString（宿主编译钩子）
#include "bytecode/PycpBytecode.hpp"       // BC::Module
#include "bytecode/PycpBytecodeObject.hpp" // WrapModule / IsModuleRef
#include "object/PycpConfig.hpp"           // SanitizeModuleName / AOT 命名约定
#include "object/PycpMap.hpp"              // Map（aot_config 返回值）

#include <memory>
#include <string>

namespace Pycp {

namespace {

std::string require_string(const char* fn, const char* param, Object* v) {
	if (v == nullptr || !IsString(v)) {
		throw TypeError(std::string(fn) + ": argument '" + param +
		                "' expects a string, got '" +
		                (v != nullptr ? v->type_name() : std::string("None")) + "'.");
	}
	return AsString(v);
}

// 取可选 filename（未给出 / None -> def）。
std::string filename_arg(const Extension::ArgResult& r, const char* def) {
	Object* fn = r["filename"];
	if (!r.given("filename") || fn == nullptr || fn == None::instance) return def;
	return require_string("compile", "filename", fn);
}

// bytecode.compile(source [, filename]) -> 字节码模块对象
Object* _builtin_compile(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("compile", {
		Extension::Arg::Required("source"),
		Extension::Arg::Optional("filename"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string source = require_string("compile", "source", r["source"]);
	const std::string filename = filename_arg(r, "<bytecode>");

	// 宿主钩子返回堆分配的 BC::Module（所有权移交调用方）。
	std::shared_ptr<BC::Module> mod(CompileSourceString(source, filename));
	return BC::WrapModule(mod); // Owned
}

// bytecode.dump(x) -> 反汇编文本（x 为字节码模块对象或源码字符串）
Object* _builtin_dump(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("dump", {
		Extension::Arg::Required("x"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* x = r["x"];
	if (x == nullptr) throw TypeError("dump: argument 'x' is null.");

	if (BC::IsModuleRef(x)) return x->__string__(); // Owned
	if (IsString(x)) {
		std::shared_ptr<BC::Module> mod(CompileSourceString(AsString(x), "<dump>"));
		Object* ref = BC::WrapModule(mod); // Owned
		Object* out = ref->__string__();   // Owned
		Decref(ref);
		return out;
	}
	throw TypeError("dump: argument must be a bytecode module or a source string.");
}

// bytecode.sanitize_module_name(name) -> String
// 模块名 -> C 标识符片段（'.' -> "__"），与运行时 ImportModule / dlsym 同源。
Object* _builtin_sanitize_module_name(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"sanitize_module_name", { Extension::Arg::Required("name") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string name =
		require_string("sanitize_module_name", "name", r["name"]);
	return String::FromCString(SanitizeModuleName(name).c_str());
}

// bytecode.aot_config() -> Map
// AOT 生成所需的命名/格式约定（与运行时 PycpConfig.hpp 同源，避免硬编码漂移）。
Object* _builtin_aot_config(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("aot_config", {});
	spec.Bind(args, kwargs);

	Map* m = Map::New();
	auto put = [&](const char* key, const std::string& val) {
		Object* k = String::FromCString(key);          // Owned
		Object* v = String::FromCString(val.c_str());  // Owned
		Object* old = m->__set_item__(k, v);           // 内部 Incref
		if (old != nullptr) Decref(old);
		Decref(k);
		Decref(v);
	};
	put("version", PYCP_VERSION);
	put("ext_pycp", EXT_PYCP);
	put("ext_cpycp", EXT_CPYCP);
	put("ext_cpp", EXT_CPP);
	put("aot_cpp_suffix", AOT_CPP_SUFFIX);
	put("aot_entry_cpp_filename", AOT_ENTRY_CPP_FILENAME);
	put("aot_builtin_reg_cpp_filename", AOT_BUILTIN_REG_CPP_FILENAME);
	put("aot_module_init_prefix", AOT_MODULE_INIT_PREFIX);
	put("aot_fn_prefix", AOT_FN_PREFIX);
	put("aot_entry_fn_name", AOT_ENTRY_FN_NAME);
	put("stdlib_dir_name", STDLIB_DIR_NAME);
	put("module_manifest_filename", MODULE_MANIFEST_FILENAME);
	put("module_top_name", MODULE_TOP_NAME);
	put("module_entry_name", MODULE_ENTRY_NAME);
	put("module_name_separator", std::string(1, MODULE_NAME_SEPARATOR));
	return m;
}

Module* make_bytecode_module() {
	Module* mod = Module::New(MODULE_NAME);
	mod->set_function("compile", _builtin_compile, /*with_keywords=*/true);
	mod->set_function("dump", _builtin_dump, /*with_keywords=*/true);
	mod->set_function("sanitize_module_name", _builtin_sanitize_module_name,
	                  /*with_keywords=*/true);
	mod->set_function("aot_config", _builtin_aot_config, /*with_keywords=*/true);
	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_bytecode）。
PYCP_EXPORT_MODULE(bytecode) {
	return make_bytecode_module();
}

} // namespace Pycp
