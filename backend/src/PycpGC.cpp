#include "PycpGC.hpp"
#include "PycpNone.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpFunction.hpp"
#include "PycpModule.hpp"

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
	// 防御：对象必须仍被登记（未被 delete）。若不在 g_tracked 中则为悬空指针。
	// 必须在解引用 obj（访问 _gc_flags / foreach_ref）之前检查。
	if (g_tracked.find(obj) == g_tracked.end()) {
		// 悬空指针（已被 delete）：跳过，避免访问已释放内存。
		return;
	}
	if ((obj->_gc_flags() & GCFlag::MARKED) == GCFlag::MARKED) return;
	if ((obj->_gc_flags() & GCFlag::PERMANENT) == GCFlag::PERMANENT) return;

	obj->_set_gc_flags(obj->_gc_flags() | GCFlag::MARKED);

	// 经虚函数枚举子引用（各子类 override foreach_ref），替代旧 Type 枚举
	// 的 switch 分发。递归标记可达对象。
	obj->foreach_ref([&](Object* child) {
		mark_reachable(child);
	});
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
	// 防御：若 obj 已被 GC 兜底回收（不在 g_tracked），其内存已释放，
	// 再次 Decref 会访问悬垂内存并可能二次 delete。此时直接跳过
	// （不递减、不释放），避免 corrupted double-linked list / double free。
	// 典型场景：被多个不可达对象共享的子对象，被 GC 删除后，其余引用者
	// 析构时对同一指针 Decref。
	if (g_tracked.count(obj) == 0) return;

	obj->_set_refcount(obj->_refcount() - 1);
	if (obj->_refcount() > 0) return;

	// refcount 归零：释放对象（其持有的子引用由各子类析构函数负责
	// Decref，不在此处经 foreach_ref 重复释放，避免双重 Decref）。
	if ((obj->_gc_flags() & GCFlag::PERMANENT) == GCFlag::PERMANENT){
		// 常驻对象仅恢复引用计数，不释放
		obj->_set_refcount(1);
		return;
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
		// 若 o 已被其他待删对象的析构经引用计数连锁释放（如被多个不可达
		// 对象共享的子对象），其已从 g_tracked 移除，此处跳过，避免对同一
		// 内存二次 delete（否则 corrupted double-linked list / double free）。
		if (g_tracked.count(o) == 0) continue;
		GC_Untrack(o);
		delete o;
	}
}

std::size_t GC_LiveCount(){
	return g_tracked.size();
}

} // namespace Pycp
