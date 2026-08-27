%{
#include <iostream>
#include <string>
#include "PycpAstNode.hpp"
#include "PycpParser.hpp"
#include "escape_handler.hpp"

using namespace Pycp::Ast;

// 当前源文件路径（定义于 PycpParser.y，由 parsef 设置）。
// 词法错误诊断据此输出 File "<file>", line <lineno> 两行格式。
extern std::string g_current_source_path;

// 词法错误标志（定义于 PycpParser.y）。lexer 出错后置位并停止扫描。
extern bool g_lexer_error;

// bison %locations 支持（PycpParser.y 已声明 %locations）：
// 每个 token 匹配完成后、动作执行前，把当前行号写入全局位置 Pycpylloc。
// 单行 token 的 first_line == last_line == 该 token 所在行；bison 侧语义动作
// 通过 @$.first_line 取得规则首符号的起始行，不受归约时刻 lookahead 影响。
extern PYCPLTYPE Pycplloc;
#define YY_USER_ACTION \
	Pycplloc.first_line = Pycplineno; \
	Pycplloc.last_line = Pycplineno; \
	Pycplloc.first_column = 0; \
	Pycplloc.last_column = 0;

std::string _string_buffer; // 用于存储字符串内容
int _comment_return_state = 0; // 记录进入块注释前的状态（0=INITIAL），注释结束后恢复

// 行指令（预处理产物，形如 "// __pycp_pp# <lineno> \"<filename>\""，C #line 语义）
// 解析辅助状态：
//   _line_directive_line  : 指令所在行的真实行号（诊断用）
//   _line_directive_lineno: 指令指定的目标行号（其后第一个 token 从此行号开始计数）
//   _line_directive_has_num/_line_directive_has_file : 行号/文件名参数是否齐全
//   _line_ret_state : 非 0 表示当前 STRING 是行指令的文件名参数，收尾时不产生 token
int _line_directive_line = 0;
int _line_directive_lineno = 0;
int _line_directive_has_num = 0;
int _line_directive_has_file = 0;
int _line_ret_state = 0;

// 括号嵌套深度与"表达式上下文"标志（list/map 跨行支持）：
//   brk_depth   : 当前处于多少个未闭合的 ( [ { 之内。>0 时换行被抑制（隐式行续接）。
//   brk_expr_ctx: 仅用于 INITIAL 状态下区分 { 是 map literal（紧跟表达式）还是
//                 code_block（紧跟语句）。map 起始 { 进入 BRACKET_BODY 抑制换行，
//                 code_block 的 { 保持原 EXPECT_ELIF 流程（顶层换行仍为语句分隔符）。
int brk_depth = 0;
int brk_expr_ctx = 0;
%}

%option yylineno
%option noyywrap

%option prefix="Pycp"

%x STRING
%x EXPECT_ELIF
%x COMMENT
%x MEMBER_ACCESS
%s BRACKET_BODY

%%

<BRACKET_BODY>"{"	{ brk_depth++; return OP_LBRACE; }
<BRACKET_BODY>"}"	{ brk_depth--; if (brk_depth == 0) { BEGIN(INITIAL); brk_expr_ctx = 0; } return OP_RBRACE; }
<BRACKET_BODY>"["	{ brk_depth++; return OP_LBRACKET; }
<BRACKET_BODY>"]"	{ brk_depth--; if (brk_depth == 0) { BEGIN(INITIAL); brk_expr_ctx = 0; } return OP_RBRACKET; }
<BRACKET_BODY>"("	{ brk_depth++; return OP_LPARENTHESES; }
<BRACKET_BODY>")"	{ brk_depth--; if (brk_depth == 0) { BEGIN(INITIAL); brk_expr_ctx = 0; } return OP_RPARENTHESES; }
<BRACKET_BODY>\n	{ /* 括号内换行被忽略：实现 list/map 跨行定义（Python 风格隐式行续接） */ }
<BRACKET_BODY>\r\n	{ /* 忽略换行 */ }
<BRACKET_BODY>\r	{ /* 忽略换行 */ }
<BRACKET_BODY>"//"[^\n]*	{ /* 忽略单行注释 */ }
<BRACKET_BODY>"/*"	{ _comment_return_state = BRACKET_BODY; BEGIN(COMMENT); }
<BRACKET_BODY>[ \t]+	{ /* 忽略空白 */ }
<BRACKET_BODY>":"	{ return OP_COLON; }

