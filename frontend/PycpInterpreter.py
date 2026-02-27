from . import PycpAstNode as _nd
import backend as _bk

class RuntimeEnvironment:
	def __init__(self):
		self.variables: dict[str, object] = {}

	def get(self, name):
		if name in self.variables:
			return self.variables[name]
		raise RuntimeError(f"Undefined variable '{name}'")

	def set(self, name, value):
		self.variables[name] = value

	def __str__(self):
		return str(self.variables)
	__repr__ = __str__

class Interpreter:
	def __init__(self):
		self.env = RuntimeEnvironment()

	def run(self, program):
		for stmt in program.statements:
			self.execute(stmt)

	# 执行语句
	def execute(self, stmt: _nd.Statement):
		method = getattr(self, f"_exec_{type(stmt).__name__}")
		return method(stmt)

	# 计算表达式
	def evaluate(self, expr: _nd.Expression):
		method = getattr(self, f"_eval_{type(expr).__name__}")
		return method(expr)
	
	def _exec_AssignmentStatement(self, stmt: _nd.AssignmentStatement):
		res = self.evaluate(stmt.value)
		self.env.set(stmt.target, res)

	# 类似于python解释器，表达式语句的结果存储在"_"中
	def _exec_ExpressionStatement(self, stmt: _nd.ExpressionStatement):
		res = self.evaluate(stmt.expression)
		self.env.set("_", res)

	def _eval_UnaryExpression(self, expr: _nd.UnaryExpression):
		res = self.evaluate(expr.operand)
		match expr.op:
			case _nd.UnaryExpression.Operator.UMINUS:
				res = res.__neg__()

		return res
	
	def _eval_BinaryExpression(self, expr: _nd.BinaryExpression):
		left = self.evaluate(expr.left)
		right = self.evaluate(expr.right)
		res = None

		match expr.op:
			case _nd.BinaryExpression.Operator.PLUS:
				res = left.__addition__(right)

			case _nd.BinaryExpression.Operator.MINUS:
				res = left.__subtraction__(right)

			case _nd.BinaryExpression.Operator.MULTIPLY:
				res = left.__multiplication__(right)

			case _nd.BinaryExpression.Operator.DIVIDE:
				res = left.__division__(right)

		return res

	def _eval_IdentifierExpression(self, expr: _nd.IdentifierExpression):
		print(self.env)
		return self.env.get(expr.name)
	
	def _eval_IntegerLiteral(self, expr: _nd.IntegerLiteral):
		return _bk.PycpInteger(expr.value)
	
	def _eval_StringLiteral(self, expr: _nd.StringLiteral):
		return _bk.PycpString(expr.value)
