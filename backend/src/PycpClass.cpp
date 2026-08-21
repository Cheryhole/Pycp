#include "PycpClass.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpString.hpp"
#include "PycpNone.hpp"
#include "PycpABI.hpp"
#include "PycpConfig.hpp"
#include "PycpMagic.hpp"

#include <sstream>
#include <vector>

namespace Pycp {

// =============================================================
// 方法内部访问标志（thread_local）
// =============================================================
namespace {
thread_local int g_internal_access_depth = 0;
thread_local std::vector<Instance*> g_current_self_stack;
}

int internal_access_depth() { return g_internal_access_depth; }
void enter_internal_access() { ++g_internal_access_depth; }
void leave_internal_access() { --g_internal_access_depth; }

// =============================================================
// 当前 self 上下文（thread_local 栈）
// =============================================================
void push_current_self(Instance* self) { g_current_self_stack.push_back(self); }
void pop_current_self() {
	if (!g_current_self_stack.empty()) g_current_self_stack.pop_back();
}
Instance* current_self() {
	if (g_current_self_stack.empty()) return nullptr;
	return g_current_self_stack.back();
}

// 将指针格式化为十六进制地址字符串（0x...）。
static std::string ptr_address(const void* p) {
	std::ostringstream oss;
	oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(p);
	return oss.str();
}

// =============================================================
// Class
// =============================================================

Class* Class::New(const std::string& name) {
	return Pycp::New<Class>(name);
}

Instance* Instance::New(Class* cls) {
	return Pycp::New<Instance>(cls);
}

Class::Class(const std::string& name)
	: Object(name), name_(name), parent_(nullptr) {}

Class::~Class() {
	for (auto& kv : methods_) {
		if (kv.second != nullptr) Decref(kv.second);
	}
	methods_.clear();
}

void Class::AddMemberName(Class* cls, const std::string& name) {
	if (cls == nullptr) throw TypeError("cannot add member to null class.");
	cls->add_member_name(name);
}

void Class::AddMethod(Class* cls, const std::string& name, Object* fn) {
	if (cls == nullptr) throw TypeError("cannot add method to null class.");
	if (fn == nullptr || !fn->is_type("Function"))
		throw TypeError("method must be a function.");
	cls->add_method(name, static_cast<Function*>(fn));
}

void Class::set_parent(Class* parent) {
	// 父类为借用引用（不 Incref，避免循环引用导致泄漏；
	// 父类生命周期由模块命名空间保证）。
	parent_ = parent;
}

void Class::add_member_name(const std::string& name) {
	add_member_name(name, false);
}

void Class::add_member_name(const std::string& name, bool is_private) {
	member_names_.push_back(name);
	member_visibility_[name] = is_private;
}

bool Class::member_is_private(const std::string& name) const {
	auto it = member_visibility_.find(name);
	if (it == member_visibility_.end()) return false;
	return it->second;
}

void Class::add_method(const std::string& name, Function* fn) {
	add_method(name, fn, false);
}

void Class::add_method(const std::string& name, Function* fn, bool is_private) {
	// 覆盖旧方法：先释放旧引用。
	auto it = methods_.find(name);
	if (it != methods_.end()) {
		if (it->second != nullptr) Decref(it->second);
		it->second = fn;
	} else {
		methods_[name] = fn;
	}
	if (fn != nullptr) Incref(fn);
	method_visibility_[name] = is_private;
}

bool Class::method_is_private(const std::string& name) const {
	auto it = method_visibility_.find(name);
	if (it == method_visibility_.end()) return false;
	return it->second;
}

Function* Class::find_method(const std::string& name) const {
	auto it = methods_.find(name);
	if (it == methods_.end()) return nullptr;
	return it->second;
}

std::vector<std::string> Class::method_names() const {
	std::vector<std::string> names;
	names.reserve(methods_.size());
	for (const auto& kv : methods_) names.push_back(kv.first);
	return names;
}

Object* Class::__get_attribute__(const std::string& name) {
	// 0) 内建只读属性 __name__：返回类型名（type_name()）对应的 String。
	if (name == "__name__") {
		return GetNameAttribute(this);
	}
	// 1) 先从成员字典中查找（支持动态 set attribute）。
	auto itm = members_.find(name);
	if (itm != members_.end() && itm->second != nullptr) {
		Incref(itm->second);
		return itm->second;
	}
	// 2) 查找方法。
	Function* fn = find_method(name);
	if (fn != nullptr) {
		return fn;
	}
	// 3) 回退到魔术方法分派（如 __members__/__string__ 等），让
	//    Class.__members__()、Class.__string__() 等可经魔术方法调用，
	//    而不仅依赖注册到 methods_ 的普通方法。
	if (Pycp::IsMagicMethodName(name)) {
		Function* magic = static_cast<Function*>(Pycp::GetMagicMethodFunction(name));
		if (magic != nullptr) {
			// 类对象上的魔术方法不绑定 self（Class 非实例），直接返回 Function。
			Incref(magic);
			return magic;
		}
	}
	throw AttributeError("class '" + name_ + "' has no attribute '" + name + "'");
}

Object* Class::__string__() {
	// 匿名类（内部名为 @anonymous）输出 "@anonymous"。
	if (name_ == ANONYMOUS_CLASS) {
		return String::FromCString("@anonymous");
	}
	// 普通类："<class \"name\">"。
	return String::FromCString(("<class \"" + name_ + "\">").c_str());
}

Object* Class::__members__() {
	// 返回类的方法名 + 通用成员。
	List* lst = static_cast<List*>(Object::__members__());
	for (const auto& n : method_names()) {
		bool found = false;
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* elem = lst->at(i);
			if (elem != nullptr && elem->is_type("String") &&
			    static_cast<String*>(elem)->get_value() == n) {
				found = true;
				break;
			}
		}
		if (!found) lst->append(String::FromCString(n.c_str()));
	}
	return lst;
}

