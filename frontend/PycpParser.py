import ply.yacc
from . import PycpLexer
from . import PycpAstNode as _nd

_debug = True

tokens = PycpLexer.tokens

precedence = (
	("left", "NEWLINE"),
	("left", "OP_PLUS", "OP_MINUS"),
	("left", "OP_MULTIPLY", "OP_DIVIDE"),
	("right", "OP_UMINUS"),
)

start = "program"

def p_program(p):
	"""
	program : statement
					| program NEWLINE statement
	"""
	if len(p) == 2:
		p[0] = _nd.Program([p[1]])
	else:
		p[0] = p[1].append(p[3])

def p_program_leading_newline(p):
	"""
	program : NEWLINE program
	"""
	p[0] = p[2]

def p_program_trailing_newline(p):
	"""
	program : program NEWLINE
	"""
	p[0] = p[1]


def p_code_block(p):
	"""
	code_block : OP_LBRACE program OP_RBRACE
	"""
	p[0] = p[2]


# 函数定义参数（名称）
def p_params_list(p):
	"""
	params : IDENTIFIER
				 | params OP_COMMA IDENTIFIER
	"""
	if len(p) == 2:
			p[0] = [p[1]]
	else:
			p[0] = p[1] + [p[3]]

def p_params_empty(p):
	"""
	params : 
	"""
	p[0] = []


# 函数调用参数（表达式）
def p_arguments_empty(p):
	"""
	arguments :
	"""
	p[0] = []

def p_arguments_list(p):
	"""
	arguments : expression
						| arguments OP_COMMA expression
	"""
	if len(p) == 2:
		p[0] = [p[1]]
	else:
		p[0] = p[1] + [p[3]]


def p_statement_assignment(p):
	"""
	statement : IDENTIFIER OP_EQUALS expression
	"""
	p[0] = _nd.AssignmentStatement(p[1], p[3], p.lineno(2))

def p_statement_return(p):
	"""
	statement : KW_RETURN expression
	"""
	p[0] = _nd.ReturnStatement(p[2], p.lineno(1))

# 以常规方式定义函数（但作为语法糖，实际为将匿名函数存入IDENTIFIER）
def p_statement_function(p):
	"""
	statement : KW_FUNC IDENTIFIER OP_LPARENTHESES params OP_RPARENTHESES code_block
	"""
	func_expr = _nd.FunctionExpression(
		p[4], # params
		p[6], # body: Program
		p[2], # name
		lineno = p.lineno(1)
	)

	p[0] = _nd.AssignmentStatement(
		p[2], # IDENTIFIER
		func_expr,
		p.lineno(1)
	)

def p_statement_expression(p):
	"""
	statement : expression
	"""
	p[0] = _nd.ExpressionStatement(p[1], p.lineno(1))


# （匿名）函数定义的实现
def p_expression_function(p):
	"""
	expression : KW_FUNC OP_LPARENTHESES params OP_RPARENTHESES code_block
	"""
	p[0] = _nd.FunctionExpression(
					 p[3], # params
					 p[5], # body: Program
					 lineno = p.lineno(1) 
				 )

def p_expression_addition(p):
	"expression : expression OP_PLUS expression"
	p[0] = _nd.BinaryExpression(
					_nd.BinaryExpression.Operator.PLUS, 
					p[1], p[3], # left right
					p.lineno(2)
				)

def p_expression_subtraction(p):
	"expression : expression OP_MINUS expression"
	p[0] = _nd.BinaryExpression(
					_nd.BinaryExpression.Operator.MINUS, 
					p[1], p[3], # left right
					p.lineno(2)
				)

def p_expression_multiplication(p):
	"expression : expression OP_MULTIPLY expression"
	p[0] = _nd.BinaryExpression(
					_nd.BinaryExpression.Operator.MULTIPLY, 
					p[1], p[3], # left right
					p.lineno(2)
				)

def p_expression_division(p):
	"expression : expression OP_DIVIDE expression"
	p[0] = _nd.BinaryExpression(
					_nd.BinaryExpression.Operator.DIVIDE,
					p[1], p[3], # left right
					p.lineno(2)
				)

def p_expression_uminus(p):
	"expression : OP_MINUS expression %prec OP_UMINUS"
	p[0] = _nd.UnaryExpression(
					_nd.UnaryExpression.Operator.UMINUS, 
					p[1],
					p.lineno(1)
				)

def p_expression(p):
	"""
	expression : factor
	"""
	p[0] = p[1]


def p_factor_parentheses(p):
	"""
	factor : OP_LPARENTHESES expression OP_RPARENTHESES
	"""
	p[0] = p[2]

def p_factor_call(p):
	"""
	factor : factor OP_LPARENTHESES arguments OP_RPARENTHESES
	"""
	p[0] = _nd.CallExpression(p[1], p[3], p.lineno(2))

def p_factor_integer(p):
	"""
	factor : LT_INTEGER
	"""
	p[0] = _nd.IntegerLiteral(p[1], p.lineno(1))

def p_factor_string(p):
	"""
	factor : LT_STRING
	"""
	p[0] = _nd.StringLiteral(p[1], p.lineno(1))

def p_factor_none(p):
	"""
	factor : KW_NONE
	"""
	p[0] = _nd.NoneLiteral(p.lineno(1))

def p_factor_identifier(p):
	"""
	factor : IDENTIFIER
	"""
	p[0] = _nd.IdentifierExpression(p[1], p.lineno(1))


# Error rule for syntax errors
def p_error(p):
	print(f"Syntax error in input! {p}")

# 解析字符串
def parse(source: str):
	lexer = PycpLexer.lexer
	parser = ply.yacc.yacc(debug=_debug)
	result = parser.parse(source)
	return result

# 解析文件
def parsef(file: str):
	with open(file, "r") as f:
		result = parse(f.read())
		
	return result

if __name__ == "__main__":
	print(parsef("test.pycp"))