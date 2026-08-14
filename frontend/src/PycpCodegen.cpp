#include "PycpCodegen.hpp"
#include "PycpException.hpp"

#include <cstdint>
#include <stdexcept>
#include <unordered_set>

namespace Pycp::Codegen {

using namespace Pycp::Ast;
using namespace Pycp::BC;

namespace {

// =============================================================
// 编译上下文
// =============================================================

class Emitter {
public:
	Module module;

	// ---- 符号表去重 ----
	std::size_t intern_name(const std::string& name) {
		for (std::size_t i = 0; i < module.symtab.size(); ++i)
			if (module.symtab[i] == name) return i;
		module.symtab.push_back(name);
		return module.symtab.size() - 1;
	}

	// ---- 常量池去重 ----
	std::size_t intern_int(int64_t v) {
		for (std::size_t i = 0; i < module.const_pool.size(); ++i)
			if (module.const_pool[i].kind == ConstKind::INTEGER &&
			    module.const_pool[i].int_value == v) return i;
		Constant c; c.kind = ConstKind::INTEGER; c.int_value = v;
		module.const_pool.push_back(std::move(c));
		return module.const_pool.size() - 1;
	}
	std::size_t intern_string(const std::string& s) {
		for (std::size_t i = 0; i < module.const_pool.size(); ++i)
			if (module.const_pool[i].kind == ConstKind::STRING &&
			    module.const_pool[i].str_value == s) return i;
		Constant c; c.kind = ConstKind::STRING; c.str_value = s;
		module.const_pool.push_back(std::move(c));
		return module.const_pool.size() - 1;
	}
	std::size_t intern_none() {
		for (std::size_t i = 0; i < module.const_pool.size(); ++i)
			if (module.const_pool[i].kind == ConstKind::NONE) return i;
		Constant c; c.kind = ConstKind::NONE;
		module.const_pool.push_back(std::move(c));
		return module.const_pool.size() - 1;
	}

	// ---- 指令发射 ----
	std::size_t emit(Op op, int32_t operand = 0) {
		CodeObject* co = &module.code_objects[co_stack.back()];
		co->code.push_back({op, operand});
		co->linenos.push_back(current_lineno);
		return co->code.size() - 1;
	}

	// 设置当前编译节点行号（进入节点编译前调用）
	void set_lineno(int line) { current_lineno = line; }
	CodeObject* current() { return &module.code_objects[co_stack.back()]; }
	std::size_t here() { return current()->code.size(); }

	void patch_jump(std::size_t ins_idx, std::size_t target) {
		// VM 语义：JUMP/JUMP_IF_* 执行 `pc = pc + operand - 1`，循环 `++pc`
		// 后新 pc = ins_idx + operand。故 operand = target - ins_idx。
		current()->code[ins_idx].operand = static_cast<int32_t>(
			static_cast<int64_t>(target) - static_cast<int64_t>(ins_idx));
	}

	// 新建代码对象并压栈（返回其全局索引）
	std::size_t push_code_object(const std::string& name) {
		CodeObject co;
		co.name = name;
		module.code_objects.push_back(std::move(co));
		co_stack.push_back(module.code_objects.size() - 1);
		return co_stack.back();
	}
	// 弹出当前代码对象（回到父级）
	void pop_code_object() {
		co_stack.pop_back();
	}

private:
	std::vector<std::size_t> co_stack;
	int current_lineno = -1; // 当前正在编译的 AST 节点行号
};

// =============================================================
// 函数编译的局部变量上下文
// =============================================================

// 编译一个函数体时，需要一个"局部变量名 -> 槽索引"的映射。
// 顶层模块没有局部变量（全部走 globals）。
struct Scope {
	// 局部变量名（含参数），顺序即槽索引
	std::vector<std::string> names;
	// 是否有局部作用域（函数体为 true，顶层为 false）
	bool has_locals = false;

