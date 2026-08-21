#include "PycpModule.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"

namespace Pycp {

Module* Module::New(const std::string& name) {
	return Pycp::New<Module>(name);
}

Module::Module(const std::string& name)
	: Object(name), name_(name) {}

Module::~Module() {
	// 命名空间内对象的引用计数由模块执行环境负责管理；
	// 此处仅释放自身，namespace_ 中的值在 VM / AOT 收尾时统一 Decref。
	namespace_.clear();
}

Object* Module::GetAttr(Module* mod, const std::string& name) {
	if (mod == nullptr) throw AttributeError("cannot get attribute from null module.");
	return mod->__get_attribute__(name);
}

Object* Module::__get_attribute__(const std::string& name) {
	// 1) 先从成员字典中查找（支持动态 set attribute）。
	auto itm = members_.find(name);
	if (itm != members_.end() && itm->second != nullptr) {
		Incref(itm->second);
		return itm->second;
	}
	// 2) 从模块命名空间中查找。
	auto it = namespace_.find(name);
	if (it == namespace_.end()) {
		throw AttributeError("module '" + name_ + "' has no attribute '" + name + "'");
	}
	// 文件级导出：private 符号对其他文件（跨模块 import / module.x 访问）
	// 不可见。模块自身内部顶层符号读取走 LOAD_VAR（直接查 globals
	// namespace），不经过 __get_attribute__，故此过滤不会误伤模块内部访问。
	if (it->second != nullptr && it->second->is_private()) {
		throw AttributeError("module '" + name_ + "' attribute '" + name +
		                     "' is private");
	}
	return it->second;
}

void Module::foreach_ref(const std::function<void(Object*)>& visit) {
	for (auto& kv : namespace_) {
		if (kv.second != nullptr) visit(kv.second);
	}
}

} // namespace Pycp