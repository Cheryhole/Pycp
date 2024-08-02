%{
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
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

True|False {
	if (yytext == "True"){
		yylval.object = new Pycp::PycpBoolean(true);
	}
	else if (yytext == "False"){
		yylval.object = new Pycp::PycpBoolean(false);
	}

	return BOOLEAN; 
}

[a-zA-Z_][a-zA-Z0-9_]* {
	yylval.identifer = yytext;
	return IDENTIFIER;
}

None {
	yylval.object = Pycp::PycpNoneRef;
	return NONE;
}


[ \t\n]+    { }
.           { return yytext[0]; }

%%

