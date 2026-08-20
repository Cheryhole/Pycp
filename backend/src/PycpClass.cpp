#include "PycpClass.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpString.hpp"
#include "PycpNone.hpp"
#include "PycpABI.hpp"
#include "PycpConfig.hpp"

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

Object* Class::__getattr__(const std::string& name) {
	Function* fn = find_method(name);
	if (fn == nullptr) {
		throw AttributeError("class '" + name_ + "' has no attribute '" + name + "'");
	}
	return fn;
}

Object* Class::__string__() {
	// 匿名类（内部名为 @anonymous）输出 "@anonymous"。
	if (name_ == ANONYMOUS_CLASS) {
		return String::FromCString("@anonymous");
	}
	// 普通类："<class \"name\">"。
	return String::FromCString(("<class \"" + name_ + "\">").c_str());
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
	if (argc != 1) {
		throw TypeError("builtin type '" + std::string(get_name()) + "' expects exactly 1 argument.");
	}
	if (argv == nullptr || argv[0] == nullptr) {
		throw TypeError("builtin type '" + std::string(get_name()) + "' argument is null.");
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

Object* Instance::__getattr__(const std::string& name) {
	// 仅字段访问；未找到返回 nullptr（方法访问经 get_bound_method）。
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

void Instance::__setattr__(const std::string& name, Object* value) {
	// 可见性检查：外部写入 private 字段抛 AttributeError。
	if (cls_ != nullptr && cls_->member_is_private(name) &&
	    internal_access_depth() == 0) {
		Decref(value);
		throw AttributeError("'" + name + "' is private in class '" +
		                     cls_->get_name() + "'");
	}
	auto it = fields_.find(name);
	if (it != fields_.end()) {
		if (it->second != nullptr) Decref(it->second);
		it->second = value;
	} else {
		fields_[name] = value;
	}
	if (value != nullptr) Incref(value);
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
	if (instance_ != nullptr) Incref(instance_);
	if (method_ != nullptr) Incref(method_);
}

BoundMethod::~BoundMethod() {
	if (instance_ != nullptr) Decref(instance_);
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
