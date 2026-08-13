#ifndef PYCP_BYTECODE_HPP
#define PYCP_BYTECODE_HPP

// =============================================================
// Pycp 字节码格式定义（.cpycp）
//
// 语言定位：Python-like 语言，沿用 Python 部分语法，但非 CPython。
// 因此 .cpycp 不兼容 CPython 的 marshal / .pyc，采用自研稳定二进制格式。
//
// 文件布局（按字节序写入，小端）：
//   [Magic 4B]["CYCP"] [Major 2B] [Minor 2B] [flags 4B]
//   [常量池] [符号表] [代码对象表]
//
// 每个段均以 uint32 长度前缀自描述，旧加载器可跳过未知段。
//
// 栈式字节码：1 字节 opcode + LEB128(无符号) 操作数。
// 0xE0~0xFF 预留扩展区间。
// =============================================================

#include "PycpObject.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Pycp::BC {

// =============================================================
// 文件头常量
// =============================================================

// 魔数 "CYCP"
extern const uint8_t MAGIC[4];
// 格式版本（Major 变更 = 不兼容，Minor 变更 = 向后兼容）
extern const uint16_t FORMAT_VERSION_MAJOR;
extern const uint16_t FORMAT_VERSION_MINOR;

// =============================================================
// 操作码（Opcode）
// =============================================================

enum class Op : uint8_t {
	// ---- 加载 / 存储 ----
	LOAD_CONST = 0x01,   // 操作数: const_idx   -> 压常量池对象
	LOAD_VAR   = 0x02,   // 操作数: name_idx    -> 压变量（全局/局部按作用域解析）
	STORE_VAR  = 0x03,   // 操作数: name_idx    -> 弹栈顶存入变量
	LOAD_NONE  = 0x04,   // 无操作数             -> 压 None
	POP_TOP    = 0x05,   // 无操作数             -> 弹栈
	DUP_TOP    = 0x06,   // 无操作数             -> 复制栈顶

	// ---- 运算（转调 ABI）----
	BINARY_ADD  = 0x10,  // a b -> c = Add(a,b)
	BINARY_SUB  = 0x11,
	BINARY_MUL  = 0x12,
	BINARY_DIV  = 0x13,
	UNARY_NEG   = 0x14,  // a -> -a

	// ---- 比较（操作数: 子操作码 CompareOp）----
	COMPARE_OP  = 0x20,  // a b -> Integer(0/1)

	// ---- 控制流 ----
	JUMP          = 0x30, // 操作数: 相对偏移(有符号)
	JUMP_IF_FALSE = 0x31, // 栈顶为假则跳转
	JUMP_IF_TRUE  = 0x32, // 栈顶为真则跳转

	// ---- 函数与调用 ----
	MAKE_FUNCTION = 0x40, // 操作数: code_idx  -> 创建 BytecodeFunction
	CALL          = 0x41, // 操作数: argc      -> 调用栈顶函数
	RETURN        = 0x42, // 弹返回值返回调用者
	RETURN_NONE   = 0x43, // 返回 None

	// ---- 其他 ----
	HALT          = 0x00, // 模块执行结束
};

// 比较子操作码（COMPARE_OP 的操作数）
enum class CompareOp : uint8_t {
	LT = 0, LE = 1, EQ = 2, NE = 3, GT = 4, GE = 5,
};

// =============================================================
// 指令
// =============================================================

struct Instruction {
	Op op;
	int32_t operand;
};

// =============================================================
// 常量池条目种类
// =============================================================

enum class ConstKind : uint8_t {
	INTEGER = 1,
	STRING  = 2,
	NONE    = 3,
};

// 常量池条目（编译期/反序列化后统一用 Object* 表示）
//   反序列化后 consts[i] 为已 GC root 的 Object*；
//   编译期 Codegen 期间 consts[i] 与 names 保持字符串/数值的临时形态。
struct Constant {
	ConstKind kind;
	int64_t int_value;         // kind == INTEGER
	std::string str_value;     // kind == STRING
};

// =============================================================
// 代码对象
// =============================================================

struct CodeObject {
	std::string name;                 // 函数名 / "<module>"
	uint16_t nparams = 0;             // 参数个数
	uint16_t nlocals = 0;             // 局部变量数（slots 大小）
	std::vector<Instruction> code;    // 指令流
	std::vector<Constant> consts;     // 本函数引用的常量（复用全局常量池索引）
	std::vector<std::string> names;   // 本函数引用的符号（复用全局符号表索引）
	std::vector<size_t> const_refs;   // code 中 LOAD_CONST 引用的全局常量索引（运行时重建用）
	std::vector<size_t> name_refs;    // code 中 LOAD_VAR/STORE_VAR 引用的全局符号索引
};

// =============================================================
// 编译单元（一个 .cpycp 文件的内存表示）
// =============================================================

struct Module {
	std::vector<Constant> const_pool;  // 全局常量池（反序列化后为 Object* 的宿主）
	std::vector<std::string> symtab;   // 全局符号表（去重）
	std::vector<CodeObject> code_objects; // code_objects[0] 为 <module> 顶层代码

	// 反序列化后重建的运行时对象（GC root 持有）
	std::vector<Object*> runtime_consts; // 与 const_pool 对齐的 Object* 实例
};

// =============================================================
// 序列化 / 反序列化
// =============================================================

// 将 Module 序列化为 .cpycp 二进制字节流。
std::vector<uint8_t> Serialize(const Module& module);

// 从 .cpycp 字节流反序列化出 Module。
//   校验魔数与版本，不匹配时抛出 Pycp::Exception。
//   runtime_consts 会被重建为 Object*（经 Integer_FromLong/String_FromString/None），
//   调用方负责对其 AddRoot（或由 VM 统一管理）。
Module Deserialize(const uint8_t* data, std::size_t size);

// =============================================================
// LEB128 编码辅助（无符号）
// =============================================================

// 写入无符号 LEB128 到 out 末尾
void WriteULEB128(std::vector<uint8_t>& out, uint64_t value);
// 从 data[offset] 起读取无符号 LEB128，返回其值并推进 offset
uint64_t ReadULEB128(const uint8_t* data, std::size_t size, std::size_t& offset);
// 写入有符号 LEB128（用于跳转偏移）
void WriteSLEB128(std::vector<uint8_t>& out, int64_t value);
// 读取有符号 LEB128
int64_t ReadSLEB128(const uint8_t* data, std::size_t size, std::size_t& offset);

} // namespace Pycp::BC

#endif // PYCP_BYTECODE_HPP
