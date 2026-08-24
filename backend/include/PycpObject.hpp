#ifndef PYCP_OBJECT_HPP
#define PYCP_OBJECT_HPP

#include "PycpException.hpp"
#include "PycpConfig.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace Pycp{

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
// 标记 PYCP_API：Windows 下构建 shared 运行时时整体导出 public 成员，
// 供内置扩展 DLL（io/Pycp/classtools）解析 Pycp::Object 的方法。
class PYCP_API Object{
	private:
		uint32_t refcount;
		GCFlag gc_flags;
		// 类型名（替代旧 Type 枚举，字符串判型）。
		std::string type_name_;

		// 可见性标记：true 表示私有（private），false 表示公开（public）。
		// 作为所有对象的通用属性，供 public/private 装饰器（C++ ABI 底层）设置：
		//   - 类内成员：控制该成员在类外的访问可见性。
		//   - 模块顶层符号：控制其他文件 import 时是否可访问。
		bool private_;

	protected:
		// 成员字典：name -> Object*（类似 Python 的 __dict__）。
		// 供 __get_attribute__ / __set_attribute__ / __members__ 使用。
		std::unordered_map<std::string, Object*> members_;

	public:
		Object(const std::string& type_name);
		virtual ~Object();

		// 类型名：用于运行时类型判定，替代旧的 Type 枚举。
		// 内置类型使用大写名（与 ABI 一致）："None" / "Integer" / "String" /
		// "Function" / "List" / "File"；class / instance / module 不采用统一
		// 大写名，而是返回各自的真实名称（get_name()），自定义类型名
		// 原样保留大小写（如 class a{} 的类与实例类型名均为 "a"）。
		const std::string& type_name() const { return type_name_; }

		// 类型判定便捷方法。
		bool is_type(const std::string& name) const { return type_name_ == name; }

		// 类型名重写（子类构造时覆盖基类初始化列表设定的类型名，
		// 例如 Boolean 继承 Integer 后需将 "Integer" 改为 "Boolean"）。
		// 注意 type_name() 非虚，类型判定依赖该成员，故此 setter 必要。
		void set_type_name(const std::string& name) { type_name_ = name; }

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
		// Class / Module 等）override 返回各自的真实名字。
		virtual const char* get_name() const;

		virtual Object* __integer__();
		virtual Object* __string__();
		// 真值判定：返回 Boolean 对象（True/False）。默认返回 True（基类语义）。
		virtual Object* __boolean__();
		virtual Object* __negation__();
		virtual Object* __get_attribute__(const std::string& name);
		virtual void __set_attribute__(const std::string& name, Object* value);
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

	// 下标运算（self[key] 与 self[key] = value）。
	// 默认抛 TypeError；List 与 Instance（转发到类的
	// __get_item__/__set_item__ 方法）override。
	virtual Object* __get_item__(Object* key);
	virtual Object* __set_item__(Object* key, Object* value);

	// list 转换（Pycp.List(obj)）。
	// 默认抛 TypeError；List 返回自身（幂等）。
	virtual Object* __list__();

	// 迭代协议。
	// __iterator__()：返回一个全新的迭代器（默认不可迭代，抛 TypeError）。
	//   List/String 可迭代，每次调用返回新的独立迭代器实例。
	// __next__()：迭代器推进，返回下一元素（Owned）；耗尽后抛 StopIteration。
	//   默认抛 TypeError（仅迭代器子类有意义）。
	virtual Object* __iterator__();
	virtual Object* __next__();

	// 属性名枚举（Pycp.get_members(obj)）：返回本对象所有成员名称的
	// List。默认返回 members_ 的 key；子类可 override 添加额外成员名。
	virtual Object* __members__();

	// GC 子引用遍历：枚举本对象持有的 Object* 子引用，供标记-清除与
	// Decref 递归释放统一使用（替代旧 Type 枚举的 switch 分发）。
	// 默认空实现；持有子引用的子类（None/Class/Instance/
	// File/List 等）override。
	virtual void foreach_ref(const std::function<void(Object*)>& visit);

		};

class Integer;
class String;

} // namespace Pycp

#endif // PYCP_OBJECT_HPP