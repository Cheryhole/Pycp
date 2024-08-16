from ply import yacc
from PycpTokenHandler import tokens

import enum
import collections as clt

BCStack = clt.deque()

class PycpByteCode(enum.IntEnum):
	LOAD_LOCAL = 0x01
	STORE_LOCAL = 0x02
	LOAD_GLOBAL = 0x03
	STORE_GLOBAL = 0x04
	LOAD_CONST = 0x05
	LOAD_ATTR = 0x06
	BUILD_LIST = 0x07
	BUILD_TUPLE = 0x08
	BUILD_OBJECT = 0x09
	STORE_SUBSCR = 0x0A
	LOAD_SUBSCR = 0x0B
	DELETE_SUBSCR = 0x0C
	OPERATOR = 0x0D
	IMPORT = 0x0E
	JUMP_IF_FALSE = 0x0F
	CALL = 0x10
	RETURN = 0x11

def p_statement(p):
	"""
	statement : statements SEMICOLON
						| statement SEMICOLON statements
	"""

def p_statements(p):
	"""
	statements : statement_new_var
						 | expression
	"""

def p_statement_new_var(p):
	"statement_new_var : VAR IDENTIFIER EQUALS expression"
	BCStack.append(p[2])

def p_expression(p):
	"expression : term"

def p_expression_plus(p):
	"expression : expression PLUS term"
	BCStack.append(p[1])
	BCStack.append(p[3])
	BCStack.append(PycpByteCode.OPERATOR)
	BCStack.append("+")

def p_expression_minus(p):
	"expression : expression MINUS term"
	BCStack.append(p[1])
	BCStack.append(p[3])
	BCStack.append(PycpByteCode.OPERATOR)
	BCStack.append("-")

def p_expression_dot_attr(p):
	"expression : expression DOT IDENTIFIER"
	BCStack.append(PycpByteCode.LOAD_ATTR)
	BCStack.append(p[3])

def p_expression_of_attr(p):
	"expression : IDENTIFIER OF expression"
	BCStack.append(PycpByteCode.LOAD_ATTR)
	BCStack.append(p[1])

def p_term(p):
	"term : factor"

def p_term_times(p):
	"term : term TIMES factor"
	BCStack.append(p[1])
	BCStack.append(p[3])
	BCStack.append(PycpByteCode.OPERATOR)
	BCStack.append("*")

def p_term_divide(p):
	"term : term DIVIDE factor"
	BCStack.append(p[1])
	BCStack.append(p[3])
	BCStack.append(PycpByteCode.OPERATOR)
	BCStack.append("/")

def p_term_modulo(p):
	"term : term MODULO factor"
	BCStack.append(p[1])
	BCStack.append(p[3])
	BCStack.append(PycpByteCode.OPERATOR)
	BCStack.append("%")

def p_factor(p):
	"""
	factor : list 
				 | tuple 
				 | object
	"""

def p_factor_paren(p):
	"""
	factor : LPAREN expression RPAREN
				 | LPAREN term RPAREN
				 | LPAREN factor RPAREN
				 | LPAREN RPAREN
	"""

def p_factor_identifier(p):
	"factor : IDENTIFIER"
	BCStack.append(p[1])
	BCStack.append(PycpByteCode.LOAD_LOCAL)

def p_factor_string(p):
	"factor : STRING"
	BCStack.append(p[1])
	BCStack.append(PycpByteCode.LOAD_CONST)

def p_factor_integer(p):
	"factor : INTEGER"
	BCStack.append(p[1])
	BCStack.append(PycpByteCode.LOAD_CONST)

def p_factor_decimal(p):
	"factor : DECIMAL"
	BCStack.append(p[1])
	BCStack.append(PycpByteCode.LOAD_CONST)

def p_factor_true(p):
	"factor : TRUE"
	BCStack.append(p[1])
	BCStack.append(PycpByteCode.LOAD_CONST)

def p_factor_false(p):
	"factor : FALSE"
	BCStack.append(p[1])
	BCStack.append(PycpByteCode.LOAD_CONST)

def p_factor_none(p):
	"factor : NONE"
	BCStack.append(p[1])
	BCStack.append(PycpByteCode.LOAD_CONST)



def p_list(p):
	"""
	list : LBRACKET list_items RBRACKET
			 | LBRACKET RBRACKET
	"""
	BCStack.append(PycpByteCode.BUILD_LIST)
	if len(p) == 3:
		BCStack.append(p[2])
	BCStack.append(PycpByteCode.STORE_LOCAL)
	
def p_list_items(p):
	"""
	list_items : factor COMMA list_items
						 | factor COMMA
						 | factor
	"""
	if len(p) == 4:
		if type(p.value) == list:
			p.value.append(p[1])
	else:
		p.value = list(p[1])

def p_tuple(p):
	"""
	tuple : LPAREN tuple_items RPAREN
				| LPAREN RPAREN
	"""
	BCStack.append(PycpByteCode.BUILD_TUPLE)
	if len(p) == 3:
		BCStack.append(p[2])
	BCStack.append(PycpByteCode.STORE_LOCAL)

def p_tuple_items(p):
	"""
	tuple_items : factor COMMA tuple_items
							| factor COMMA	
	"""
	if len(p) == 4:
		if type(p.value) == list:
			p.value.append(p[1])
	else:
		p.value = list(p[1])

def p_object(p):
	"""
	object : LBRACE object_items RBRACE
				 | LBRACE RBRACE
	"""
	BCStack.append(PycpByteCode.BUILD_OBJECT)
	if len(p) == 3:
		BCStack.append(p[2])
	BCStack.append(PycpByteCode.STORE_LOCAL)

def p_object_items(p):
	"""
	object_items : factor COLON factor COMMA object_items
							 | factor COLON factor COMMA
							 | factor COLON factor
	"""
	kv = (p[1], p[2]) # key-value pair
	if len(p) == 5:
		if type(p.value) == list:
			p.value.append(kv)
	else:
		p.value = list(kv)
	

def p_error(p):
	print("Syntax error in input!", p)

parser:yacc.LRParser = yacc.yacc()
source = """
var dec = 123;
var ddec = 0d123;
var hex = 0x1A;
var bin = 0b1010;
var oct = 0o123;
var udec = 0D123;
var uhex = 0X1A;
var ubin = 0B1010;
var uoct = 0O123;
var zero = 0;
var decm = 120.456;
var ddecm = 0d123.456;
var hexm = 0x1A.3B;
var binm = 0b1010.1010;
var octm = 0o123.456;
var udecm = 0D123.456;
var uhexm = 0X1A.3B;
var ubinm = 0B1010.1010;
var uoctm = 0O123.456;
var zerom = 0.0;
var znum = 0.123;
""".strip()
result = parser.parse(source, tracking= True)
print(result)
print(BCStack)

