#ifndef PYCP_BYTECODE_VM_HPP
#define PYCP_BYTECODE_VM_HPP

// =============================================================
// Pycp 字节码虚拟机（栈式）
//
// 职责：
//   - 维护执行环境（全局/局部作用域）
//   - 解释执行 CodeObject 指令流
//   - 运算经 PycpABI 转发；比较返回 Integer 0/1
//   - 函数调用 / 返回 / 错误处理
//
// 作用域模型：
//   - 全局环境：std::unordered_map<std::string, Object*>（可变，可动态添加）
//   - 函数局部：调用时创建的 slots（vector<Object*>），参数绑定到 slots[0..nparams)
//   - 闭包捕获：BytecodeFunction 持有定义时的局部环境（shared_ptr），
//     变量查找顺序：局部 -> 捕获链 -> 全局。
// =============================================================

#include "PycpBytecode.hpp"
#include "PycpObject.hpp"
#include "PycpFunction.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Pycp {

namespace BC {

struct Module;

// =============================================================
// 执行环境（作用域）
// =============================================================

struct Environment {
	// 局部变量槽（函数调用时分配，大小 = nlocals）
	std::vector<Object*> locals;
	// 局部变量名 -> slots 索引（编译期确定，与 locals 对齐）
	std::vector<std::string> local_names;
	// 捕获的外部环境（闭包，可能成链）
	std::shared_ptr<Environment> captured;
	// 全局变量（共享可变 map；函数与模块共用同一实例）
	std::unordered_map<std::string, Object*>* globals = nullptr;

	// 在局部作用域查找变量，返回槽索引（-1 未找到）
	long find_local(const std::string& name) const {
		for (std::size_t i = 0; i < local_names.size(); ++i) {
			if (local_names[i] == name) return static_cast<long>(i);
		}
		return -1;
	}
};

// =============================================================
// VM：模块级执行上下文
// =============================================================

class VM {
public:
	// 从反序列化后的 Module 构造 VM。
	//   module 的 runtime_consts 已为 GC root 保护的对象，
	//   其生命周期由调用方（Module 持有者）保证存活于 VM 使用期间。
	explicit VM(Module* module);
	~VM();

	// 执行顶层代码（code_objects[0]），返回其返回值（通常为 None）。
	Object* run();

	// 供 BytecodeFunction::invoke 调用：执行指定代码对象。
	//   co_idx : code_objects 索引
	//   argv/argc : 实参
	//   captured  : 闭包捕获环境（定义时所在环境）
	Object* call(size_t co_idx, Object** argv, std::size_t argc,
	             std::shared_ptr<Environment> captured);

	// 获取所属模块（供 BytecodeFunction 读取函数名等元数据）
	Module* get_module() const { return module_; }

private:
	Module* module_;
	std::shared_ptr<Environment> global_env_; // 持有 globals map

	// 执行单个代码对象（共享执行循环核心）
	Object* execute(CodeObject* co,
	                std::shared_ptr<Environment> env,
	                Object** argv, std::size_t argc);
};

} // namespace BC

// =============================================================
// 字节码函数对象
// =============================================================

class BytecodeFunction : public Function {
private:
	BC::VM* vm;                                 // 所属 VM（执行上下文）
	std::size_t code_idx;                       // code_objects 索引
	std::shared_ptr<BC::Environment> captured;  // 定义时环境（闭包）

public:
	BytecodeFunction(BC::VM* vm, std::size_t code_idx,
	                 std::shared_ptr<BC::Environment> captured);

	Object* invoke(Object** argv, std::size_t argc) override;

	std::size_t get_code_idx() const { return code_idx; }
};

} // namespace Pycp

#endif // PYCP_BYTECODE_VM_HPP
