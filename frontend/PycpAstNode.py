from typing import Sequence, no_type_check
import enum, struct

def _pack_str(s: str) -> bytes:
	data = s.encode("utf-8")
	return struct.pack("I", len(data)) + data

def _pack_list(items: Sequence[PycpAstNode]) -> bytes:
	data = struct.pack("I", len(items))
	for item in items:
		data += item.as_bytecode()
	return data

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
			
	def _wrap_as_bytecode(self, node_type: int, payload: bytes = b"") -> bytes:
		lineno = self.lineno if self.lineno is not None else -1
		return (
						 struct.pack("H", node_type) +
						 struct.pack("i", lineno) +
						 struct.pack("I", len(payload)) +
						 payload
					 )

	def as_bytecode(self) -> bytes:
		return bytes()

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

	def as_bytecode(self) -> bytes:
		payload = _pack_list(self.statements)
		return self._wrap_as_bytecode(int(self.Type.PROGRAM), payload)

	def __repr__(self):
		return "\n".join(str(stmt) for stmt in self.statements)
	
	def append(self, statement: Statement) -> "Program":
		self.statements.append(statement)
		return self

# 语句节点
class Statement(PycpAstNode):
	def as_bytecode(self) -> bytes:
		return self._wrap_as_bytecode(int(self.Type.STATEMENT))

# 赋值语句 target = value
class AssignmentStatement(Statement):
	def __init__(self, target: str, value: "Expression", lineno=None):
		super().__init__(lineno)
		self.target = target
		self.value = value

	def __repr__(self):
		return f"<Assignment: {self.target} = {self.value}>"
	
	def as_bytecode(self) -> bytes:
		payload = _pack_str(self.target) + self.value.as_bytecode()
		return self._wrap_as_bytecode(int(self.Type.ASSIGNMENT_STATEMENT), payload)

# 返回语句
class ReturnStatement(Statement):
	def __init__(self, expression: "Expression", lineno=None):
		super().__init__(lineno)
		self.expression = expression

	def __repr__(self):
		return f"<Return: {self.expression}>"
	
	def as_bytecode(self) -> bytes:
		payload = self.expression.as_bytecode()
		return self._wrap_as_bytecode(int(self.Type.RETURN_STATEMENT),payload)

# 表达式语句，即单独的表达式作为语句
class ExpressionStatement(Statement):
	def __init__(self, expression: "Expression", lineno=None):
		super().__init__(lineno)
		self.expression = expression

	def __repr__(self):
		return f"<Expression: {self.expression}>"
	
	def as_bytecode(self) -> bytes:
		return self._wrap_as_bytecode(
																	int(self.Type.EXPRESSION_STATEMENT),
																	self.expression.as_bytecode()
																 )


# 表达式节点
class Expression(PycpAstNode):
	def as_bytecode(self) -> bytes:
		return self._wrap_as_bytecode(int(self.Type.EXPRESSION))

# 一元表达式
class UnaryExpression(Expression):
	# 一元运算支持的运算符
	class Operator(_Enum):
		UMINUS = enum.auto()

	def __init__(self, op: "Operator", operand: "Expression", lineno=None):
		super().__init__(lineno)
		self.op = op
		self.operand = operand

	def __repr__(self):
		return f"<Unary: {self.op} {self.operand}>"
	
	def as_bytecode(self) -> bytes:
		payload = struct.pack("H", int(self.op))  # 添加操作符
		payload += self.operand.as_bytecode()
		return self._wrap_as_bytecode(int(self.Type.UNARY_EXPRESSION), payload)

# 二元表达式
class BinaryExpression(Expression):
	# 二元运算支持的运算符
	class Operator(_Enum):
		PLUS = enum.auto()
		MINUS = enum.auto()
		MULTIPLY = enum.auto()
		DIVIDE = enum.auto()

	def __init__(self, op: "Operator", left: "Expression", right: "Expression", lineno=None):
		super().__init__(lineno)
		self.op = op
		self.left = left
		self.right = right

	def __repr__(self):
		return f"<Binary: {self.left} {self.op} {self.right}>"
	
	def as_bytecode(self) -> bytes:
		payload = struct.pack("H", int(self.op))  # 添加操作符！
		payload += self.left.as_bytecode()
		payload += self.right.as_bytecode()
		return self._wrap_as_bytecode(int(self.Type.BINARY_EXPRESSION), payload)

# 匿名函数表达式
class FunctionExpression(Expression):
	def __init__(self, params: list[str], body: Program, name: str = "@anonymous", lineno=None):
		super().__init__(lineno)
		self.params = params
		self.body = body
		self.name = name

	def __repr__(self):
		return f"<Function: {self.name}({self.params}) {self.body}>"
	
	def as_bytecode(self) -> bytes:
		payload = b""

		# 函数名
		payload += _pack_str(self.name)

		# 参数列表
		payload += struct.pack("I", len(self.params))
		for p in self.params:
				payload += _pack_str(p)

		# 函数体
		payload += self.body.as_bytecode()

		return self._wrap_as_bytecode(int(self.Type.FUNCTION_EXPRESSION), payload)