func     { return KW_FUNC; }
return   { brk_expr_ctx = 1; return KW_RETURN; }
if       { brk_expr_ctx = 1; return KW_IF; }
elif     { brk_expr_ctx = 1; return KW_ELIF; }
else     { return KW_ELSE; }
None     { brk_expr_ctx = 0; return KW_NONE; }
True     { brk_expr_ctx = 0; return KW_TRUE; }
False    { brk_expr_ctx = 0; return KW_FALSE; }
import   { return KW_IMPORT; }
from     { return KW_FROM; }
as       { return KW_AS; }
class    { return KW_CLASS; }
inherits { return KW_INHERITS; }
repeat   { return KW_REPEAT; }
to       { return KW_TO; }
break    { return KW_BREAK; }
delete   { return KW_DELETE; }
by       { return KW_BY; }
for      { return KW_FOR; }
in       { brk_expr_ctx = 1; return KW_IN; }

[a-zA-Z_][a-zA-Z0-9_]* { // identifier
	Pycplval.text = new std::string(Pycptext);
	brk_expr_ctx = 0;
	return IDENTIFIER;
}

[0-9]+ { // integer
	Pycplval.text = new std::string(Pycptext); 
	brk_expr_ctx = 0;
	return LT_INTEGER;
}
<*>"//"[ \t]*"__pycp_pp#"[^\n]*\n	{ /* line directive: // __pycp_pp# <lineno> "<file>"; consume whole line, set Pycplineno after '\n' auto-increment. <*> so it works after '}' (EXPECT_ELIF) etc. */
	const char* p = Pycptext + 2;   /* 跳过 "//" */
	while (*p == ' ' || *p == '\t') ++p;   /* 跳过 "//" 后空白 */
	p += 10;   /* 越过 "__pycp_pp#" 标记 */
	while (*p == ' ' || *p == '\t') ++p;   /* 跳过标记后空白 */
	int n = 0; bool ok = false;
	std::string file;
	while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; ok = true; }
	while (*p == ' ' || *p == '\t') ++p;   /* 跳过行号后空白 */
	if (*p == '"') {
		++p;
		while (*p && *p != '"' && *p != '\n') {
			if (*p == '\\' && p[1]) { ++p; }   /* 处理转义（\" 与 \\） */
			file += *p;
			++p;
		}
		if (ok) {
			Pycplineno = n;
			g_current_source_path = file;
		}
	}
	/* 整行（含换行）已消费，不产生 token。 */
}
<*>"//"[^\n]*	{ /* 单行（行内）注释：跳过到行尾，换行仍由 NEWLINE 规则处理 */ }
"/*"	{ _comment_return_state = INITIAL; BEGIN(COMMENT); }
<COMMENT>"*/"	{ BEGIN(_comment_return_state); }
<COMMENT>([^*]|\n)+	{ /* 吞掉块注释内容（含换行） */ }
<COMMENT>"*"+[^*/]	{ /* 吞掉块注释内的星号序列 */ }
<COMMENT>"*"+	{ /* 吞掉注释结尾前的星号 */ }
<COMMENT><<EOF>>	{
	std::cerr << "File \"" << g_current_source_path << "\", line " << Pycplineno << "\nUnterminated comment." << std::endl;
	g_lexer_error = true;
	return 0;
}

"+"    { brk_expr_ctx = 1; return OP_PLUS; }
"-"    { brk_expr_ctx = 1; return OP_MINUS; }
"**"   { brk_expr_ctx = 1; return OP_POWER; }
"*"    { brk_expr_ctx = 1; return OP_MULTIPLY; }
"/"    { brk_expr_ctx = 1; return OP_DIVIDE; }
"("    { brk_depth = 1; brk_expr_ctx = 1; BEGIN(BRACKET_BODY); return OP_LPARENTHESES; }
")"    { return OP_RPARENTHESES; }
"["    { brk_depth = 1; brk_expr_ctx = 0; BEGIN(BRACKET_BODY); return OP_LBRACKET; }
"]"    { return OP_RBRACKET; }
"{"    { if (brk_expr_ctx != 0) { brk_depth = 1; brk_expr_ctx = 0; BEGIN(BRACKET_BODY); } return OP_LBRACE; }
"}"    { BEGIN(EXPECT_ELIF); return OP_RBRACE; }