void Class::foreach_ref(const std::function<void(Object*)>& visit) {
	for (auto& kv : methods_) {
		if (kv.second != nullptr) visit(kv.second);
	}
}

Object* Class::instantiate(Object** argv, std::size_t argc) {
	Instance* inst = Pycp::New<Instance>(this);

	// 1) 应用成员初始值（若类定义了隐式 __init_defaults__）。
	Function* init_defaults = find_method("__init_defaults__");
	if (init_defaults != nullptr) {
		Object* self = inst;
		Object* dv[1] = { self };
		Object* r = init_defaults->invoke(dv, 1);
		if (r != nullptr) Decref(r);
	}

	// 2) 调用 __initialize__（若定义）。
	Function* init = find_method(Pycp::MAGIC_INITIALIZE);
	if (init != nullptr) {
		std::vector<Object*> init_args;
		init_args.reserve(argc + 1);
		init_args.push_back(inst);
		for (std::size_t i = 0; i < argc; ++i) init_args.push_back(argv[i]);
		Object* r = init->invoke(init_args.data(), init_args.size());
		if (r != nullptr) Decref(r);
	}

	return inst;
}

// =============================================================
// BuiltinTypeClass
// =============================================================

BuiltinTypeClass::BuiltinTypeClass(const std::string& name, PycpNativeFunction ctor)
	: Class(name), ctor_(ctor) {}

Object* BuiltinTypeClass::instantiate(Object** argv, std::size_t argc) {
	if (ctor_ == nullptr) {
		throw TypeError("builtin type '" + std::string(get_name()) + "' has no constructor.");
	}
	// 不在此强制 argc == 1：各类型构造器（ctor_）自行校验参数个数
	// （如 io.File 支持 1 或 2 个参数 path [, mode]；Pycp.String/Integer/
	// List 仍各自要求 argc == 1）。这样 BuiltinTypeClass 既能表达单参
	// 类型构造，也能表达带可选参数的类型构造。
	if (argv == nullptr) {
		throw TypeError("builtin type '" + std::string(get_name()) +
		                "' argument array is null.");
	}
	return ctor_(nullptr, argv, argc);
}

// =============================================================
// Instance
// =============================================================

Instance::Instance(Class* cls)
	: Object(cls != nullptr ? cls->get_name() : "@anonymous"), cls_(cls) {
	if (cls_ != nullptr) Incref(cls_);
}

Instance::~Instance() {
	for (auto& kv : fields_) {
		if (kv.second != nullptr) Decref(kv.second);
	}
	fields_.clear();
	if (cls_ != nullptr) Decref(cls_);
}

