#ifndef PYCP_CLASS_HPP
#define PYCP_CLASS_HPP

// =============================================================
// Pycp 类与实例对象（Class / Instance）
//
// Class 对应一次 class 定义：持有类名、成员变量名表与
// 方法表（方法名 -> Function*，含 __initialize__ / __string__ /
// 运算符重载等魔术方法）。
//
// Instance 对应一次实例化：持有所属 Class 与实例
// 字段表（字段名 -> Object*）。字段读写经 __get_attribute__/__set_attribute__。
//
// 运算符重载语义：Instance 的 __addition__ 等虚函数转发到
// 其类中同名魔术方法（__addition__ 等），即通过 ABI 的 Call 调
// 用该方法。未定义对应魔术方法时抛 TypeError。
//
// 注意：本版不支持继承（class 语法无 inherits 子句）。
// =============================================================

#include "PycpObject.hpp"
#include "PycpFunction.hpp"
#include "PycpList.hpp"
#include "PycpMethodTable.hpp"   // MethodEntry / MethodTableFn / PycpCFunction

#include <string>
#include <unordered_map>
#include <vector>

namespace Pycp {

class Module;   // RegisterTypeObject 的宿主模块参数（仅以指针使用）

// =============================================================
// 运行时「类型类」注册表（typeof / __class__ 用）
// =============================================================
// 映射「C++ 运行时类型名 -> 类对象」，供内置值对象（String/Integer/
// List/Map/File/None/Function/迭代器等）解析其所属类。native 模块
// （pycp/io）在创建类型类后登记；未登记的类型名由 LookupTypeClass
// 惰性合成普通 Class 并缓存。
PYCP_API void RegisterTypeClass(const std::string& type_name, Class* cls);
PYCP_API Class* LookupTypeClass(const std::string& type_name);  // Borrowed

// pycp.Object 类全局指针（typeof 对类对象 / 类自身的 __class__ 返回它）。
PYCP_API void RegisterObjectClass(Class* cls);
PYCP_API Class* LookupObjectClass();                            // Borrowed

// =============================================================
// 统一类型对象注册（方法表驱动，一次性完整注册）
//
// 把「创建类型对象 + 放入模块命名空间 + 逐条注册方法 + 注册对象自身
// __initialize__ + 登记类型类注册表」收敛为单次调用；方法清单以传入的
// 方法表为唯一权威来源。供 pycp / io 等原生扩展共用（ABI 层）。
//
//   mod        : 宿主模块（必须非空；类型对象放入其命名空间）。
//   name       : 类型名（= 命名空间键 = 类型类注册键）。
//   ctor       : 非空 → BuiltinTypeClass（实例化走 ctor）；
//                空   → 普通 Class（实例化走默认 Instance 创建）。
//   initialize : 对象自身的 __initialize__（nullptr 表示无）。
//   table      : 方法表访问器（全部方法的唯一权威来源，可为 nullptr）。
// 返回创建的类型对象（Borrowed，由命名空间与类型类注册表持有）。
// =============================================================
PYCP_API Class* RegisterTypeObject(Module* mod, const char* name,
                                   PycpCFunction ctor,
                                   PycpCFunction initialize,
                                   MethodTableFn table);

class PYCP_API Class : public Object {
private:
	std::string name_;
	// 父类（单继承，无父类时为 nullptr）。
	Class* parent_;
	// 成员变量名（类定义中声明的顺序）。
	std::vector<std::string> member_names_;
	// 成员变量可见性：name -> true 表示 private。
	std::unordered_map<std::string, bool> member_visibility_;
	// 成员变量只读：name -> true 表示 readonly（实例字段赋值/删除被拦截）。
	std::unordered_map<std::string, bool> member_readonly_;
	// 方法表：方法名 -> 方法对象（Function*）。类拥有其引用。
	std::unordered_map<std::string, Function*> methods_;
	// 方法可见性：name -> true 表示 private。
	std::unordered_map<std::string, bool> method_visibility_;

public:
	explicit Class(const std::string& name);
	~Class() override;

	// 静态工厂：创建指定名字的类对象（返回 Owned，refcount=1）。
	static Class* New(const std::string& name);

	// 静态操作：添加方法（与旧 ABI 自由函数语义一致）。
	static void AddMethod(Class* cls, const std::string& name, Object* fn);

