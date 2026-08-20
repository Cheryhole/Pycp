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

#include <string>
#include <unordered_map>

namespace Pycp {

class Module : public Object {
private:
	std::string name_;   // 模块名（不含 .pycp 后缀）
	// 模块命名空间：该模块顶层定义的名称（含函数、变量等）。
	// 由执行该模块顶层的 VM / AOT 填充。
	std::unordered_map<std::string, Object*> namespace_;

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

	// 读取/写入命名空间（供 VM / AOT 填充与查询）。
	// 注意：直接操作裸指针，引用计数由调用方管理。
	std::unordered_map<std::string, Object*>* get_namespace() { return &namespace_; }

	// 属性访问：namespace_ 中查 name，未找到抛 AttributeError。
	Object* __getattr__(const std::string& name) override;

	// GC 子引用遍历：枚举命名空间（namespace_）中的值，供标记-清除从
	// 模块 root 出发标记可达对象。仅遍历不 Decref（析构不释放值，由
	// 模块执行环境收尾时统一管理）。
	void foreach_ref(const std::function<void(Object*)>& visit) override;
};

} // namespace Pycp

#endif // PYCP_MODULE_HPP
