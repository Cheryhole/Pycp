import ply.lex
from objprint import objprint as op

reserved_keywords: list[str] = [
	"func", # 函数
	"return", # 返回
	"if", # 如果
	"elif", # 否则如果
	"else", # 否则
	"None", # 空值
]

states = (
	("string", "exclusive"),  # 专属状态，用于字符串内部
)

# Defining tokens
tokens: list[str] = [
	"IDENTIFIER", # 标识符
	"NEWLINE", # 换行符

	# Operators (OP)
	"OP_PLUS", # 加号
	"OP_MINUS", # 减号
	"OP_MULTIPLY", # 乘号
	"OP_DIVIDE", # 除号
	"OP_LPARENTHESES", # 左小括号
	"OP_RPARENTHESES", # 右小括号
	"OP_LBRACKET", # 左中括号
	"OP_RBRACKET", # 右中括号
	"OP_LBRACE", # 左大括号
	"OP_RBRACE", # 右大括号
	"OP_EQUALS", # 等号
	"OP_COMMA", # 逗号

	# Literals (LT)
	"LT_INTEGER", # 整数
	"LT_STRING", # 字符串
] + \
	[f"KW_{kw.upper()}" for kw in reserved_keywords] # Reserved keywords 

t_OP_PLUS = r"\+"
t_OP_MINUS = r"-"
t_OP_MULTIPLY = r"\*"
t_OP_DIVIDE = r"/"
t_OP_LPARENTHESES = r"\("
t_OP_RPARENTHESES = r"\)"
t_OP_LBRACKET = r"\["
t_OP_RBRACKET = r"\]"
t_OP_LBRACE = r"\{"
t_OP_RBRACE = r"\}"
t_OP_EQUALS = r"="
t_OP_COMMA = r","

t_LT_INTEGER = r"\d+"

t_ignore = " \t\r"

# 关键字与标识符一起处理，
# 即将reserved_keywords中标识符视作关键字
def t_IDENTIFIER(t):
	r"[a-zA-Z_][a-zA-Z_0-9]*"
	if t.value in reserved_keywords:
		t.type = f"KW_{t.value.upper()}"
	else:
		t.type = "IDENTIFIER"
	return t

def t_NEWLINE(t):
	r"\n+"
	t.lexer.lineno += len(t.value)
	return t

def t_error(t):
	print(f"Illegal character '{t.value[0]}' at line {t.lineno}")
	t.lexer.skip(1)


t_string_ignore = ""  # 字符串状态下不忽略字符

# 普通状态下检测字符串起始
def t_INITIAL_STRING_START(t):
	"\""
	t.lexer.string_buf = ""  # 临时缓冲字符串内容
	t.lexer.push_state("string")  # 进入字符串状态

# 字符串状态下的规则
def t_string_content(t):
	"([^\\\\\"]|\\.)+"  # 匹配非引号或转义字符
	t.lexer.string_buf += t.value

# 字符串状态下遇到结束引号
def t_string_STRING_END(t):
	"\""
	s = t.lexer.string_buf
	s = s.encode().decode("unicode_escape")  # 转义字符转义
	t.value = s
	t.type = "LT_STRING"
	t.lexer.pop_state()  # 回到普通状态
	return t

# 字符串状态下换行也算内容
def t_string_newline(t):
	r"\n"
	t.lexer.lineno += 1
	t.lexer.string_buf += "\n"

# 错误处理
def t_string_error(t):
	# 遇到非法字符就加入内容
	t.lexer.string_buf += t.value[0]
	t.lexer.skip(1)

lexer = ply.lex.lex()

if __name__ == "__main__":
	with open("test.pycp", "r") as f:
		lexer.input(f.read())

	while True:
		tok = lexer.token()
		if not tok:
			break
		print(tok)
