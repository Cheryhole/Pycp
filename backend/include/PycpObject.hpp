#ifndef PYCP_OBJECT_HPP
#define PYCP_OBJECT_HPP

#include "PycpException.hpp"
#include "PycpConfig.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace Pycp{

// 前向声明（Object::get_type_class 等返回 Class*，需在使用前可见）。
class Class;

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

// =============================================================
// 类型标识与类型标志位（对齐 CPython 的类型系统思路，C++17 实现）
//
//   PycpTypeId   : 精确类型标识（编译期枚举，O(1) 整数比较）。因
//                  Class / Instance / Module 的 type_name 是动态名
//                  （类名 / 模块名），独立 type_id 是区分它们的必要手段。
//   PycpTypeFlag : 位标志（对应 CPython 的 tp_flags），表达子类型族
//                  与能力（如 IntegerSubclass 覆盖 Integer 与 Boolean）。
// =============================================================
enum class PycpTypeId : uint32_t{
	Unknown = 0,
	None,
	Integer,
	Boolean,
	String,
	List,
	FixedList,
	Map,
	File,
	Function,
	Class,
	Instance,
	Module,
	ListIterator,
	StringIterator,
	FixedListIterator,
};

enum class PycpTypeFlag : uint32_t{
	None             = 0,
	IntegerSubclass  = 1u << 0,  // int 族：Integer 及 Boolean（bool 是 int 子类）
	StringSubclass   = 1u << 1,  // str 族
	SequenceSubclass = 1u << 2,  // List 族
	MappingSubclass  = 1u << 3,  // Map 族
	Callable         = 1u << 4,  // 可调用（Function / Class）
	Hashable         = 1u << 5,  // 可哈希（None / Integer / Boolean / String）
	Iterable         = 1u << 6,  // 可迭代（List / String / 迭代器）
	Mutable          = 1u << 7,  // 可变（List / Map / File）
};

