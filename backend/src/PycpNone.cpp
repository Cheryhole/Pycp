#include "PycpNone.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpGC.hpp"

namespace Pycp {

None* None::instance = nullptr;

void None::Initialize(){
	None::instance = New<None>();
	// None 对象为常驻 root，永不参与回收
	None::instance->_set_gc_flags(None::instance->_gc_flags() | GCFlag::PERMANENT);
	GC_AddRoot(None::instance);
}

void None::Finalize(){
	if (None::instance != nullptr){
		GC_RemoveRoot(None::instance);
		None::instance->_set_gc_flags(None::instance->_gc_flags() | GCFlag::NONE);
		Decref(None::instance);
		None::instance = nullptr;
	}
}

None::None() : Object("None"){
	none_str_ = New<String>("None");
	Incref(none_str_);  // 子引用持有
}

None::~None(){
	// 析构由 Decref 统一驱动，子引用在此释放
	Decref(none_str_);
}

Object* None::__integer__(){
	// 防御：确保小整数池已初始化（避免初始化顺序问题导致野指针）
	if (Integer::instances[0] == nullptr){
		Integer::Initialize();
	}
	return Integer::instances[0];
}

Object* None::__string__(){
	return none_str_;
}

void None::foreach_ref(const std::function<void(Object*)>& visit){
	if (none_str_ != nullptr) visit(none_str_);
}

} // namespace Pycp