	// 覆盖基类虚函数：返回类名（供默认 __string__ 与异常信息使用）。
	const char* get_name() const override { return name_.c_str(); }

	// 类对象的类型（typeof/__class__）：统一返回 pycp.Object。
	Class* get_type_class() override;

	Class* get_parent() const { return parent_; }
	void set_parent(Class* parent);

	const std::vector<std::string>& get_member_names() const { return member_names_; }
	void add_member_name(const std::string& name);
	// 带可见性添加成员变量名。
	void add_member_name(const std::string& name, bool is_private);
	// 带可见性 + 只读添加成员变量名。
	void add_member_name(const std::string& name, bool is_private, bool is_readonly);

	// 成员可见性查询：name 未记录时默认 public（false）。
	bool member_is_private(const std::string& name) const;

	// 成员只读查询：name 未记录时默认可写（false）。
	bool member_is_readonly(const std::string& name) const;

	// 添加方法（接管引用计数：内部 Incref，析构 Decref）。
	void add_method(const std::string& name, Function* fn);
	// 带可见性添加方法。
	void add_method(const std::string& name, Function* fn, bool is_private);

	// 方法可见性查询：name 未记录时默认 public（false）。
	bool method_is_private(const std::string& name) const;

	// 查找方法，返回 Borrowed；未找到返回 nullptr。
	Function* find_method(const std::string& name) const;

	// 枚举全部方法名（供继承复制父类方法到子类）。
	std::vector<std::string> method_names() const;

	// 类自身属性访问（方法查找）。
	Object* __get_attribute__(const std::string& name) override;

	// 类的字符串表示：<class "name">。
	// 匿名类（内部名为 @anonymous）沿用同一格式，输出 <class "@anonymous">。
	Object* __string__() override;

	// 属性名枚举：返回类的方法名 + 通用成员。
	Object* __inspect__() override;

	// 数据成员键值对（含类方法名），供 __map__ 视图遍历/字符串化使用。
	std::vector<std::pair<std::string, Object*>> member_pairs() const override;

	// 实例化钩子：调用类时由 VM 的 CALL 指令触发。
	// 默认实现创建 Instance，先应用成员初始值（__init_defaults__），
	// 再调用 __initialize__（MAGIC_INITIALIZE）并返回该实例（Owned）。
	// 子类（如 BuiltinTypeClass）可重写以返回内置对象。
	virtual Object* instantiate(Object** argv, std::size_t argc);

	// GC 子引用遍历：枚举方法表（methods_）中的 Function。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// =============================================================
// BuiltinTypeClass：内置类型类（String / Integer 等）
//
// 通过「类实例化」语义暴露内置类型：调用 BuiltinTypeClass 时把
// 参数传给原生构造回调，直接返回内置对象（String / Integer），
// 而非 Instance。语义对齐 Python 的 str(x) / int(x)。
// =============================================================
class PYCP_API BuiltinTypeClass : public Class {
private:
	// 原生构造回调：接收 argv/argc，返回 Owned 内置对象。
	PycpCFunction ctor_;

public:
	BuiltinTypeClass(const std::string& name, PycpCFunction ctor);

	// 校验参数个数后调用构造回调，返回内置对象。
	Object* instantiate(Object** argv, std::size_t argc) override;
};

class PYCP_API Instance : public Object {
private:
	Class* cls_;
	// 实例字段表：字段名 -> Object*。实例拥有其引用。
	std::unordered_map<std::string, Object*> fields_;

	// 转发运算符重载到类中的同名魔术方法（不存在则抛 TypeError）。
	Object* dispatch_magic(const std::string& name, Object* other);

public:
	explicit Instance(Class* cls);
	~Instance() override;

	// 静态工厂：创建指定类的实例（返回 Owned，refcount=1）。
	static Instance* New(Class* cls);

	Class* get_class() const { return cls_; }

	// 实例的类型（typeof/__class__）：返回所属类对象 cls_。
	Class* get_type_class() override;

	// 字段读写。
	Object* __get_attribute__(const std::string& name) override;
	void __set_attribute__(const std::string& name, Object* value) override;

	// 成员字典视图（类似 Python 的 __dict__）：返回绑定本实例、合并
	// fields_ 与动态 members_ 的 Map 视图（实时同步）。
	Object* __map__() override;

