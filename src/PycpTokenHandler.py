import re
from ply import lex

# tokens
pre_tokens = [
	"IDENTIFIER",
	"INTEGER",
	"DECIMAL",
	"STRING",
	"LPAREN",
	"RPAREN",
	"LBRACKET",
	"RBRACKET",
	"LBRACE",
	"RBRACE",
	"COMMA",
	"COLON",
	"SEMICOLON",
	"DOT",
	"EQUALS",
	"PLUS",
	"MINUS",
	"TIMES",
	"DIVIDE",
	"MODULO",
	"COMMENT"
]

keywords = (
	"None", "True", "False",
	"if", "elif", "else",
	"while", "for", "break", "continue",
	"func", "return",
	"pass",
	"import", 
	"new", "del", "var", "const",
	"and", "or", "not", "in", "of"
)

for kw in keywords:
	pre_tokens.append(kw.upper())
	exec(f"t_{kw.upper()} = r\"\\b{kw}\\b\"")
tokens = tuple(pre_tokens)

kw_regex = r"\b(?:" + "|".join(map(re.escape, keywords)) + r")\b"
no_kw_pattern = f"(?!{kw_regex})"

t_DECIMAL = r"\b(0[xX][0-9a-fA-F]*\.[0-9a-fA-F]+|0[dD][0-9]*\.[0-9]+|0[oO][0-7]*\.[0-7]+|0[bB][01]*\.[01]+|[0-9]*\.[0-9]+)\b"
t_INTEGER = r"\b(0[xX][0-9a-fA-F]+|0[dD][0-9]+|0[oO][0-7]+|0[bB][01]+|[1-9][0-9]*|0)\b"
t_STRING = r"\"(?:\\\"|[^\"])*\"|\'(?:\\\'|[^\'])*\'"
t_LPAREN = r"\("
t_RPAREN = r"\)"
t_LBRACKET = r"\["
t_RBRACKET = r"\]"
t_LBRACE = r"\{"
t_RBRACE = r"\}"
t_COMMA = r"\,"
t_COLON = r"\:"
t_SEMICOLON = r"\;"
t_DOT = r"\."
t_PLUS = r"\+"
t_MINUS = r"\-"
t_TIMES = r"\*"
t_DIVIDE = r"/"
t_MODULO = r"\%"
t_EQUALS = r"="
t_COMMENT = r"//.*"

exec(f"t_IDENTIFIER = r\"{no_kw_pattern}([a-zA-Z_][a-zA-Z0-9_]*)\"")

states = (
	("multicomment", "exclusive"),
)

def t_multicomment_start(t):
	r"/\*"
	t.lexer.begin("multicomment")

# Comment end token
def t_multicomment_end(t):
	r"\*/"
	t.lexer.lineno += t.value.count("\n")
	t.lexer.begin("INITIAL")

def t_multicomment_error(t):
	print("Multi-Comment Illegal character '%s'" % t.value[0])
	t.lexer.skip(1)

t_multicomment_ignore = " "

def t_newline(t):
	r"\n+"
	t.lexer.lineno += len(t.value)

t_ignore  = " \t"


# Error handling rule
def t_error(t):
	print("Illegal character '%s'" % t.value[0])
	t.lexer.skip(1)

lexer:lex.Lexer = lex.lex(optimize=True, lextab="PycpTokensTable")

if __name__ == "__main__":
	lexer.input(
	"""
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
	""")
	while True:
		tok = lexer.token()
		if not tok:
			break
		if tok.type in ("INTEGER", "DECIMAL"):
			print(tok)
		