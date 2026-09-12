#ifndef PYCP_MODULE_HPP
#define PYCP_MODULE_HPP

// =============================================================
// Pycp 模块对象（Module）
//
// 对应 Python 的模块对象：import foo 后在导入者作用域绑定一个
// Module，通过属性访问（foo.x）读取该模块顶层定义的名称。
//
// 每个模块拥有独立的命名空间（namespace_：name -> Object*），
// 与其它模块的 globals 隔离，避免跨模块同名变量互相污染。
// =============================================================

#include "PycpObject.hpp"
#include "PycpString.hpp"
#include "PycpMethodTable.hpp"   // PycpCFunction / MethodEntry / MethodTableFn

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Pycp {

class Class;

// 绑定级访问属性（模块顶层符号 / 类成员等的权威来源）：
//   priv     : 私有（对其他文件 / 类外不可见）
//   readonly : 只读常量绑定（禁止再次赋值覆盖）
// 与 Object 上的值级 private_/readonly_ 区分：绑定级属性描述“这个名字”，
// 不污染绑定的值对象（尤其被池化共享的 Integer/String）。
struct AccessAttrs {
	bool priv = false;
	bool readonly = false;
};

class PYCP_API Module : public Object {
private:
	std::string name_;   // 模块名（不含 .pycp 后缀）
	// __name__ 规范值：入口模块为 "__main__"，其余模块为模块名。
	// 与 name_ 解耦，使入口模块的 __string__/__name__ 显示 "__main__"
	// 而不影响 name_（模块缓存键 / type_name）。
	std::string module_name_;
	// 模块命名空间：该模块顶层定义的名称（含函数、变量等）。
	// 由执行该模块顶层的 VM / AOT 填充。
	std::unordered_map<std::string, Object*> namespace_;
	// 绑定级访问属性：name -> {priv, readonly}。由顶层声明装饰器登记，
	// 为模块符号可见性 / 常量绑定的权威来源。
	std::unordered_map<std::string, AccessAttrs> binding_attrs_;

public:
	explicit Module(const std::string& name);
	~Module() override;

	// 静态工厂：创建指定名字的模块对象（返回 Owned，refcount=1）。
	static Module* New(const std::string& name);

	// 静态操作：从模块对象取属性（与旧 ABI 自由函数语义一致）。
	// 返回 Borrowed 引用；未找到抛 AttributeError。
	static Object* GetAttr(Module* mod, const std::string& name);

	// 覆盖基类虚函数：返回模块名。
	const char* get_name() const override { return name_.c_str(); }

	// 模块的类型（typeof/__class__）：Module 的 type_name_ 是模块名，
	// 固定查注册表 "Module" 键（未登记则合成 <class "Module">）。
	Class* get_type_class() override;

	// 设置 __name__ 规范值（入口模块设为 "__main__"，其余保持模块名）。
	void set_module_name(const std::string& n) { module_name_ = n; }

	// 读取/写入命名空间（供 VM / AOT 填充与查询）。
	// 注意：直接操作裸指针，引用计数由调用方管理。扩展作者应优先使用下面
	// 的 set_* 系列显式绑定 API，而非直接操作命名空间。
	std::unordered_map<std::string, Object*>* get_namespace() { return &namespace_; }

	// =============================================================
	// 显式绑定 API（替代旧 PYCP_SET_FUNC 宏与手工写命名空间）
	//
	// 全部方法把值放入本模块命名空间；引用计数由本方法负责移交
	// （命名空间持有一份，调用方无需 Incref / Decref）。
	// =============================================================

	// 绑定原生函数。
	//   fn            : 容器形态原生函数（PycpCFunction，见 PycpMethodTable.hpp）
	//   with_keywords : 该函数是否接受关键字参数（供 VM 判定是否允许以
	//                   `f(x=1)` 形式调用；当前语言层无关键字实参来源，
	//                   先作为元信息登记）。参数个数 / 名字 / 默认值由函数
	//                   内部经 Extension::CompileArgs 的规范表自行声明与校验。
	void set_function(const char* name, PycpCFunction fn, bool with_keywords = false);

	// 绑定普通变量（可被脚本层重新赋值覆盖）。
	void set_variable(const char* name, Object* value);

	// 绑定只读常量（脚本层再次赋值抛 AttributeError）。
	void set_constant(const char* name, Object* value);

	// 通用对象登记：把任意对象放入命名空间；若对象是类型对象（Class），
	// 同时登记到运行时类型类注册表（typeof / __class__ 解析用）。
	void set_object(const char* name, Object* obj);

	// 类型对象语义化注册（一次性完整注册）。
	//   name       : 类型名（= 命名空间键 = 类型类注册键）
	//   ctor       : 非空 -> BuiltinTypeClass（实例化走 ctor）；
	//                空   -> 普通 Class（实例化走默认 Instance 创建）
	//   initialize : 对象自身的 __initialize__（nullptr 表示无）
	//   table      : 方法表访问器（全部方法的唯一权威来源，可为 nullptr）
	// 返回创建的类型对象（Borrowed，由命名空间与类型类注册表持有）。
	Class* set_type(const char* name, PycpCFunction ctor,
	                PycpCFunction initialize, MethodTableFn table);

	// 绑定级属性登记/查询。
	//   mark_binding(name, attrs) 覆盖写入该名字的属性；
	//   binding_attrs(name) 返回其属性（未登记默认全 false）；
	//   is_readonly_binding 为只读常量绑定的便捷查询（兼容旧调用点）。
	void mark_binding(const std::string& name, const AccessAttrs& attrs) {
		binding_attrs_[name] = attrs;
	}
	AccessAttrs binding_attrs(const std::string& name) const {
		auto it = binding_attrs_.find(name);
		return (it != binding_attrs_.end()) ? it->second : AccessAttrs{};
	}
	void mark_readonly_binding(const std::string& name) {
		binding_attrs_[name].readonly = true;
	}
	bool is_readonly_binding(const std::string& name) const {
		return binding_attrs(name).readonly;
	}

	// 解析 __name__ 值对象（无递归）：
	//   members_ 覆盖 -> namespace_["__name__"] -> 回退 String(module_name_)。
	// 供 __get_attribute__("__name__") / __string__ / GetCurrentModuleName 共用。
	Object* resolve_name_value();

	// 属性访问：namespace_ 中查 name，未找到抛 AttributeError。
	Object* __get_attribute__(const std::string& name) override;

	// 属性赋值：拒绝写入只读常量绑定（@readonly）；其余走通用动态成员。
	void __set_attribute__(const std::string& name, Object* value) override;

	// 覆盖基类虚函数：返回该模块所有可用成员名（成员字典 key +
	// 命名空间中非 private 的公开名称）的 List。
	Object* __inspect__() override;

	Object* __string__();
	// GC 子引用遍历：枚举命名空间（namespace_）中的值，供标记-清除从
	// 模块 root 出发标记可达对象。仅遍历不 Decref（析构不释放值，由
	// 模块执行环境收尾时统一管理）。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// 全局「当前正在执行的模块」：VM / AOT 在执行某模块顶层前设置、后恢复。
// 用于实现 pycp.__name__（即当前文件名称）。
PYCP_API extern Module* current_module_;

// 获取当前模块名（pycp.__name__ 的来源）：取 current_module_->resolve_name_value()，
// current_module_ 为空时回退 "__main__"。返回 Owned。
PYCP_API Object* GetCurrentModuleName();

// 类型萃取特化：Module。
template <> struct TypeTraits<Module> {
	static constexpr PycpTypeId   id            = PycpTypeId::Module;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::None;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif // PYCP_MODULE_HPP