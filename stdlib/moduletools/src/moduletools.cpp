#include "moduletools.hpp"

#include "PycpModule.hpp"    // runtime 的 Module 完整定义（含包角色状态）
#include "PycpFunction.hpp"
#include "PycpString.hpp"
#include "PycpInteger.hpp"
#include "PycpBoolean.hpp"
#include "PycpNone.hpp"
#include "PycpMap.hpp"
#include "PycpFixedList.hpp"
#include "PycpClass.hpp"
#include "PycpABI.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpExtension.hpp" // 扩展唯一对外头（导出宏 + set_* + 参数规范框架）

namespace Pycp {

namespace {

// =============================================================
// 辅助：把原生函数挂到自定义对象上（作为普通成员，调用时经 GetAttr
// 包装为 BoundMethod，self 自动注入）。
//   __set_attribute__ 内部 Incref，故此处释放本地那份引用。
// =============================================================
void attach_method(Object* self, const char* name, PycpCFunction fn) {
	Function* f = Pycp::New<Function>(name, fn);
	self->__set_attribute__(name, f);
	Decref(f);
}

class ModuleSpecObject;   // Project::__get_item__ 的返回类型（定义见下）

// =============================================================
// Project：__codegen__() 返回的项目描述对象
//
//   proj.set_executable_name(s)   设置可执行文件 / CMake 目标名
//   proj["<模块名>"]              返回 ModuleSpec（惰性，不写回）
//   proj.executable_name          String 或 None（AOT 侧读取）
//   proj.module_kinds             Map[String]String（AOT 侧读取）
//
// AOT 侧只用通用 ABI（GetAttr）读取这两个属性，故无需导出任何私有符号。
// =============================================================
class ProjectObject : public Object {
private:
	Map*    kinds_;      // 模块名 -> "static" / "shared"
	Object* exe_name_;   // String 或 None

public:
	explicit ProjectObject(const std::string& name)
		: Object(name), kinds_(Map::New()), exe_name_(None::instance) {
		Incref(exe_name_);
		set_type_info(PycpTypeId::Unknown, PycpTypeFlag::None);
		attach_method(this, "set_executable_name", _set_executable_name);
	}
	~ProjectObject() override {
		Decref(kinds_);
		Decref(exe_name_);
	}

	Map* kinds() { return kinds_; }

	// AOT 侧读取口：返回 Borrowed（GetAttr 会转 Owned）。
	Object* __get_attribute__(const std::string& name) override {
		if (name == "module_kinds")    return kinds_;
		if (name == "executable_name") return exe_name_;
		return Object::__get_attribute__(name);
	}

	// proj["<模块名>"] -> ModuleSpec（惰性绑定，不写回形态表）。
	// 定义在 ModuleSpecObject 之后（需要其完整类型）。
	Object* __get_item__(Object* key) override;

	Object* __string__() override {
		return String::FromCString("<moduletools.Project>");
	}

	void foreach_ref(const std::function<void(Object*)>& visit) override {
		Object::foreach_ref(visit);
		if (kinds_ != nullptr) visit(kinds_);
		if (exe_name_ != nullptr) visit(exe_name_);
	}

private:
	static Object* _set_executable_name(Object* self, FixedList* args, Map* kwargs);
};

// =============================================================
// ModuleSpec：proj["<模块名>"] 返回的形态说明对象
//
// 惰性绑定 {project, 模块名}，只在调用 .static() / .shared() 时才写回
// Project 的形态表——避免「只取下标不设形态」被当成某种默认值。
// =============================================================
class ModuleSpecObject : public Object {
public:
	ProjectObject* project_;   // 所属 Project（持有引用）
	std::string    module_name_;

	ModuleSpecObject(ProjectObject* proj, const std::string& module_name)
		: Object("ModuleSpec"), project_(proj), module_name_(module_name) {
		Incref(project_);
		set_type_info(PycpTypeId::Unknown, PycpTypeFlag::None);
		attach_method(this, "static", _spec_static);
		attach_method(this, "shared", _spec_shared);
	}
	~ModuleSpecObject() override { Decref(project_); }

	// 写回形态表：kind 为 "static" / "shared"。
	void set_kind(const char* kind) {
		Object* key = String::FromCString(module_name_.c_str()); // Owned
		Object* val = String::FromCString(kind);                 // Owned
		// Map::__set_item__ 内部 Incref 键值，故此处释放本地引用。
		Object* r = project_->kinds()->__set_item__(key, val);
		if (r != nullptr) Decref(r);
		Decref(key);
		Decref(val);
	}

