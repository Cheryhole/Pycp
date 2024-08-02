%{
#include <cstdio>
#include <cstdlib>
#include "Pycp.hpp"
#include "PycpParser.h" // 包含生成的头文件

void yyerror(const char *s);
%}

%union {
	PycpObject* object;
	char* identifer;
}

%token INTEGER
%token STRING
%token DECIMAL
%token BOOLEAN
%token NONE
%token IDENTIFIER

%token SEMICOLON
%token DOT OF
%token COMMA
%token PLUS MINUS TIMES DIVIDE
%token RPAREN LPAREN
%token LBRACE RBRACE

%left PLUS MINUS
%left TIMES DIVIDE

%start program

%%

program:
	| program statement
;

statement:
	| statement SEMICOLON statement
	| statement expression SEMICOLON
	| expression SEMICOLON
;

%type <object> INTEGER STRING DECIMAL BOOLEAN NONE IDENTIFIER
%type <object> expression

expression:
	| INTEGER { $$ = yylval; }
	| STRING { $$ = yylval; }
	| DECIMAL { $$ = yylval; }
	| BOOLEAN { $$ = yylval; }
	| NONE { $$ = yylval; }
	| IDENTIFIER { $$ = yylval.identifer; }
	| LPAREN expression RPAREN
	| expression PLUS expression
	| expression MINUS expression
	| expression TIMES expression
	| expression DIVIDE expression
;

%destructor {
	delete $$;
} INTEGER STRING DECIMAL BOOLEAN

%%

void yyerror(const char *s){
	fprintf(stderr, "Error: %s\n", s);
}

int main() {
	printf("Enter an expression: ");
	yyparse();
	return 0;
}