Object* Instance::__get_attribute__(const std::string& name) {
	// 0) 内建只读属性 __name__：返回类型名（type_name()）对应的 String。
	if (name == "__name__") {
		return GetNameAttribute(this);
	}
	// 1) 先从成员字典中查找（支持动态 set attribute）。
	auto itm = members_.find(name);
	if (itm != members_.end() && itm->second != nullptr) {
		Incref(itm->second);
		return itm->second;
	}
	// 2) 仅字段访问；未找到返回 nullptr（方法访问经 get_bound_method）。
	auto it = fields_.find(name);
	if (it != fields_.end()) {
		// 可见性检查：外部访问 private 字段抛 AttributeError。
		if (cls_ != nullptr && cls_->member_is_private(name) &&
		    internal_access_depth() == 0) {
			throw AttributeError("'" + name + "' is private in class '" +
			                     cls_->get_name() + "'");
		}
		return it->second;
	}
	// 3) 魔术方法：回退到通用分派（转发到类同名魔术方法）。
	if (Object* magic = GetMagicMethodFunction(name)) {
		return magic;
	}
	return nullptr;
}

Object* Instance::get_bound_method(const std::string& name) {
	if (cls_ == nullptr) return nullptr;
	Function* fn = cls_->find_method(name);
	if (fn == nullptr) return nullptr;
	// 可见性检查：外部访问 private 方法抛 AttributeError。
	if (cls_->method_is_private(name) && internal_access_depth() == 0) {
		throw AttributeError("'" + name + "' is private in class '" +
		                     cls_->get_name() + "'");
	}
	return Pycp::New<BoundMethod>(this, fn);
}

Object* Instance::__members__() {
	// 字段名 + 类方法名 + 通用成员。
	List* lst = static_cast<List*>(Object::__members__());
	std::vector<std::string> extra;
	for (const auto& kv : fields_) extra.push_back(kv.first);
	if (cls_ != nullptr) {
		for (const auto& m : cls_->method_names()) extra.push_back(m);
	}
	for (const auto& n : extra) {
		bool found = false;
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* elem = lst->at(i);
			if (elem != nullptr && elem->is_type("String") &&
			    static_cast<String*>(elem)->get_value() == n) {
				found = true;
				break;
			}
		}
		if (!found) lst->append(String::FromCString(n.c_str()));
	}
	return lst;
}

void Instance::__set_attribute__(const std::string& name, Object* value) {
	// 可见性检查：外部写入 private 字段抛 AttributeError。
	if (cls_ != nullptr && cls_->member_is_private(name) &&
	    internal_access_depth() == 0) {
		Decref(value);
		throw AttributeError("'" + name + "' is private in class '" +
		                     cls_->get_name() + "'");
	}
	// 优先写入 fields_（类声明的字段），否则写入 members_（动态属性）。
	auto itf = fields_.find(name);
	if (itf != fields_.end()) {
		if (itf->second != nullptr) Decref(itf->second);
		itf->second = value;
		if (value != nullptr) Incref(value);
		return;
	}
	// 动态属性写入 members_（由基类管理引用计数）。
	Object::__set_attribute__(name, value);
}

Object* Instance::__string__() {
	if (cls_ != nullptr) {
		Function* fn = cls_->find_method("__string__");
		if (fn != nullptr) {
			Object* self = this;
			Object* argv[1] = { self };
			return fn->invoke(argv, 1);
		}
	}
	// 默认表示："<ClassName instance at 0xADDR>"
	return String::FromCString(("<" + std::string(cls_ ? cls_->get_name() : "?") +
	                          " instance at " + ptr_address(this) + ">").c_str());
}

Object* Instance::__integer__() {
	if (cls_ == nullptr) {
		throw TypeError("instance has no class.");
	}
	Function* fn = cls_->find_method("__integer__");
	if (fn == nullptr) {
		throw TypeError("class '" + std::string(cls_->get_name()) + "' does not define '__integer__'");
	}
	Object* self = this;
	Object* argv[1] = { self };
	return fn->invoke(argv, 1);
}

Object* Instance::dispatch_magic(const std::string& name, Object* other) {
	if (cls_ == nullptr) {
		throw TypeError("instance has no class.");
	}
	Function* fn = cls_->find_method(name);
	if (fn == nullptr) {
		throw TypeError("class '" + std::string(cls_->get_name()) + "' does not define '" + name + "'");
	}
	Object* self = this;
	if (other == nullptr) {
		Object* argv[1] = { self };
		return fn->invoke(argv, 1);
	}
	Object* argv[2] = { self, other };
	return fn->invoke(argv, 2);
}