<EXPECT_ELIF>"elif"/[^a-zA-Z0-9_]	{ BEGIN(INITIAL); return KW_ELIF; }
<EXPECT_ELIF>"else"/[^a-zA-Z0-9_]	{ BEGIN(INITIAL); return KW_ELSE; }
<EXPECT_ELIF>"/*"	{ _comment_return_state = EXPECT_ELIF; BEGIN(COMMENT); }
<EXPECT_ELIF>\n	{ /* 忽略换行 */ }
<EXPECT_ELIF>\r\n	{ /* 忽略换行 */ }
<EXPECT_ELIF>\r	{ /* 忽略换行 */ }
<EXPECT_ELIF>[ \t]+	{ /* 忽略空白 */ }
<EXPECT_ELIF>[a-zA-Z_][a-zA-Z0-9_]*	{ BEGIN(INITIAL); yyless(0); return NEWLINE; }
<EXPECT_ELIF>.	{ BEGIN(INITIAL); unput(Pycptext[0]); return NEWLINE; }
<EXPECT_ELIF><<EOF>>	{ BEGIN(INITIAL); return NEWLINE; }
"="    { brk_expr_ctx = 1; return OP_EQUALS; }
"=="   { brk_expr_ctx = 1; return OP_EQ; }
"!="   { brk_expr_ctx = 1; return OP_NE; }
"<"    { brk_expr_ctx = 1; return OP_LT; }
">"    { brk_expr_ctx = 1; return OP_GT; }
"<="   { brk_expr_ctx = 1; return OP_LE; }
">="   { brk_expr_ctx = 1; return OP_GE; }
","    { brk_expr_ctx = 1; return OP_COMMA; }
":"    { return OP_COLON; }
"."    { BEGIN(MEMBER_ACCESS); return OP_DOT; }
"@"    { return OP_AT; }

<MEMBER_ACCESS>[a-zA-Z_][a-zA-Z0-9_]* { Pycplval.text = new std::string(Pycptext); brk_expr_ctx = 0; BEGIN(brk_depth > 0 ? BRACKET_BODY : INITIAL); return IDENTIFIER; }
<MEMBER_ACCESS>[^a-zA-Z0-9_] { BEGIN(brk_depth > 0 ? BRACKET_BODY : INITIAL); yyless(0); }

\"   { 
	_string_buffer.clear();
	BEGIN(STRING);
}
<STRING>\" {
	if (_line_ret_state) {
		// 行指令的文件名参数：采用解码后的字符串作为新文件名，不产生 token。
		g_current_source_path = _string_buffer;
		_line_directive_has_file = 1;
		_line_ret_state = 0;
		BEGIN(INITIAL);
	} else {
		Pycplval.text = new std::string(_string_buffer);
		BEGIN(brk_depth > 0 ? BRACKET_BODY : INITIAL);
		brk_expr_ctx = 0;
		return LT_STRING;
	}
}

<STRING>\\u[0-9a-fA-F]{4} {
	// Unicode 转义 \uXXXX：剥离前导 '\' 后交给 escape_handler 解码（得到 "uXXXX"）。
	_string_buffer += Pycp::Lexer::decode_escape(std::string(Pycptext + 1));
}

<STRING>\\x[0-9a-fA-F]{2} {
	// 十六进制字节转义 \xHH：剥离前导 '\' 后交给 escape_handler 解码（得到 "xHH"）。
	_string_buffer += Pycp::Lexer::decode_escape(std::string(Pycptext + 1));
}

<STRING>\\. {
	// 常见转义（\n \t \r \0 \\ \" 等）。Pycptext 形如 "\n"（含前导反斜杠）。
	// 剥离前导 '\' 后交给 escape_handler 解码，追加解码结果到缓冲区。
	std::string seq(Pycptext + 1);
	_string_buffer += Pycp::Lexer::decode_escape(seq);
}

<STRING>[^\\\"\n]+ {
	_string_buffer += Pycptext;
}

<STRING>\n {
	std::cerr << "File \"" << g_current_source_path 
		<< "\", line " << Pycplineno << "\nUnterminated string." << std::endl;
	g_lexer_error = true;
	return 0;
}

\n        { return NEWLINE; }
\r\n     { return NEWLINE; }
\r       { return NEWLINE; }

[ \t]     { /* 忽略水平空白字符 */ }
.        { 
	std::cerr << "File \"" << g_current_source_path 
		<< "\", line " << Pycplineno << "\nUnexpected character: " << Pycptext << std::endl;
	g_lexer_error = true;
	return 0;
}

%%

