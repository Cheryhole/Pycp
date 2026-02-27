import enum

class PycpAstNode:
	lineno: int | None = None

	def __init__(self, lineno: int | None = None):
		self.lineno = lineno

	def accept(self, visitor):
		method_name = f"visit_{self.__class__.__name__}"
		visitor_method = getattr(visitor, method_name, visitor.generic_visit)
		return visitor_method(self)
	
	def __repr__(self) -> str:
		return "<Node>"

class _Enum(enum.IntEnum):
	def __str__(self) -> str:
		return self.name

	def __repr__(self) -> str:
		return self.name

# 程序节点，即所有语句的集合
class Program(PycpAstNode):
	statements: list[Statement] = []

	def __init__(self, statements):
		super().__init__()
		self.statements = statements

	def __repr__(self):
		return "\n".join(str(stmt) for stmt in self.statements)
	
	def append(self, statement: Statement) -> "Program":
		self.statements.append(statement)
		return self

# 语句节点
class Statement(PycpAstNode):
	pass

# 赋值语句 target = value
class AssignmentStatement(Statement):
	def __init__(self, target, value: "Expression", lineno=None):
		super().__init__(lineno)
		self.target = target
		self.value = value

	def __repr__(self):
		return f"<Assignment: {self.target} = {self.value}>"

# 返回语句
class ReturnStatement(Statement):
	def __init__(self, expression: "Expression", lineno=None):
		super().__init__(lineno)
		self.expression = expression

	def __repr__(self):
		return f"<Return: {self.expression}>"

# 表达式语句，即单独的表达式作为语句
class ExpressionStatement(Statement):
	def __init__(self, expression, lineno=None):
		super().__init__(lineno)
		self.expression = expression

	def __repr__(self):
		return f"<Expression: {self.expression}>"

# 表达式节点
class Expression(PycpAstNode):
	pass

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

# 匿名函数表达式
class FunctionExpression(Expression):
	def __init__(self, params, body: Program, name = "@anonymous", lineno=None):
		super().__init__(lineno)
		self.params = params
		self.body = body
		self.name = name

	def __repr__(self):
		return f"<Function: {self.name}({self.params}) {self.body}>"

# 函数调用表达式
class CallExpression(Expression):
	def __init__(self, callee, arguments, lineno):
		super().__init__(lineno)
		self.callee = callee
		self.arguments = arguments

	def __repr__(self):
		return f"<Call: {self.callee}({self.arguments})>"

# 标识符表达式
class IdentifierExpression(Expression):
	def __init__(self, name, lineno=None):
		super().__init__(lineno)
		self.name = name

	def __repr__(self):
		return f"<Identifier: {self.name}>"

# 字面量
class Literal(Expression):
	pass

# 整数字面量
class IntegerLiteral(Literal):
	def __init__(self, value, lineno=None):
		super().__init__(lineno)
		self.value = value

	def __repr__(self):
		return f"<Integer: {self.value}>"
	
class StringLiteral(Literal):
	def __init__(self, value, lineno=None):
		super().__init__(lineno)
		self.value = value

	def __repr__(self):
		return f"<String: \"{repr(self.value)}\">"

class NoneLiteral(Literal):
	def _init__(self, lineno = None):
		super().__init__(lineno)

	def __repr__(self):
		return "<None>"

