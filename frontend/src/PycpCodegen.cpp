#include "PycpCodegen.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"

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
	BC::Module module;

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

	// 登记被导入模块名，返回其在 module.imports 中的索引（LOAD_MODULE 操作数）。
	// 同时记录 import 语句行号（当前 current_lineno），供 ImportError 报错位置。
	std::size_t intern_import(const std::string& name) {
		for (std::size_t i = 0; i < module.imports.size(); ++i)
			if (module.imports[i] == name) return i;
		module.imports.push_back(name);
		module.import_linenos.push_back(current_lineno);
		return module.imports.size() - 1;
	}

	// 登记一个类定义，返回其在 module.classes 中的索引（MAKE_CLASS 操作数）。
	std::size_t intern_class(const ClassDef& cd) {
		module.classes.push_back(cd);
		return module.classes.size() - 1;
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

	// 构造带位置信息的编译错误（供 Codegen 抛 Pycp::Exception 时携带文件名/行号）。
	Pycp::Exception make_error(int line, const std::string& msg) {
		if (line < 0) return Pycp::Exception(msg);
		return Pycp::Exception(module.source_path, line, msg);
	}

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

	// 编译循环时维护的"循环栈"：每层循环记录其 break 占位指令索引。
	// 遇到 BreakStatement 时将 emit(Op::BREAK) 的索引压入 loop_stack.back()；
	// 循环编译完成时把这些占位 patch 到循环 end，再 pop。嵌套循环各层
	// break 自然归位，正确实现"仅退出当前一层"。
	std::vector<std::vector<std::size_t>> loop_stack;

	// 生成唯一临时变量名（供范围循环缓存 end/step），避免嵌套循环冲突。
	int next_temp_id = 0;
	std::string new_temp(const std::string& hint) {
		return "$range_" + std::to_string(next_temp_id++) + "_" + hint;
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
static void compile_stmt(Emitter& em, Statement* s, Scope& scope, bool top_level = true);
static void compile_program(Emitter& em, Program* p, Scope& scope, bool top_level = true);
static void compile_class_def(Emitter& em, ClassDefinition* cd, Scope& scope);
static void compile_class_expr(Emitter& em, ClassExpression* ce, Scope& scope);
// 通用类编译：根据类名/父类名/成员/方法构造 ClassDef 并发射 MAKE_CLASS。
static void compile_class_body(Emitter& em, const std::string& name,
                               const std::string* parent_name,
                               std::vector<Statement*>* member_variables,
                               std::vector<Statement*>* methods,
                               Scope& scope);

// =============================================================
// 表达式编译
// =============================================================

// 若表达式为整数字面量，取出其值（否则返回 false）。用于 repeat 循环
// 方向/步长推导。
static bool literal_int_value(Expression* e, int64_t& out) {
	if (e == nullptr) return false;
	if (e->get_type() == NodeType::INTEGER_LITERAL) {
		IntegerLiteral* lit = static_cast<IntegerLiteral*>(e);
		try {
			out = std::stoll(*lit->value);
			return true;
		} catch (...) {
			return false;
		}
	}
	if (e->get_type() == NodeType::UNARY_EXPRESSION) {
		UnaryExpression* ue = static_cast<UnaryExpression*>(e);
		if (ue->op == UnaryOp::UMINUS) {
			int64_t v = 0;
			if (literal_int_value(ue->operand, v)) {
				out = -v;
				return true;
			}
		}
	}
	return false;
}

// 解析 repeat from a to b [by s] 的方向与步长：
//   - 返回 false 表示方向矛盾（字面量校验失败，编译期应报错）。
//   - 返回 true：cmp 为比较算子（LE 递增 / GE 递减），step 为步长常量。
// 规则：
//   * 显式 step 字面量 s：cmp = (s>0 ? LE : GE)；方向与 s 符号一致。
//   * 自动步长（无 by）：取 a/b 字面量；a<=b 则 step=+1(LE)，a>b 则 step=-1(GE)；
//     非字面量无法静态判定时默认 +1/LE（运行时由比较算子决定）。
//   * 矛盾（a,b,s 均为字面量且 (a<=b && s<=0) || (a>=b && s>=0)）返回 false。
static bool resolve_range_direction(RepeatStatement* rs, CompareOp& cmp, int64_t& step) {
	int64_t a = 0, b = 0, s = 0;
	bool has_a = literal_int_value(rs->start_expr, a);
	bool has_b = literal_int_value(rs->end_expr, b);
	bool has_s = (rs->step_expr != nullptr) && literal_int_value(rs->step_expr, s);

	if (has_s) {
		// 显式步长：方向由 s 决定
		cmp = (s > 0) ? CompareOp::LE : CompareOp::GE;
		step = s;
		// 仅当端点也均为字面量时才能检测矛盾
		if (has_a && has_b) {
			if ((a <= b && s <= 0) || (a >= b && s >= 0))
				return false;
		}
		return true;
	}

	// 自动步长：端点均为字面量则据其方向选 ±1
	if (has_a && has_b) {
		if (a <= b) { step = 1;  cmp = CompareOp::LE; }
		else         { step = -1; cmp = CompareOp::GE; }
	} else {
		// 无法静态判定，默认递增（运行时比较算子 + 实际步长驱动）
		step = 1; cmp = CompareOp::LE;
	}
	return true;
}

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
		case NodeType::BOOLEAN_LITERAL: {
			BooleanLiteral* lit = static_cast<BooleanLiteral*>(e);
			if (lit->value) {
				em.emit(Op::LOAD_TRUE);
			} else {
				em.emit(Op::LOAD_FALSE);
			}
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

			compile_program(em, fe->body, fn_scope, false);
			// 末尾补 RETURN_NONE
			if (em.current()->code.empty() ||
			    (em.current()->code.back().op != Op::RETURN &&
			     em.current()->code.back().op != Op::RETURN_NONE)) {
				em.emit(Op::RETURN_NONE);
			}

			// 弹回父代码对象。
			em.pop_code_object();

			// 装饰器（顶层函数定义 @decorator func）：装饰器作为 callee
			// 需在栈底，被装饰函数作为参数在栈顶。故先求值装饰器表达式，
			// 再发射 MAKE_FUNCTION，最后 CALL 1 用装饰器返回对象替换函数。
			if (fe->decorator != nullptr) {
				compile_expr(em, fe->decorator, scope);
				em.emit(Op::MAKE_FUNCTION, static_cast<int32_t>(co_idx));
				em.emit(Op::CALL, 1);
			} else {
				em.emit(Op::MAKE_FUNCTION, static_cast<int32_t>(co_idx));
			}
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
		case NodeType::ATTRIBUTE_EXPRESSION: {
			AttributeExpression* ae = static_cast<AttributeExpression*>(e);
			compile_expr(em, ae->target, scope);
			em.emit(Op::LOAD_ATTR, static_cast<int32_t>(em.intern_name(*ae->attr)));
			break;
		}
		case NodeType::CLASS_EXPRESSION: {
			ClassExpression* ce = static_cast<ClassExpression*>(e);
			compile_class_expr(em, ce, scope);
			break;
		}
		case NodeType::LIST_LITERAL: {
			ListLiteral* ll = static_cast<ListLiteral*>(e);
			// 元素依次求值压栈（保持书写顺序），最后 BUILD_LIST n。
			std::size_t n = (ll->elements != nullptr) ? ll->elements->size() : 0;
			if (ll->elements != nullptr) {
				for (Expression* el : *(ll->elements)) {
					compile_expr(em, el, scope);
				}
			}
			em.emit(Op::BUILD_LIST, static_cast<int32_t>(n));
			break;
		}
		case NodeType::MAP_LITERAL: {
			MapLiteral* ml = static_cast<MapLiteral*>(e);
			// 每对键值先压 key 再压 value（栈序 ... k1 v1 k2 v2），
			// 最后 BUILD_MAP n（VM 按 v=pop(); k=pop(); 逆序还原）。
			std::size_t n = (ml->pairs != nullptr) ? ml->pairs->size() : 0;
			if (ml->pairs != nullptr) {
				for (auto& kv : *(ml->pairs)) {
					compile_expr(em, kv.first, scope);
					compile_expr(em, kv.second, scope);
				}
			}
			em.emit(Op::BUILD_MAP, static_cast<int32_t>(n));
			break;
		}
		case NodeType::INDEX_EXPRESSION: {
			IndexExpression* ie = static_cast<IndexExpression*>(e);
			compile_expr(em, ie->target, scope);
			compile_expr(em, ie->index, scope);
			em.emit(Op::GET_ITEM);
			break;
		}
		default:
			throw Pycp::Exception("Codegen: unsupported expression type.");
	}
}

// =============================================================
// 语句编译
// =============================================================

static void compile_stmt(Emitter& em, Statement* s, Scope& scope, bool top_level) {
	if (s->lineno >= 0) em.set_lineno(s->lineno);
	switch (s->get_type()) {
		case NodeType::ASSIGNMENT_STATEMENT: {
			AssignmentStatement* as = static_cast<AssignmentStatement*>(s);
			if (as->target->get_type() == NodeType::IDENTIFIER_EXPRESSION) {
				compile_expr(em, as->value, scope);
				IdentifierExpression* id = static_cast<IdentifierExpression*>(as->target);
				em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(*id->name)));
			} else if (as->target->get_type() == NodeType::ATTRIBUTE_EXPRESSION) {
				// 成员赋值：obj.attr = value
				AttributeExpression* ae = static_cast<AttributeExpression*>(as->target);
				compile_expr(em, ae->target, scope);
				compile_expr(em, as->value, scope);
				em.emit(Op::STORE_ATTR, static_cast<int32_t>(em.intern_name(*ae->attr)));
			} else if (as->target->get_type() == NodeType::INDEX_EXPRESSION) {
				// 下标赋值：obj[key] = value
				IndexExpression* ie = static_cast<IndexExpression*>(as->target);
				compile_expr(em, ie->target, scope);
				compile_expr(em, ie->index, scope);
				compile_expr(em, as->value, scope);
				em.emit(Op::SET_ITEM);
			} else {
				throw Pycp::Exception("Codegen: unsupported assignment target.");
			}
			break;
		}
		case NodeType::EXPRESSION_STATEMENT: {
			ExpressionStatement* es = static_cast<ExpressionStatement*>(s);
			compile_expr(em, es->expression, scope);
			// REPL 求值模式下，顶层表达式语句保留栈顶值作为模块返回值，
			// 供 REPL 回显结果（与 Python 一致）；函数体/分支内的表达式
			// 语句（top_level=false）或非 REPL 模块仍丢弃栈顶。
			if (em.module.repl_eval && top_level) {
				em.emit(Op::RETURN);
			} else {
				em.emit(Op::POP_TOP);
			}
			break;
		}
		case NodeType::IMPORT_STATEMENT: {
			ImportStatement* is = static_cast<ImportStatement*>(s);
			// LOAD_MODULE <import_idx> 加载模块对象压栈
			em.emit(Op::LOAD_MODULE,
			        static_cast<int32_t>(em.intern_import(*is->module_name)));
			// 绑定名：alias 优先，否则模块名
			const std::string& bind_name = (is->alias != nullptr) ? *is->alias : *is->module_name;
			em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(bind_name)));
			break;
		}
		case NodeType::FROM_IMPORT_STATEMENT: {
			FromImportStatement* fis = static_cast<FromImportStatement*>(s);
			// from module import a, b, ...：加载模块，逐个从模块命名空间
			// 取属性并绑定到当前命名空间（super/private/public 等均为普通
			// 运行时对象，走同一路径）。
			em.emit(Op::LOAD_MODULE,
			        static_cast<int32_t>(em.intern_import(*fis->module_name)));
			for (std::string* n : *(fis->names)) {
				em.emit(Op::DUP_TOP);
				em.emit(Op::LOAD_ATTR, static_cast<int32_t>(em.intern_name(*n)));
				em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(*n)));
			}
			em.emit(Op::POP_TOP); // 丢弃模块对象
			break;
		}
		case NodeType::CLASS_DEFINITION: {
			ClassDefinition* cd = static_cast<ClassDefinition*>(s);
			compile_class_def(em, cd, scope);
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
			compile_program(em, is->if_branch->body, scope, false);
			end_jumps.push_back(em.emit(Op::JUMP));

			// elif 分支
			for (IfBranch* br : *(is->elif_branches)) {
				std::size_t branch_start = em.here();
				// 回填上一个 false 占位到本分支起始
				em.patch_jump(false_patches.back(), branch_start);
				false_patches.pop_back();

				compile_expr(em, br->condition, scope);
				false_patches.push_back(em.emit(Op::JUMP_IF_FALSE));
				compile_program(em, br->body, scope, false);
				end_jumps.push_back(em.emit(Op::JUMP));
			}

			// else 分支
			if (is->else_body != nullptr) {
				std::size_t else_start = em.here();
				em.patch_jump(false_patches.back(), else_start);
				false_patches.pop_back();
				compile_program(em, is->else_body, scope, false);
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
		case NodeType::REPEAT_STATEMENT: {
			RepeatStatement* rs = static_cast<RepeatStatement*>(s);
			// 循环起始标签（body 前）
			std::size_t loop_start = em.here();
			// 新建一层循环栈，记录该层所有 break 占位
			em.loop_stack.emplace_back();

			// 若为计数/范围循环且指定了 as 变量，编译 body 前将其登记为
			// 局部（函数体）或全局（顶层）。顶层无 locals，STORE_VAR/LOAD_VAR
			// 自动建全局，符合 Python 泄漏行为。
			if (rs->var_name != nullptr) scope.add(*rs->var_name);

			std::size_t end_jump = static_cast<std::size_t>(-1); // 条件为假跳转占位（WHILE/RANGE/COUNT）
			std::size_t second_end_jump = static_cast<std::size_t>(-1); // RANGE 非字面量步长的第二个退出占位

			switch (rs->mode) {
				case RepeatMode::INFINITE: {
					// 直接编译 body，末尾跳回 loop_start
					compile_program(em, rs->body, scope, false);
					em.patch_jump(em.emit(Op::JUMP), loop_start);
					break;
				}
				case RepeatMode::WHILE: {
					// 每轮 body 前先判断 cond，false 退出
					compile_expr(em, rs->cond_expr, scope);
					end_jump = em.emit(Op::JUMP_IF_FALSE);
					compile_program(em, rs->body, scope, false);
					em.patch_jump(em.emit(Op::JUMP), loop_start);
					break;
				}
				case RepeatMode::COUNT: {
					// 用 var_name（或匿名临时名）承载计数器：v 取 0..N-1
					std::string v = (rs->var_name != nullptr)
						? *rs->var_name : "$repeat";
					em.emit(Op::LOAD_CONST, static_cast<int32_t>(em.intern_int(0)));
					em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(v)));
					loop_start = em.here(); // 条件判断处即循环起点
					em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(v)));
					compile_expr(em, rs->count_expr, scope);
					em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::LT));
					end_jump = em.emit(Op::JUMP_IF_FALSE);
					compile_program(em, rs->body, scope, false);
					// v = v + 1
					em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(v)));
					em.emit(Op::LOAD_CONST, static_cast<int32_t>(em.intern_int(1)));
					em.emit(Op::BINARY_ADD);
					em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(v)));
					em.patch_jump(em.emit(Op::JUMP), loop_start);
					break;
				}
				case RepeatMode::RANGE: {
					// 用 var_name 承载循环变量 i，取 a, a+step, ..., b（含端点）
					std::string v = (rs->var_name != nullptr)
						? *rs->var_name : "$repeat";
					// 临时变量缓存端点/步长（唯一名避免嵌套冲突）
					std::string end_tmp = em.new_temp("to");
					std::string step_tmp = em.new_temp("by");

					// ---- 显式步长（by s，字面量或表达式）：统一运行时方向校验 ----
					if (rs->step_expr != nullptr) {
						// 循环前求值 start/end/step 各一次（对齐 Python range：
						// stop/step 求值一次并缓存）。
						compile_expr(em, rs->start_expr, scope);
						em.emit(Op::CHECK_INT);
						em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(v)));
						compile_expr(em, rs->end_expr, scope);
						em.emit(Op::CHECK_INT);
						em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(end_tmp)));
						compile_expr(em, rs->step_expr, scope);
						em.emit(Op::CHECK_INT);
						em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(step_tmp)));

						// 方向矛盾检测：压 a b s 给 CHECK_RANGE_DIRECTION。
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(v)));
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(end_tmp)));
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(step_tmp)));
						em.emit(Op::CHECK_RANGE_DIRECTION);

						loop_start = em.here();
						// 判定步长符号：step < 0 ?（JUMP_IF_FALSE 会消费该 bool）
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(step_tmp)));
						em.emit(Op::LOAD_CONST, static_cast<int32_t>(em.intern_int(0)));
						em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::LT));
						em.emit(Op::JUMP_IF_FALSE);
						std::size_t neg_step_cond = em.here() - 1; // step<0 的 false 跳转
						// 负步长分支：v >= b 继续
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(v)));
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(end_tmp)));
						em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::GE));
						end_jump = em.emit(Op::JUMP_IF_FALSE); // 负分支退出
						em.emit(Op::JUMP);
						std::size_t body_jump = em.here() - 1; // 负分支跳 body
						// 非负步长分支入口（step>=0）
						std::size_t nonneg_pos = em.here();
						em.patch_jump(neg_step_cond, nonneg_pos);
						// 非负步长分支：v <= b 继续
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(v)));
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(end_tmp)));
						em.emit(Op::COMPARE_OP, static_cast<int32_t>(CompareOp::LE));
						second_end_jump = em.emit(Op::JUMP_IF_FALSE); // 非负分支退出
						// body 入口
						std::size_t body_pos = em.here();
						em.patch_jump(body_jump, body_pos);
						compile_program(em, rs->body, scope, false);
						// v = v + step
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(v)));
						em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(step_tmp)));
						em.emit(Op::BINARY_ADD);
						em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(v)));
						em.patch_jump(em.emit(Op::JUMP), loop_start);
						break;
					}

					// ---- 自动步长（无 by）：编译期按端点静态定方向，永不方向矛盾 ----
					CompareOp dir_cmp = CompareOp::LE;
					int64_t step = 1;
					resolve_range_direction(rs, dir_cmp, step);
					// 发射 start 值（先校验为 int）并存入 v
					compile_expr(em, rs->start_expr, scope);
					em.emit(Op::CHECK_INT);
					em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(v)));

					loop_start = em.here();
					em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(v)));
					compile_expr(em, rs->end_expr, scope);
					em.emit(Op::CHECK_INT);
					// 按方向用 LE（递增）或 GE（递减）；步长由 step 常量决定。
					em.emit(Op::COMPARE_OP, static_cast<int32_t>(dir_cmp));
					end_jump = em.emit(Op::JUMP_IF_FALSE);
					compile_program(em, rs->body, scope, false);
					// v = v + step
					em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(v)));
					em.emit(Op::LOAD_CONST, static_cast<int32_t>(em.intern_int(step)));
					em.emit(Op::BINARY_ADD);
					em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(v)));
					em.patch_jump(em.emit(Op::JUMP), loop_start);
					break;
				}
			}

			// 循环结束位置
			std::size_t loop_end = em.here();

			// 回填条件假跳转（WHILE/COUNT/RANGE 的 end_jump）
			if (end_jump != static_cast<std::size_t>(-1)) {
				em.patch_jump(end_jump, loop_end);
			}
			// RANGE 非字面量步长的第二个退出占位
			if (second_end_jump != static_cast<std::size_t>(-1)) {
				em.patch_jump(second_end_jump, loop_end);
			}

			// 回填本层所有 break 占位到 loop_end（天然仅退出一层）
			for (std::size_t b : em.loop_stack.back()) {
				em.patch_jump(b, loop_end);
			}
			em.loop_stack.pop_back();
			break;
		}
		case NodeType::BREAK_STATEMENT: {
			BreakStatement* bs = static_cast<BreakStatement*>(s);
			if (em.loop_stack.empty()) {
				throw Pycp::Exception(
					em.make_error(bs->lineno,
						"break is only allowed inside a loop."));
			}
			// emit 占位，循环编译完成时回填到本层 loop_end
			std::size_t bidx = em.emit(Op::BREAK);
			em.loop_stack.back().push_back(bidx);
			break;
		}
		case NodeType::FOREACH_STATEMENT: {
			// for var in iterable { body }
			ForeachStatement* fs = static_cast<ForeachStatement*>(s);
			std::string it_tmp = em.new_temp("iter"); // 迭代器临时变量（唯一名）
			scope.add(*fs->var_name);

			// setup: it = iterable.__iterator__()（返回全新迭代器；不可迭代抛 TypeError）
			compile_expr(em, fs->iterable, scope);
			em.emit(Op::GET_ITER);
			em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(it_tmp)));

			// 新建一层循环栈，记录该层所有 break 占位
			em.loop_stack.emplace_back();

			std::size_t loop_start = em.here();
			// 每轮: elem = it.__next__()；StopIteration 时 FOR_ITER 按操作数跳转退出
			em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name(it_tmp)));
			std::size_t end_jump = em.emit(Op::FOR_ITER);
			// var = elem
			em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(*fs->var_name)));
			compile_program(em, fs->body, scope, false);
			// 回跳 loop_start
			em.patch_jump(em.emit(Op::JUMP), loop_start);

			// 循环结束位置
			std::size_t loop_end = em.here();
			em.patch_jump(end_jump, loop_end);
			// 回填本层所有 break 占位到 loop_end（仅退出一层）
			for (std::size_t b : em.loop_stack.back()) {
				em.patch_jump(b, loop_end);
			}
			em.loop_stack.pop_back();
			break;
		}
		case NodeType::DELETE_STATEMENT: {
			// delete 语句三种形态：
			//   delete obj        -> obj.__delete__()
			//   delete obj.attr   -> obj.__delete_attribute__("attr")
			//   delete obj[key]   -> obj.__delete_item__(key)
			DeleteStatement* ds = static_cast<DeleteStatement*>(s);
			Expression* t = ds->target;
			if (t->get_type() == NodeType::ATTRIBUTE_EXPRESSION) {
				AttributeExpression* ae = static_cast<AttributeExpression*>(t);
				compile_expr(em, ae->target, scope);
				em.emit(Op::LOAD_ATTR,
				        static_cast<int32_t>(em.intern_name("__delete_attribute__")));
				// 属性名作为字符串实参压栈。
				em.emit(Op::LOAD_CONST,
				        static_cast<int32_t>(em.intern_string(*ae->attr)));
				em.emit(Op::CALL, 1);
			} else if (t->get_type() == NodeType::INDEX_EXPRESSION) {
				IndexExpression* ie = static_cast<IndexExpression*>(t);
				compile_expr(em, ie->target, scope);
				em.emit(Op::LOAD_ATTR,
				        static_cast<int32_t>(em.intern_name("__delete_item__")));
				compile_expr(em, ie->index, scope);
				em.emit(Op::CALL, 1);
			} else {
				// 标识符/其他表达式：delete obj -> obj.__delete__()
				compile_expr(em, t, scope);
				em.emit(Op::LOAD_ATTR,
				        static_cast<int32_t>(em.intern_name("__delete__")));
				em.emit(Op::CALL, 0);
			}
			em.emit(Op::POP_TOP); // 丢弃返回值（None）
			break;
		}
		default:
			throw Pycp::Exception("Codegen: unsupported statement type.");
	}
}

