#ifndef PYCP_OBJECT_HPP
#define PYCP_OBJECT_HPP

#include "PycpException.hpp"
#include "PycpConfig.hpp"

#include <cstdint>

namespace Pycp{

enum class Type{
	OBJECT,
	NONE,
	INTEGER,
	STRING,
	FUNCTION,
};

// 对象头 GC 标记位（gc_flags）
enum class GCFlag : uint32_t{
	NONE    = 0,
	MARKED  = 1u << 0,  // 标记-清除阶段：可达
	PERMANENT = 1u << 1, // 常驻对象（如小整数池），GC 永不回收
};

inline GCFlag operator|(GCFlag a, GCFlag b){
	return static_cast<GCFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline GCFlag operator&(GCFlag a, GCFlag b){
	return static_cast<GCFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline GCFlag operator~(GCFlag a){
	return static_cast<GCFlag>(~static_cast<uint32_t>(a));
}
inline bool operator==(GCFlag a, GCFlag b){
	return static_cast<uint32_t>(a) == static_cast<uint32_t>(b);
}
inline bool operator!=(GCFlag a, GCFlag b){
	return static_cast<uint32_t>(a) != static_cast<uint32_t>(b);
}

// 所有运行时对象的基类。
// 内存生命周期统一由 PycpGC 管理：
//   - refcount : 引用计数（RC 为主回收依据）
//   - gc_flags : 标记-清除所需标记位，并预留 PERMANENT 常驻位
// 业务代码禁止直接 delete，统一经 Incref/Decref（C-ABI 别名 PYCP_Incref/PYCP_Decref）。
class Object{
	private:
		uint32_t refcount;
		GCFlag gc_flags;

	public:
		Type type;

		Object(Type type = Type::OBJECT);
		virtual ~Object();

		// 引用计数访问（仅 GC 层使用）
		uint32_t _refcount() const { return refcount; }
		void _set_refcount(uint32_t n) { refcount = n; }

		GCFlag _gc_flags() const { return gc_flags; }
		void _set_gc_flags(GCFlag f) { gc_flags = f; }
		void _clear_mark() {
			gc_flags = gc_flags & ~GCFlag::MARKED;
		}

		virtual Object* __integer__();
		virtual Object* __string__();
		virtual Object* __negation__();
		virtual Object* __call__(Object*);
		virtual Object* __addition__(Object*);
		virtual Object* __subtraction__(Object*);
		virtual Object* __multiplication__(Object*);
		virtual Object* __division__(Object*);
		virtual Object* __power__(Object*);

};

class Integer;
class String;

} // namespace Pycp

#endif // PYCP_OBJECT_HPP