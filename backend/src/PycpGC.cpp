#include "PycpGC.hpp"
#include "PycpNone.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpFunction.hpp"

#include <unordered_set>
#include <vector>
#include <iostream>

namespace Pycp {

// =============================================================
// GC 内部状态
// =============================================================

namespace {

// 存活对象登记集合（用于诊断与标记-清除遍历）
std::unordered_set<Object*> g_tracked;

// 显式 GC root 集合（AddRoot/RemoveRoot）
std::unordered_set<Object*> g_roots;

// 标记阶段辅助：递归标记从 obj 出发可达的对象
void mark_reachable(Object* obj){
	if (obj == nullptr) return;
	if ((obj->_gc_flags() & GCFlag::MARKED) == GCFlag::MARKED) return;
	if ((obj->_gc_flags() & GCFlag::PERMANENT) == GCFlag::PERMANENT) return;

	obj->_set_gc_flags(obj->_gc_flags() | GCFlag::MARKED);

	// 按对象类型枚举其持有的引用字段（当前仅 None/Function 持有子引用）
	switch (obj->type){
		case Type::NONE: {
			None* n = static_cast<None*>(obj);
			if (n->none_str() != nullptr) mark_reachable(n->none_str());
			break;
		}
		case Type::FUNCTION: {
			// Function 当前不持有除自身外的 PycpObject 引用
			break;
		}
		default:
			break;
	}
}

} // anonymous namespace

// =============================================================
// 引用计数核心
// =============================================================

void Incref(Object* obj){
	if (obj == nullptr) return;
	obj->_set_refcount(obj->_refcount() + 1);
}

void Decref(Object* obj){
	if (obj == nullptr) return;

	obj->_set_refcount(obj->_refcount() - 1);
	if (obj->_refcount() > 0) return;

	// refcount 归零：先递归 Decref 其持有的子引用，再释放
	if ((obj->_gc_flags() & GCFlag::PERMANENT) == GCFlag::PERMANENT){
		// 常驻对象仅恢复引用计数，不释放
		obj->_set_refcount(1);
		return;
	}

	// 枚举子引用并 Decref（与 mark_reachable 保持同步）
	switch (obj->type){
		case Type::NONE: {
			None* n = static_cast<None*>(obj);
			if (n->none_str() != nullptr) Decref(n->none_str());
			break;
		}
		default:
			break;
	}

	GC_Untrack(obj);
	delete obj;
}

// =============================================================
// 分配登记 / 注销
// =============================================================

void GC_Track(Object* obj){
	if (obj == nullptr) return;
	obj->_set_refcount(1);  // 创建即 Owned（refcount = 1）
	obj->_set_gc_flags(GCFlag::NONE);
	g_tracked.insert(obj);
}

void GC_Untrack(Object* obj){
	if (obj == nullptr) return;
	g_tracked.erase(obj);
}

// =============================================================
// root 管理
// =============================================================

void GC_AddRoot(Object* obj){
	if (obj == nullptr) return;
	g_roots.insert(obj);
}

void GC_RemoveRoot(Object* obj){
	if (obj == nullptr) return;
	g_roots.erase(obj);
}

// =============================================================
// 标记-清除兜底回收
// =============================================================

void GC_Collect(){
	// 1. 清除所有对象的 MARKED 位（保留 PERMANENT）
	for (Object* o : g_tracked){
		o->_clear_mark();
	}

	// 2. 从显式 root 出发标记可达对象
	for (Object* r : g_roots){
		mark_reachable(r);
	}
	// 常驻对象（小整数池等）视为隐式 root
	for (Object* o : g_tracked){
		if ((o->_gc_flags() & GCFlag::PERMANENT) == GCFlag::PERMANENT){
			mark_reachable(o);
		}
	}

	// 3. 清除未标记且非永久对象（环或不可达泄漏）
	std::vector<Object*> to_free;
	for (Object* o : g_tracked){
		if ((o->_gc_flags() & GCFlag::MARKED) != GCFlag::MARKED &&
			(o->_gc_flags() & GCFlag::PERMANENT) != GCFlag::PERMANENT){
			to_free.push_back(o);
		}
	}
	for (Object* o : to_free){
		GC_Untrack(o);
		delete o;
	}
}

std::size_t GC_LiveCount(){
	return g_tracked.size();
}

} // namespace Pycp
