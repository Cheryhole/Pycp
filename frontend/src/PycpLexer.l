%{
#include <iostream>
#include <string>
#include "PycpAstNode.hpp"
#include "PycpParser.hpp"

using namespace Pycp::Ast;

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

func { return KW_FUNC; }
return { return KW_RETURN; }
if    { return KW_IF; }
elif  { return KW_ELIF; }
else  { return KW_ELSE; }
None  { return KW_NONE; }

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
	std::cerr << "Unterminated comment.\n";
	BEGIN(INITIAL);
	return 0;
}

"+"    { return OP_PLUS; }
"-"    { return OP_MINUS; }
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

\"   { 
	_string_buffer.clear();
	BEGIN(STRING);
}
<STRING>\" {
	Pycplval.text = new std::string(_string_buffer);
	BEGIN(INITIAL);
	return LT_STRING;
}

<STRING>\\. {
	_string_buffer += Pycptext;
}

<STRING>[^\\\"\n]+ {
	_string_buffer += Pycptext;
}

<STRING>\n {
	std::cerr << "Unterminated string.\n";
	BEGIN(INITIAL);
}

\n        { return NEWLINE; }
\r\n     { return NEWLINE; }
\r       { return NEWLINE; }

[ \t]     { /* 忽略水平空白字符 */ }
.        { 
	std::cerr << "Unexpected character: " << Pycptext << std::endl;
	// 可以选择返回一个错误标记，或者直接忽略
	// return ERROR;
}

%%

