#include "bytecode/PycpBytecodeObject.hpp"

#include "bytecode/PycpBytecodeDump.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpFloat.hpp"
#include "object/PycpDecimal.hpp"
#include "object/PycpString.hpp"
#include "object/PycpList.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpException.hpp"

#include <sstream>
#include <utility>
#include <vector>

namespace Pycp::BC {

namespace {

// ---- 小工具（均返回 Owned）----
Object* Str(const std::string& s) { return String::FromCString(s.c_str()); }

Object* StrList(const std::vector<std::string>& v) {
	List* l = List::New();
	for (const std::string& s : v) {
		Object* o = Str(s);
		l->append(o);
		Decref(o);
	}
	return l;
}

template <typename T>
Object* IntList(const std::vector<T>& v) {
	List* l = List::New();
	for (T x : v) {
		Object* o = Integer::FromLong(static_cast<long long>(x));
		l->append(o);
		Decref(o);
	}
	return l;
}

Object* IntArrayList(const std::vector<std::vector<uint32_t>>& v) {
	List* l = List::New();
	for (const std::vector<uint32_t>& g : v) {
		Object* o = IntList(g);
		l->append(o);
		Decref(o);
	}
	return l;
}

// 常量池条目 -> pycp 原始对象（Owned；None 为常驻单例）。
Object* ConstToObject(const Constant& c) {
	switch (c.kind) {
		case ConstKind::INTEGER: return Integer::FromLong(c.int_value);
		case ConstKind::FLOAT:   return New<Float>(c.float_value);
		case ConstKind::STRING:  return Str(c.str_value);
		case ConstKind::DECIMAL: return New<Decimal>(Str(c.str_value));
		case ConstKind::NONE:    return None::instance;
	}
	return None::instance;
}

Object* ConstList(const std::vector<Constant>& v) {
	List* l = List::New();
	for (const Constant& c : v) {
		Object* o = ConstToObject(c);
		l->append(o);
		Decref(o);
	}
	return l;
}

// 形参种类编码 -> 名称（与 CodeObject::param_kinds 对齐）。
const char* ParamKindCodeName(uint8_t k) {
	switch (static_cast<ParamKindCode>(k)) {
		case ParamKindCode::Required:     return "Required";
		case ParamKindCode::Optional:     return "Optional";
		case ParamKindCode::Rest:         return "Rest";
		case ParamKindCode::RestKeywords: return "RestKeywords";
		case ParamKindCode::BareStar:     return "BareStar";
	}
	return "Unknown";
}

} // anonymous namespace

// =============================================================
// OpName
// =============================================================
const char* OpName(Op op) {
	switch (op) {
		case Op::HALT: return "HALT";
		case Op::LOAD_CONST: return "LOAD_CONST";
		case Op::LOAD_VAR: return "LOAD_VAR";
		case Op::STORE_VAR: return "STORE_VAR";
		case Op::LOAD_NONE: return "LOAD_NONE";
		case Op::POP_TOP: return "POP_TOP";
		case Op::DUP_TOP: return "DUP_TOP";
		case Op::LOAD_TRUE: return "LOAD_TRUE";
		case Op::LOAD_FALSE: return "LOAD_FALSE";
		case Op::BINARY_ADD: return "BINARY_ADD";
		case Op::BINARY_SUB: return "BINARY_SUB";
		case Op::BINARY_MUL: return "BINARY_MUL";
		case Op::BINARY_DIV: return "BINARY_DIV";
		case Op::BINARY_POW: return "BINARY_POW";
		case Op::UNARY_NEG: return "UNARY_NEG";
		case Op::COMPARE_OP: return "COMPARE_OP";
		case Op::JUMP: return "JUMP";
		case Op::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
		case Op::JUMP_IF_TRUE: return "JUMP_IF_TRUE";
		case Op::BREAK: return "BREAK";
		case Op::CHECK_INT: return "CHECK_INT";
		case Op::CHECK_RANGE_DIRECTION: return "CHECK_RANGE_DIRECTION";
		case Op::GET_ITER: return "GET_ITER";
		case Op::FOR_ITER: return "FOR_ITER";
		case Op::MAKE_FUNCTION: return "MAKE_FUNCTION";
		case Op::CALL: return "CALL";
		case Op::RETURN: return "RETURN";
		case Op::RETURN_NONE: return "RETURN_NONE";
		case Op::LOAD_MODULE: return "LOAD_MODULE";
		case Op::GET_ATTR: return "GET_ATTR";
		case Op::MAKE_CLASS: return "MAKE_CLASS";
		case Op::LOAD_ATTR: return "LOAD_ATTR";
		case Op::STORE_ATTR: return "STORE_ATTR";
		case Op::BUILD_LIST: return "BUILD_LIST";
		case Op::GET_ITEM: return "GET_ITEM";
		case Op::SET_ITEM: return "SET_ITEM";
		case Op::BUILD_MAP: return "BUILD_MAP";
		case Op::MARK_BINDING: return "MARK_BINDING";
		case Op::CALL_KW: return "CALL_KW";
	}
	return "UNKNOWN";
}

// =============================================================
// ModuleRef
// =============================================================
ModuleRef::ModuleRef(std::shared_ptr<Module> module)
	: Object("BytecodeModule"), module_(std::move(module)) {}

Object* ModuleRef::__get_attribute__(const std::string& name) {
	if (members_.find(name) == members_.end()) {
		if (Object* v = BuildField(name)) members_[name] = v; // 接管所有权
	}
	return Object::__get_attribute__(name);
}

Object* ModuleRef::BuildField(const std::string& name) {
	if (!module_) return nullptr;
	if (name == "source_path") return Str(module_->source_path);
	if (name == "constants") return ConstList(module_->const_pool);
	if (name == "symbols") return StrList(module_->symtab);
	if (name == "imports") return StrList(module_->imports);
	if (name == "import_linenos") return IntList(module_->import_linenos);
	if (name == "repl_eval") {
		return module_->repl_eval ? static_cast<Object*>(Boolean::True())
		                          : static_cast<Object*>(Boolean::False());
	}
	if (name == "code_objects") {
		List* l = List::New();
		for (std::size_t i = 0; i < module_->code_objects.size(); ++i) {
			Object* o = New<CodeRef>(module_, i);
			l->append(o);
			Decref(o);
		}
		return l;
	}
	if (name == "classes") {
		List* l = List::New();
		for (std::size_t i = 0; i < module_->classes.size(); ++i) {
			Object* o = New<ClassRef>(module_, i);
			l->append(o);
			Decref(o);
		}
		return l;
	}
	return nullptr;
}

Object* ModuleRef::__inspect__() {
	static const std::vector<std::string> names = {
		"source_path", "constants", "symbols", "imports", "import_linenos",
		"repl_eval", "code_objects", "classes"};
	return StrList(names);
}

Object* ModuleRef::__string__() {
	if (!module_) return Str("");
	std::ostringstream oss;
	DumpModule(*module_, oss);
	return Str(oss.str());
}

Object* ModuleRef::__raw_string__() { return __string__(); }

// =============================================================
// CodeRef
// =============================================================
CodeRef::CodeRef(std::shared_ptr<Module> module, std::size_t index)
	: Object("BytecodeCode"), module_(std::move(module)), index_(index) {}

Object* CodeRef::__get_attribute__(const std::string& name) {
	if (members_.find(name) == members_.end()) {
		if (Object* v = BuildField(name)) members_[name] = v;
	}
	return Object::__get_attribute__(name);
}

Object* CodeRef::BuildField(const std::string& name) {
	if (!module_ || index_ >= module_->code_objects.size()) return nullptr;
	const CodeObject& co = module_->code_objects[index_];
	if (name == "name") return Str(co.name);
	if (name == "nparams") return Integer::FromLong(co.nparams);
	if (name == "default_count") return Integer::FromLong(co.default_count);
	if (name == "nlocals") return Integer::FromLong(co.nlocals);
	if (name == "param_kinds") return IntList(co.param_kinds);
	if (name == "param_kind_names") {
		std::vector<std::string> names;
		names.reserve(co.param_kinds.size());
		for (uint8_t k : co.param_kinds) names.push_back(ParamKindCodeName(k));
		return StrList(names);
	}
	if (name == "consts") return ConstList(co.consts);
	if (name == "names") return StrList(co.names);
	if (name == "const_refs") return IntList(co.const_refs);
	if (name == "name_refs") return IntList(co.name_refs);
	if (name == "free_names") return StrList(co.free_names);
	if (name == "linenos") return IntList(co.linenos);
	if (name == "instructions") {
		List* l = List::New();
		for (std::size_t i = 0; i < co.code.size(); ++i) {
			const int line = (i < co.linenos.size()) ? co.linenos[i] : -1;
			Object* o = New<InstrRef>(co.code[i].op, co.code[i].operand, line);
			l->append(o);
			Decref(o);
		}
		return l;
	}
	return nullptr;
}

Object* CodeRef::__inspect__() {
	static const std::vector<std::string> names = {
		"name", "nparams", "default_count", "nlocals", "param_kinds",
		"param_kind_names", "consts", "names", "const_refs", "name_refs",
		"free_names", "linenos", "instructions"};
	return StrList(names);
}

Object* CodeRef::__string__() {
	if (!module_ || index_ >= module_->code_objects.size()) return Str("");
	return Str(module_->code_objects[index_].name);
}

Object* CodeRef::__raw_string__() { return __string__(); }

// =============================================================
// ClassRef
// =============================================================
ClassRef::ClassRef(std::shared_ptr<Module> module, std::size_t index)
	: Object("BytecodeClass"), module_(std::move(module)), index_(index) {}

Object* ClassRef::__get_attribute__(const std::string& name) {
	if (members_.find(name) == members_.end()) {
		if (Object* v = BuildField(name)) members_[name] = v;
	}
	return Object::__get_attribute__(name);
}

Object* ClassRef::BuildField(const std::string& name) {
	if (!module_ || index_ >= module_->classes.size()) return nullptr;
	const ClassDef& cd = module_->classes[index_];
	if (name == "name") return Str(cd.name);
	if (name == "parent_name") return Str(cd.parent_name);
	if (name == "member_names") return StrList(cd.member_names);
	if (name == "member_decorators") return IntArrayList(cd.member_decorators);
	if (name == "method_names") {
		std::vector<std::string> v;
		v.reserve(cd.methods.size());
		for (const auto& p : cd.methods) v.push_back(p.first);
		return StrList(v);
	}
	if (name == "method_code_indices") {
		std::vector<uint32_t> v;
		v.reserve(cd.methods.size());
		for (const auto& p : cd.methods) v.push_back(p.second);
		return IntList(v);
	}
	if (name == "method_decorators") return IntArrayList(cd.method_decorators);
	if (name == "decorator_count") return Integer::FromLong(cd.decorator_count);
	return nullptr;
}

Object* ClassRef::__inspect__() {
	static const std::vector<std::string> names = {
		"name", "parent_name", "member_names", "member_decorators",
		"method_names", "method_code_indices", "method_decorators",
		"decorator_count"};
	return StrList(names);
}

Object* ClassRef::__string__() {
	if (!module_ || index_ >= module_->classes.size()) return Str("");
	return Str(module_->classes[index_].name);
}

Object* ClassRef::__raw_string__() { return __string__(); }

// =============================================================
// InstrRef
// =============================================================
InstrRef::InstrRef(Op op, int32_t operand, int line)
	: Object("BytecodeInstr"), op_(op), operand_(operand), line_(line) {}

Object* InstrRef::__get_attribute__(const std::string& name) {
	if (members_.find(name) == members_.end()) {
		if (Object* v = BuildField(name)) members_[name] = v;
	}
	return Object::__get_attribute__(name);
}

Object* InstrRef::BuildField(const std::string& name) {
	if (name == "op") return Str(OpName(op_));
	if (name == "opcode") return Integer::FromLong(static_cast<long long>(op_));
	if (name == "operand") return Integer::FromLong(operand_);
	if (name == "line") return Integer::FromLong(line_);
	return nullptr;
}

Object* InstrRef::__inspect__() {
	static const std::vector<std::string> names = {"op", "opcode", "operand", "line"};
	return StrList(names);
}

Object* InstrRef::__string__() {
	std::ostringstream oss;
	oss << OpName(op_) << " " << operand_ << " (line " << line_ << ")";
	return Str(oss.str());
}

Object* InstrRef::__raw_string__() { return __string__(); }

// =============================================================
// 包装 / 判定 / 解包
// =============================================================
ModuleRef* WrapModule(std::shared_ptr<Module> module) {
	if (!module) return nullptr;
	return New<ModuleRef>(std::move(module));
}

bool IsModuleRef(const Object* obj) {
	return obj != nullptr && dynamic_cast<const ModuleRef*>(obj) != nullptr;
}

std::shared_ptr<Module> UnwrapModule(const Object* obj) {
	const ModuleRef* ref = dynamic_cast<const ModuleRef*>(obj);
	return ref != nullptr ? ref->module() : nullptr;
}

} // namespace Pycp::BC
