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
#include "object/PycpList.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpException.hpp"
#include "abi/PycpABI.hpp"                 // Call / GetAttr / RunBytecodeModule
#include "vm/PycpBytecodeVM.hpp"           // BC::VM（清单求值）

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

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

// 向 Map 写入（owned_value 所有权移交 Map：__set_item__ 内部 Incref，
// 故本函数最后释放调用方的引用）。
void MapPut(Map* m, const char* key, Object* owned_value) {
	Object* k = String::FromCString(key); // Owned
	Object* old = m->__set_item__(k, owned_value);
	if (old != nullptr) Decref(old);
	Decref(k);
	Decref(owned_value);
}

// Map 的字符串键集合（忽略非字符串键）。
std::vector<std::string> MapStringKeys(Map* m) {
	std::vector<std::string> out;
	FixedList* keys = m->keys(); // Owned
	if (keys != nullptr) {
		for (std::size_t i = 0; i < keys->size(); ++i) {
			Object* k = keys->at(i);
			if (k != nullptr && IsString(k)) out.push_back(AsString(k));
		}
		Decref(keys);
	}
	return out;
}

// 把 List / FixedList 等可迭代对象收集为字符串集合（忽略非字符串元素）。
std::set<std::string> IterableStrings(Object* obj) {
	std::set<std::string> out;
	if (obj == nullptr || obj->is_type("None")) return out;
	Object* it = obj->__iterator__(); // Owned
	if (it == nullptr) return out;
	while (true) {
		Object* e = nullptr;
		try {
			e = it->__next__(); // Owned
		} catch (const StopIteration&) {
			break;
		}
		if (e != nullptr) {
			if (IsString(e)) out.insert(AsString(e));
			Decref(e);
		}
	}
	Decref(it);
	return out;
}