// =============================================================
// 类定义编译
// =============================================================

// 命名类定义：class name [inherits parent]{...}，绑定到类名。
static void compile_class_def(Emitter& em, ClassDefinition* cd, Scope& scope) {
	compile_class_body(em, *cd->name, cd->parent_name,
	                   cd->member_variables, cd->methods, scope);
	em.emit(Op::STORE_VAR, static_cast<int32_t>(em.intern_name(*cd->name)));
}

// 匿名类表达式：class [inherits parent]{...}，内部名用 config 常量。
static void compile_class_expr(Emitter& em, ClassExpression* ce, Scope& scope) {
	compile_class_body(em, Pycp::ANONYMOUS_CLASS, ce->parent_name,
	                   ce->member_variables, ce->methods, scope);
	// 匿名类作为表达式值：结果留在栈上（不 STORE_VAR）。
}

// 通用类编译：构造 ClassDef 并发射 MAKE_CLASS。
static void compile_class_body(Emitter& em, const std::string& name,
                               const std::string* parent_name,
                               std::vector<Statement*>* member_variables,
                               std::vector<Statement*>* methods,
                               Scope& scope) {
	ClassDef cdef;
	cdef.name = name;
	cdef.parent_name = (parent_name != nullptr) ? *parent_name : "";

	// 装饰器栈槽计数器：带装饰器的成员依次编号 0,1,2...，其装饰器对象
	// 在 MAKE_CLASS 之前按「先成员变量、后方法」顺序求值压栈。
	uint32_t deco_idx = 0;

	// 成员变量名 + 装饰器（声明顺序）。带装饰器的成员先求值装饰器表达式
	// 压栈，并记录栈槽序号；无装饰器记为 UINT32_MAX。
	for (Statement* mv : *member_variables) {
		MemberVariable* m = static_cast<MemberVariable*>(mv);
		cdef.member_names.push_back(*m->name);
		if (m->decorator != nullptr) {
			compile_expr(em, m->decorator, scope);
			cdef.member_decorators.push_back(deco_idx++);
		} else {
			cdef.member_decorators.push_back(UINT32_MAX);
		}
	}

	// 方法：编译每个方法为独立代码对象，记录 (方法名, code_idx) + 装饰器。
	for (Statement* ms : *methods) {
		MethodDefinition* md = static_cast<MethodDefinition*>(ms);
		FunctionExpression* fe = md->function;

		std::size_t co_idx = em.push_code_object(fe->name);

		// 构造方法局部作用域：参数（含 self）+ 方法体内赋值目标。
		Scope fn_scope;
		fn_scope.has_locals = true;
		for (std::string* p : fe->params) fn_scope.add(*p);
		std::unordered_set<std::string> targets;
		collect_assignment_targets(fe->body, targets);
		for (const auto& t : targets) fn_scope.add(t);

		em.current()->nparams = static_cast<uint16_t>(fe->params.size());
		em.current()->nlocals = static_cast<uint16_t>(fn_scope.names.size());
		em.current()->names = fn_scope.names;

		compile_program(em, fe->body, fn_scope, false);
		if (em.current()->code.empty() ||
		    (em.current()->code.back().op != Op::RETURN &&
		     em.current()->code.back().op != Op::RETURN_NONE)) {
			em.emit(Op::RETURN_NONE);
		}

		em.pop_code_object();

		cdef.methods.emplace_back(fe->name, static_cast<uint32_t>(co_idx));
		if (md->decorator != nullptr) {
			// 回到外层 code object 后求值装饰器表达式压栈。
			compile_expr(em, md->decorator, scope);
			cdef.method_decorators.push_back(deco_idx++);
		} else {
			cdef.method_decorators.push_back(UINT32_MAX);
		}
	}

	// 成员变量初始值：若存在带初始值的成员（var = expr），生成隐式方法
	// __init_defaults__，方法体为 self.<name> = <expr> 序列，实例化时在
	// __initialize__ 之前执行，完成字段默认值赋值。
	{
		// 收集带初始值的成员。
		std::vector<std::pair<std::string, Expression*>> inits;
		for (Statement* mv : *member_variables) {
			MemberVariable* m = static_cast<MemberVariable*>(mv);
			if (m->value != nullptr) inits.emplace_back(*m->name, m->value);
		}

		if (!inits.empty()) {
			std::size_t co_idx = em.push_code_object("__init_defaults__");

			// 方法作用域：仅 self 参数。
			Scope fn_scope;
			fn_scope.has_locals = true;
			fn_scope.add("self");

			em.current()->nparams = 1;
			em.current()->nlocals = 1;
			em.current()->names = fn_scope.names;

			// 依次发射 self.<name> = <expr>。
			for (const auto& [iname, value] : inits) {
				em.emit(Op::LOAD_VAR, static_cast<int32_t>(em.intern_name("self")));
				compile_expr(em, value, fn_scope);
				em.emit(Op::STORE_ATTR, static_cast<int32_t>(em.intern_name(iname)));
			}
			em.emit(Op::RETURN_NONE);

			em.pop_code_object();

			cdef.methods.emplace_back("__init_defaults__", static_cast<uint32_t>(co_idx));
			cdef.method_decorators.push_back(UINT32_MAX); // __init_defaults__ 无装饰器（public）
			}
	}

	// 装饰器对象总数（MAKE_CLASS 据此从栈上取装饰器并弹出）。
	cdef.decorator_count = deco_idx;

	// 登记类定义，发射 MAKE_CLASS。
	std::size_t cidx = em.intern_class(cdef);
	em.emit(Op::MAKE_CLASS, static_cast<int32_t>(cidx));
}

static void compile_program(Emitter& em, Program* p, Scope& scope, bool top_level) {
	for (Statement* s : *(p->statements)) {
		compile_stmt(em, s, scope, top_level);
	}
}

} // anonymous namespace

// =============================================================
// 顶层入口
// =============================================================

BC::Module Compile(Program* program, bool repl_eval) {
	Emitter em;
	em.module.repl_eval = repl_eval; // 须在编译语句前设定，影响顶层表达式语句的 RETURN

	// 顶层代码对象 MODULE_TOP_NAME：无局部变量（全部走 globals）
	// 将顶层代码对象名 intern 进符号表，保证序列化时能正确解析其名称。
	em.intern_name(Pycp::MODULE_TOP_NAME);
	em.push_code_object(Pycp::MODULE_TOP_NAME);
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
