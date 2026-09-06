#include "PycpModule.hpp"
#include "PycpException.hpp"
#include "PycpList.hpp"
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

Module::Module(const std::string& name)
	: Object(name), name_(name), module_name_(name) {}

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
	// 不可见。模块自身内部顶层符号读取走 LOAD_VAR（直接查 globals
	// namespace），不经过 __get_attribute__，故此过滤不会误伤模块内部访问。
	if (it->second != nullptr && it->second->is_private()) {
		throw AttributeError("module '" + name_ + "' attribute '" + name +
		                     "' is private");
	}
	// 返回 Owned：__get_attribute__ 的调用方（Pycp::GetAttr / Module::GetAttr）
	// 不再额外 Incref，统一在此将借用语义的命名空间成员转为 Owned。
	Incref(it->second);
	return it->second;
}

Object* Module::__inspect__() {
	// 先收集成员字典 members_ 中的 key，再补充命名空间中可被外部访问
	// 的公开名称（与 __get_attribute__ 的可见性保持一致：private 符号
	// 对模块外部不可见，故不列入成员列表）。
	List* lst = static_cast<List*>(Object::__inspect__());
	std::vector<std::string> exported;
	for (const auto& kv : namespace_) {
		if (kv.second == nullptr) continue;
		if (kv.second->is_private()) continue; // 与 __get_attribute__ 过滤一致
		exported.push_back(kv.first);
	}
	for (const auto& n : exported) {
		// 避免与 members_ 中的 key 重复。
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
	// Module 豁免 __get_attribute__/__set_attribute__/__delete_attribute__，
	// 仅暴露其真实支持的魔术方法（命名空间若已含同名则不重复添加）。
	auto append_unique = [&lst](const char* n) {
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* elem = lst->at(i);
			if (elem != nullptr && elem->is_type("String") &&
			    static_cast<String*>(elem)->get_value() == n) {
				return;
			}
		}
		lst->append(String::FromCString(n));
	};
	append_unique("__string__");
	append_unique("__inspect__");
	append_unique("__name__");
	return lst;
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