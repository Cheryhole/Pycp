%{
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include "Pycp.hpp"
#include "PycpParser.h"

%}

%option yylineno
%option noyywrap

%%

\"[^"]*\" { 
	yylval.object = new PycpString(yytext); 
	return STRING; 
}

\'[^']*\' { 
	yylval.object = new PycpString(yytext); 
	return STRING;
}

[+-]?[0-9]+ {
	int64_t i = atoll(yytext);
	yylval.object = new PycpInteger(i);
	return INTEGER;
}

[+-]?([0-9]*\.[0-9]+)([eE][+-]?[0-9]+)? {
	yylval.object = new PycpDecimal(yytext);
	return DECIMAL;
}

"True" | "False" {
	if (strcmp(yytext, "True") == 0){
		yylval.object = new Pycp::PycpBoolean(true);
	}
	else if (strcmp(yytext, "False") == 0){
		yylval.object = new Pycp::PycpBoolean(false);
	}

	return BOOLEAN; 
}

[a-zA-Z_][a-zA-Z0-9_]* {
	yylval.identifer = yytext;
	return IDENTIFIER;
}

"None" {
	yylval.object = Pycp::PycpNoneRef;
	return NONE;
}

[ \t\n]+ { }
. { return yytext[0]; }
"//"[^\n]* ;

";"          { return SEMICOLON; }
"("          { return LPAREN; }
")"          { return RPAREN; }
"{"          { return LBRACE; }
"}"          { return RBRACE; }
"+"          { return PLUS; }
"-"          { return MINUS; }
"*"          { return TIMES; }
"/"          { return DIVIDE; }
"of"         { return OF; }
"."          { return DOT; }
","          { return COMMA; }


%%