Object* Instance::__negation__() {
	return dispatch_magic("__negation__", nullptr);
}

Object* Instance::__addition__(Object* other) {
	return dispatch_magic("__addition__", other);
}

Object* Instance::__subtraction__(Object* other) {
	return dispatch_magic("__subtraction__", other);
}

Object* Instance::__multiplication__(Object* other) {
	return dispatch_magic("__multiplication__", other);
}

Object* Instance::__division__(Object* other) {
	return dispatch_magic("__division__", other);
}

Object* Instance::__power__(Object* other) {
	return dispatch_magic("__power__", other);
}

Object* Instance::__less_than__(Object* other) {
	return dispatch_magic("__less_than__", other);
}

Object* Instance::__less_equal__(Object* other) {
	return dispatch_magic("__less_equal__", other);
}

Object* Instance::__equal__(Object* other) {
	return dispatch_magic("__equal__", other);
}

Object* Instance::__not_equal__(Object* other) {
	return dispatch_magic("__not_equal__", other);
}

Object* Instance::__greater_than__(Object* other) {
	return dispatch_magic("__greater_than__", other);
}

Object* Instance::__greater_equal__(Object* other) {
	return dispatch_magic("__greater_equal__", other);
}

Object* Instance::__get_item__(Object* key) {
	// 下标访问：转发到类的 __get_item__(self, key) 方法。
	if (cls_ == nullptr) {
		throw TypeError("instance has no class.");
	}
	Function* fn = cls_->find_method("__get_item__");
	if (fn == nullptr) {
		throw TypeError("class '" + std::string(cls_->get_name()) +
		                "' does not define '__get_item__'");
	}
	Object* self = this;
	Object* argv[2] = { self, key };
	return fn->invoke(argv, 2);
}

Object* Instance::__set_item__(Object* key, Object* value) {
	// 下标赋值：转发到类的 __set_item__(self, key, value) 方法。
	if (cls_ == nullptr) {
		throw TypeError("instance has no class.");
	}
	Function* fn = cls_->find_method("__set_item__");
	if (fn == nullptr) {
		throw TypeError("class '" + std::string(cls_->get_name()) +
		                "' does not define '__set_item__'");
	}
	Object* self = this;
	Object* argv[3] = { self, key, value };
	return fn->invoke(argv, 3);
}

Object* Instance::__list__() {
	// list 转换：转发到类的 __list__(self) 方法。
	if (cls_ == nullptr) {
		throw TypeError("instance has no class.");
	}
	Function* fn = cls_->find_method("__list__");
	if (fn == nullptr) {
		throw TypeError("class '" + std::string(cls_->get_name()) +
		                "' does not define '__list__'");
	}
	Object* self = this;
	Object* argv[1] = { self };
	return fn->invoke(argv, 1);
}

void Instance::foreach_ref(const std::function<void(Object*)>& visit) {
	if (cls_ != nullptr) visit(cls_);
	for (auto& kv : fields_) {
		if (kv.second != nullptr) visit(kv.second);
	}
}

// =============================================================
// BoundMethod
// =============================================================

BoundMethod::BoundMethod(Object* inst, Function* method)
	: Function(method != nullptr ? method->get_name() : ""),
	  instance_(inst), method_(method) {
	if (instance_ != nullptr) {
		Incref(instance_);
	}
	if (method_ != nullptr) Incref(method_);
}

BoundMethod::~BoundMethod() {
	if (instance_ != nullptr) {
		Decref(instance_);
	}
	if (method_ != nullptr) Decref(method_);
	instance_ = nullptr;
	method_ = nullptr;
}

Object* BoundMethod::invoke(Object** argv, std::size_t argc) {
	if (method_ == nullptr) {
		throw TypeError("bound method has no underlying method.");
	}
	// 构造实参数组：self（实例）+ 用户实参。
	std::vector<Object*> args;
	args.reserve(argc + 1);
	args.push_back(instance_);
	for (std::size_t i = 0; i < argc; ++i) args.push_back(argv[i]);
	return method_->invoke(args.data(), args.size());
}

void BoundMethod::foreach_ref(const std::function<void(Object*)>& visit) {
	if (instance_ != nullptr) visit(instance_);
	if (method_ != nullptr) visit(method_);
}

} // namespace Pycp