#include "PycpBytecode.hpp"
#include "PycpException.hpp"
#include "PycpABI.hpp"
#include "PycpNone.hpp"
#include "PycpGC.hpp"
#include "PycpConfig.hpp"

#include <cstring>
#include <stdexcept>

namespace Pycp::BC {

// =============================================================
// 文件头常量定义（值统一来自 Pycp::config 常量，见 PycpConfig.hpp）
// =============================================================

// 魔数 "CYCP" 从 config 字符串常量逐字节派生（保持 extern 数组定义）。
const uint8_t MAGIC[4] = {
	static_cast<uint8_t>(Pycp::BYTECODE_MAGIC[0]),
	static_cast<uint8_t>(Pycp::BYTECODE_MAGIC[1]),
	static_cast<uint8_t>(Pycp::BYTECODE_MAGIC[2]),
	static_cast<uint8_t>(Pycp::BYTECODE_MAGIC[3]),
};
const uint16_t FORMAT_VERSION_MAJOR = Pycp::BYTECODE_VERSION_MAJOR;
const uint16_t FORMAT_VERSION_MINOR = Pycp::BYTECODE_VERSION_MINOR;

// =============================================================
// 小端读写辅助
// =============================================================

namespace {

inline void put_u8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

inline void put_u16(std::vector<uint8_t>& out, uint16_t v) {
	out.push_back(static_cast<uint8_t>(v & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void put_u32(std::vector<uint8_t>& out, uint32_t v) {
	out.push_back(static_cast<uint8_t>(v & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline uint8_t get_u8(const uint8_t* data, std::size_t size, std::size_t& off) {
	if (off + 1 > size) throw BytecodeError("unexpected end of data.");
	return data[off++];
}

inline uint16_t get_u16(const uint8_t* data, std::size_t size, std::size_t& off) {
	if (off + 2 > size) throw BytecodeError("unexpected end of data.");
	uint16_t v = static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
	off += 2;
	return v;
}

inline uint32_t get_u32(const uint8_t* data, std::size_t size, std::size_t& off) {
	if (off + 4 > size) throw BytecodeError("unexpected end of data.");
	uint32_t v = static_cast<uint32_t>(data[off]) |
	             (static_cast<uint32_t>(data[off + 1]) << 8) |
	             (static_cast<uint32_t>(data[off + 2]) << 16) |
	             (static_cast<uint32_t>(data[off + 3]) << 24);
	off += 4;
	return v;
}

inline void put_bytes(std::vector<uint8_t>& out, const uint8_t* p, std::size_t n) {
	out.insert(out.end(), p, p + n);
}

} // anonymous namespace

// =============================================================
// LEB128 实现
// =============================================================

void WriteULEB128(std::vector<uint8_t>& out, uint64_t value) {
	do {
		uint8_t byte = static_cast<uint8_t>(value & 0x7F);
		value >>= 7;
		if (value != 0) byte |= 0x80;
		out.push_back(byte);
	} while (value != 0);
}

uint64_t ReadULEB128(const uint8_t* data, std::size_t size, std::size_t& offset) {
	uint64_t result = 0;
	unsigned shift = 0;
	for (;;) {
		if (offset >= size) throw BytecodeError("truncated LEB128.");
		uint8_t byte = data[offset++];
		if (shift >= 64) throw BytecodeError("LEB128 overflow.");
		result |= static_cast<uint64_t>(byte & 0x7F) << shift;
		if ((byte & 0x80) == 0) break;
		shift += 7;
	}
	return result;
}

void WriteSLEB128(std::vector<uint8_t>& out, int64_t value) {
	bool more = true;
	while (more) {
		uint8_t byte = static_cast<uint8_t>(value & 0x7F);
		value >>= 7; // 算术右移（有符号）
		bool sign = (byte & 0x40) != 0;
		more = !((value == 0 && !sign) || (value == -1 && sign));
		if (more) byte |= 0x80;
		out.push_back(byte);
	}
}

int64_t ReadSLEB128(const uint8_t* data, std::size_t size, std::size_t& offset) {
	int64_t result = 0;
	unsigned shift = 0;
	uint8_t byte;
	do {
		if (offset >= size) throw BytecodeError("truncated SLEB128.");
		byte = data[offset++];
		if (shift >= 64) throw BytecodeError("SLEB128 overflow.");
		result |= static_cast<int64_t>(byte & 0x7F) << shift;
		shift += 7;
	} while ((byte & 0x80) != 0);
	// 符号扩展
	if (shift < 64 && (byte & 0x40) != 0) {
		result |= -(static_cast<int64_t>(1) << shift);
	}
	return result;
}

// =============================================================
// 序列化
// =============================================================

std::vector<uint8_t> Serialize(const Module& module) {
	std::vector<uint8_t> out;

	// ---- 文件头 ----
	put_bytes(out, MAGIC, 4);
	put_u16(out, FORMAT_VERSION_MAJOR);
	put_u16(out, FORMAT_VERSION_MINOR);
	put_u32(out, 0); // flags 预留

	// ---- 源文件路径段 ----
	{
		std::vector<uint8_t> seg;
		WriteULEB128(seg, module.source_path.size());
		put_bytes(seg, reinterpret_cast<const uint8_t*>(module.source_path.data()),
		          module.source_path.size());
		put_u32(out, static_cast<uint32_t>(seg.size()));
		put_bytes(out, seg.data(), seg.size());
	}

	// ---- 常量池段 ----
	{
		std::vector<uint8_t> seg;
		WriteULEB128(seg, module.const_pool.size());
		for (const auto& c : module.const_pool) {
			seg.push_back(static_cast<uint8_t>(c.kind));
			if (c.kind == ConstKind::INTEGER) {
				WriteSLEB128(seg, c.int_value);
			} else if (c.kind == ConstKind::STRING) {
				WriteULEB128(seg, c.str_value.size());
				put_bytes(seg, reinterpret_cast<const uint8_t*>(c.str_value.data()),
				          c.str_value.size());
			}
			// NONE 无附加数据
		}
		put_u32(out, static_cast<uint32_t>(seg.size()));
		put_bytes(out, seg.data(), seg.size());
	}

	// ---- 符号表段 ----
	{
		std::vector<uint8_t> seg;
		WriteULEB128(seg, module.symtab.size());
		for (const auto& name : module.symtab) {
			WriteULEB128(seg, name.size());
			put_bytes(seg, reinterpret_cast<const uint8_t*>(name.data()), name.size());
		}
		put_u32(out, static_cast<uint32_t>(seg.size()));
		put_bytes(out, seg.data(), seg.size());
	}

	// ---- 导入表段（imports）----
	{
		std::vector<uint8_t> seg;
		WriteULEB128(seg, module.imports.size());
		for (std::size_t i = 0; i < module.imports.size(); ++i) {
			const auto& name = module.imports[i];
			WriteULEB128(seg, name.size());
			put_bytes(seg, reinterpret_cast<const uint8_t*>(name.data()), name.size());
			// 行号（缺失时以 -1 补齐）
			int lineno = (i < module.import_linenos.size()) ? module.import_linenos[i] : -1;
			WriteSLEB128(seg, lineno);
		}
		put_u32(out, static_cast<uint32_t>(seg.size()));
		put_bytes(out, seg.data(), seg.size());
	}

	// ---- 代码对象表段 ----
	{
		std::vector<uint8_t> seg;
		WriteULEB128(seg, module.code_objects.size());
		for (const auto& co : module.code_objects) {
			// name（符号索引）
			size_t name_idx = 0;
			for (size_t i = 0; i < module.symtab.size(); ++i) {
				if (module.symtab[i] == co.name) { name_idx = i; break; }
			}
			WriteULEB128(seg, name_idx);
			WriteULEB128(seg, co.nparams);
			WriteULEB128(seg, co.nlocals);

			// 局部变量名表（符号索引序列）
			WriteULEB128(seg, co.names.size());
			for (const auto& n : co.names) {
				size_t nidx = 0;
				for (size_t i = 0; i < module.symtab.size(); ++i) {
					if (module.symtab[i] == n) { nidx = i; break; }
				}
				WriteULEB128(seg, nidx);
			}

			// 指令流
			WriteULEB128(seg, co.code.size());
			for (const auto& ins : co.code) {
				seg.push_back(static_cast<uint8_t>(ins.op));
				WriteSLEB128(seg, ins.operand);
			}

			// 行号表（与指令流逐条对齐，缺失时以 -1 补齐）
			WriteULEB128(seg, co.linenos.size());
			for (int ln : co.linenos) {
				WriteSLEB128(seg, ln);
			}
		}
		put_u32(out, static_cast<uint32_t>(seg.size()));
		put_bytes(out, seg.data(), seg.size());
	}

	// ---- 类定义表段 ----
	{
		std::vector<uint8_t> seg;
		WriteULEB128(seg, module.classes.size());
		for (const auto& cd : module.classes) {
			// 类名（符号索引）
			size_t name_idx = 0;
			for (size_t i = 0; i < module.symtab.size(); ++i) {
				if (module.symtab[i] == cd.name) { name_idx = i; break; }
			}
			WriteULEB128(seg, name_idx);

			// 父类名（符号索引，空串则索引为 ULEB128 0 特殊标记）
			if (cd.parent_name.empty()) {
				WriteULEB128(seg, 0);
			} else {
				size_t pidx = 0;
				bool found = false;
				for (size_t i = 0; i < module.symtab.size(); ++i) {
					if (module.symtab[i] == cd.parent_name) { pidx = i + 1; found = true; break; }
				}
				if (!found) pidx = 0; // 父类名不在符号表（防御）
				WriteULEB128(seg, pidx);
			}

			// 成员变量名表（符号索引序列）
			WriteULEB128(seg, cd.member_names.size());
			for (const auto& mn : cd.member_names) {
				size_t nidx = 0;
				for (size_t i = 0; i < module.symtab.size(); ++i) {
					if (module.symtab[i] == mn) { nidx = i; break; }
				}
				WriteULEB128(seg, nidx);
			}

			// 成员装饰器栈槽序号（与 member_names 对齐；UINT32_MAX 表示无装饰器）
			WriteULEB128(seg, cd.member_decorators.size());
			for (uint32_t d : cd.member_decorators) {
				WriteULEB128(seg, d);
			}

			// 方法表：方法名（符号索引）+ 方法代码对象索引
			WriteULEB128(seg, cd.methods.size());
			for (const auto& m : cd.methods) {
				size_t nidx = 0;
				for (size_t i = 0; i < module.symtab.size(); ++i) {
					if (module.symtab[i] == m.first) { nidx = i; break; }
				}
				WriteULEB128(seg, nidx);
				WriteULEB128(seg, m.second);
			}

			// 方法装饰器栈槽序号（与 methods 对齐；UINT32_MAX 表示无装饰器）
			WriteULEB128(seg, cd.method_decorators.size());
			for (uint32_t d : cd.method_decorators) {
				WriteULEB128(seg, d);
			}

			// 装饰器对象总数
			WriteULEB128(seg, cd.decorator_count);
		}
		put_u32(out, static_cast<uint32_t>(seg.size()));
		put_bytes(out, seg.data(), seg.size());
	}

	return out;
}

// =============================================================
// 反序列化
// =============================================================

Module Deserialize(const uint8_t* data, std::size_t size) {
	if (data == nullptr || size < 12) {
		throw BytecodeError("file too small.");
	}

	std::size_t off = 0;

	// ---- 文件头校验 ----
	for (int i = 0; i < 4; ++i) {
		if (get_u8(data, size, off) != MAGIC[i]) {
			throw BytecodeError("bad magic (not a .cpycp file).");
		}
	}
	uint16_t major = get_u16(data, size, off);
	uint16_t minor = get_u16(data, size, off);
	(void)minor;
	if (major != FORMAT_VERSION_MAJOR) {
		throw BytecodeError("unsupported version " +
		                std::to_string(major) + "." + std::to_string(minor) +
		                " (expected " + std::to_string(FORMAT_VERSION_MAJOR) + ").");
	}
	(void)get_u32(data, size, off); // flags 预留

	Module module;

	// ---- 源文件路径段 ----
	{
		uint32_t seg_len = get_u32(data, size, off);
		std::size_t seg_end = off + seg_len;
		if (seg_end > size) throw BytecodeError("source path truncated.");
		uint64_t len = ReadULEB128(data, size, off);
		if (off + len > size) throw BytecodeError("source path truncated.");
		module.source_path.assign(reinterpret_cast<const char*>(data + off), len);
		off = seg_end;
	}

	// ---- 常量池段 ----
	{
		uint32_t seg_len = get_u32(data, size, off);
		std::size_t seg_end = off + seg_len;
		if (seg_end > size) throw BytecodeError("constant pool truncated.");
		uint64_t count = ReadULEB128(data, size, off);
		module.const_pool.reserve(count);
		for (uint64_t i = 0; i < count; ++i) {
			Constant c;
			c.kind = static_cast<ConstKind>(get_u8(data, size, off));
			if (c.kind == ConstKind::INTEGER) {
				c.int_value = ReadSLEB128(data, size, off);
			} else if (c.kind == ConstKind::STRING) {
				uint64_t len = ReadULEB128(data, size, off);
				if (off + len > size) throw BytecodeError("string constant truncated.");
				c.str_value.assign(reinterpret_cast<const char*>(data + off), len);
				off += len;
			}
			// NONE 无附加
			module.const_pool.push_back(std::move(c));
		}
		off = seg_end;
	}

	// ---- 符号表段 ----
	{
		uint32_t seg_len = get_u32(data, size, off);
		std::size_t seg_end = off + seg_len;
		if (seg_end > size) throw BytecodeError("symbol table truncated.");
		uint64_t count = ReadULEB128(data, size, off);
		module.symtab.reserve(count);
		for (uint64_t i = 0; i < count; ++i) {
			uint64_t len = ReadULEB128(data, size, off);
			if (off + len > size) throw BytecodeError("symbol truncated.");
			module.symtab.emplace_back(reinterpret_cast<const char*>(data + off), len);
			off += len;
		}
		off = seg_end;
	}

	// ---- 导入表段（imports）----
	{
		uint32_t seg_len = get_u32(data, size, off);
		std::size_t seg_end = off + seg_len;
		if (seg_end > size) throw BytecodeError("imports truncated.");
		uint64_t count = ReadULEB128(data, size, off);
		module.imports.reserve(count);
		module.import_linenos.reserve(count);
		for (uint64_t i = 0; i < count; ++i) {
			uint64_t len = ReadULEB128(data, size, off);
			if (off + len > size) throw BytecodeError("import name truncated.");
			module.imports.emplace_back(reinterpret_cast<const char*>(data + off), len);
			off += len;
			module.import_linenos.push_back(static_cast<int>(ReadSLEB128(data, size, off)));
		}
		off = seg_end;
	}

	// ---- 代码对象表段 ----
	{
		uint32_t seg_len = get_u32(data, size, off);
		std::size_t seg_end = off + seg_len;
		if (seg_end > size) throw BytecodeError("code objects truncated.");
		uint64_t count = ReadULEB128(data, size, off);
		module.code_objects.reserve(count);
		for (uint64_t i = 0; i < count; ++i) {
			CodeObject co;
			uint64_t name_idx = ReadULEB128(data, size, off);
			if (name_idx >= module.symtab.size())
				throw BytecodeError("invalid symbol index.");
			co.name = module.symtab[name_idx];
			co.nparams = static_cast<uint16_t>(ReadULEB128(data, size, off));
			co.nlocals = static_cast<uint16_t>(ReadULEB128(data, size, off));

			// 局部变量名表（符号索引序列）
			uint64_t ncount = ReadULEB128(data, size, off);
			co.names.reserve(ncount);
			for (uint64_t k = 0; k < ncount; ++k) {
				uint64_t nidx = ReadULEB128(data, size, off);
				if (nidx >= module.symtab.size())
					throw BytecodeError("invalid local name index.");
				co.names.push_back(module.symtab[nidx]);
			}

			uint64_t icount = ReadULEB128(data, size, off);
			co.code.reserve(icount);
			for (uint64_t k = 0; k < icount; ++k) {
				Instruction ins;
				ins.op = static_cast<Op>(get_u8(data, size, off));
				ins.operand = static_cast<int32_t>(ReadSLEB128(data, size, off));
				co.code.push_back(ins);
			}

			// 行号表
			uint64_t lcount = ReadULEB128(data, size, off);
			co.linenos.reserve(lcount);
			for (uint64_t k = 0; k < lcount; ++k) {
				co.linenos.push_back(static_cast<int>(ReadSLEB128(data, size, off)));
			}
			module.code_objects.push_back(std::move(co));
		}
		off = seg_end;
	}

	// ---- 类定义表段 ----
	{
		uint32_t seg_len = get_u32(data, size, off);
		std::size_t seg_end = off + seg_len;
		if (seg_end > size) throw BytecodeError("classes truncated.");
		uint64_t count = ReadULEB128(data, size, off);
		module.classes.reserve(count);
		for (uint64_t i = 0; i < count; ++i) {
			ClassDef cd;
			uint64_t name_idx = ReadULEB128(data, size, off);
			if (name_idx >= module.symtab.size())
				throw BytecodeError("invalid class name index.");
			cd.name = module.symtab[name_idx];

			// 父类名（符号索引，0 表示无父类）
			uint64_t pidx = ReadULEB128(data, size, off);
			if (pidx == 0) {
				cd.parent_name.clear();
			} else {
				if (pidx - 1 >= module.symtab.size())
					throw BytecodeError("invalid parent name index.");
				cd.parent_name = module.symtab[pidx - 1];
			}

			// 成员变量名表（符号索引序列）
			uint64_t mcount = ReadULEB128(data, size, off);
			cd.member_names.reserve(mcount);
			for (uint64_t k = 0; k < mcount; ++k) {
				uint64_t nidx = ReadULEB128(data, size, off);
				if (nidx >= module.symtab.size())
					throw BytecodeError("invalid member name index.");
				cd.member_names.push_back(module.symtab[nidx]);
			}

			// 成员装饰器栈槽序号（与 member_names 对齐；UINT32_MAX = 无装饰器）
			uint64_t mvcount = ReadULEB128(data, size, off);
			cd.member_decorators.reserve(mvcount);
			for (uint64_t k = 0; k < mvcount; ++k) {
				cd.member_decorators.push_back(
					static_cast<uint32_t>(ReadULEB128(data, size, off)));
			}

			// 方法表：方法名（符号索引）+ 方法代码对象索引
			uint64_t mtd_count = ReadULEB128(data, size, off);
			cd.methods.reserve(mtd_count);
			for (uint64_t k = 0; k < mtd_count; ++k) {
				uint64_t nidx = ReadULEB128(data, size, off);
				if (nidx >= module.symtab.size())
					throw BytecodeError("invalid method name index.");
				uint64_t co_idx = ReadULEB128(data, size, off);
				cd.methods.emplace_back(module.symtab[nidx], static_cast<uint32_t>(co_idx));
			}

			// 方法装饰器栈槽序号（与 methods 对齐；UINT32_MAX = 无装饰器）
			uint64_t mtvcount = ReadULEB128(data, size, off);
			cd.method_decorators.reserve(mtvcount);
			for (uint64_t k = 0; k < mtvcount; ++k) {
				cd.method_decorators.push_back(
					static_cast<uint32_t>(ReadULEB128(data, size, off)));
			}

			// 装饰器对象总数
			cd.decorator_count = static_cast<uint32_t>(ReadULEB128(data, size, off));

			module.classes.push_back(std::move(cd));
		}
		off = seg_end;
	}

	// ---- 重建运行时常量对象（GC root 保护）----
	module.runtime_consts.reserve(module.const_pool.size());
	for (const auto& c : module.const_pool) {
		switch (c.kind) {
			case ConstKind::INTEGER:
				module.runtime_consts.push_back(Integer_FromLong(c.int_value));
				break;
			case ConstKind::STRING:
				module.runtime_consts.push_back(String_FromString(c.str_value.c_str()));
				break;
			case ConstKind::NONE:
				module.runtime_consts.push_back(None::instance);
				Incref(None::instance);
				break;
			default:
				throw BytecodeError("unknown constant kind.");
		}
		GC_AddRoot(module.runtime_consts.back());
	}

	return module;
}

} // namespace Pycp::BC
