#include "aot/PycpScriptCodegen.hpp"

#include "vm/PycpBytecodeVM.hpp"
#include "loader/PycpModuleLoader.hpp"
#include "abi/PycpABI.hpp"
#include "object/PycpMap.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpString.hpp"
#include "object/PycpException.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace Pycp::AOT {

namespace {

// 常量：与 stdlib/moduletools 的约定保持一致。
constexpr const char* kCodegenFn   = "__codegen__";
constexpr const char* kProjectType = "Project";
constexpr const char* kExeAttr     = "executable_name";
constexpr const char* kKindsAttr   = "module_kinds";
constexpr const char* kAsProgram   = "as_program";
constexpr const char* kAsLibrary   = "as_library";

bool symtab_has(const BC::Module& m, const char* name) {
	return std::find(m.symtab.begin(), m.symtab.end(), std::string(name)) !=
	       m.symtab.end();
}

// 读 Project 的 module_kinds：Map[String]String，校验键与值。
bool read_kinds(Object* proj, const std::vector<std::string>& translated,
                std::map<std::string, ModuleKind>* out, std::string* err) {
	Object* kinds_obj = Pycp::GetAttr(proj, kKindsAttr); // Owned
	if (kinds_obj == nullptr) return true;
	Map* km = dynamic_cast<Map*>(kinds_obj);
	if (km == nullptr) {
		Pycp::Decref(kinds_obj);
		*err = "__codegen__() 返回的 Project.module_kinds 不是 Map。";
		return false;
	}
	Object* keys_obj = km->keys(); // Owned
	FixedList* keys = dynamic_cast<FixedList*>(keys_obj);
	if (keys == nullptr) {
		Pycp::Decref(keys_obj);
		Pycp::Decref(kinds_obj);
		*err = "__codegen__() 返回的 Project.module_kinds 无法枚举键。";
		return false;
	}
	bool ok = true;
	for (std::size_t i = 0; i < keys->size() && ok; ++i) {
		Object* k = keys->at(i);
		if (k == nullptr || !IsString(k)) {
			*err = "__codegen__() 的模块名必须为字符串。";
			ok = false;
			break;
		}
		const std::string name = AsString(k);
		if (std::find(translated.begin(), translated.end(), name) ==
		    translated.end()) {
			*err = "__codegen__() 指定了未参与转译的模块 '" + name +
			       "'（拼写错误？已转译模块：" ;
			for (std::size_t j = 0; j < translated.size(); ++j) {
				if (j != 0) *err += ", ";
				*err += translated[j];
			}
			*err += "）。";
			ok = false;
			break;
		}
		Object* v = km->__get_item__(k); // Borrowed
		if (v == nullptr || !IsString(v)) {
			*err = "__codegen__() 中模块 '" + name +
			       "' 的形态必须为 \"static\" 或 \"shared\"。";
			ok = false;
			break;
		}
		const std::string kind = AsString(v);
		if (kind == "static") {
			(*out)[name] = ModuleKind::kStatic;
		} else if (kind == "shared") {
			(*out)[name] = ModuleKind::kShared;
		} else {
			*err = "__codegen__() 中模块 '" + name + "' 的形态 '" + kind +
			       "' 非法（只接受 static / shared）。";
			ok = false;
			break;
		}
	}
	Pycp::Decref(keys_obj);
	Pycp::Decref(kinds_obj);
	return ok;
}

} // anonymous namespace

