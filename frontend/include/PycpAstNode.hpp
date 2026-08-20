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
	NONE_LITERAL = 14,
	IF_STATEMENT = 15,
	IF_BRANCH = 16,
	IMPORT_STATEMENT = 17,
	ATTRIBUTE_EXPRESSION = 18,
	CLASS_DEFINITION = 19,
	MEMBER_VARIABLE = 20,
	METHOD_DEFINITION = 21,
	MEMBER_ASSIGNMENT = 22,
	CLASS_EXPRESSION = 24,
	FROM_IMPORT_STATEMENT = 25,
	LIST_LITERAL = 26,
	INDEX_EXPRESSION = 27
};

enum class UnaryOp : uint16_t {
	UMINUS = 0
};

enum class BinaryOp : uint16_t {
	PLUS = 0,
	MINUS = 1,
	MULTIPLY = 2,
	DIVIDE = 3,
	POWER = 4,  // 乘方（**）
	// 比较运算符（用于条件表达式）
	LESS_THAN = 5,
	GREATER_THAN = 6,
	LESS_EQUAL = 7,
	GREATER_EQUAL = 8,
	EQUAL = 9,
	NOT_EQUAL = 10
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
	std::vector<std::string*> params;
	Program* body;
	// 顶层函数装饰器表达式（@decorator），nullptr 表示无装饰器。
	// 仅用于顶层函数定义（func name(){}）；匿名函数/类内方法无此字段。
	Expression* decorator;

	FunctionExpression(std::vector<std::string*> p, Program* b,
	                   std::string n = "@anonymous", int line = -1,
	                   Expression* deco = nullptr);
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
	std::string* name;

	explicit IdentifierExpression(std::string* n, int line = -1);
	~IdentifierExpression() override;

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
	std::string* value;

	explicit IntegerLiteral(std::string* v, int line = -1);
	~IntegerLiteral() override;

	NodeType get_type() const override { return NodeType::INTEGER_LITERAL; }
	std::string to_string() const override;
};

// ============================================================
// StringLiteral 节点
// ============================================================
struct StringLiteral : Literal {
	std::string* value;

	explicit StringLiteral(std::string* v, int line = -1);
	~StringLiteral() override;

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

// ============================================================
// IfBranch 节点（单个条件分支：if / elif / else）
//   condition 为 nullptr 时表示 else 分支（无条件）
//   body     为大括号代码块对应的语句列表
//   is_elif  标记该分支是否为 elif（区别 if / elif 输出标签）
// ============================================================
struct IfBranch : Node {
	Expression* condition;
	Program* body;
	bool is_elif;

	IfBranch(Expression* cond, Program* b, int line = -1, bool elif = false);
	~IfBranch() override;

	NodeType get_type() const override { return NodeType::IF_BRANCH; }
	std::string to_string() const override;
	// 生成带关键字标签（if / elif / else）的分支字符串，indent 为前缀缩进
	std::string to_string(const std::string& indent) const;
};

// ============================================================
// IfStatement 节点（完整条件判断）
//   if_branch      必有的 if 分支
//   elif_branches  0..n 个 elif 分支（按书写顺序）
//   else_body      可选的 else 分支语句块（nullptr 表示无 else）
// 嵌套条件通过 body / else_body 内再包含 IfStatement 自然支持。
// ============================================================
struct IfStatement : Statement {
	IfBranch* if_branch;
	std::vector<IfBranch*>* elif_branches;
	Program* else_body;

	IfStatement(IfBranch* if_br,
	            std::vector<IfBranch*>* elif_brs,
	            Program* else_b,
	            int line = -1);
	~IfStatement() override;

	NodeType get_type() const override { return NodeType::IF_STATEMENT; }
	std::string to_string() const override;
};

// ============================================================
// ImportStatement 节点
// ============================================================
// import 语句：`import foo` 或 `import foo as bar`
//   module_name : 被导入的模块名（不含 .pycp 后缀）
//   alias       : 可选别名（as bar），无则 nullptr，此时绑定名为 module_name
struct ImportStatement : Statement {
	std::string* module_name;
	std::string* alias;

	ImportStatement(std::string* mod, std::string* al, int line = -1);
	~ImportStatement() override;

	NodeType get_type() const override { return NodeType::IMPORT_STATEMENT; }
	std::string to_string() const override;
};

// ============================================================
// FromImportStatement 节点（from module import a, b, ...）
// ============================================================
//   module_name : 被导入的模块名
//   names       : 导入的名称列表（含关键字 super/private/public，以字符串承载）
// 语义：加载模块；对普通标识符绑定模块属性，对关键字名（super/private/
// public）仅触发模块加载（它们本身是语言关键字，无需绑定）。
struct FromImportStatement : Statement {
	std::string* module_name;
	std::vector<std::string*>* names;

