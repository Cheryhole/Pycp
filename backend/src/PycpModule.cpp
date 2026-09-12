#include "PycpModule.hpp"
#include "PycpException.hpp"
#include "PycpList.hpp"
#include "PycpFixedList.hpp"
#include "PycpMagic.hpp"
#include "PycpClass.hpp"
#include "PycpGC.hpp"

namespace Pycp {

Module* Module::New(const std::string& name) {
	return Pycp::New<Module>(name);
}

// 全局「当前正在执行的模块」：VM / AOT 在执行某模块顶层前设置、后恢复。
Module* current_module_ = nullptr;

// pycp.__name__ 的来源：当前文件的名称。返回 Owned。
Object* GetCurrentModuleName() {
	if (current_module_ == nullptr) return String::FromCString("__main__");
	return current_module_->resolve_name_value(); // 无递归（resolve 不走 pycp 回退）
}

Class* Module::get_type_class() {
	// Module 的 type_name_ 是模块名，不能按名查表，固定使用 "Module" 类型类。
	return LookupTypeClass("Module");
}

Module::Module(const std::string& name)
	: Object(name), name_(name), module_name_(name) {
	set_type_info(PycpTypeId::Module, PycpTypeFlag::None);
}

Module::~Module() {
	// 命名空间内对象的引用计数由模块执行环境负责管理；
	// 此处仅释放自身，namespace_ 中的值在 VM / AOT 收尾时统一 Decref。
	namespace_.clear();
}

Object* Module::GetAttr(Module* mod, const std::string& name) {
	if (mod == nullptr) throw AttributeError("cannot get attribute from null module.");
	return mod->__get_attribute__(name);
}

Object* Module::resolve_name_value() {
	// members_ 覆盖 -> namespace_["__name__"] -> 回退 module_name_（无递归）。
	auto itm = members_.find("__name__");
	if (itm != members_.end() && itm->second != nullptr) {
		Incref(itm->second);
		return itm->second;
	}
	auto it = namespace_.find("__name__");
	if (it != namespace_.end() && it->second != nullptr) {
		Incref(it->second);
		return it->second;
	}
	return String::FromCString(module_name_.c_str()); // Owned
}

Object* Module::__get_attribute__(const std::string& name) {
	// 0) __name__：members_ / namespace_ 命中即用；都不命中回退全局当前模块名
	//    （即 pycp.__name__，对应规则 1：入口文件经回退得 "__main__"）。
	if (name == "__name__") {
		auto itm = members_.find(name);
		if (itm != members_.end() && itm->second != nullptr) {
			Incref(itm->second);
			return itm->second;
		}
		auto it = namespace_.find(name);
		if (it != namespace_.end() && it->second != nullptr) {
			Incref(it->second);
			return it->second;
		}
		return GetCurrentModuleName();
	}
	// 0.1) 只读 __class__：返回 Module 类型类对象（Owned）。
	if (name == "__class__") {
		Class* c = get_type_class();
		Incref(c);
		return c;
	}
	// 1) 先从成员字典中查找（支持动态 set attribute）。
	auto itm = members_.find(name);
	if (itm != members_.end() && itm->second != nullptr) {
		Incref(itm->second);
		return itm->second;
	}
	// 2) 从模块命名空间中查找。
	auto it = namespace_.find(name);
	if (it == namespace_.end()) {
		// 3) 魔术方法回退（与 Object/List/Instance 一致）：允许通过
		//    属性访问调用 __inspect__ 等内建魔术方法。将其包装为
		//    BoundMethod（self = 模块对象），使调用时 receiver 自动绑定，
		//    与 GetAttr 的 Module 分支（不绑定普通模块函数）区分开。
		if (Object* magic = GetMagicMethodFunction(name)) {
			if (Function* fn = dynamic_cast<Function*>(magic)) {
				return Pycp::New<BoundMethod>(this, fn);
			}
			Incref(magic);
			return magic;
		}
		throw AttributeError("module '" + name_ + "' has no attribute '" + name + "'");
	}
	// 文件级导出：private 符号对其他文件（跨模块 import / module.x 访问）
	// 不可见。以绑定级属性为权威（@private 声明经 MARK_BINDING 登记），
	// 不使用值级 is_private()，避免对共享池化值（小整数等）打标造成误伤。
	// 模块自身内部顶层符号读取走 LOAD_VAR（直接查 globals namespace），
	// 不经过 __get_attribute__，故此过滤不会误伤模块内部访问。
	if (binding_attrs(name).priv) {
		throw AttributeError("module '" + name_ + "' attribute '" + name +
		                     "' is private");
	}
	// 返回 Owned：__get_attribute__ 的调用方（Pycp::GetAttr / Module::GetAttr）
	// 不再额外 Incref，统一在此将借用语义的命名空间成员转为 Owned。
	Incref(it->second);
	return it->second;
}

void Module::__set_attribute__(const std::string& name, Object* value) {
	// 只读常量绑定（@readonly）：禁止经 module.attr = v 覆盖（含跨模块）。
	if (binding_attrs(name).readonly) {
		if (value != nullptr) Decref(value); // 消费待写引用（对齐 STORE_ATTR）
		throw AttributeError("cannot reassign read-only binding '" + name + "'.");
	}
	Object::__set_attribute__(name, value);
}

Object* Module::__inspect__() {
	// 成员字典 members_ 名 + 命名空间中可被外部访问的公开名称 +
	// 模块支持的魔术方法（定型为 FixedList）。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	for (const auto& kv : namespace_) {
		if (kv.second == nullptr) continue;
		// 与 __get_attribute__ 过滤一致：仅以绑定级属性判定。
		if (binding_attrs(kv.first).priv) continue;
		CollectUniqueName(names, kv.first);
	}
	// Module 豁免 __get_attribute__/__set_attribute__/__delete_attribute__，
	// 仅暴露其真实支持的魔术方法。
	CollectUniqueName(names, "__string__");
	CollectUniqueName(names, "__inspect__");
	CollectUniqueName(names, "__name__");
	CollectUniqueName(names, "__class__");
	return FixedList::New(names);
}

Object* Module::__string__(){
	// 用 __name__ 渲染：值为 String 直接嵌入；否则调用其 __string__()。
	Object* no = resolve_name_value();
	std::string repr;
	if (no->is_type("String")) {
		repr = static_cast<String*>(no)->get_value();
	} else {
		Object* s = no->__string__();   // 返回 Owned
		repr = static_cast<String*>(s)->get_value();
		Decref(s);
	}
	return String::FromCString(("<module \"" + repr + "\">").c_str());
}

void Module::foreach_ref(const std::function<void(Object*)>& visit) {
	for (auto& kv : namespace_) {
		if (kv.second != nullptr) visit(kv.second);
	}
}

} // namespace Pycp