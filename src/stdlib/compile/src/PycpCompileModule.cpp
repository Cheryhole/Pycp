#include "compile_stdlib.hpp"

#include "object/PycpModule.hpp"
#include "object/PycpFunction.hpp"
#include "object/PycpString.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpExtension.hpp"        // 参数规范框架 / PYCP_EXPORT_MODULE

#include "abi/PycpNativeExt.hpp"           // CompileSourceString / ExecSourceString
#include "abi/PycpABI.hpp"                 // RunBytecodeModule
#include "bytecode/PycpBytecode.hpp"       // BC::Module
#include "bytecode/PycpBytecodeObject.hpp" // WrapModule / IsModuleRef / UnwrapModule

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

// compile.compile(source [, filename]) -> 字节码模块对象
Object* _builtin_compile(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("compile", {
		Extension::Arg::Required("source"),
		Extension::Arg::Optional("filename"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string source = require_string("compile", "source", r["source"]);

	std::string filename = "<compile>";
	Object* fn = r["filename"];
	if (r.given("filename") && fn != nullptr && fn != None::instance) {
		filename = require_string("compile", "filename", fn);
	}

	std::shared_ptr<BC::Module> mod(CompileSourceString(source, filename));
	return BC::WrapModule(mod); // Owned
}

// compile.exec(code [, globals]) -> 顶层结果
Object* _builtin_exec(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("exec", {
		Extension::Arg::Required("code"),
		Extension::Arg::Optional("globals"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* code = r["code"];
	Object* globals = r.given("globals") ? r["globals"] : nullptr;
	if (code == nullptr) throw TypeError("exec: argument 'code' is null.");

	// 字节码模块对象：直接用运行时 VM 执行（含 globals 预置/写回）。
	if (BC::IsModuleRef(code)) {
		std::shared_ptr<BC::Module> mod = BC::UnwrapModule(code);
		if (!mod) throw TypeError("exec: invalid bytecode module object.");
		return RunBytecodeModule(mod.get(), globals);
	}
	// 源码字符串：编译 + 执行（经宿主 SourceExecutor 钩子）。
	if (IsString(code)) {
		return ExecSourceString(AsString(code), "<exec>", globals);
	}
	throw TypeError("exec: argument must be a bytecode module or a source string.");
}

Module* make_compile_module() {
	Module* mod = Module::New(MODULE_NAME);
	mod->set_function("compile", _builtin_compile, /*with_keywords=*/true);
	mod->set_function("exec", _builtin_exec, /*with_keywords=*/true);
	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_compile）。
PYCP_EXPORT_MODULE(compile) {
	return make_compile_module();
}

} // namespace Pycp
