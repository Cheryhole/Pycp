// =============================================================
// Pycp 字节码查看（dump）实现
//
// 将字节码 Module 的内存表示格式化为可读文本。所有排版细节封装
// 在本文件内，对外仅暴露 Pycp::BC::DumpModule（见 PycpBytecodeDump.hpp）。
// 排版逻辑从原 PycpMain.cpp 迁移而来，输出格式保持不变。
// =============================================================

#include "PycpBytecodeDump.hpp"

namespace Pycp::BC {

namespace {

// 操作码 -> 可读名称
const char* op_name(Op op) {
	switch (op) {
		case Op::HALT:          return "HALT";
		case Op::LOAD_CONST:    return "LOAD_CONST";
		case Op::LOAD_VAR:      return "LOAD_VAR";
		case Op::STORE_VAR:     return "STORE_VAR";
		case Op::LOAD_NONE:     return "LOAD_NONE";
		case Op::LOAD_TRUE:     return "LOAD_TRUE";
		case Op::LOAD_FALSE:    return "LOAD_FALSE";
		case Op::POP_TOP:       return "POP_TOP";
		case Op::DUP_TOP:       return "DUP_TOP";
		case Op::BINARY_ADD:    return "BINARY_ADD";
		case Op::BINARY_SUB:    return "BINARY_SUB";
		case Op::BINARY_MUL:    return "BINARY_MUL";
		case Op::BINARY_DIV:    return "BINARY_DIV";
		case Op::BINARY_POW:    return "BINARY_POW";
		case Op::UNARY_NEG:     return "UNARY_NEG";
		case Op::COMPARE_OP:    return "COMPARE_OP";
		case Op::JUMP:          return "JUMP";
		case Op::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
		case Op::JUMP_IF_TRUE:  return "JUMP_IF_TRUE";
		case Op::BREAK:         return "BREAK";
		case Op::CHECK_INT:     return "CHECK_INT";
		case Op::CHECK_RANGE_DIRECTION: return "CHECK_RANGE_DIRECTION";
		case Op::GET_ITER:       return "GET_ITER";
		case Op::FOR_ITER:       return "FOR_ITER";
		case Op::MAKE_FUNCTION: return "MAKE_FUNCTION";
		case Op::CALL:          return "CALL";
		case Op::RETURN:        return "RETURN";
		case Op::RETURN_NONE:   return "RETURN_NONE";
		case Op::LOAD_MODULE:   return "LOAD_MODULE";
		case Op::GET_ATTR:      return "GET_ATTR";
		case Op::MAKE_CLASS:    return "MAKE_CLASS";
		case Op::LOAD_ATTR:     return "LOAD_ATTR";
		case Op::STORE_ATTR:    return "STORE_ATTR";
		case Op::BUILD_MAP:     return "BUILD_MAP";
		default:                return "UNKNOWN";
	}
}

// 比较子操作码 -> 可读名称
const char* cmp_op_name(uint8_t op) {
	switch (static_cast<CompareOp>(op)) {
		case CompareOp::LT: return "LT";
		case CompareOp::LE: return "LE";
		case CompareOp::EQ: return "EQ";
		case CompareOp::NE: return "NE";
		case CompareOp::GT: return "GT";
		case CompareOp::GE: return "GE";
		default:            return "?";
	}
}

// 常量池条目 -> 可读文本
std::string const_text(const Constant& c) {
	switch (c.kind) {
		case ConstKind::INTEGER: return std::to_string(c.int_value);
		case ConstKind::STRING:  return "\"" + c.str_value + "\"";
		case ConstKind::NONE:    return "None";
		default:                 return "<unknown>";
	}
}

// 打印单条指令（带行号与操作数解释）
void dump_instruction(std::ostream& os, const Instruction& ins,
                      int lineno, const Module& module) {
	os << "  " << (lineno >= 0 ? std::to_string(lineno) : "?") << "  "
	   << op_name(ins.op);

	// 带操作数注释，便于阅读
	if (ins.op == Op::LOAD_CONST) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.const_pool.size()) {
			os << " " << ins.operand << "  # "
			   << const_text(module.const_pool[ins.operand]);
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.op == Op::LOAD_VAR ||
	           ins.op == Op::STORE_VAR) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.symtab.size()) {
			os << " " << ins.operand << "  # " << module.symtab[ins.operand];
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.op == Op::COMPARE_OP) {
		os << " " << ins.operand << "  # " << cmp_op_name(static_cast<uint8_t>(ins.operand));
	} else if (ins.op == Op::MAKE_FUNCTION) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.code_objects.size()) {
			os << " " << ins.operand << "  # " << module.code_objects[ins.operand].name;
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.op == Op::LOAD_MODULE) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.imports.size()) {
			os << " " << ins.operand << "  # import " << module.imports[ins.operand];
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.op == Op::GET_ATTR) {
		if (ins.operand >= 0 &&
		    static_cast<size_t>(ins.operand) < module.symtab.size()) {
			os << " " << ins.operand << "  # ." << module.symtab[ins.operand];
		} else {
			os << " " << ins.operand;
		}
	} else if (ins.operand != 0) {
		os << " " << ins.operand;
	}
	os << "\n";
}

} // anonymous namespace

// 查看字节码内容：常量池 / 符号表 / 代码对象（方法签名、字段、指令与行号）
void DumpModule(const Module& module, std::ostream& os) {
	os << "==============================================\n"
	   << "Pycp Bytecode Dump\n"
	   << "==============================================\n";

	// ---- 文件头 ----
	os << "\n[Header]\n"
	   << "  format version : " << FORMAT_VERSION_MAJOR << "."
	   << FORMAT_VERSION_MINOR << "\n"
	   << "  source path    : " << module.source_path << "\n";

	// ---- 常量池 ----
	os << "\n[Constant Pool] (" << module.const_pool.size() << " entries)\n";
	for (size_t i = 0; i < module.const_pool.size(); ++i) {
		os << "  " << i << ": " << const_text(module.const_pool[i]) << "\n";
	}

	// ---- 符号表 ----
	os << "\n[Symbol Table] (" << module.symtab.size() << " entries)\n";
	for (size_t i = 0; i < module.symtab.size(); ++i) {
		os << "  " << i << ": " << module.symtab[i] << "\n";
	}

	// ---- 代码对象（含方法签名 / 字段 / 指令与行号）----
	os << "\n[Code Objects] (" << module.code_objects.size() << ")\n";
	for (size_t ci = 0; ci < module.code_objects.size(); ++ci) {
		const CodeObject& co = module.code_objects[ci];
		os << "\n--- CodeObject[" << ci << "] ---\n"
		   << "  name    : " << co.name << "\n"
		   << "  nparams : " << co.nparams << "\n"
		   << "  nlocals : " << co.nlocals << "\n";

		// 局部变量名表（字段信息）
		if (!co.names.empty()) {
			os << "  locals  : ";
			for (size_t k = 0; k < co.names.size(); ++k) {
				if (k) os << ", ";
				os << co.names[k];
			}
			os << "\n";
		}

		// 常量引用
		if (!co.const_refs.empty()) {
			os << "  const_refs:";
			for (size_t k : co.const_refs) os << " " << k;
			os << "\n";
		}

		// 指令流（含行号）
		os << "  code (" << co.code.size() << " instrs):\n";
		for (size_t k = 0; k < co.code.size(); ++k) {
			int lineno = (k < co.linenos.size()) ? co.linenos[k] : -1;
			dump_instruction(os, co.code[k], lineno, module);
		}
	}
	os << "\n==============================================\n";
}

} // namespace Pycp::BC
