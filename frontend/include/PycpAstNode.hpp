#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace Pycp::Ast {

// ============================================================
// 枚举定义
// ============================================================
enum class NodeType : uint16_t {
	PROGRAM = 0,
	STATEMENT = 1,
	ASSIGNMENT_STATEMENT = 2,
	RETURN_STATEMENT = 3,
	EXPRESSION_STATEMENT = 4,
	EXPRESSION = 5,
	UNARY_EXPRESSION = 6,
	BINARY_EXPRESSION = 7,
	FUNCTION_EXPRESSION = 8,
	CALL_EXPRESSION = 9,
	IDENTIFIER_EXPRESSION = 10,
	LITERAL = 11,
	INTEGER_LITERAL = 12,
	STRING_LITERAL = 13,
	NONE_LITERAL = 14
};

enum class UnaryOp : uint16_t {
	UMINUS = 0
};

enum class BinaryOp : uint16_t {
	PLUS = 0,
	MINUS = 1,
	MULTIPLY = 2,
	DIVIDE = 3
};

// ============================================================
// 基类 Node
// ============================================================
struct Node {
	int lineno = -1;
	virtual ~Node() = default;
	virtual NodeType get_type() const = 0;
	virtual std::string to_string() const = 0;
};

// ============================================================
// Expression 基类
// ============================================================
struct Expression : Node {
	~Expression() override = default;

	NodeType get_type() const override { return NodeType::EXPRESSION; }
	std::string to_string() const override { return "<Expression>"; }
};

// ============================================================
// Statement 基类
// ============================================================
struct Statement : Node {
	~Statement() override = default;

	NodeType get_type() const override { return NodeType::STATEMENT; }
	std::string to_string() const override { return "<Statement>"; }
};

// ============================================================
// Program 节点
// ============================================================
struct Program : Node {
	std::vector<Statement*>* statements;

	Program() = default;
	explicit Program(std::vector<Statement*>* stmts);
	~Program() override;

	NodeType get_type() const override { return NodeType::PROGRAM; }
	std::string to_string() const override;

	void append(Statement* stmt);
};

// ============================================================
// AssignmentStatement 节点
// ============================================================
struct AssignmentStatement : Statement {
	Expression* target;
	Expression* value;

	AssignmentStatement(Expression* t, Expression* v, int line = -1);
	~AssignmentStatement() override;

	NodeType get_type() const override { return NodeType::ASSIGNMENT_STATEMENT; }
	std::string to_string() const override;
};

// ============================================================
// ReturnStatement 节点
// ============================================================
struct ReturnStatement : Statement {
	Expression* expression;

	explicit ReturnStatement(Expression* expr, int line = -1);
	~ReturnStatement() override;

	NodeType get_type() const override { return NodeType::RETURN_STATEMENT; }
	std::string to_string() const override;
};

// ============================================================
// ExpressionStatement 节点
// ============================================================
struct ExpressionStatement : Statement {
	Expression* expression;

	explicit ExpressionStatement(Expression* expr, int line = -1);
	~ExpressionStatement() override;

	NodeType get_type() const override { return NodeType::EXPRESSION_STATEMENT; }
	std::string to_string() const override;
};

// ============================================================
// UnaryExpression 节点
// ============================================================
struct UnaryExpression : Expression {
	UnaryOp op;
	Expression* operand;

	UnaryExpression(UnaryOp o, Expression* operand_, int line = -1);
	~UnaryExpression() override;

	NodeType get_type() const override { return NodeType::UNARY_EXPRESSION; }
	std::string to_string() const override;
};

// ============================================================
// BinaryExpression 节点
// ============================================================
struct BinaryExpression : Expression {
	BinaryOp op;
	Expression* left;
	Expression* right;

	BinaryExpression(BinaryOp o, Expression* l, Expression* r, int line = -1);
	~BinaryExpression() override;

	NodeType get_type() const override { return NodeType::BINARY_EXPRESSION; }
	std::string to_string() const override;
};

// ============================================================
// FunctionExpression 节点
// ============================================================
struct FunctionExpression : Expression {
	std::string name;
	std::vector<std::string> params;
	Program* body;

	FunctionExpression(std::vector<std::string> p, Program* b,
	                   std::string n = "@anonymous", int line = -1);
	~FunctionExpression() override;

	NodeType get_type() const override { return NodeType::FUNCTION_EXPRESSION; }
	std::string to_string() const override;
};

// ============================================================
// CallExpression 节点
// ============================================================
struct CallExpression : Expression {
	Expression* callee;
	std::vector<Expression*> arguments;

	CallExpression(Expression* callee_,
	               std::vector<Expression*> args, int line = -1);
	~CallExpression() override;

	NodeType get_type() const override { return NodeType::CALL_EXPRESSION; }
	std::string to_string() const override;
};

// ============================================================
// IdentifierExpression 节点
// ============================================================
struct IdentifierExpression : Expression {
	std::string name;

	explicit IdentifierExpression(std::string n, int line = -1);
	~IdentifierExpression() override = default;

	NodeType get_type() const override { return NodeType::IDENTIFIER_EXPRESSION; }
	std::string to_string() const override;
};

// ============================================================
// Literal 基类
// ============================================================
struct Literal : Expression {
	~Literal() override = default;

	NodeType get_type() const override { return NodeType::LITERAL; }
	std::string to_string() const override { return "<Literal>"; }
};

// ============================================================
// IntegerLiteral 节点
// ============================================================
struct IntegerLiteral : Literal {
	std::string value;

	explicit IntegerLiteral(std::string v, int line = -1);
	~IntegerLiteral() override = default;

	NodeType get_type() const override { return NodeType::INTEGER_LITERAL; }
	std::string to_string() const override;
};

// ============================================================
// StringLiteral 节点
// ============================================================
struct StringLiteral : Literal {
	std::string value;

	explicit StringLiteral(std::string v, int line = -1);
	~StringLiteral() override = default;

	NodeType get_type() const override { return NodeType::STRING_LITERAL; }
	std::string to_string() const override;
};

// ============================================================
// NoneLiteral 节点
// ============================================================
struct NoneLiteral : Literal {
	explicit NoneLiteral(int line = -1);
	~NoneLiteral() override = default;

	NodeType get_type() const override { return NodeType::NONE_LITERAL; }
	std::string to_string() const override;
};

} // namespace Pycp::AstNode