	std::size_t index_of(const std::string& name) const {
		for (std::size_t i = 0; i < names.size(); ++i)
			if (names[i] == name) return i;
		return static_cast<std::size_t>(-1);
	}
	void add(const std::string& name) {
		if (index_of(name) == static_cast<std::size_t>(-1))
			names.push_back(name);
	}
};

// 预扫描函数体，收集所有赋值目标标识符作为局部变量
static void collect_assignment_targets(Program* p, std::unordered_set<std::string>& out) {
	for (Statement* s : *(p->statements)) {
		if (s->get_type() == NodeType::ASSIGNMENT_STATEMENT) {
			AssignmentStatement* as = static_cast<AssignmentStatement*>(s);
			if (as->target->get_type() == NodeType::IDENTIFIER_EXPRESSION) {
				IdentifierExpression* id = static_cast<IdentifierExpression*>(as->target);
				out.insert(*id->name);
			}
		} else if (s->get_type() == NodeType::IF_STATEMENT) {
			IfStatement* is = static_cast<IfStatement*>(s);
			collect_assignment_targets(is->if_branch->body, out);
			for (IfBranch* br : *(is->elif_branches))
				collect_assignment_targets(br->body, out);
			if (is->else_body) collect_assignment_targets(is->else_body, out);
		}
	}
}

// =============================================================
// 前向声明
// =============================================================

static void compile_expr(Emitter& em, Expression* e, Scope& scope);
static void compile_stmt(Emitter& em, Statement* s, Scope& scope);
static void compile_program(Emitter& em, Program* p, Scope& scope);

// =============================================================
// 表达式编译
// =============================================================

static void compile_expr(Emitter& em, Expression* e, Scope& scope) {
	if (e->lineno >= 0) em.set_lineno(e->lineno);
	switch (e->get_type()) {
		case NodeType::INTEGER_LITERAL: {
			IntegerLiteral* lit = static_cast<IntegerLiteral*>(e);
			em.emit(Op::LOAD_CONST, static_cast<int32_t>(em.intern_int(std::stoll(*lit->value))));
			break;
		}
		case NodeType::STRING_LITERAL: {
			StringLiteral* lit = static_cast<StringLiteral*>(e);
			em.emit(Op::LOAD_CONST, static_cast<int32_t>(em.intern_string(*lit->value)));
			break;
		}
		case NodeType::NONE_LITERAL: {
			em.emit(Op::LOAD_CONST, static_cast<int32_t>(em.intern_none()));
			break;
		}
		case NodeType::IDENTIFIER_EXPRESSION: {
			IdentifierExpression* id = static_cast<IdentifierExpression*>(e);
			// 变量引用统一走 LOAD_VAR（VM 按 局部->捕获->全局 查找）
			em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(*id->name)));
			break;
		}
		case NodeType::UNARY_EXPRESSION: {
			UnaryExpression* ue = static_cast<UnaryExpression*>(e);
			compile_expr(em, ue->operand, scope);
			if (ue->op == UnaryOp::UMINUS) em.emit(Op::UNARY_NEG);
			break;
		}
		case NodeType::BINARY_EXPRESSION: {
			BinaryExpression* be = static_cast<BinaryExpression*>(e);
			compile_expr(em, be->left, scope);
			compile_expr(em, be->right, scope);
			switch (be->op) {
				case BinaryOp::PLUS:        em.emit(Op::BINARY_ADD); break;
				case BinaryOp::MINUS:       em.emit(Op::BINARY_SUB); break;
				case BinaryOp::MULTIPLY:    em.emit(Op::BINARY_MUL); break;
				case BinaryOp::DIVIDE:      em.emit(Op::BINARY_DIV); break;
				case BinaryOp::POWER:       em.emit(Op::BINARY_POW); break;
				case BinaryOp::LESS_THAN:    em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::LT)); break;
				case BinaryOp::GREATER_THAN: em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::GT)); break;
				case BinaryOp::LESS_EQUAL:   em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::LE)); break;
				case BinaryOp::GREATER_EQUAL:em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::GE)); break;
				case BinaryOp::EQUAL:        em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::EQ)); break;
				case BinaryOp::NOT_EQUAL:    em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::NE)); break;
				default: throw Pycp::Exception("Codegen: unknown binary op.");
			}
			break;
		}
		case NodeType::FUNCTION_EXPRESSION: {
			FunctionExpression* fe = static_cast<FunctionExpression*>(e);
			// 新代码对象（压栈，成为 current）
			std::size_t co_idx = em.push_code_object(fe->name);

			// 构造函数局部作用域：参数 + 函数体内赋值目标
			Scope fn_scope;
			fn_scope.has_locals = true;
			for (std::string* p : fe->params) fn_scope.add(*p);
			std::unordered_set<std::string> targets;
			collect_assignment_targets(fe->body, targets);
			for (const auto& t : targets) fn_scope.add(t);

			em.current()->nparams = static_cast<uint16_t>(fe->params.size());
			em.current()->nlocals = static_cast<uint16_t>(fn_scope.names.size());
			em.current()->names = fn_scope.names;

			compile_program(em, fe->body, fn_scope);
			// 末尾补 RETURN_NONE
			if (em.current()->code.empty() ||
			    (em.current()->code.back().op != Op::RETURN &&
			     em.current()->code.back().op != Op::RETURN_NONE)) {
				em.emit(Op::RETURN_NONE);
			}

			// 弹回父代码对象，发射 MAKE_FUNCTION
			em.pop_code_object();
			em.emit(Op::MAKE_FUNCTION, static_cast<int32_t>(co_idx));
			break;
		}
		case NodeType::CALL_EXPRESSION: {
			CallExpression* ce = static_cast<CallExpression*>(e);
			compile_expr(em, ce->callee, scope);
			for (Expression* arg : ce->arguments)
				compile_expr(em, arg, scope);
			em.emit(Op::CALL, static_cast<int32_t>(ce->arguments.size()));
			break;
		}
		default:
			throw Pycp::Exception("Codegen: unsupported expression type.");
	}
}