// bytecode.load_set(entry) -> Map
//   递归加载入口及其 import 依赖（经宿主 SourceModuleSetLoader 钩子）。
//   返回 { entry_name: String, modules: Map[name -> 字节码模块对象],
//          package_names: List[String] }。
Object* _builtin_load_set(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("load_set", {
		Extension::Arg::Required("entry"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string entry = require_string("load_set", "entry", r["entry"]);

	std::string entry_name;
	std::map<std::string, BC::Module*> raw;
	std::set<std::string> pkgs;
	LoadSourceModuleSet(entry, &entry_name, &raw, &pkgs);

	// 模块名 -> 字节码模块对象（shared_ptr 接管堆对象所有权）。
	Map* mods = Map::New();
	for (auto& kv : raw) {
		std::shared_ptr<BC::Module> sp(kv.second);
		MapPut(mods, kv.first.c_str(), BC::WrapModule(sp));
	}

	List* pkg_list = List::New();
	for (const std::string& p : pkgs) {
		Object* s = String::FromCString(p.c_str());
		pkg_list->append(s);
		Decref(s);
	}

	Map* out = Map::New();
	MapPut(out, "entry_name", String::FromCString(entry_name.c_str()));
	MapPut(out, "modules", mods);
	MapPut(out, "package_names", pkg_list);
	return out;
}

// ---- 包清单约定（与 moduletools 一致）----
constexpr const char* kCodegenFn   = "__codegen__";
constexpr const char* kProjectType = "Project";
constexpr const char* kExeAttr     = "executable_name";
constexpr const char* kKindsAttr   = "module_kinds";
constexpr const char* kAsProgram   = "as_program";
constexpr const char* kAsLibrary   = "as_library";

bool SymtabHas(const BC::Module& m, const char* name) {
	return std::find(m.symtab.begin(), m.symtab.end(), std::string(name)) !=
	       m.symtab.end();
}

// bytecode.eval_manifest(modules, entry_name [, package_names]) -> Map
//   在受控 VM（依赖以注册表注入、标记包模块）中执行包清单顶层一趟，读回：
//     has_codegen / role("program"/"library") / role_declared /
//     executable_name / module_kinds(Map[name -> "static"/"shared"])
//   跨模块的语义校验（已转译集合、名称冲突、角色与可执行名的匹配）留给
//   调用方（aot 模块）完成。
Object* _builtin_eval_manifest(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("eval_manifest", {
		Extension::Arg::Required("modules"),
		Extension::Arg::Required("entry_name"),
		Extension::Arg::Optional("package_names"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);

	Map* mods_map = dynamic_cast<Map*>(r["modules"]);
	if (mods_map == nullptr) {
		throw TypeError("eval_manifest: 'modules' must be a Map.");
	}
	const std::string entry_name =
		require_string("eval_manifest", "entry_name", r["entry_name"]);
	const std::set<std::string> pkgs = IterableStrings(r["package_names"]);

	// 收集模块；holders 保证底层 Module 在本次求值期间存活。
	std::vector<std::shared_ptr<BC::Module>> holders;
	std::map<std::string, BC::Module*> all;
	for (const std::string& name : MapStringKeys(mods_map)) {
		Object* key = String::FromCString(name.c_str()); // Owned
		Object* v = mods_map->__get_item__(key);         // Borrowed
		Decref(key);
		std::shared_ptr<BC::Module> sp = BC::UnwrapModule(v);
		if (!sp) continue;
		all[name] = sp.get();
		holders.push_back(std::move(sp));
	}
	auto it_entry = all.find(entry_name);
	if (it_entry == all.end()) {
		throw ValueError("eval_manifest: entry module '" + entry_name +
		                 "' is not in the module set.");
	}
	BC::Module* manifest = it_entry->second;

	const bool has_codegen = SymtabHas(*manifest, kCodegenFn);
	const bool has_role_api =
		SymtabHas(*manifest, kAsProgram) || SymtabHas(*manifest, kAsLibrary);

	Map* out = Map::New();
	MapPut(out, "has_codegen", has_codegen ? static_cast<Object*>(Boolean::True())
	                                       : static_cast<Object*>(Boolean::False()));

	// 粗筛：既无 __codegen__ 也未声明角色 -> 不执行顶层（零副作用）。
	if (!has_codegen && !has_role_api) {
		MapPut(out, "role", String::FromCString("library"));
		MapPut(out, "role_declared", Boolean::False());
		MapPut(out, "executable_name", String::FromCString(""));
		MapPut(out, "module_kinds", Map::New());
		return out;
	}

	// 依赖以注册表注入（排除入口自身），不退化到文件系统查找。
	std::map<std::string, BC::Module*> registry;
	for (auto& kv : all) {
		if (kv.first != entry_name) registry[kv.first] = kv.second;
	}

	ResetPackageRole();
	SetPackageRole(PackageRole::kLibrary, /*declared=*/false);

	BC::VM vm(manifest, &registry, entry_name);
	vm.set_package_names(pkgs);
	{
		Object* res = vm.run(); // Owned；异常由调用方处理
		if (res != nullptr) Decref(res);
	}

	const bool is_program = (GetPackageRole() == PackageRole::kProgram);
	MapPut(out, "role",
	       String::FromCString(is_program ? "program" : "library"));
	MapPut(out, "role_declared",
	       IsPackageRoleDeclared() ? static_cast<Object*>(Boolean::True())
	                               : static_cast<Object*>(Boolean::False()));

	std::string exe_name;
	Map* kinds = Map::New();
	if (has_codegen) {
		auto* ns = vm.get_entry_module()->get_namespace();
		auto it_fn = ns->find(kCodegenFn);
		if (it_fn == ns->end() || it_fn->second == nullptr ||
		    !it_fn->second->is_type("Function")) {
			Decref(kinds);
			throw ValueError("包清单 '" + entry_name + "' 未定义可调用的 " +
			                 kCodegenFn + "()。");
		}
		Object* proj = Call(it_fn->second, nullptr, 0); // Owned
		if (proj == nullptr) {
			Decref(kinds);
			throw ValueError("包清单 '" + entry_name + "' 的 " + kCodegenFn +
			                 "() 返回了空值。");
		}
		if (proj->type_name() != kProjectType) {
			Decref(proj);
			Decref(kinds);
			throw ValueError("包清单 '" + entry_name + "' 的 " + kCodegenFn +
			                 "() 必须返回 moduletools.Project。");
		}

		// 可执行名
		{
			Object* exe = GetAttr(proj, kExeAttr); // Owned
			if (exe != nullptr) {
				if (IsString(exe)) {
					exe_name = AsString(exe);
				} else if (!exe->is_type("None")) {
					Decref(exe);
					Decref(proj);
					Decref(kinds);
					throw ValueError("set_executable_name() 的参数必须是字符串。");
				}
				Decref(exe);
			}
		}
		// 子模块形态
		{
			Object* kinds_obj = GetAttr(proj, kKindsAttr); // Owned
			if (kinds_obj != nullptr) {
				Map* km = dynamic_cast<Map*>(kinds_obj);
				if (km == nullptr) {
					Decref(kinds_obj);
					Decref(proj);
					Decref(kinds);
					throw ValueError("__codegen__() 返回的 Project.module_kinds 不是 Map。");
				}
				for (const std::string& name : MapStringKeys(km)) {
					Object* key = String::FromCString(name.c_str()); // Owned
					Object* v = km->__get_item__(key);               // Borrowed
					Decref(key);
					if (v == nullptr || !IsString(v)) {
						Decref(kinds_obj);
						Decref(proj);
						Decref(kinds);
						throw ValueError("__codegen__() 中模块 '" + name +
						                 "' 的形态必须为 \"static\" 或 \"shared\"。");
					}
					const std::string kind = AsString(v);
					if (kind != "static" && kind != "shared") {
						Decref(kinds_obj);
						Decref(proj);
						Decref(kinds);
						throw ValueError("__codegen__() 中模块 '" + name +
						                 "' 的形态 '" + kind +
						                 "' 非法（只接受 static / shared）。");
					}
					MapPut(kinds, name.c_str(), String::FromCString(kind.c_str()));
				}
				Decref(kinds_obj);
			}
		}
		Decref(proj);
	}

	MapPut(out, "executable_name", String::FromCString(exe_name.c_str()));
	MapPut(out, "module_kinds", kinds);
	return out;
}

Module* make_bytecode_module() {
	Module* mod = Module::New(MODULE_NAME);
	mod->set_function("compile", _builtin_compile, /*with_keywords=*/true);
	mod->set_function("dump", _builtin_dump, /*with_keywords=*/true);
	mod->set_function("sanitize_module_name", _builtin_sanitize_module_name,
	                  /*with_keywords=*/true);
	mod->set_function("aot_config", _builtin_aot_config, /*with_keywords=*/true);
	mod->set_function("load_set", _builtin_load_set, /*with_keywords=*/true);
	mod->set_function("eval_manifest", _builtin_eval_manifest,
	                  /*with_keywords=*/true);
	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_bytecode）。
PYCP_EXPORT_MODULE(bytecode) {
	return make_bytecode_module();
}

} // namespace Pycp
