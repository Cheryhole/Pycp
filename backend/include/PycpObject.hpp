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
	MODULE,
	CLASS,
	INSTANCE,
	FILE,
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
		// 可见性标记：true 表示私有（private），false 表示公开（public）。
		// 作为所有对象的通用属性，供 public/private 装饰器（C++ ABI 底层）设置：
		//   - 类内成员：控制该成员在类外的访问可见性。
		//   - 模块顶层符号：控制其他文件 import 时是否可访问。
		bool private_;

	public:
		Type type;

		Object(Type type = Type::OBJECT);
		virtual ~Object();

		// 可见性查询/设置（默认 public，即 private_ == false）。
		bool is_private() const { return private_; }
		void set_private(bool priv) { private_ = priv; }

		// 引用计数访问（仅 GC 层使用）
		uint32_t _refcount() const { return refcount; }
		void _set_refcount(uint32_t n) { refcount = n; }

		GCFlag _gc_flags() const { return gc_flags; }
		void _set_gc_flags(GCFlag f) { gc_flags = f; }
		void _clear_mark() {
			gc_flags = gc_flags & ~GCFlag::MARKED;
		}

		// 对象显示名（供默认 __string__ 输出 <name at 0xADDR> 使用）。
		// 默认返回匿名占位名 @anonymous；有名字的子类型（Function /
		// ClassObject / ModuleObject 等）override 返回各自的真实名字。
		virtual const char* get_name() const;

		virtual Object* __integer__();
		virtual Object* __string__();
		virtual Object* __negation__();
		virtual Object* __getattr__(const std::string& name);
		virtual void __setattr__(const std::string& name, Object* value);
		virtual Object* __call__(Object*);
		virtual Object* __addition__(Object*);
		virtual Object* __subtraction__(Object*);
		virtual Object* __multiplication__(Object*);
		virtual Object* __division__(Object*);
		virtual Object* __power__(Object*);

		// 比较运算：返回 Integer 0/1（小整数池对象）。
		// 与 __addition__ 等一致，由 ABI 的 Compare 按操作符分发。
		virtual Object* __less_than__(Object*);
		virtual Object* __less_equal__(Object*);
		virtual Object* __equal__(Object*);
		virtual Object* __not_equal__(Object*);
		virtual Object* __greater_than__(Object*);
		virtual Object* __greater_equal__(Object*);

		};

class Integer;
class String;

} // namespace Pycp

#endif // PYCP_OBJECT_HPP