	FromImportStatement(std::string* mod, std::vector<std::string*>* ns, int line = -1);
	~FromImportStatement() override;

	NodeType get_type() const override { return NodeType::FROM_IMPORT_STATEMENT; }
	std::string to_string() const override;
};

// ============================================================
// AttributeExpression 节点（属性访问：obj.attr）
// ============================================================
// 用于 import 后的模块成员访问（如 mod_math.pi）。
//   target : 被访问的对象表达式
//   attr   : 属性名（标识符）
struct AttributeExpression : Expression {
	Expression* target;
	std::string* attr;

	AttributeExpression(Expression* t, std::string* a, int line = -1);
	~AttributeExpression() override;

	NodeType get_type() const override { return NodeType::ATTRIBUTE_EXPRESSION; }
	std::string to_string() const override;
};

// ============================================================
// ListLiteral 节点（列表字面量：[a, b, c]）
// ============================================================
//   elements : 元素表达式列表（可能为空，即 []）。
struct ListLiteral : Expression {
	std::vector<Expression*>* elements;

	explicit ListLiteral(std::vector<Expression*>* elems, int line = -1);
	~ListLiteral() override;

	NodeType get_type() const override { return NodeType::LIST_LITERAL; }
	std::string to_string() const override;
};

// ============================================================
// IndexExpression 节点（下标访问/赋值：obj[key] / obj[key] = value）
// ============================================================
//   target : 被访问的对象表达式
//   index  : 下标表达式
struct IndexExpression : Expression {
	Expression* target;
	Expression* index;

	IndexExpression(Expression* t, Expression* idx, int line = -1);
	~IndexExpression() override;

	NodeType get_type() const override { return NodeType::INDEX_EXPRESSION; }
	std::string to_string() const override;
};

// ============================================================
// ClassExpression 节点（匿名类：class [inherits parent]{...}）
// ============================================================
// 匿名类作为表达式（如 X = class inherits Pycp.Object{...}）。
// 字段与 ClassDefinition 一致，name 恒为 nullptr（内部名由 Codegen 填）。
struct ClassExpression : Expression {
	std::string* parent_name;
	std::vector<Statement*>* member_variables;
	std::vector<Statement*>* methods;

	ClassExpression(std::string* parent,
	                std::vector<Statement*>* mv,
	                std::vector<Statement*>* ms, int line = -1);
	~ClassExpression() override;

	NodeType get_type() const override { return NodeType::CLASS_EXPRESSION; }
	std::string to_string() const override;
};

// ============================================================
// ClassDefinition 节点（类定义：class name{...} 或匿名类）
// ============================================================
//   name            : 类名（匿名类时为 nullptr，由 Codegen 填入内部名）
//   parent_name     : 继承的父类名（无继承时为 nullptr）
//   member_variables: 成员变量声明列表（MemberVariable）
//   methods         : 方法定义列表（MethodDefinition）
struct ClassDefinition : Statement {
	std::string* name;
	std::string* parent_name;
	std::vector<Statement*>* member_variables;
	std::vector<Statement*>* methods;

	ClassDefinition(std::string* n,
	                std::string* parent,
	                std::vector<Statement*>* mv,
	                std::vector<Statement*>* ms, int line = -1);
	~ClassDefinition() override;

	NodeType get_type() const override { return NodeType::CLASS_DEFINITION; }
	std::string to_string() const override;
};

// ============================================================
// MemberVariable 节点（成员变量声明：name = value 或 name）
// ============================================================
//   name      : 成员变量名
//   value     : 初始值表达式（可能为 nullptr，表示仅声明不初始化）
//   decorator : 装饰器表达式（@expr），nullptr 表示无装饰器（默认 public）。
//               可为 IdentifierExpression（@private）或 AttributeExpression
//               （@classtools.private），运行时求值得到装饰器函数。
struct MemberVariable : Statement {
	std::string* name;
	Expression* value;
	Expression* decorator;

	MemberVariable(std::string* n, Expression* v, int line = -1,
	               Expression* decorator = nullptr);
	~MemberVariable() override;

	NodeType get_type() const override { return NodeType::MEMBER_VARIABLE; }
	std::string to_string() const override;
};

// ============================================================
// MethodDefinition 节点（类内方法：func name(params){...}）
// ============================================================
//   复用 FunctionExpression 承载方法签名与函数体。
//   decorator : 装饰器表达式（@expr），nullptr 表示无装饰器（默认 public）。
struct MethodDefinition : Statement {
	FunctionExpression* function;
	Expression* decorator;

	explicit MethodDefinition(FunctionExpression* f, int line = -1,
	                           Expression* decorator = nullptr);
	~MethodDefinition() override;

	NodeType get_type() const override { return NodeType::METHOD_DEFINITION; }
	std::string to_string() const override;
};

} // namespace Pycp::AstNode