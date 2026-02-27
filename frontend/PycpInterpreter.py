from . import PycpAstNode as _nd
import backend as _bk

class RuntimeEnvironment:
	def __init__(self, parent: RuntimeEnvironment | None = None):
		self.parent = parent
		self.variables: dict[str, object] = {}

	def get(self, name):
		if name in self.variables:
			return self.variables[name]
		elif self.parent is not None:
			return self.parent.get(name)
		else:
			raise RuntimeError(f"Undefined variable '{name}'")

	def set(self, name, value):
		if name in self.variables:
				self.variables[name] = value
		elif self.parent:
				self.parent.set(name, value)
		else:
				self.variables[name] = value

	def __str__(self):
		if self.parent != None:
			self.variables.update(self.parent.variables)
			#print("C: ", self.variables, self.parent.variables)
		return str(self.variables)
	
	__repr__ = __str__

_global_env = RuntimeEnvironment()
_global_env.set("print", _bk.PycpBuiltinFunction.print)

class Interpreter:
	def __init__(self, env: RuntimeEnvironment | None=None):
		self.env = env if env else RuntimeEnvironment(_global_env)

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

	def _exec_ReturnStatement(self, stmt):
		value = self.evaluate(stmt.expression)
		raise _bk.ReturnException(value)

	# 类似于python解释器，表达式语句的结果存储在"_"中
	def _exec_ExpressionStatement(self, stmt: _nd.ExpressionStatement):
		res = self.evaluate(stmt.expression)
		self.env.set("_", res)


	def _eval_UnaryExpression(self, expr: _nd.UnaryExpression):
		res = self.evaluate(expr.operand)
		match expr.op:
			case _nd.UnaryExpression.Operator.UMINUS:
				res = res.__negation__()

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

	def _eval_FunctionExpression(self, expr: _nd.FunctionExpression):
		return _bk.PycpFunction(
						 expr.name,
						 expr.params, 
						 expr.body, 
						 self.env # 闭包时有用，记录当前环境
					 )
	
	def _eval_CallExpression(self, expr):
		func_obj: _bk.PycpFunction = self.evaluate(expr.callee)

		if len(expr.arguments) != len(func_obj.params):
			raise RuntimeError("Argument count mismatch")

		# 检查是否为内置函数
		if func_obj.is_builtin():
			args = {}

			for name, arg_expr in zip(func_obj.params, expr.arguments):
				value = self.evaluate(arg_expr)
				args[name] = value
			return func_obj.__call__(**args)

		# 创建新的环境
		new_env = RuntimeEnvironment(func_obj.env)

		# 绑定参数
		for name, arg_expr in zip(func_obj.params, expr.arguments):
			value = self.evaluate(arg_expr)
			new_env.variables[name] = value

		# 创建新的解释器
		child_interpreter = Interpreter(new_env)

		try:
			# 执行函数体
			for stmt in func_obj.body.statements:
				child_interpreter.execute(stmt)

			# 没有 return
			return _bk.PycpNone.inst

		except _bk.ReturnException as r:
			return r.value

	def _eval_IdentifierExpression(self, expr: _nd.IdentifierExpression):
		return self.env.get(expr.name)
	

	def _eval_IntegerLiteral(self, expr: _nd.IntegerLiteral):
		return _bk.PycpInteger(expr.value)
	
	def _eval_StringLiteral(self, expr: _nd.StringLiteral):
		return _bk.PycpString(expr.value)

	def _eval_NoneLiteral(self, expr: _nd.NoneLiteral):
		return _bk.PycpNone.inst

