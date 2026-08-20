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
#include "PycpEnvironment.hpp"
#include "PycpModule.hpp"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Pycp {

namespace BC {

struct Module;

// =============================================================
// VM：模块级执行上下文
// =============================================================

class VM {
public:
	// 从反序列化后的 Module 构造 VM。
	//   module 的 runtime_consts 已为 GC root 保护的对象，
	//   其生命周期由调用方（Module 持有者）保证存活于 VM 使用期间。
	//   registry : 模块注册表（模块名 -> Module*）。含入口模块及其所有
	//              import 依赖。为 nullptr 时 VM 仅执行单模块，
	//              遇到 LOAD_MODULE 抛 ImportError。
	//   entry_name : 入口模块名（registry 中 module 对应的 key）。
	//              用于循环导入占坑；空串表示无注册表。
	explicit VM(Module* module, std::map<std::string, Module*>* registry = nullptr,
	            const std::string& entry_name = "");
	~VM();

	// 执行顶层代码（code_objects[0]），返回其返回值（通常为 None）。
	Object* run();

	// 供 BytecodeFunction::invoke 调用：执行指定模块的指定代码对象。
	//   m       : 所属模块（函数定义时所在模块，跨模块调用需切换）
	//   co_idx  : code_objects 索引
	//   argv/argc : 实参
	//   captured  : 闭包捕获环境（定义时所在环境）
	Object* call(Module* m, size_t co_idx, Object** argv, std::size_t argc,
	             std::shared_ptr<Environment> captured);

	// 获取所属模块（供 BytecodeFunction 读取函数名等元数据）
	Module* get_module() const { return module_; }

private:
	Module* module_;
	std::shared_ptr<Environment> global_env_; // 持有 globals map
	Pycp::Module* entry_mod_ = nullptr;       // 入口模块对象（其 namespace 即顶层 globals）

	// 模块注册表（模块名 -> Module*）与已加载模块对象缓存。
	// 生命周期由调用方保证（registry 中的 Module 存活于 VM 使用期间）。
	std::map<std::string, Module*>* registry_;
	std::map<std::string, Pycp::Module*> module_cache_;

	// 执行单个代码对象（共享执行循环核心）
	Object* execute(CodeObject* co,
	                std::shared_ptr<Environment> env,
	                Object** argv, std::size_t argc);

	// 加载（或取缓存）指定模块，返回其 Pycp::Module（Borrowed，不增引用）。
	// 首次加载会执行该模块顶层并填充命名空间。
	Pycp::Module* load_module(const std::string& name);
};

} // namespace BC

// =============================================================
// 字节码函数对象
// =============================================================

class BytecodeFunction : public Function {
private:
	BC::VM* vm;                                 // 所属 VM（执行上下文）；native 模式下为 nullptr
	BC::Module* module;                         // 定义时所属模块（跨模块调用切换用）；native 模式下为 nullptr
	std::size_t code_idx;                       // code_objects 索引
	std::shared_ptr<BC::Environment> captured;  // 定义时环境（闭包）
	PycpNativeFunction native_fn_ = nullptr;    // native 模式目标函数指针（AOT 生成的 pycp_fn_N）

public:
	BytecodeFunction(BC::VM* vm, BC::Module* module, std::size_t code_idx,
	                 std::shared_ptr<BC::Environment> captured);

	// native 模式构造（AOT 产物用，不依赖 VM）：
	// 将 AOT 生成的 pycp_fn_N 作为 invoke 目标，self 即本对象（可经 get_captured 取捕获环境）。
	BytecodeFunction(const std::string& name, PycpNativeFunction native_fn,
	                 std::shared_ptr<BC::Environment> captured);

	Object* invoke(Object** argv, std::size_t argc) override;

	std::size_t get_code_idx() const { return code_idx; }

	// 捕获环境访问器（native 模式下 AOT 生成的 pycp_fn_N 经 self 读取）。
	const std::shared_ptr<BC::Environment>& get_captured() const { return captured; }
};

} // namespace Pycp

#endif // PYCP_BYTECODE_VM_HPP
