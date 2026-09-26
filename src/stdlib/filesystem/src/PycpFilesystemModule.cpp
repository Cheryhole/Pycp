#include "filesystem_stdlib.hpp"
#include "file_common.hpp"            // common：File 类型类模块绑定样板

#include "object/PycpModule.hpp"
#include "object/PycpFunction.hpp"
#include "object/PycpString.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpList.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpExtension.hpp"   // 参数规范框架 / PYCP_EXPORT_MODULE
#include "object/PycpFile.hpp"        // File / File_method_table（已上提至运行时）
#include "object/PycpException.hpp"

#include "abi/PycpNativeExt.hpp"      // GetExeDir / GetStdlibDir
#include "abi/PycpABI.hpp"            // IsFalse（真值判定）

#include <cstdlib>
#include <filesystem>
#include <string>

namespace Pycp {

namespace {

namespace fs = std::filesystem;

std::string require_string(const char* fn, const char* param, Object* v) {
	if (v == nullptr || !IsString(v)) {
		throw TypeError(std::string(fn) + ": argument '" + param +
		                "' expects a string, got '" +
		                (v != nullptr ? v->type_name() : std::string("None")) + "'.");
	}
	return AsString(v);
}

Object* BoolObj(bool b) {
	return b ? static_cast<Object*>(Boolean::True())
	         : static_cast<Object*>(Boolean::False());
}

// mkdir(path [, parents])
Object* _builtin_mkdir(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("mkdir", {
		Extension::Arg::Required("path"),
		Extension::Arg::Optional("parents"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string path = require_string("mkdir", "path", r["path"]);
	const bool parents = !IsFalse(r["parents"]);

	std::error_code ec;
	if (parents) {
		fs::create_directories(path, ec);
	} else {
		fs::create_directory(path, ec);
	}
	if (ec) {
		throw RuntimeError("mkdir: cannot create directory '" + path +
		                   "': " + ec.message());
	}
	return None::instance;
}

// listdir(path) -> List[String]
Object* _builtin_listdir(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("listdir", {
		Extension::Arg::Required("path"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string path = require_string("listdir", "path", r["path"]);

	std::error_code ec;
	if (!fs::is_directory(path, ec)) {
		throw RuntimeError("listdir: not a directory: '" + path + "'.");
	}
	List* out = List::New();
	for (const fs::directory_entry& entry : fs::directory_iterator(path, ec)) {
		Object* name = String::FromCString(entry.path().filename().string().c_str());
		out->append(name);
		Decref(name);
	}
	return out;
}

// join(*parts) -> String（绝对分量会重置，语义同 os.path.join）
Object* _builtin_join(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("join", {
		Extension::Arg::Rest("parts"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* parts_obj = r["parts"];
	if (parts_obj == nullptr) return String::FromCString("");
	FixedList* parts = static_cast<FixedList*>(parts_obj);

	fs::path p;
	for (std::size_t i = 0; i < parts->size(); ++i) {
		Object* e = parts->at(i);
		if (!IsString(e)) throw TypeError("join: all parts must be strings.");
		p /= AsString(e);
	}
	return String::FromCString(p.generic_string().c_str());
}

// basename(path) -> String
Object* _builtin_basename(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("basename", {
		Extension::Arg::Required("path"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string path = require_string("basename", "path", r["path"]);
	return String::FromCString(fs::path(path).filename().string().c_str());
}

// dirname(path) -> String（无父目录时返回 "."）
Object* _builtin_dirname(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("dirname", {
		Extension::Arg::Required("path"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string path = require_string("dirname", "path", r["path"]);
	std::string parent = fs::path(path).parent_path().generic_string();
	if (parent.empty()) parent = ".";
	return String::FromCString(parent.c_str());
}

// exists / is_file / is_dir
Object* _builtin_exists(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("exists", {
		Extension::Arg::Required("path"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string path = require_string("exists", "path", r["path"]);
	std::error_code ec;
	return BoolObj(fs::exists(path, ec));
}

Object* _builtin_is_file(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("is_file", {
		Extension::Arg::Required("path"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string path = require_string("is_file", "path", r["path"]);
	std::error_code ec;
	return BoolObj(fs::is_regular_file(path, ec));
}

Object* _builtin_is_dir(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("is_dir", {
		Extension::Arg::Required("path"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string path = require_string("is_dir", "path", r["path"]);
	std::error_code ec;
	return BoolObj(fs::is_directory(path, ec));
}

// getenv(name [, default]) -> String 或 default（未给出时 None）
Object* _builtin_getenv(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("getenv", {
		Extension::Arg::Required("name"),
		Extension::Arg::Optional("default"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	const std::string name = require_string("getenv", "name", r["name"]);

	if (const char* v = std::getenv(name.c_str())) {
		return String::FromCString(v);
	}
	Object* def = r["default"];
	if (def == nullptr) def = None::instance;
	Incref(def); // 以 Owned 语义返回
	return def;
}

// cwd() -> String
Object* _builtin_cwd(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("cwd", {});
	spec.Bind(args, kwargs);
	std::error_code ec;
	return String::FromCString(fs::current_path(ec).generic_string().c_str());
}

// exe_dir() / stdlib_dir()
Object* _builtin_exe_dir(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("exe_dir", {});
	spec.Bind(args, kwargs);
	return String::FromCString(GetExeDir().c_str());
}

Object* _builtin_stdlib_dir(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("stdlib_dir", {});
	spec.Bind(args, kwargs);
	return String::FromCString(GetStdlibDir().c_str());
}

Module* make_filesystem_module() {
	Module* mod = Module::New(MODULE_NAME);

	mod->set_function("mkdir", _builtin_mkdir, /*with_keywords=*/true);
	mod->set_function("listdir", _builtin_listdir, /*with_keywords=*/true);
	mod->set_function("join", _builtin_join, /*with_keywords=*/true);
	mod->set_function("basename", _builtin_basename, /*with_keywords=*/true);
	mod->set_function("dirname", _builtin_dirname, /*with_keywords=*/true);
	mod->set_function("exists", _builtin_exists, /*with_keywords=*/true);
	mod->set_function("is_file", _builtin_is_file, /*with_keywords=*/true);
	mod->set_function("is_dir", _builtin_is_dir, /*with_keywords=*/true);
	mod->set_function("getenv", _builtin_getenv, /*with_keywords=*/true);
	mod->set_function("cwd", _builtin_cwd, /*with_keywords=*/true);
	mod->set_function("exe_dir", _builtin_exe_dir, /*with_keywords=*/true);
	mod->set_function("stdlib_dir", _builtin_stdlib_dir, /*with_keywords=*/true);

	// File：绑定运行时唯一的 File 类型类——与 io.File 指向同一对象
	// （别名）；样板已上提到 stdlib/common/file_common.hpp。
	BindFileType(mod);

	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_filesystem）。
PYCP_EXPORT_MODULE(filesystem) {
	return make_filesystem_module();
}

} // namespace Pycp