	Object* __string__() override {
		return String::FromCString(("<moduletools.ModuleSpec " + module_name_ + ">").c_str());
	}

	void foreach_ref(const std::function<void(Object*)>& visit) override {
		Object::foreach_ref(visit);
		if (project_ != nullptr) visit(project_);
	}

private:
	static Object* _spec_static(Object* self, FixedList* args, Map* kwargs);
	static Object* _spec_shared(Object* self, FixedList* args, Map* kwargs);
};

// Project() 构造：无参。
Object* _project_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec =
		Extension::CompileArgs("Project", {});
	spec.Bind(args, kwargs);
	return Pycp::New<ProjectObject>("Project");
}

// proj.set_executable_name(name)
Object* ProjectObject::_set_executable_name(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"set_executable_name", { Extension::Arg::Required("name") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* name = r["name"];
	if (name == nullptr || !IsString(name)) {
		throw TypeError("set_executable_name() expects a string name.");
	}
	ProjectObject* proj = static_cast<ProjectObject*>(self);
	Decref(proj->exe_name_);
	proj->exe_name_ = name;
	Incref(proj->exe_name_);
	return None::instance;
}

// proj["<模块名>"]
Object* ProjectObject::__get_item__(Object* key) {
	if (key == nullptr || !IsString(key)) {
		throw TypeError("moduletools.Project[...] expects a string module name.");
	}
	return Pycp::New<ModuleSpecObject>(this, AsString(key));
}

// spec.static() / spec.shared()
Object* ModuleSpecObject::_spec_static(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("static", {});
	spec.Bind(args, kwargs);
	static_cast<ModuleSpecObject*>(self)->set_kind("static");
	return None::instance;
}

Object* ModuleSpecObject::_spec_shared(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("shared", {});
	spec.Bind(args, kwargs);
	static_cast<ModuleSpecObject*>(self)->set_kind("shared");
	return None::instance;
}

// =============================================================
// this()：当前包模块对象
// =============================================================
Object* _moduletools_this(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("this", {});
	spec.Bind(args, kwargs);
	// 包上下文优先（由 VM 在执行包清单顶层期间设置）；无包上下文时退化到
	// 「当前模块」，使 moduletools.this() 在普通模块内也有意义。
	Module* m = GetCurrentPackage();
	if (m == nullptr) m = current_module_;
	if (m == nullptr) {
		throw RuntimeError("moduletools.this(): no current module context.");
	}
	Incref(m); // 返回 Owned
	return m;
}

// =============================================================
// 角色声明与查询
// =============================================================
Object* _as_program(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("as_program", {});
	spec.Bind(args, kwargs);
	SetPackageRole(PackageRole::kProgram, /*declared=*/true);
	return None::instance;
}

Object* _as_library(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("as_library", {});
	spec.Bind(args, kwargs);
	SetPackageRole(PackageRole::kLibrary, /*declared=*/true);
	return None::instance;
}

Object* _role(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("role", {});
	spec.Bind(args, kwargs);
	return String::FromCString(GetPackageRole() == PackageRole::kProgram
	                               ? "program" : "library");
}

Object* _is_program(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("is_program", {});
	spec.Bind(args, kwargs);
	return GetPackageRole() == PackageRole::kProgram ? Boolean::True()
	                                                 : Boolean::False();
}

Object* _is_library(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("is_library", {});
	spec.Bind(args, kwargs);
	return GetPackageRole() == PackageRole::kLibrary ? Boolean::True()
	                                                 : Boolean::False();
}

Module* make_moduletools_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 与其它 stdlib 扩展一致：注入 __name__ = 模块名。
	mod->set_variable("__name__", String::FromCString(MODULE_NAME));

	// 当前包模块对象。
	mod->set_function("this", _moduletools_this);

	// 角色声明 / 查询。
	mod->set_function("as_program", _as_program);
	mod->set_function("as_library", _as_library);
	mod->set_function("role",       _role);
	mod->set_function("is_program", _is_program);
	mod->set_function("is_library", _is_library);

	// Project 类型（__codegen__ 的返回载体）：有构造回调，调用 Project()
	// 直接产出 ProjectObject 而非 Instance。
	mod->set_type("Project", _project_ctor, /*initialize=*/nullptr,
	              /*table=*/nullptr);

	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_moduletools，按模块名导出）。
PYCP_EXPORT_MODULE(moduletools) {
	return make_moduletools_module();
}

} // namespace Pycp
