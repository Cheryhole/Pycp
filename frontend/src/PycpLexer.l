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

std::string _string_buffer; // 用于存储字符串内容
int _comment_return_state = 0; // 记录进入块注释前的状态（0=INITIAL），注释结束后恢复
%}

%option yylineno
%option noyywrap

%option prefix="Pycp"

%x STRING
%x EXPECT_ELIF
%x COMMENT

%%

func     { return KW_FUNC; }
return   { return KW_RETURN; }
if       { return KW_IF; }
elif     { return KW_ELIF; }
else     { return KW_ELSE; }
None     { return KW_NONE; }
import   { return KW_IMPORT; }
from     { return KW_FROM; }
as       { return KW_AS; }
class    { return KW_CLASS; }
inherits { return KW_INHERITS; }

[a-zA-Z_][a-zA-Z0-9_]* { // identifier
	Pycplval.text = new std::string(Pycptext);
	return IDENTIFIER;
}

[0-9]+ { // integer
	Pycplval.text = new std::string(Pycptext); 
	return LT_INTEGER;
}
"//"[^\n]*	{ /* 单行注释：跳过到行尾，换行仍由 NEWLINE 规则处理 */ }
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

"+"    { return OP_PLUS; }
"-"    { return OP_MINUS; }
"**"   { return OP_POWER; }
"*"    { return OP_MULTIPLY; }
"/"    { return OP_DIVIDE; }
"("    { return OP_LPARENTHESES; }
")"    { return OP_RPARENTHESES; }
"["    { return OP_LBRACKET; }
"]"    { return OP_RBRACKET; }
"{"    { return OP_LBRACE; }
"}"    { BEGIN(EXPECT_ELIF); return OP_RBRACE; }

<EXPECT_ELIF>"elif"/[^a-zA-Z0-9_]	{ BEGIN(INITIAL); return KW_ELIF; }
<EXPECT_ELIF>"else"/[^a-zA-Z0-9_]	{ BEGIN(INITIAL); return KW_ELSE; }
<EXPECT_ELIF>"//"[^\n]*	{ /* 单行注释：跳过到行尾 */ }
<EXPECT_ELIF>"/*"	{ _comment_return_state = EXPECT_ELIF; BEGIN(COMMENT); }
<EXPECT_ELIF>\n	{ /* 忽略换行 */ }
<EXPECT_ELIF>\r\n	{ /* 忽略换行 */ }
<EXPECT_ELIF>\r	{ /* 忽略换行 */ }
<EXPECT_ELIF>[ \t]+	{ /* 忽略空白 */ }
<EXPECT_ELIF>[a-zA-Z_][a-zA-Z0-9_]*	{ BEGIN(INITIAL); yyless(0); return NEWLINE; }
<EXPECT_ELIF>.	{ BEGIN(INITIAL); unput(Pycptext[0]); return NEWLINE; }
<EXPECT_ELIF><<EOF>>	{ BEGIN(INITIAL); return NEWLINE; }
"="    { return OP_EQUALS; }
"=="   { return OP_EQ; }
"!="   { return OP_NE; }
"<"    { return OP_LT; }
">"    { return OP_GT; }
"<="   { return OP_LE; }
">="   { return OP_GE; }
","    { return OP_COMMA; }
"."    { return OP_DOT; }
"@"    { return OP_AT; }

\"   { 
	_string_buffer.clear();
	BEGIN(STRING);
}
<STRING>\" {
	Pycplval.text = new std::string(_string_buffer);
	BEGIN(INITIAL);
	return LT_STRING;
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

