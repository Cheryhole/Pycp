#include "ast_stdlib.hpp"

#include "object/PycpModule.hpp"
#include "object/PycpFunction.hpp"
#include "object/PycpString.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpExtension.hpp"   // 参数规范框架 / PYCP_EXPORT_MODULE

#include "abi/PycpNativeExt.hpp"      // ParseSourceString（宿主注册的解析钩子）

namespace Pycp {

namespace {

// 参数拆箱辅助（业务侧自行判型：框架只校验个数与名字）。
std::string require_string(const char* fn, const char* param, Object* v) {
	if (v == nullptr || !IsString(v)) {
		throw TypeError(std::string(fn) + ": argument '" + param +
		                "' expects a string, got '" +
		                (v != nullptr ? v->type_name() : std::string("None")) + "'.");
	}
	return AsString(v);
}

// ast.parse(source [, filename]) -> 节点树根（pycp 对象）
Object* _builtin_parse(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("parse", {
		Extension::Arg::Required("source"),
		Extension::Arg::Optional("filename"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string source = require_string("parse", "source", r["source"]);

	std::string filename = "<ast>";
	Object* fn = r["filename"];
	if (r.given("filename") && fn != nullptr && fn != None::instance) {
		filename = require_string("parse", "filename", fn);
	}
	return ParseSourceString(source, filename); // Owned
}

// ast.dump(node) -> String（该节点的 to_string 文本）
Object* _builtin_dump(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("dump", {
		Extension::Arg::Required("node"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* node = r["node"];
	if (node == nullptr) throw TypeError("dump: argument 'node' is null.");
	return node->__string__(); // Owned
}

// ast.type_of(node) -> String（节点类型名）
Object* _builtin_type_of(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("type_of", {
		Extension::Arg::Required("node"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* node = r["node"];
	if (node == nullptr) throw TypeError("type_of: argument 'node' is null.");
	return String::FromCString(node->type_name().c_str());
}

Module* make_ast_module() {
	Module* mod = Module::New(MODULE_NAME);
	// 均开启关键字支持：parse 带可选 filename，dump / type_of 允许 node= 关键字。
	mod->set_function("parse", _builtin_parse, /*with_keywords=*/true);
	mod->set_function("dump", _builtin_dump, /*with_keywords=*/true);
	mod->set_function("type_of", _builtin_type_of, /*with_keywords=*/true);
	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_ast，按模块名导出）。
// 由 VM::load_module 经 LoadNativeModule 的 dlsym("PycpModule_ast") 调用。
// 必须走 PYCP_EXPORT_MODULE：Windows 下没有 __declspec(dllexport) 时，
// 一旦本 TU 出现其它导出符号，GetProcAddress 就找不到入口，import 会静默失效。
PYCP_EXPORT_MODULE(ast) {
	return make_ast_module();
}

} // namespace Pycp
