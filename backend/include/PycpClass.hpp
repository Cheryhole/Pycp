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
// 字段表（字段名 -> Object*）。字段读写经 __getattr__/__setattr__。
//
// 运算符重载语义：Instance 的 __addition__ 等虚函数转发到
// 其类中同名魔术方法（__addition__ 等），即通过 ABI 的 Call 调
// 用该方法。未定义对应魔术方法时抛 TypeError。
//
// 注意：本版不支持继承（class 语法无 inherits 子句）。
// =============================================================

#include "PycpObject.hpp"
#include "PycpFunction.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Pycp {

class PYCP_API Class : public Object {
private:
	std::string name_;
	// 父类（单继承，无父类时为 nullptr）。
	Class* parent_;
	// 成员变量名（类定义中声明的顺序）。
	std::vector<std::string> member_names_;
	// 成员变量可见性：name -> true 表示 private。
	std::unordered_map<std::string, bool> member_visibility_;
	// 方法表：方法名 -> 方法对象（Function*）。类拥有其引用。
	std::unordered_map<std::string, Function*> methods_;
	// 方法可见性：name -> true 表示 private。
	std::unordered_map<std::string, bool> method_visibility_;

public:
	explicit Class(const std::string& name);
	~Class() override;

	// 静态工厂：创建指定名字的类对象（返回 Owned，refcount=1）。
	static Class* New(const std::string& name);

	// 静态操作：添加成员名 / 方法（与旧 ABI 自由函数语义一致）。
	static void AddMemberName(Class* cls, const std::string& name);
	static void AddMethod(Class* cls, const std::string& name, Object* fn);

	// 覆盖基类虚函数：返回类名（供默认 __string__ 与异常信息使用）。
	const char* get_name() const override { return name_.c_str(); }

	Class* get_parent() const { return parent_; }
	void set_parent(Class* parent);

	const std::vector<std::string>& get_member_names() const { return member_names_; }
	void add_member_name(const std::string& name);
	// 带可见性添加成员变量名。
	void add_member_name(const std::string& name, bool is_private);

	// 成员可见性查询：name 未记录时默认 public（false）。
	bool member_is_private(const std::string& name) const;

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
	Object* __getattr__(const std::string& name) override;

	// 类的字符串表示：<Name class at 0xADDR>。
	Object* __string__() override;

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
	PycpNativeFunction ctor_;

public:
	BuiltinTypeClass(const std::string& name, PycpNativeFunction ctor);

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

	// 字段读写。
	Object* __getattr__(const std::string& name) override;
	void __setattr__(const std::string& name, Object* value) override;

	// 取绑定方法：新建 BoundMethod（Owned，refcount=1）。
	//   仅当 name 为类方法时有效，否则返回 nullptr。
	Object* get_bound_method(const std::string& name);

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
// Instance 的 __getattr__/__setattr__ 据此区分「方法内部 self 访问」
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

} // namespace Pycp

#endif // PYCP_CLASS_HPP
