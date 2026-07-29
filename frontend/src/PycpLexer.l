%{
#include <string>
#include "PycpAstNode.hpp"
#include "PycpParser.hpp"

using namespace Pycp::Ast;
%}

%option yylineno
%option noyywrap
%option nounput

%option prefix="Pycp"

%%

[a-zA-Z_][a-zA-Z0-9_]* { // identifier
	Pycplval.text = new std::string(Pycptext);
	return IDENTIFIER;
}

[0-9]+ { // integer
	Pycplval.text = new std::string(Pycptext); 
	return LT_INTEGER;
}
"+"    { return OP_PLUS; }
"-"    { return OP_MINUS; }
"*"    { return OP_MULTIPLY; }
"/"    { return OP_DIVIDE; }
"("    { return OP_LPARENTHESES; }
")"    { return OP_RPARENTHESES; }
"="    { return OP_EQUALS; }
\n+     { return NEWLINE; }
[ \t\r]  { /* 忽略空白字符 */ }
.      { printf("未知字符: %c\n", *yytext); }

%%