// =============================================================
// 语句编译
// =============================================================

static void compile_stmt(Emitter& em, Statement* s, Scope& scope) {
	if (s->lineno >= 0) em.set_lineno(s->lineno);
	switch (s->get_type()) {
		case NodeType::ASSIGNMENT_STATEMENT: {
			AssignmentStatement* as = static_cast<AssignmentStatement*>(s);
			if (as->target->get_type() != NodeType::IDENTIFIER_EXPRESSION)
				throw Pycp::Exception("Codegen: assignment target must be identifier.");
			compile_expr(em, as->value, scope);
			IdentifierExpression* id = static_cast<IdentifierExpression*>(as->target);
			em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(*id->name)));
			break;
		}
		case NodeType::EXPRESSION_STATEMENT: {
			ExpressionStatement* es = static_cast<ExpressionStatement*>(s);
			compile_expr(em, es->expression, scope);
			em.emit(Op::POP_TOP);
			break;
		}
		case NodeType::RETURN_STATEMENT: {
			ReturnStatement* rs = static_cast<ReturnStatement*>(s);
			compile_expr(em, rs->expression, scope);
			em.emit(Op::RETURN);
			break;
		}
		case NodeType::IF_STATEMENT: {
			IfStatement* is = static_cast<IfStatement*>(s);

			// 收集所有需要回填到 end 的"分支结束跳转"
			std::vector<std::size_t> end_jumps;
			// 收集所有条件为假时需要回填的 JUMP_IF_FALSE 占位
			std::vector<std::size_t> false_patches;

			// if 分支
			compile_expr(em, is->if_branch->condition, scope);
			false_patches.push_back(em.emit(Op::JUMP_IF_FALSE));
			compile_program(em, is->if_branch->body, scope);
			end_jumps.push_back(em.emit(Op::JUMP));

			// elif 分支
			for (IfBranch* br : *(is->elif_branches)) {
				std::size_t branch_start = em.here();
				// 回填上一个 false 占位到本分支起始
				em.patch_jump(false_patches.back(), branch_start);
				false_patches.pop_back();

				compile_expr(em, br->condition, scope);
				false_patches.push_back(em.emit(Op::JUMP_IF_FALSE));
				compile_program(em, br->body, scope);
				end_jumps.push_back(em.emit(Op::JUMP));
			}

			// else 分支
			if (is->else_body != nullptr) {
				std::size_t else_start = em.here();
				em.patch_jump(false_patches.back(), else_start);
				false_patches.pop_back();
				compile_program(em, is->else_body, scope);
			}

			// 回填最后一个未回填的 false 占位（无 else 时跳到 end）
			std::size_t end = em.here();
			while (!false_patches.empty()) {
				em.patch_jump(false_patches.back(), end);
				false_patches.pop_back();
			}
			// 回填所有分支末尾跳转到 end
			for (std::size_t j : end_jumps) {
				em.patch_jump(j, end);
			}
			break;
		}
		default:
			throw Pycp::Exception("Codegen: unsupported statement type.");
	}
}

static void compile_program(Emitter& em, Program* p, Scope& scope) {
	for (Statement* s : *(p->statements)) {
		compile_stmt(em, s, scope);
	}
}

} // anonymous namespace

// =============================================================
// 顶层入口
// =============================================================

Module Compile(Program* program) {
	Emitter em;

	// 顶层代码对象 "<module>"：无局部变量（全部走 globals）
	// 将 "<module>" 名称 intern 进符号表，保证序列化时能正确解析其名称。
	em.intern_name("<module>");
	em.push_code_object("<module>");
	Scope top_scope;
	top_scope.has_locals = false;

	compile_program(em, program, top_scope);
	em.emit(Op::HALT);

	em.current()->nparams = 0;
	em.current()->nlocals = 0;
	em.current()->names.clear();

	return std::move(em.module);
}

} // namespace Pycp::Codegen
