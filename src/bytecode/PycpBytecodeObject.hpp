#ifndef PYCP_BYTECODE_OBJECT_HPP
#define PYCP_BYTECODE_OBJECT_HPP

// =============================================================
// 字节码结构的一等 pycp 对象包装（供 bytecode / compile / aot 模块共享）
// -------------------------------------------------------------
// 把 BC::Module 及其子结构（代码对象 / 类定义 / 指令）暴露为可遍历的 pycp
// 对象（类似 CPython 的 code object + dis 的结构化形态），字段为属性：
//   ModuleRef : source_path / constants / symbols / imports / code_objects /
//               classes；__string__() 返回 DumpModule 文本。
//   CodeRef   : name / nparams / default_count / nlocals / param_kinds /
//               param_kind_names / consts / names / const_refs / name_refs /
//               free_names / linenos / instructions。
//   ClassRef  : name / parent_name / member_names / member_decorators /
//               method_names / method_code_indices / method_decorators /
//               decorator_count。
//   InstrRef  : op / opcode / operand / line。
//
// 字段【惰性构建】并缓存于 members_：未访问的字段不创建对象；已构建字段随
// 本对象一起被 GC 遍历与释放。底层 Module 经 shared_ptr 共同持有，故子对象
// 在任意存活顺序下均安全。
// =============================================================

#include "object/PycpObject.hpp"
#include "bytecode/PycpBytecode.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace Pycp::BC {

// 包装一个字节码模块。
class ModuleRef : public Object {
public:
	explicit ModuleRef(std::shared_ptr<Module> module);

	Object* __get_attribute__(const std::string& name) override;
	Object* __inspect__() override;
	Object* __string__() override;
	Object* __raw_string__() override;

	const std::shared_ptr<Module>& module() const { return module_; }

private:
	Object* BuildField(const std::string& name);
	std::shared_ptr<Module> module_;
};

// 包装模块内的一个代码对象（按索引引用 module 的 code_objects）。
class CodeRef : public Object {
public:
	CodeRef(std::shared_ptr<Module> module, std::size_t index);

	Object* __get_attribute__(const std::string& name) override;
	Object* __inspect__() override;
	Object* __string__() override;
	Object* __raw_string__() override;

private:
	Object* BuildField(const std::string& name);
	std::shared_ptr<Module> module_;
	std::size_t index_;
};

// 包装模块内的一个类定义（按索引引用 module 的 classes）。
class ClassRef : public Object {
public:
	ClassRef(std::shared_ptr<Module> module, std::size_t index);

	Object* __get_attribute__(const std::string& name) override;
	Object* __inspect__() override;
	Object* __string__() override;
	Object* __raw_string__() override;

private:
	Object* BuildField(const std::string& name);
	std::shared_ptr<Module> module_;
	std::size_t index_;
};

// 包装一条指令（op / opcode / operand / line）。
class InstrRef : public Object {
public:
	InstrRef(Op op, int32_t operand, int line);

	Object* __get_attribute__(const std::string& name) override;
	Object* __inspect__() override;
	Object* __string__() override;
	Object* __raw_string__() override;

private:
	Object* BuildField(const std::string& name);
	Op op_;
	int32_t operand_;
	int line_;
};

// 包装（返回 Owned）。module 为空时返回 nullptr。
ModuleRef* WrapModule(std::shared_ptr<Module> module);

// 判定是否为 ModuleRef。
bool IsModuleRef(const Object* obj);

// 解包：非 ModuleRef 返回空 shared_ptr。
std::shared_ptr<Module> UnwrapModule(const Object* obj);

// 指令操作码名称（如 LOAD_CONST / BINARY_ADD）；未知返回 "UNKNOWN"。
const char* OpName(Op op);

} // namespace Pycp::BC

#endif // PYCP_BYTECODE_OBJECT_HPP