	// 删除协议：delete obj.attr 触发，优先从 fields_ 删除，否则 members_。
	void __delete_attribute__(const std::string& name) override;
	// delete obj 触发：用户定义 __delete__ 则调用，否则 Object 默认抛错。
	Object* __delete__() override;
	// delete obj[key] 触发：用户定义 __delete_item__ 则调用，否则默认抛错。
	Object* __delete_item__(Object* key) override;

	// 数据成员键值对（合并 fields_ + 动态 members_，排除类方法）。
	// 供 __map__ 视图遍历/字符串化使用。
	std::vector<std::pair<std::string, Object*>> member_pairs() const override;

	// 取绑定方法：新建 BoundMethod（Owned，refcount=1）。
	//   仅当 name 为类方法时有效，否则返回 nullptr。
	Object* get_bound_method(const std::string& name);

	// 属性名枚举：返回字段名 + 类方法名 + 通用成员。
	Object* __inspect__() override;

	// 字符串转换：类定义 __string__ 时转发，否则返回默认 "<ClassName instance>"。
	Object* __string__() override;

	// 整数转换：类定义 __integer__ 时转发，否则抛 TypeError。
	Object* __integer__() override;

	// 运算符重载（转发到类的同名魔术方法）。
	Object* __negation__() override;
	Object* __addition__(Object* other) override;
	Object* __subtraction__(Object* other) override;
	Object* __multiplication__(Object* other) override;
	Object* __division__(Object* other) override;
	Object* __power__(Object* other) override;
	Object* __less_than__(Object* other) override;
	Object* __less_equal__(Object* other) override;
	Object* __equal__(Object* other) override;
	Object* __not_equal__(Object* other) override;
	Object* __greater_than__(Object* other) override;
	Object* __greater_equal__(Object* other) override;

	// 下标运算：转发到类的 __get_item__ / __set_item__ 方法。
	Object* __get_item__(Object* key) override;
	Object* __set_item__(Object* key, Object* value) override;

	// list 转换：转发到类的 __list__ 方法（未定义抛 TypeError）。
	Object* __list__() override;

	// GC 子引用遍历：枚举所属类（cls_）与实例字段（fields_）。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// =============================================================
// 方法内部访问标志（thread_local）
// =============================================================
// 方法体执行期间（BytecodeFunction::invoke）递增深度，退出递减。
// Instance 的 __get_attribute__/__set_attribute__ 据此区分「方法内部 self 访问」
// 与「外部 obj 访问」：深度 > 0 时放行 private，否则拦截。
// =============================================================
int internal_access_depth();
void enter_internal_access();
void leave_internal_access();

// =============================================================
// 当前 self 上下文（thread_local 栈）
// =============================================================
// 方法体执行期间压入当前接收者实例（self），供 super() 原生函数读取
// 当前实例 → 所属类 → 父类。与 internal_access_depth 配套使用。
// =============================================================
// 标记 PYCP_API：Windows shared 运行时导出，供扩展 DLL 与 VM 跨 DLL 解析。
PYCP_API void push_current_self(Instance* self);
PYCP_API void pop_current_self();
PYCP_API Instance* current_self();

// 当前方法所属类上下文（供 super() 解析父类，避免基于最派生实例类
// 导致继承链 super 调用无限递归到自身）。
PYCP_API void push_current_class(Class* cls);
PYCP_API void pop_current_class();
PYCP_API Class* current_class();

// =============================================================
// BoundMethod：绑定到接收者对象的方法对象
//
// 当从实例 / 文件等对象取方法（obj.method）时返回 BoundMethod，其
// invoke 会将接收者作为 self（argv[0]）传入底层方法，实现 self 的
// 自动绑定。接收者为任意 Object*（Instance / File 等）。
// =============================================================
class PYCP_API BoundMethod : public Function {
private:
	Object* instance_;   // 绑定接收者（持有引用）
	Function* method_;   // 底层方法（持有引用）

public:
	BoundMethod(Object* inst, Function* method);
	~BoundMethod() override;

	Object* invoke(Object** argv, std::size_t argc) override;

	// GC 子引用遍历：枚举绑定接收者（instance_）与底层方法（method_）。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// 类型萃取特化：Class（可调用）/ Instance。
template <> struct TypeTraits<Class> {
	static constexpr PycpTypeId   id            = PycpTypeId::Class;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::Callable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};
template <> struct TypeTraits<Instance> {
	static constexpr PycpTypeId   id            = PycpTypeId::Instance;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::None;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif // PYCP_CLASS_HPP