bool EvalPackageManifest(
	std::map<std::string, Pycp::BC::Module>& modules,
	const std::string& entry_name,
	const std::set<std::string>& package_names,
	const std::vector<std::string>& translated_modules,
	ScriptCodegenPlan* out, std::string* err) {
	if (out == nullptr) return false;
	auto it_entry = modules.find(entry_name);
	if (it_entry == modules.end()) {
		if (err != nullptr) {
			*err = "包清单求值失败：入口模块 '" + entry_name + "' 不在转译集合中。";
		}
		return false;
	}

	// ---- ① 粗筛：清单既无 __codegen__ 也未声明角色时，不执行顶层 ----
	const BC::Module& manifest = it_entry->second;
	const bool has_codegen = symtab_has(manifest, kCodegenFn);
	const bool has_role_api = symtab_has(manifest, kAsProgram) ||
	                          symtab_has(manifest, kAsLibrary);
	out->has_codegen = has_codegen;
	if (!has_codegen && !has_role_api) {
		// 默认：库角色、未声明、无配置。零副作用。
		out->role = Pycp::PackageRole::kLibrary;
		out->role_declared = false;
		return true;
	}

	// ---- ② 用独立 VM 执行清单顶层一趟 ----
	// 依赖以注册表注入：清单里的兄弟 import 直接命中同批模块，不退化到
	// 文件系统查找（避免受 cwd 影响）。
	std::map<std::string, BC::Module*> registry;
	for (auto& kv : modules) {
		if (kv.first == entry_name) continue;
		registry[kv.first] = &kv.second;
	}

	// 角色状态按趟重置：AOT 默认为「库 + 未声明」，清单可显式覆盖。
	Pycp::ResetPackageRole();
	Pycp::SetPackageRole(Pycp::PackageRole::kLibrary, /*declared=*/false);

	Pycp::BC::VM vm(const_cast<BC::Module*>(&manifest), &registry, entry_name);
	vm.set_package_names(package_names);
	try {
		Pycp::Object* r = vm.run();
		if (r != nullptr) Pycp::Decref(r);
	} catch (const Pycp::Exception& e) {
		if (err != nullptr) {
			*err = "执行包清单 '" + entry_name + "' 顶层失败：" + e.what();
		}
		return false;
	} catch (const std::exception& e) {
		if (err != nullptr) {
			*err = std::string("执行包清单 '") + entry_name + "' 顶层失败：" + e.what();
		}
		return false;
	}

	// ---- ③ 读回角色 ----
	out->role = Pycp::GetPackageRole();
	out->role_declared = Pycp::IsPackageRoleDeclared();

	// ---- ④ 读回 __codegen__ 的 Project ----
	if (!has_codegen) return true;

	auto* ns = vm.get_entry_module()->get_namespace();
	auto it_fn = ns->find(kCodegenFn);
	if (it_fn == ns->end() || it_fn->second == nullptr ||
	    !it_fn->second->is_type("Function")) {
		if (err != nullptr) {
			*err = "包清单 '" + entry_name + "' 未定义可调用的 " + kCodegenFn + "()。";
		}
		return false;
	}

	Pycp::Object* proj = nullptr;
	try {
		proj = Pycp::Call(it_fn->second, nullptr, 0); // Owned
	} catch (const Pycp::Exception& e) {
		if (err != nullptr) {
			*err = "包清单 '" + entry_name + "' 的 " + kCodegenFn +
			       "() 调用失败：" + e.what();
		}
		return false;
	}
	if (proj == nullptr) {
		if (err != nullptr) {
			*err = "包清单 '" + entry_name + "' 的 " + kCodegenFn + "() 返回了空值。";
		}
		return false;
	}
	if (std::string(proj->type_name()) != kProjectType) {
		Pycp::Decref(proj);
		if (err != nullptr) {
			*err = "包清单 '" + entry_name + "' 的 " + kCodegenFn +
			       "() 必须返回 moduletools.Project。";
		}
		return false;
	}

	bool ok = true;
	// 可执行名
	{
		Pycp::Object* exe = Pycp::GetAttr(proj, kExeAttr); // Owned
		if (exe != nullptr) {
			if (Pycp::IsString(exe)) {
				out->executable_name = AsString(exe);
			} else if (!exe->is_type("None")) {
				if (err != nullptr) {
					*err = "set_executable_name() 的参数必须是字符串。";
				}
				ok = false;
			}
			Pycp::Decref(exe);
		}
	}
	// 子模块形态
	if (ok) ok = read_kinds(proj, translated_modules, &out->kinds, err);

	Pycp::Decref(proj);
	if (!ok) return false;

	// ---- ⑤ 用途校验 ----
	// 库角色下指定可执行名：几乎总是「作者以为这是入口」的信号，静默忽略
	// 最难排查，故直接中止并给出可操作提示。
	if (!out->executable_name.empty() &&
	    out->role == Pycp::PackageRole::kLibrary) {
		if (err != nullptr) {
			*err = "__codegen__() 指定了 set_executable_name(\"" +
			       out->executable_name +
			       "\")，但本包按【库】角色转译（AOT 默认为库；如需生成可执行"
			       "项目，请在 " + Pycp::MODULE_MANIFEST_FILENAME +
			       " 中调用 moduletools.as_program()）。可执行文件名由入口项目决定。";
		}
		return false;
	}
	// 可执行名不得与任何模块名（含点号全名与其 sanitize 形式）重名：
	// CMake 生成器用 spec.name 兼作依赖图入口 token（deps[exe]）。
	if (!out->executable_name.empty()) {
		for (const std::string& m : translated_modules) {
			if (m == out->executable_name ||
			    Pycp::SanitizeModuleName(m) == out->executable_name) {
				if (err != nullptr) {
					*err = "set_executable_name(\"" + out->executable_name +
					       "\") 与模块名冲突（CMake 目标 / 依赖图入口键同名）。";
				}
				return false;
			}
		}
	}
	return true;
}

} // namespace Pycp::AOT
