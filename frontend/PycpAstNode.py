import enum

class _Enum(enum.IntEnum):
	def __int__(self):
		return self.value

	def __str__(self) -> str:
		return self.name

	def __repr__(self) -> str:
		return self.name

class PycpAstNode:
	lineno: int | None = None

	class Type(_Enum):
		PROGRAM = enum.auto()

		STATEMENT = enum.auto()
		ASSIGNMENT_STATEMENT = enum.auto()
		RETURN_STATEMENT = enum.auto()
		EXPRESSION_STATEMENT = enum.auto()

		EXPRESSION = enum.auto()
		UNARY_EXPRESSION = enum.auto()
		BINARY_EXPRESSION = enum.auto()
		FUNCTION_EXPRESSION = enum.auto()
		CALL_EXPRESSION = enum.auto()
		IDENTIFIER_EXPRESSION = enum.auto()

		LITERAL = enum.auto()
		INTEGER_LITERAL = enum.auto()
		STRING_LITERAL = enum.auto()
		NONE_LITERAL = enum.auto()
		
	def __init__(self, lineno: int | None = None):
		self.lineno = lineno
	
	def __repr__(self) -> str:
		return "<Node>"

# 程序节点，即所有语句的集合
class Program(PycpAstNode):
	statements: list[Statement] = []

	def __init__(self, statements):
		super().__init__()
		self.statements = statements

	def __repr__(self):
		return "\n".join(str(stmt) for stmt in self.statements)
	
	def as_dict(self):
		return {
					   "type": self.Type.PROGRAM, 
						 "statements": [stmt.as_dict() for stmt in self.statements]
					 }
	
	def append(self, statement: Statement) -> "Program":
		self.statements.append(statement)
		return self

# 语句节点
class Statement(PycpAstNode):
	def as_dict(self):
		return {
					   "type": self.Type.STATEMENT
					 }

# 赋值语句 target = value
class AssignmentStatement(Statement):
	def __init__(self, target, value: "Expression", lineno=None):
		super().__init__(lineno)
		self.target = target
		self.value = value

	def __repr__(self):
		return f"<Assignment: {self.target} = {self.value}>"
	
	def as_dict(self):
		return {
					   "type": self.Type.ASSIGNMENT_STATEMENT, 
						 "target": self.target, 
						 "value": self.value.as_dict()
					 }

# 返回语句
class ReturnStatement(Statement):
	def __init__(self, expression: "Expression", lineno=None):
		super().__init__(lineno)
		self.expression = expression

	def __repr__(self):
		return f"<Return: {self.expression}>"
	
	def as_dict(self):
		return {
					   "type": self.Type.RETURN_STATEMENT, 
						 "expression": self.expression.as_dict()
					 }

# 表达式语句，即单独的表达式作为语句
class ExpressionStatement(Statement):
	def __init__(self, expression, lineno=None):
		super().__init__(lineno)
		self.expression = expression

	def __repr__(self):
		return f"<Expression: {self.expression}>"
	
	def as_dict(self):
		return {
					   "type": self.Type.EXPRESSION_STATEMENT, 
						 "expression": self.expression.as_dict()
					 }

# 表达式节点
class Expression(PycpAstNode):
	def as_dict(self):
		return {
					   "type": self.Type.EXPRESSION
					 }

# 一元表达式
class UnaryExpression(Expression):
	# 一元运算支持的运算符
	class Operator(_Enum):
		UMINUS = enum.auto()

	def __init__(self, op, operand, lineno=None):
		super().__init__(lineno)
		self.op = op
		self.operand = operand

	def __repr__(self):
		return f"<Unary: {self.op} {self.operand}>"
	
	def as_dict(self):
		return {
					   "type": self.Type.UNARY_EXPRESSION, 
						 "op": self.op, 
						 "operand": self.operand.as_dict()
					 }


# 二元表达式
class BinaryExpression(Expression):
	# 二元运算支持的运算符
	class Operator(_Enum):
		PLUS = enum.auto(),
		MINUS = enum.auto(),
		MULTIPLY = enum.auto(),
		DIVIDE = enum.auto()

	def __init__(self, op, left, right, lineno=None):
		super().__init__(lineno)
		self.op = op
		self.left = left
		self.right = right

	def __repr__(self):
		return f"<Binary: {self.left} {self.op} {self.right}>"
	
	def as_dict(self):
		return {
					   "type": self.Type.BINARY_EXPRESSION, 
						 "op": self.op, 
						 "left": self.left.as_dict(), 
						 "right": self.right.as_dict()
					 }

# 匿名函数表达式
class FunctionExpression(Expression):
	def __init__(self, params, body: Program, name = "@anonymous", lineno=None):
		super().__init__(lineno)
		self.params = params
		self.body = body
		self.name = name

	def __repr__(self):
		return f"<Function: {self.name}({self.params}) {self.body}>"
	
	def as_dict(self):
		return {
					   "type": self.Type.FUNCTION_EXPRESSION, 
						 "params": self.params, 
						 "body": self.body.as_dict()
					 }

# 函数调用表达式
class CallExpression(Expression):
	def __init__(self, callee, arguments, lineno):
		super().__init__(lineno)
		self.callee = callee
		self.arguments = arguments

	def __repr__(self):
		return f"<Call: {self.callee}({self.arguments})>"
	
	def as_dict(self):
		return {
					   "type": self.Type.CALL_EXPRESSION, 
						 "callee": self.callee.as_dict(), 
						 "arguments": self.arguments
					 }

# 标识符表达式
class IdentifierExpression(Expression):
	def __init__(self, name, lineno=None):
		super().__init__(lineno)
		self.name = name

	def __repr__(self):
		return f"<Identifier: {self.name}>"
	
	def as_dict(self):
		return {
					   "type": self.Type.IDENTIFIER_EXPRESSION, 
						 "name": self.name
					 }

# 字面量
class Literal(Expression):
	def as_dict(self):
		return {
					   "type": self.Type.LITERAL
					 }
	
# 整数字面量
class IntegerLiteral(Literal):
	def __init__(self, value, lineno=None):
		super().__init__(lineno)
		self.value = value

	def __repr__(self):
		return f"<Integer: {self.value}>"
	
	def as_dict(self):
		return {
					   "type": self.Type.INTEGER_LITERAL, 
						 "value": self.value
					 }
	
class StringLiteral(Literal):
	def __init__(self, value, lineno=None):
		super().__init__(lineno)
		self.value = value

	def __repr__(self):
		return f"<String: \"{repr(self.value)}\">"
	
	def as_dict(self):
		return {
					   "type": self.Type.STRING_LITERAL, 
						 "value": self.value
					 }

class NoneLiteral(Literal):
	def _init__(self, lineno = None):
		super().__init__(lineno)

	def __repr__(self):
		return "<None>"
	
	def as_dict(self):
		return {
					   "type": self.Type.NONE_LITERAL
					 }

