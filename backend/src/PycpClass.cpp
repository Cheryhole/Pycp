#include "PycpClass.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpString.hpp"
#include "PycpConfig.hpp"
#include "PycpMagic.hpp"
#include "PycpMap.hpp"

#include <unordered_set>
#include <unordered_map>
#include <mutex>

#include <sstream>
#include <vector>

namespace Pycp {

// =============================================================
// 方法内部访问标志（thread_local）
// =============================================================
namespace {
thread_local int g_internal_access_depth = 0;
thread_local std::vector<Instance*> g_current_self_stack;
thread_local std::vector<Class*> g_current_class_stack;
// 正在执行 __init_defaults__（构造初值写入）的深度：此期间允许写入
// readonly 字段（声明初值），其余路径仍拦截。
thread_local int g_in_init_defaults = 0;
}

int internal_access_depth() { return g_internal_access_depth; }
void enter_internal_access() { ++g_internal_access_depth; }
void leave_internal_access() { --g_internal_access_depth; }

// =============================================================
// 运行时「类型类」注册表（typeof / __class__）
// =============================================================
namespace {
std::mutex g_type_reg_mutex;
// 类型名 -> 类对象。条目持强引用（Incref）并被 GC 常驻 root，
// 与模块/合成类型类同生命周期（进程常驻，native 模块不卸载）。
std::unordered_map<std::string, Class*> g_type_classes;
Class* g_object_class = nullptr;   // pycp.Object 类指针（root 持有）
} // anonymous namespace

void RegisterTypeClass(const std::string& type_name, Class* cls) {
	if (cls == nullptr) return;
	std::lock_guard<std::mutex> lock(g_type_reg_mutex);
	auto it = g_type_classes.find(type_name);
	if (it != g_type_classes.end() && it->second != nullptr) {
		// 后注册覆盖先前的同名条目（可能为惰性合成类）。
		GC_RemoveRoot(it->second);
		Decref(it->second);
	}
	Incref(cls);
	GC_AddRoot(cls);
	g_type_classes[type_name] = cls;
}

Class* LookupTypeClass(const std::string& type_name) {
	std::lock_guard<std::mutex> lock(g_type_reg_mutex);
	auto it = g_type_classes.find(type_name);
	if (it != g_type_classes.end()) return it->second;
	// 未登记：惰性合成一个普通 Class（名=类型名，如 None/Function/Module）。
	// 合成类无构造回调，仅为 typeof/__class__ 提供类对象标识。
	Class* c = Class::New(type_name);
	GC_AddRoot(c);
	g_type_classes[type_name] = c;   // 持有 root，不额外 Incref
	return c;                        // Borrowed
}

void RegisterObjectClass(Class* cls) {
	if (cls == nullptr) return;
	std::lock_guard<std::mutex> lock(g_type_reg_mutex);
	if (g_object_class != nullptr) {
		GC_RemoveRoot(g_object_class);
		Decref(g_object_class);
	}
	Incref(cls);
	GC_AddRoot(cls);
	g_object_class = cls;
}

Class* LookupObjectClass() {
	std::lock_guard<std::mutex> lock(g_type_reg_mutex);
	return g_object_class;
}

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

// =============================================================
// 当前方法所属类上下文（thread_local 栈）：供 super() 正确解析
// 父类——基于"当前执行方法所属类"，而非最派生实例的类（否则
// 继承链上重复调用 super 会无限递归到自身）。
// =============================================================
void push_current_class(Class* cls) { g_current_class_stack.push_back(cls); }
void pop_current_class() {
	if (!g_current_class_stack.empty()) g_current_class_stack.pop_back();
}
Class* current_class() {
	if (g_current_class_stack.empty()) return nullptr;
	return g_current_class_stack.back();
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
	// 父类引用由引用计数持有（见 set_parent），此处释放以避免悬垂指针。
	if (parent_ != nullptr) {
		Decref(parent_);
		parent_ = nullptr;
	}
}

void Class::AddMethod(Class* cls, const std::string& name, Object* fn) {
	if (cls == nullptr) throw TypeError("cannot add method to null class.");
	if (fn == nullptr || !fn->is_type("Function"))
		throw TypeError("method must be a function.");
	Function* f = static_cast<Function*>(fn);
	f->set_owner_class(cls);   // 供 super() 解析当前方法所属类
	cls->add_method(name, f);
}

void Class::set_parent(Class* parent) {
	// 子类持有父类引用（不形成环：父类不知晓子类），故引用计数持有，
	// 保证父类生命周期 >= 所有子类，杜绝父类被提前释放导致子类 parent_ 悬垂。
	if (parent_ != nullptr) Decref(parent_);
	parent_ = parent;
	if (parent_ != nullptr) Incref(parent_);
}

void Class::add_member_name(const std::string& name) {
	add_member_name(name, false);
}

void Class::add_member_name(const std::string& name, bool is_private) {
	add_member_name(name, is_private, false);
}

void Class::add_member_name(const std::string& name, bool is_private,
                            bool is_readonly) {
	member_names_.push_back(name);
	member_visibility_[name] = is_private;
	member_readonly_[name] = is_readonly;
}

bool Class::member_is_private(const std::string& name) const {
	auto it = member_visibility_.find(name);
	if (it == member_visibility_.end()) return false;
	return it->second;
}

bool Class::member_is_readonly(const std::string& name) const {
	auto it = member_readonly_.find(name);
	if (it == member_readonly_.end()) return false;
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

Class* Class::get_type_class() {
	// 类对象统一归类为 pycp.Object（metaclass 语义）。
	Class* oc = LookupObjectClass();
	return oc != nullptr ? oc : LookupTypeClass("Object");
}

Object* Class::__get_attribute__(const std::string& name) {
	// 0) 内建只读属性 __name__：返回类型名（type_name()）对应的 String。
	if (name == "__name__") {
		return GetNameAttribute(this);
	}
	// 0.1) 只读 __class__：返回 pycp.Object（类对象的类型）。
	if (name == "__class__") {
		return get_type_class(); // Borrowed（pycp.Object 由注册表 root 持有）
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
		// 可见性检查：外部访问 private 方法抛 AttributeError。
		// 类内部 self 调用经 internal_access（depth>0）放行，与 Instance 一致。
		if (method_is_private(name) && internal_access_depth() == 0) {
			throw AttributeError("'" + name + "' is private in class '" +
			                     name_ + "'");
		}
		// __init_defaults__ 为内部初始化方法，禁止外部访问（实例化内部
		// 经 find_method 直接取，不经过此属性访问路径）。
		if (name == "__init_defaults__" && internal_access_depth() == 0) {
			throw AttributeError("'" + name + "' is internal and not accessible.");
		}
		return fn;
	}
	// 3) 回退到魔术方法分派（如 __inspect__/__string__ 等），让
	//    Class.__inspect__()、Class.__string__() 等可经魔术方法调用，
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
	// 匿名类的内部名本身即 @anonymous，故无需特例分支：统一套用
	// "<class \"name\">" 格式即可自然得到 <class "@anonymous">。
	// name_ 为空时同样归一化为 @anonymous（防御性，与函数侧对称）。
	const std::string display = name_.empty() ? ANONYMOUS_CLASS : name_;
	return String::FromCString(("<class \"" + display + "\">").c_str());
}

Object* Class::__inspect__() {
	// 返回类的字段声明名 + 方法名 + 通用成员。
	List* lst = static_cast<List*>(Object::__inspect__());
	// 类字段声明（如 mem1/mem2/mem3），过滤 private。
	for (const auto& n : get_member_names()) {
		if (member_is_private(n)) continue;
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
	// 类方法名（如 a/__initialize__），过滤 private 与 internal 的
	// __init_defaults__（内部字段初始化方法，不暴露到 pycp 代码）。
	for (const auto& n : method_names()) {
		if (method_is_private(n)) continue;
		if (n == "__init_defaults__") continue;
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
	// 类对象的类型属性 __class__（只读，dir 可见）。
	{
		bool found = false;
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* elem = lst->at(i);
			if (elem != nullptr && elem->is_type("String") &&
			    static_cast<String*>(elem)->get_value() == "__class__") {
				found = true;
				break;
			}
		}
		if (!found) lst->append(String::FromCString("__class__"));
	}
	return lst;
}

std::vector<std::pair<std::string, Object*>> Class::member_pairs() const {
	// 合并字段声明名 + 类方法名（方法对应值经 __get_attribute__ 动态取，
	// 此处填 nullptr），使 __map__ 视图同时包含属性与方法。
	std::vector<std::pair<std::string, Object*>> out;
	std::unordered_set<std::string> seen;
	for (const auto& n : get_member_names()) {
		if (member_is_private(n)) continue;
		if (seen.insert(n).second) out.emplace_back(n, nullptr);
	}
	for (const auto& n : method_names()) {
		if (method_is_private(n)) continue;
		if (n == "__init_defaults__") continue;
		if (seen.insert(n).second) out.emplace_back(n, nullptr);
	}
	return out;
}

void Class::foreach_ref(const std::function<void(Object*)>& visit) {
	for (auto& kv : methods_) {
		if (kv.second != nullptr) visit(kv.second);
	}
}

Object* Class::instantiate(Object** argv, std::size_t argc) {
	Instance* inst = Pycp::New<Instance>(this);

	// 1) 应用成员初始值（若类定义了隐式 __init_defaults__）。
	//    此期间放开 readonly 字段写入（声明初值），其余路径仍拦截。
	Function* init_defaults = find_method("__init_defaults__");
	if (init_defaults != nullptr) {
		++g_in_init_defaults;
		try {
			Object* self = inst;
			Object* dv[1] = { self };
			Object* r = init_defaults->invoke(dv, 1);
			if (r != nullptr) Decref(r);
		} catch (...) {
			--g_in_init_defaults;
			throw;
		}
		--g_in_init_defaults;
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
		// 空参构造（如 Pycp.Map()）：提供占位数组，避免 ctor 访问空指针。
		// 构造器仍按 argc == 0 分支自行处理。
		static Object* empty[]{nullptr};
		argv = empty;
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

Class* Instance::get_type_class() {
	if (cls_ != nullptr) return cls_;
	return LookupTypeClass("Instance");
}

Object* Instance::__get_attribute__(const std::string& name) {
	// 0) 内建只读属性 __name__：返回类型名（type_name()）对应的 String。
	if (name == "__name__") {
		return GetNameAttribute(this);
	}
	// 0.1) 只读 __class__：返回所属类对象 cls_。
	if (name == "__class__") {
		return get_type_class(); // Borrowed（所属类由实例持引用）
	}
	// 0.5) 用户 override 的属性访问钩子 __get_attribute__(self, name)。
	//      Object 提供的默认实现（owner_class 为 Object）不触发，走 C++
	//      默认路径，避免无限递归；用户自定义实现则优先调用（劫持取值）。
	if (cls_ != nullptr) {
		// 仅类外部访问（internal_access_depth==0）触发钩子；方法体内
		// self.x 访问走 C++ 默认，避免钩子内部 self.x 再触发钩子无限递归。
		Function* hook = cls_->find_method("__get_attribute__");
		if (hook != nullptr && internal_access_depth() == 0 &&
		    (hook->get_owner_class() == nullptr ||
		     std::string(hook->get_owner_class()->get_name()) != "Object")) {
			Object* self = this;
			Object* name_s = String::FromCString(name.c_str());
			Object* argv[2] = { self, name_s };
			Object* r = hook->invoke(argv, 2);
			Decref(name_s);
			return r; // invoke 返回 Owned，直接转交
		}
	}
	// 1) 先从成员字典中查找（支持动态 set attribute）。
	auto itm = members_.find(name);
	if (itm != members_.end() && itm->second != nullptr) {
		Incref(itm->second);
		return itm->second;
	}
	// 2) 字段访问，未找到再尝试类方法（绑定为可调用 BoundMethod）。
	//    与 VM GetAttr 路径一致：实例字段优先，其次类方法，最后魔术方法。
	//    这样 __map__ 视图经 __get_attribute__ 也能取到类方法（可调用）。
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
	if (Object* bm = get_bound_method(name)) {
		return bm;
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
	// __init_defaults__ 为内部初始化方法，禁止外部访问（实例化内部经
	// find_method 直接取，不经过绑定方法路径）。
	if (name == "__init_defaults__" && internal_access_depth() == 0) {
		throw AttributeError("'" + name + "' is internal and not accessible.");
	}
	return Pycp::New<BoundMethod>(this, fn);
}

Object* Instance::__inspect__() {
	// 字段名 + 类方法名 + 通用成员。
	List* lst = static_cast<List*>(Object::__inspect__());
	std::vector<std::string> extra;
	for (const auto& kv : fields_) extra.push_back(kv.first);
	if (cls_ != nullptr) {
		for (const auto& m : cls_->method_names()) {
			// 过滤 private 方法（与 Class::__inspect__ 一致），
			// 避免经实例对象暴露 @private 方法。
			if (cls_->method_is_private(m)) continue;
			// 过滤 internal 的 __init_defaults__，不暴露到 pycp 代码。
			if (m == "__init_defaults__") continue;
			extra.push_back(m);
		}
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
	// 实例的类型属性 __class__（只读，dir 可见）。
	{
		bool found = false;
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* elem = lst->at(i);
			if (elem != nullptr && elem->is_type("String") &&
			    static_cast<String*>(elem)->get_value() == "__class__") {
				found = true;
				break;
			}
		}
		if (!found) lst->append(String::FromCString("__class__"));
	}
	return lst;
}

void Instance::__set_attribute__(const std::string& name, Object* value) {
	// __class__ 只读：拒绝赋值（消费 value 引用，对齐 private 错误路径惯例）。
	if (name == "__class__") {
		if (value != nullptr) Decref(value);
		throw AttributeError("'__class__' is read-only.");
	}
	// 只读拦截：对象冻结或字段被声明为 readonly（@readonly）。
	// 构造初值写入（g_in_init_defaults>0）放行 readonly 字段；对象冻结始终拦截。
	if (is_readonly() ||
	    (cls_ != nullptr && cls_->member_is_readonly(name) &&
	     g_in_init_defaults == 0)) {
		if (value != nullptr) Decref(value);
		throw AttributeError("'" + name + "' is read-only.");
	}
	// 用户 override 的属性赋值钩子 __set_attribute__(self, name, value)。
	// Object 提供的默认实现（owner_class 为 Object）不触发，走 C++ 默认。
	if (cls_ != nullptr) {
		// 仅类外部赋值（internal_access_depth==0）触发钩子；方法体内
		// self.x = v 走 C++ 默认，避免钩子内部 self.x = v 再触发钩子递归。
		Function* hook = cls_->find_method("__set_attribute__");
		if (hook != nullptr && internal_access_depth() == 0 &&
		    (hook->get_owner_class() == nullptr ||
		     std::string(hook->get_owner_class()->get_name()) != "Object")) {
			Object* self = this;
			Object* name_s = String::FromCString(name.c_str());
			Object* argv[3] = { self, name_s, value };
			Object* r = hook->invoke(argv, 3);
			Decref(name_s);
			if (r != nullptr) Decref(r);
			return; // 钩子接管赋值（存储/丢弃由钩子负责）
		}
	}
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
		// 仅调用用户 override 的实现；Object 提供的默认 __string__（owner_class
		// 为 Object，经继承复制而来）回退 C++ 默认，避免 `_object_string` 再调
		// argv[0]->__string__() 造成无限递归。
		if (fn != nullptr &&
		    (fn->get_owner_class() == nullptr ||
		     std::string(fn->get_owner_class()->get_name()) != "Object")) {
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

std::vector<std::pair<std::string, Object*>> Instance::member_pairs() const {
	// 合并 fields_、动态 members_（数据成员）与类方法名，使 __map__ 视图
	// 同时包含属性与方法（方法对应值经 __get_attribute__ 动态取，此处填 nullptr）。
	std::vector<std::pair<std::string, Object*>> out;
	std::unordered_set<std::string> seen;
	for (const auto& kv : fields_) {
		if (seen.insert(kv.first).second) {
			out.emplace_back(kv.first, kv.second);
		}
	}
	for (const auto& kv : members_) {
		if (kv.second == nullptr) continue;
		if (seen.insert(kv.first).second) {
			out.emplace_back(kv.first, kv.second);
		}
	}
	if (cls_ != nullptr) {
		for (const auto& m : cls_->method_names()) {
			// 过滤 private 方法（与 __inspect__ 一致），
			// 避免经实例对象暴露 @private 方法。
			if (cls_->method_is_private(m)) continue;
			// 过滤 internal 的 __init_defaults__，不暴露到 pycp 代码。
			if (m == "__init_defaults__") continue;
			if (seen.insert(m).second) {
				out.emplace_back(m, nullptr);
			}
		}
	}
	return out;
}

Object* Instance::__map__() {
	// 返回绑定本实例、合并 fields_+members_ 的 Map 视图。
	return Map::NewView(this);
}

void Instance::__delete_attribute__(const std::string& name) {
	// 只读拦截：对象冻结或字段被声明为 readonly。
	if (is_readonly() ||
	    (cls_ != nullptr && cls_->member_is_readonly(name))) {
		throw AttributeError("'" + name + "' is read-only.");
	}
	// 优先从实例字段删除；否则从动态成员字典删除。
	// 若用户定义了 __delete_attribute__(self, name) 钩子，则由其接管。
	if (cls_ != nullptr) {
		Function* hook = cls_->find_method("__delete_attribute__");
		if (hook != nullptr &&
		    (hook->get_owner_class() == nullptr ||
		     std::string(hook->get_owner_class()->get_name()) != "Object")) {
			Object* self = this;
			Object* name_s = String::FromCString(name.c_str());
			Object* argv[2] = { self, name_s };
			Object* r = hook->invoke(argv, 2);
			Decref(name_s);
			if (r != nullptr) Decref(r);
			return;
		}
	}
	auto itf = fields_.find(name);
	if (itf != fields_.end()) {
		if (itf->second != nullptr) Decref(itf->second);
		fields_.erase(itf);
		return;
	}
	Object::__delete_attribute__(name);
}

Object* Instance::__delete__() {
	// 用户定义 __delete__ 则调用，否则回退 Object 默认实现。
	if (cls_ != nullptr) {
		Function* fn = cls_->find_method("__delete__");
		if (fn != nullptr &&
		    (fn->get_owner_class() == nullptr ||
		     std::string(fn->get_owner_class()->get_name()) != "Object")) {
			Object* self = this;
			Object* argv[1] = { self };
			return fn->invoke(argv, 1);
		}
	}
	return Object::__delete__();
}

Object* Instance::__delete_item__(Object* key) {
	// 用户定义 __delete_item__ 则调用，否则回退 Object 默认实现。
	if (cls_ != nullptr) {
		Function* fn = cls_->find_method("__delete_item__");
		if (fn != nullptr &&
		    (fn->get_owner_class() == nullptr ||
		     std::string(fn->get_owner_class()->get_name()) != "Object")) {
			Object* self = this;
			Object* argv[2] = { self, key };
			return fn->invoke(argv, 2);
		}
	}
	return Object::__delete_item__(key);
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