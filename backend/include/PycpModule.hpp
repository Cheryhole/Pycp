#ifndef PYCP_MODULE_HPP
#define PYCP_MODULE_HPP

// =============================================================
// Pycp 模块对象（ModuleObject）
//
// 对应 Python 的模块对象：import foo 后在导入者作用域绑定一个
// ModuleObject，通过属性访问（foo.x）读取该模块顶层定义的名称。
//
// 每个模块拥有独立的命名空间（namespace_：name -> Object*），
// 与其它模块的 globals 隔离，避免跨模块同名变量互相污染。
// =============================================================

#include "PycpObject.hpp"

#include <string>
#include <unordered_map>

namespace Pycp {

class ModuleObject : public Object {
private:
	std::string name_;   // 模块名（不含 .pycp 后缀）
	// 模块命名空间：该模块顶层定义的名称（含函数、变量等）。
	// 由执行该模块顶层的 VM / AOT 填充。
	std::unordered_map<std::string, Object*> namespace_;

public:
	explicit ModuleObject(const std::string& name);
	~ModuleObject() override;

	// 覆盖基类虚函数：返回模块名。
	const char* get_name() const override { return name_.c_str(); }

	// 读取/写入命名空间（供 VM / AOT 填充与查询）。
	// 注意：直接操作裸指针，引用计数由调用方管理。
	std::unordered_map<std::string, Object*>* get_namespace() { return &namespace_; }

	// 属性访问：namespace_ 中查 name，未找到抛 AttributeError。
	Object* __getattr__(const std::string& name) override;
};

} // namespace Pycp

#endif // PYCP_MODULE_HPP