# 函数调用表达式
class CallExpression(Expression):
	def __init__(self, callee: "FunctionExpression", arguments: list["Expression"], lineno):
		super().__init__(lineno)
		self.callee = callee
		self.arguments = arguments

	def __repr__(self):
		return f"<Call: {self.callee}({self.arguments})>"
	
	def as_bytecode(self) -> bytes:
		payload = b""
		payload += self.callee.as_bytecode()

		payload += struct.pack("I", len(self.arguments))
		for arg in self.arguments:
			payload += arg.as_bytecode()

		return self._wrap_as_bytecode(int(self.Type.CALL_EXPRESSION), payload)

# 标识符表达式
class IdentifierExpression(Expression):
	def __init__(self, name, lineno=None):
		super().__init__(lineno)
		self.name = name

	def __repr__(self):
		return f"<Identifier: {self.name}>"
	
	def as_bytecode(self) -> bytes:
		payload = _pack_str(self.name)
		return self._wrap_as_bytecode(int(self.Type.IDENTIFIER_EXPRESSION),payload)


# 字面量
class Literal(Expression):
	def as_bytecode(self) -> bytes:
		return self._wrap_as_bytecode(int(self.Type.LITERAL))
		
# 整数字面量
class IntegerLiteral(Literal):
	def __init__(self, value: str, lineno=None):
		super().__init__(lineno)
		self.value = value

	def __repr__(self):
		return f"<Integer: {self.value}>"
	
	def as_bytecode(self) -> bytes:
		payload = _pack_str(self.value)
		return self._wrap_as_bytecode(int(self.Type.INTEGER_LITERAL), payload)
	
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
	
	def as_bytecode(self) -> bytes:
		payload = _pack_str(self.value)
		return self._wrap_as_bytecode(int(self.Type.STRING_LITERAL), payload)

class NoneLiteral(Literal):
	def __init__(self, lineno = None):
		super().__init__(lineno)

	def __repr__(self):
		return "<None>"
	
	def as_bytecode(self) -> bytes:
		return self._wrap_as_bytecode(int(self.Type.NONE_LITERAL))


class _ByteCodeReader:
	def __init__(self, data: bytes):
		self.data = data
		self.offset = 0

	def read(self, size: int) -> bytes:
		chunk = self.data[self.offset : self.offset + size]
		self.offset += size
		return chunk

	def read_u16(self) -> int:
		return struct.unpack("H", self.read(2))[0]

	def read_i32(self) -> int:
		return struct.unpack("i", self.read(4))[0]

	def read_u32(self) -> int:
		return struct.unpack("I", self.read(4))[0]

	def read_i64(self) -> int:
		return struct.unpack("q", self.read(8))[0]

	def read_string(self) -> str:
		length = self.read_u32()
		return self.read(length).decode("utf-8")

@no_type_check
def _read_node(reader: _ByteCodeReader) -> PycpAstNode:
	node_type = reader.read_u16()
	lineno = reader.read_i32()
	payload_len = reader.read_u32()

	payload_start = reader.offset

	node_type_enum = PycpAstNode.Type(node_type)

	
	match node_type_enum:

			case PycpAstNode.Type.PROGRAM:
					count = reader.read_u32()
					statements = [_read_node(reader) for _ in range(count)]
					node = Program(statements)

			case PycpAstNode.Type.ASSIGNMENT_STATEMENT:
					target = reader.read_string()
					value = _read_node(reader)
					node = AssignmentStatement(target, value) 

			case PycpAstNode.Type.RETURN_STATEMENT:
					expr = _read_node(reader)
					node = ReturnStatement(expr)

			case PycpAstNode.Type.EXPRESSION_STATEMENT:
					expr = _read_node(reader)
					node = ExpressionStatement(expr)

			case PycpAstNode.Type.UNARY_EXPRESSION:
					op = UnaryExpression.Operator(reader.read_u16())
					operand = _read_node(reader)
					node = UnaryExpression(op, operand)

			case PycpAstNode.Type.BINARY_EXPRESSION:
					op = BinaryExpression.Operator(reader.read_u16())
					left = _read_node(reader)
					right = _read_node(reader)
					node = BinaryExpression(op, left, right)

			case PycpAstNode.Type.FUNCTION_EXPRESSION:
					name = reader.read_string()
					param_count = reader.read_u32()
					params = [reader.read_string() for _ in range(param_count)]
					body = _read_node(reader)
					node = FunctionExpression(params, body, name=name)

			case PycpAstNode.Type.CALL_EXPRESSION:
					callee = _read_node(reader)
					arg_count = reader.read_u32()
					args = [_read_node(reader) for _ in range(arg_count)]
					node = CallExpression(callee, args, lineno)

			case PycpAstNode.Type.IDENTIFIER_EXPRESSION:
					name = reader.read_string()
					node = IdentifierExpression(name)

			case PycpAstNode.Type.INTEGER_LITERAL:
					value = reader.read_string()
					node = IntegerLiteral(value)

			case PycpAstNode.Type.STRING_LITERAL:
					value = reader.read_string()
					node = StringLiteral(value)

			case PycpAstNode.Type.NONE_LITERAL:
					node = NoneLiteral()

			case _:
					raise ValueError(f"Unknown node type: {node_type_enum}")

	node.lineno = None if lineno == -1 else lineno

	# 跳过剩余payload（安全校验）
	reader.offset = payload_start + payload_len

	return node

def from_bytecode(data: bytes) -> Program:
	reader = _ByteCodeReader(data)
	return _read_node(reader) # type: ignore