constexpr PycpTypeFlag operator|(PycpTypeFlag a, PycpTypeFlag b){
	return static_cast<PycpTypeFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr PycpTypeFlag operator&(PycpTypeFlag a, PycpTypeFlag b){
	return static_cast<PycpTypeFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
constexpr bool has_flag(PycpTypeFlag value, PycpTypeFlag flag){
	return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

// 编译期类型萃取：绑定 C++ 类型 <=> { id, flags, subclass_flag }。
// 主模板为未知类型；各内置类型在其头文件提供特化。
//   id            : 精确类型标识（IsExact<T> 用）。
//   flags         : 该类型自身携带的能力/族标志（HasFlag 语义）。
//   subclass_flag : 用于「T 及其子类」检查的族标志；None 表示无子类，
//                   「T 及其子类」退化为精确检查（IsInstance<T> 用）。
template <typename T>
struct TypeTraits{
	static constexpr PycpTypeId   id            = PycpTypeId::Unknown;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::None;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

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
		// 类型标识（精确类型，替代字符串判型；Class/Instance/Module 的
		// type_name 是动态名，故独立 type_id 是区分它们的必要手段）。
		PycpTypeId type_id_ = PycpTypeId::Unknown;
		// 类型标志位（子类型族/能力，对应 CPython 的 tp_flags）。
		PycpTypeFlag type_flags_ = PycpTypeFlag::None;

		// 可见性标记：true 表示私有（private），false 表示公开（public）。
		// 作为所有对象的通用属性，供 public/private 装饰器（C++ ABI 底层）设置：
		//   - 类内成员：控制该成员在类外的访问可见性。
		//   - 模块顶层符号：控制其他文件 import 时是否可访问。
		bool private_;
		// 只读标记：true 表示对象被冻结（readonly）。由 readonly 装饰器
		// （C++ ABI 底层）设置，用于：
		//   - 任意对象冻结：阻止该对象的动态属性写入/删除（obj.attr = v）。
		//   - 模块只读绑定（常量）：顶层符号绑定不可被再次赋值覆盖。
		bool readonly_ = false;

	protected:
		// 成员字典：name -> Object*（类似 Python 的 __dict__）。
		// 供 __get_attribute__ / __set_attribute__ / __inspect__ 使用。
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

		// 精确类型标识（编译期枚举，O(1)）。
		PycpTypeId type_id() const { return type_id_; }
		// 类型标志位（子类型族/能力）。
		PycpTypeFlag type_flags() const { return type_flags_; }

		// 类型判定便捷方法。
		bool is_type(const std::string& name) const { return type_name_ == name; }

		// 类型名重写（子类构造时覆盖基类初始化列表设定的类型名，
		// 例如 Boolean 继承 Integer 后需将 "Integer" 改为 "Boolean"）。
		// 注意 type_name() 非虚，类型判定依赖该成员，故此 setter 必要。
		void set_type_name(const std::string& name) { type_name_ = name; }

		// 设置类型信息（子类构造函数用）：精确类型 id 与类型标志位 flags。
		// type_name 仅用于显示与 __name__，不参与类型判断。
		void set_type_info(PycpTypeId id, PycpTypeFlag flags) {
			type_id_ = id;
			type_flags_ = flags;
		}

		// 可见性查询/设置（默认 public，即 private_ == false）。
		bool is_private() const { return private_; }
		void set_private(bool priv) { private_ = priv; }

		// 只读查询/设置（默认可写，即 readonly_ == false）。
		bool is_readonly() const { return readonly_; }
		void set_readonly(bool ro) { readonly_ = ro; }

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

		// 对象所属「类型类」（typeof / __class__ 用）。
		// 默认按运行时类型名查注册表（LookupTypeClass），未登记则惰性合成；
		// Class 覆写为返回 pycp.Object，Instance 返回所属类，Module 固定查
		// "Module" 键（其 type_name_ 为模块名，不能按名查表）。
		virtual Class* get_type_class();

		virtual Object* __integer__();
		virtual Object* __string__();
		// 原始字符串形式（对应 Python 的 __repr__）：供容器渲染元素与键值时
		// 调用，使「是否加引号 / 如何转义」由元素自身决定（字符串带引号并
		// 转义，数值与 None 等保持无引号形式）。
		//
		// 默认实现返回固定形式 "<name at 0xADDR>"，且**不**转调虚 __string__：
		// repr 与 str 相互独立，用户覆写 __string__ 不会改变默认 repr。
		virtual Object* __raw_string__();
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

	// 成员字典视图（类似 Python 的 __dict__）。
	// 返回绑定本对象的 Map 视图，实时反映/修改对象成员
	// （经 __get_attribute__ / __set_attribute__ / __delete_attribute__）。
	// 默认实现：返回 owner=this 的 Map 视图（仅含 members_）。
	virtual Object* __map__();

	// 数据成员键值对（用于 __map__ 视图遍历/字符串化）。
	// 默认返回 members_ 中所有项；Instance override 合并 fields_+动态成员，
	// 且排除类方法（仅反映实例数据，对齐 Python __dict__）。
	virtual std::vector<std::pair<std::string, Object*>> member_pairs() const;

	// 哈希值（Pycp.Integer）。默认抛 TypeError（不可哈希类型）。
	// 可哈希类型（Integer/String）override 返回对象哈希。
	virtual Object* __hash__();

	// 删除协议（delete 关键字）。
	// __delete__()：delete obj 触发（默认抛 TypeError，供用户自定义清理逻辑，
	//   不会触发 C++ 析构，内存仍由 GC 管理）。
	// __delete_attribute__(name)：delete obj.attr 触发（默认从 members_ 删除，
	//   不存在抛 AttributeError）。Instance override 优先从 fields_ 删除。
	// __delete_item__(key)：delete obj[key] 触发（默认抛 TypeError，Map/List 改
	//   为按 key/索引删除）。
	virtual Object* __delete__();
	virtual void __delete_attribute__(const std::string& name);
	virtual Object* __delete_item__(Object* key);

	// 迭代协议。
	// __iterator__()：返回一个全新的迭代器（默认不可迭代，抛 TypeError）。
	//   List/String 可迭代，每次调用返回新的独立迭代器实例。
	// __next__()：迭代器推进，返回下一元素（Owned）；耗尽后抛 StopIteration。
	//   默认抛 TypeError（仅迭代器子类有意义）。
	virtual Object* __iterator__();
	virtual Object* __next__();

	// 属性名枚举（Pycp.insp(obj)）：返回本对象所有成员名称的
	// List（含属性与方法）。默认返回 members_ 的 key；子类可 override 添加额外成员名。
	virtual Object* __inspect__();

	// GC 子引用遍历：枚举本对象持有的 Object* 子引用，供标记-清除与
	// Decref 递归释放统一使用（替代旧 Type 枚举的 switch 分发）。
	// 默认空实现；持有子引用的子类（None/Class/Instance/
	// File/List 等）override。
	virtual void foreach_ref(const std::function<void(Object*)>& visit);

		};

// =============================================================
// 类型判断接口（语义对齐 CPython，C++17 实现，无宏）
//
//   TypeOf(ob)          ~ Py_TYPE            获取类型标识
//   IsType(ob, id)      ~ Py_IS_TYPE         精确类型比较
//   HasFlag(ob, flag)   ~ PyType_HasFeature  类型标志位检查
//   IsInteger(ob)       ~ PyLong_Check       子类检查（含 Boolean）
//   IsIntegerExact(ob)  ~ PyLong_CheckExact  精确检查（不含 Boolean）
//   IsExact<T>(ob)      ~ 模板化精确检查
//   IsInstance<T>(ob)   ~ 模板化子类/族检查
//
// 迁移指引（旧写法 → 新写法，行为等价）：
//   obj->is_type("Integer")                → IsIntegerExact(obj)
//   dynamic_cast<Integer*>(obj) != nullptr → IsInteger(obj)   // 含 Boolean（is-a）
//   obj->is_type("String")                 → IsString(obj)
//   obj->type_name() == "X"                → IsType(obj, PycpTypeId::X)
// =============================================================

// 获取对象类型标识（对应 Py_TYPE；nullptr 返回 Unknown）。
inline PycpTypeId TypeOf(const Object* ob){
	return (ob != nullptr) ? ob->type_id() : PycpTypeId::Unknown;
}

// 精确类型比较（对应 Py_IS_TYPE）。
inline bool IsType(const Object* ob, PycpTypeId id){
	return (ob != nullptr) && ob->type_id() == id;
}

// 类型标志位检查（对应 PyType_HasFeature）。
inline bool HasFlag(const Object* ob, PycpTypeFlag flag){
	return (ob != nullptr) && has_flag(ob->type_flags(), flag);
}

// Integer 子类检查（对应 PyLong_Check：含 Boolean）。
inline bool IsInteger(const Object* ob){
	return HasFlag(ob, PycpTypeFlag::IntegerSubclass);
}

// Integer 精确检查（对应 PyLong_CheckExact：不含 Boolean）。
inline bool IsIntegerExact(const Object* ob){
	return IsType(ob, PycpTypeId::Integer);
}

// String 子类检查（当前 String 无子类，等价精确）。
inline bool IsString(const Object* ob){
	return HasFlag(ob, PycpTypeFlag::StringSubclass);
}

// 编译期精确检查：ob 的精确类型是否为 T（对应 PyLong_CheckExact 泛化）。
template <typename T>
inline bool IsExact(const Object* ob){
	return IsType(ob, TypeTraits<T>::id);
}

// 编译期子类/族检查：ob 是否为 T 或 T 的子类（对应 PyLong_Check 泛化）。
//   若 T 有子类族标志（如 Integer 的 IntegerSubclass），按标志判断；
//   否则（无子类的类型）退化为精确检查。
template <typename T>
inline bool IsInstance(const Object* ob){
	if constexpr (TypeTraits<T>::subclass_flag != PycpTypeFlag::None){
		return HasFlag(ob, TypeTraits<T>::subclass_flag);
	} else {
		return IsExact<T>(ob);
	}
}

class Integer;
class String;
class Class;

// 引用计数操作（C-ABI 别名 PYCP_Incref/PYCP_Decref），声明于 PycpGC.hpp。
void Decref(Object* obj);
// 对象相等判定辅助：调用 a->__equal__(b)，读取返回 Integer 0/1 后释放临时结果。
// 完整内联定义见 PycpInteger.hpp（依赖 Integer 完整类型）。
bool object_equal(Object* a, Object* b);

} // namespace Pycp

#endif // PYCP_OBJECT_HPP