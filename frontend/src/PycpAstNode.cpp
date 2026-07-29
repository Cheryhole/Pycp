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
		case BinaryOp::PLUS:     return "PLUS";
		case BinaryOp::MINUS:    return "MINUS";
		case BinaryOp::MULTIPLY: return "MULTIPLY";
		case BinaryOp::DIVIDE:   return "DIVIDE";
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
FunctionExpression::FunctionExpression(std::vector<std::string> p,
                                       Program* b,
                                       std::string n, int line)
	: name(std::move(n)), params(std::move(p)), body(b) {
	lineno = line;
}

FunctionExpression::~FunctionExpression() {
	delete body;
}

std::string FunctionExpression::to_string() const {
	std::string params_str;
	for (size_t i = 0; i < params.size(); ++i) {
		if (i > 0) params_str += ", ";
		params_str += params[i];
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
IdentifierExpression::IdentifierExpression(std::string n, int line)
	: name(std::move(n)) {
	lineno = line;
}

std::string IdentifierExpression::to_string() const {
	return "<Identifier: " + name + ">";
}

// ============================================================
// IntegerLiteral 实现
// ============================================================
IntegerLiteral::IntegerLiteral(std::string v, int line)
	: value(std::move(v)) {
	lineno = line;
}

std::string IntegerLiteral::to_string() const {
	return "<Integer: " + value + ">";
}

// ============================================================
// StringLiteral 实现
// ============================================================
StringLiteral::StringLiteral(std::string v, int line)
	: value(std::move(v)) {
	lineno = line;
}

std::string StringLiteral::to_string() const {
	return "<String: \"" + value + "\">";
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

} // namespace Pycp::AstNode