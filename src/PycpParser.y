%{
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include "Pycp.hpp"
#include "PycpParser.h" // 包含生成的头文件

void yyerror(const char *s);
%}

%union {
	PycpObject* object;
	std::string_view identifer;
}

%type YYSTYPE
%token <intValue> INTEGER
%token <strValue> STRING
%token <decValue> DECIMAL
%token <boolValue> BOOLEAN
%token NONE
%token <identifier> IDENTIFIER

%left '+' '-'
%left '*' '/'

%start program

%%

program
	:
	| program statement
	;

statement
	:
	| statement statement
	| statement expression ';'
	| expression ';'
	;

%type <objtype> expression

expression
	:
	| INTEGER {
		$$ = $1.intValue;
	}
	| STRING
	| DECIMAL
	| BOOLEAN
	| NONE
	| IDENTIFIER
	| '(' expression ')'
	| expression '+' expression
	| expression '-' expression
	| expression '*' expression
	| expression '/' expression

%%

void yyerror(const char *s){
	fprintf(stderr, "Error: %s\n", s);
}

int main() {
	printf("Enter an expression: ");
	yyparse();
	return 0;
}
