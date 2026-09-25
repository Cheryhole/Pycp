#include "bytecode_stdlib.hpp"

#include "object/PycpModule.hpp"
#include "object/PycpFunction.hpp"
#include "object/PycpString.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpExtension.hpp"        // 参数规范框架 / PYCP_EXPORT_MODULE

#include "abi/PycpNativeExt.hpp"           // CompileSourceString（宿主编译钩子）
#include "bytecode/PycpBytecode.hpp"       // BC::Module
#include "bytecode/PycpBytecodeObject.hpp" // WrapModule / IsModuleRef

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

Module* make_bytecode_module() {
	Module* mod = Module::New(MODULE_NAME);
	mod->set_function("compile", _builtin_compile, /*with_keywords=*/true);
	mod->set_function("dump", _builtin_dump, /*with_keywords=*/true);
	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_bytecode）。
PYCP_EXPORT_MODULE(bytecode) {
	return make_bytecode_module();
}

} // namespace Pycp
