#include "PycpAstNode.hpp"

namespace Pycp::Ast {

// ============================================================
// 辅助函数
// ============================================================
static std::string unary_op_to_string(UnaryOp op) {
	switch (op) {
		case UnaryOp::UMINUS: return "UMINUS";
		default: return "UNKNOWN";
	}
}

static std::string binary_op_to_string(BinaryOp op) {
	switch (op) {
		case BinaryOp::PLUS:     			return "PLUS";
		case BinaryOp::MINUS:    			return "MINUS";
		case BinaryOp::MULTIPLY: 			return "MULTIPLY";
		case BinaryOp::DIVIDE:   			return "DIVIDE";
		case BinaryOp::POWER:    			return "POWER";
		case BinaryOp::LESS_THAN:     return "LESS_THAN";
		case BinaryOp::GREATER_THAN:  return "GREATER_THAN";
		case BinaryOp::LESS_EQUAL:    return "LESS_EQUAL";
		case BinaryOp::GREATER_EQUAL: return "GREATER_EQUAL";
		case BinaryOp::EQUAL:         return "EQUAL";
		case BinaryOp::NOT_EQUAL:     return "NOT_EQUAL";
		default: return "UNKNOWN";
	}
}

// ============================================================
// Program 实现
// ============================================================
Program::Program(std::vector<Statement*>* stmts)
	: statements(stmts) {}

Program::~Program() {
	if (statements == nullptr) {
	  return;
	}

	for (Statement* stmt : *statements) {
		delete stmt;
	}
	delete statements;
}

void Program::append(Statement* stmt) {
	statements->push_back(stmt);
}

std::string Program::to_string() const {
	std::string result;
	for (size_t i = 0; i < statements->size(); ++i) {
		if (i > 0) {
			result += "\n";
		}
		result += (*statements)[i]->to_string();
	}
	return result;
}

// ============================================================
// AssignmentStatement 实现
// ============================================================
AssignmentStatement::AssignmentStatement(Expression* t, Expression* v, int line)
	: target(t), value(v) {
	lineno = line;
}

AssignmentStatement::~AssignmentStatement() {
	delete value;
	delete target;
}

std::string AssignmentStatement::to_string() const {
	return "<Assignment: " + target->to_string() + " = " + value->to_string() + ">";
}

// ============================================================
// ReturnStatement 实现
// ============================================================
ReturnStatement::ReturnStatement(Expression* expr, int line)
	: expression(expr) {
	lineno = line;
}

ReturnStatement::~ReturnStatement() {
	delete expression;
}

std::string ReturnStatement::to_string() const {
	return "<Return: " + expression->to_string() + ">";
}

// ============================================================
// ExpressionStatement 实现
// ============================================================
ExpressionStatement::ExpressionStatement(Expression* expr, int line)
	: expression(expr) {
	lineno = line;
}

ExpressionStatement::~ExpressionStatement() {
	delete expression;
}

std::string ExpressionStatement::to_string() const {
	return "<Expression: " + expression->to_string() + ">";
}

// ============================================================
// UnaryExpression 实现
// ============================================================
UnaryExpression::UnaryExpression(UnaryOp o, Expression* operand_, int line)
	: op(o), operand(operand_) {
	lineno = line;
}

UnaryExpression::~UnaryExpression() {
	delete operand;
}

std::string UnaryExpression::to_string() const {
	return "<Unary: " + unary_op_to_string(op) + " " + operand->to_string() + ">";
}

// ============================================================
// BinaryExpression 实现
// ============================================================
BinaryExpression::BinaryExpression(BinaryOp o, Expression* l, Expression* r, int line)
	: op(o), left(l), right(r) {
	lineno = line;
}

BinaryExpression::~BinaryExpression() {
	delete left;
	delete right;
}

std::string BinaryExpression::to_string() const {
	return "<Binary: " + left->to_string() + " " + binary_op_to_string(op) + " " + right->to_string() + ">";
}

// ============================================================
// FunctionExpression 实现
// ============================================================
FunctionExpression::FunctionExpression(std::vector<std::string*> p,
                                       Program* b,
                                       std::string n, int line,
                                       Expression* deco)
	: name(std::move(n)), params(std::move(p)), body(b), decorator(deco) {
	lineno = line;
}

FunctionExpression::~FunctionExpression() {
	delete body;
	delete decorator;
	for (std::string* param : params) {
		delete param;
	}
}

std::string FunctionExpression::to_string() const {
	std::string params_str;
	for (size_t i = 0; i < params.size(); ++i) {
		if (i > 0) params_str += ", ";
		params_str += *(params[i]);
	}

	std::string body_str = body->to_string();
	// 缩进处理
	size_t pos = 0;
	while (pos < body_str.length()) {
		pos = body_str.find('\n', pos);
		if (pos == std::string::npos) break;
		body_str.insert(pos + 1, "  ");
		pos += 3;
	}
	if (!body_str.empty()) {
		body_str = "\n  " + body_str + "\n";
	}

	return "<Function: " + name + "(" + params_str + ")" + body_str + ">";
}

// ============================================================
// CallExpression 实现
// ============================================================
CallExpression::CallExpression(Expression* callee_,
                               std::vector<Expression*> args, int line)
	: callee(callee_), arguments(std::move(args)) {
	lineno = line;
}

CallExpression::~CallExpression() {
	delete callee;
	for (Expression* arg : arguments) {
		delete arg;
	}
}

std::string CallExpression::to_string() const {
	std::string args_str;
	for (size_t i = 0; i < arguments.size(); ++i) {
		if (i > 0) args_str += ", ";
		args_str += arguments[i]->to_string();
	}
	return "<Call: " + callee->to_string() + "(" + args_str + ")>";
}

// ============================================================
// IdentifierExpression 实现
// ============================================================
IdentifierExpression::IdentifierExpression(std::string* n, int line)
	: name(n) {
	lineno = line;
}

IdentifierExpression::~IdentifierExpression() {
	delete name;
}

std::string IdentifierExpression::to_string() const {
	return "<Identifier: " + *name + ">";
}

// ============================================================
// IntegerLiteral 实现
// ============================================================
IntegerLiteral::IntegerLiteral(std::string* v, int line)
	: value(v) {
	lineno = line;
}

IntegerLiteral::~IntegerLiteral() {
  delete value;
}

std::string IntegerLiteral::to_string() const {
	return "<Integer: " + *value + ">";
}

// ============================================================
// StringLiteral 实现
// ============================================================
StringLiteral::StringLiteral(std::string* v, int line)
	: value(v) {
	lineno = line;
}

StringLiteral::~StringLiteral() {
  delete value;
}

std::string StringLiteral::to_string() const {
	return "<String: \"" + *value + "\">";
}

// ============================================================
// NoneLiteral 实现
// ============================================================
NoneLiteral::NoneLiteral(int line) {
	lineno = line;
}

std::string NoneLiteral::to_string() const {
	return "<None>";
}

// ============================================================
// IfBranch 实现
// ============================================================
IfBranch::IfBranch(Expression* cond, Program* b, int line, bool elif)
	: condition(cond), body(b), is_elif(elif) {
	lineno = line;
}

IfBranch::~IfBranch() {
	delete condition;
	delete body;
}

// 对代码块内部做缩进处理，与 FunctionExpression 保持一致
static std::string indent_block(const std::string& raw, const std::string& base_indent) {
	std::string body_str = raw;
	size_t pos = 0;
	while (pos < body_str.length()) {
		pos = body_str.find('\n', pos);
		if (pos == std::string::npos) break;
		body_str.insert(pos + 1, base_indent + "  ");
		pos += base_indent.length() + 3;
	}
	if (!body_str.empty()) {
		body_str = "\n" + base_indent + "  " + body_str + "\n" + base_indent;
	}
	return body_str;
}

std::string IfBranch::to_string() const {
	return to_string("");
}

std::string IfBranch::to_string(const std::string& indent) const {
	std::string body_str = indent_block(body->to_string(), indent);

	if (condition == nullptr) {
		// else 分支：无条件
		return indent + "<Else:" + body_str + ">";
	}
	const std::string keyword = is_elif ? "<Elif: " : "<If: ";
	return indent + keyword + condition->to_string() + body_str + ">";
}

// ============================================================
// IfStatement 实现
// ============================================================
IfStatement::IfStatement(IfBranch* if_br,
                         std::vector<IfBranch*>* elif_brs,
                         Program* else_b,
                         int line)
	: if_branch(if_br), elif_branches(elif_brs), else_body(else_b) {
	lineno = line;
}

IfStatement::~IfStatement() {
	delete if_branch;
	if (elif_branches != nullptr) {
		for (IfBranch* br : *elif_branches) {
			delete br;
		}
		delete elif_branches;
	}
	delete else_body;
}

std::string IfStatement::to_string() const {
	// 不显式包裹组合节点标签，仅以 If/Elif/Else 区分各分支并体现层级关联。
	const std::string indent = "  ";
	std::string result = if_branch->to_string(indent);

	if (elif_branches != nullptr) {
		for (IfBranch* br : *elif_branches) {
			result += "\n" + br->to_string(indent);
		}
	}

	if (else_body != nullptr) {
		// else 分支以独立 IfBranch（condition==nullptr）形式复用打印逻辑
		IfBranch else_branch(nullptr, else_body);
		result += "\n" + else_branch.to_string(indent);
	}

	return result;
}

// ============================================================
// ImportStatement 实现
// ============================================================
ImportStatement::ImportStatement(std::string* mod, std::string* al, int line)
	: module_name(mod), alias(al) {
	lineno = line;
}

ImportStatement::~ImportStatement() {
	delete module_name;
	delete alias;
}

std::string ImportStatement::to_string() const {
	if (alias != nullptr) {
		return "<Import: " + *module_name + " as " + *alias + ">";
	}
	return "<Import: " + *module_name + ">";
}

// ============================================================
// FromImportStatement 实现
// ============================================================
FromImportStatement::FromImportStatement(std::string* mod,
                                         std::vector<std::string*>* ns, int line)
	: module_name(mod), names(ns) {
	lineno = line;
}

FromImportStatement::~FromImportStatement() {
	delete module_name;
	if (names != nullptr) {
		for (std::string* n : *names) delete n;
		delete names;
	}
}

std::string FromImportStatement::to_string() const {
	std::string result = "<FromImport: " + *module_name + " import ";
	if (names != nullptr) {
		for (std::size_t i = 0; i < names->size(); ++i) {
			if (i > 0) result += ", ";
			result += *((*names)[i]);
		}
	}
	return result + ">";
}

// ============================================================
// AttributeExpression 实现
// ============================================================
AttributeExpression::AttributeExpression(Expression* t, std::string* a, int line)
	: target(t), attr(a) {
	lineno = line;
}

AttributeExpression::~AttributeExpression() {
	delete target;
	delete attr;
}

std::string AttributeExpression::to_string() const {
	return "<Attribute: " + target->to_string() + "." + *attr + ">";
}

// ============================================================
// ListLiteral 实现
// ============================================================
ListLiteral::ListLiteral(std::vector<Expression*>* elems, int line)
	: elements(elems) {
	lineno = line;
}

ListLiteral::~ListLiteral() {
	if (elements != nullptr) {
		for (Expression* e : *elements) delete e;
		delete elements;
	}
}

std::string ListLiteral::to_string() const {
	std::string result = "[";
	if (elements != nullptr) {
		for (std::size_t i = 0; i < elements->size(); ++i) {
			if (i > 0) result += ", ";
			result += (*elements)[i]->to_string();
		}
	}
	return result + "]";
}

// ============================================================
// IndexExpression 实现
// ============================================================
IndexExpression::IndexExpression(Expression* t, Expression* idx, int line)
	: target(t), index(idx) {
	lineno = line;
}

IndexExpression::~IndexExpression() {
	delete target;
	delete index;
}

std::string IndexExpression::to_string() const {
	return target->to_string() + "[" + index->to_string() + "]";
}

// ============================================================
// ClassDefinition 实现
// ============================================================
ClassDefinition::ClassDefinition(std::string* n,
                                 std::string* parent,
                                 std::vector<Statement*>* mv,
                                 std::vector<Statement*>* ms, int line)
	: name(n), parent_name(parent), member_variables(mv), methods(ms) {
	lineno = line;
}

ClassDefinition::~ClassDefinition() {
	delete name;
	delete parent_name;
	if (member_variables != nullptr) {
		for (Statement* s : *member_variables) delete s;
		delete member_variables;
	}
	if (methods != nullptr) {
		for (Statement* s : *methods) delete s;
		delete methods;
	}
}

std::string ClassDefinition::to_string() const {
	std::string result = "<Class: " + (name ? *name : "?");
	if (parent_name != nullptr) {
		result += " inherits " + *parent_name;
	}
	if (member_variables != nullptr) {
		for (Statement* s : *member_variables) {
			result += "\n  " + s->to_string();
		}
	}
	if (methods != nullptr) {
		for (Statement* s : *methods) {
			result += "\n  " + s->to_string();
		}
	}
	return result + ">";
}

// ============================================================
// MemberVariable 实现
// ============================================================
MemberVariable::MemberVariable(std::string* n, Expression* v, int line,
                               Expression* decorator)
	: name(n), value(v), decorator(decorator) {
	lineno = line;
}

MemberVariable::~MemberVariable() {
	delete name;
	delete value;
	delete decorator;
}

std::string MemberVariable::to_string() const {
	std::string dec = (decorator != nullptr) ? ("@" + decorator->to_string() + " ") : "";
	if (value != nullptr) {
		return "<Member: " + dec + (name ? *name : "?") + " = " + value->to_string() + ">";
	}
	return "<Member: " + dec + (name ? *name : "?") + ">";
}

// ============================================================
// MethodDefinition 实现
// ============================================================
MethodDefinition::MethodDefinition(FunctionExpression* f, int line,
                                   Expression* decorator)
	: function(f), decorator(decorator) {
	lineno = line;
}

MethodDefinition::~MethodDefinition() {
	delete function;
	delete decorator;
}

std::string MethodDefinition::to_string() const {
	std::string dec = (decorator != nullptr) ? ("@" + decorator->to_string() + " ") : "";
	return "<Method: " + dec + (function ? function->to_string() : "?") + ">";
}

// ============================================================
// RepeatStatement 实现
// ============================================================
RepeatStatement::RepeatStatement(RepeatMode m,
                                 Expression* cnt, Expression* st,
                                 Expression* en, Expression* step,
                                 Expression* cond, std::string* var,
                                 Program* b, int line)
	: mode(m), count_expr(cnt), start_expr(st), end_expr(en),
	  step_expr(step), cond_expr(cond), var_name(var), body(b) {
	lineno = line;
}

RepeatStatement::~RepeatStatement() {
	delete count_expr;
	delete start_expr;
	delete end_expr;
	delete step_expr;
	delete cond_expr;
	delete var_name;
	delete body;
}

std::string RepeatStatement::to_string() const {
	const char* kind = "?";
	switch (mode) {
		case RepeatMode::INFINITE: kind = "infinite"; break;
		case RepeatMode::WHILE:    kind = "while";    break;
		case RepeatMode::COUNT:    kind = "count";    break;
		case RepeatMode::RANGE:    kind = "range";    break;
	}
	std::string var = (var_name != nullptr) ? (" as " + *var_name) : "";
	return "<Repeat " + std::string(kind) + var + ">";
}

// ============================================================
// BreakStatement 实现
// ============================================================
BreakStatement::BreakStatement(int line)
	: Statement() {
	lineno = line;
}

BreakStatement::~BreakStatement() = default;

std::string BreakStatement::to_string() const {
	return "<Break>";
}

// ============================================================
// ForeachStatement 实现
// ============================================================
ForeachStatement::ForeachStatement(std::string* var, Expression* it,
                                   Program* b, int line)
	: var_name(var), iterable(it), body(b) {
	lineno = line;
}

ForeachStatement::~ForeachStatement() {
	delete var_name;
	delete iterable;
	delete body;
}

std::string ForeachStatement::to_string() const {
	std::string var = (var_name != nullptr) ? *var_name : "?";
	std::string it = (iterable != nullptr) ? iterable->to_string() : "?";
	return "<For " + var + " in " + it + ">";
}

// ============================================================
// ClassExpression 实现
// ============================================================
ClassExpression::ClassExpression(std::string* parent,
                                 std::vector<Statement*>* mv,
                                 std::vector<Statement*>* ms, int line)
	: parent_name(parent), member_variables(mv), methods(ms) {
	lineno = line;
}

ClassExpression::~ClassExpression() {
	delete parent_name;
	if (member_variables != nullptr) {
		for (Statement* s : *member_variables) delete s;
		delete member_variables;
	}
	if (methods != nullptr) {
		for (Statement* s : *methods) delete s;
		delete methods;
	}
}

std::string ClassExpression::to_string() const {
	std::string result = "<ClassExpr: ?";
	if (parent_name != nullptr) result += " inherits " + *parent_name;
	if (member_variables != nullptr) {
		for (Statement* s : *member_variables) result += "\n  " + s->to_string();
	}
	if (methods != nullptr) {
		for (Statement* s : *methods) result += "\n  " + s->to_string();
	}
	return result + ">";
}

} // namespace Pycp::AstNode