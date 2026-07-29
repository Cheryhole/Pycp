%{
#include <iostream>
#include "PycpAstNode.hpp"
#include "PycpLexer.hpp"

using namespace Pycp::Ast;

//Pycp::Ast::Node* _final_asttree;

void Pycperror(Pycp::Ast::Node*&, const char *);
extern int Pycplex();
extern YY_BUFFER_STATE Pycp_scan_string(const char*);
extern void Pycp_delete_buffer(YY_BUFFER_STATE);
%}

%union {
	std::string* text; // for Lexer

	Pycp::Ast::Node* node; // for Parser
	std::vector<Pycp::Ast::Statement*>* statements;
}

%define api.prefix {Pycp}
%parse-param { Pycp::Ast::Node*& _final_asttree }

%token NEWLINE
%token <text> LT_INTEGER IDENTIFIER
%token OP_PLUS OP_MINUS OP_MULTIPLY OP_DIVIDE
%token OP_LPARENTHESES OP_RPARENTHESES
%token OP_EQUALS

%left OP_PLUS OP_MINUS
%left OP_MULTIPLY OP_DIVIDE
%right UMINUS
%left OP_LPARENTHESES OP_RPARENTHESES

%type <node> program
%type <statements> statement_list
%type <node> statement
%type <node> assignment_statement
%type <node> assignment_object

%type <node> expression
%type <node> additive_expression
%type <node> multiplicative_expression
%type <node> unary_expression
%type <node> primary_expression

%%

program: statement_list {
		$$ = new Program(
			static_cast<std::vector<Statement*>*>($1)
		);
		_final_asttree = static_cast<Node*>($$);
	}
	| statement_list statement {
		$1->push_back(static_cast<Statement*>($2));

		$$ = new Program(
			static_cast<std::vector<Statement*>*>($1)
		);
		_final_asttree = static_cast<Node*>($$);
	}
;

statement_list: %empty {
			$$ = new std::vector<Statement*>();
		}
		| statement_list NEWLINE {
			$$ = $1;
		}
		| statement_list statement NEWLINE {
			static_cast<std::vector<Statement*>*>($1)
				->push_back(static_cast<Statement*>($2));

			$$ = $1;
		}
statement: assignment_statement {
			$$ = $1;
		}
		| expression {
			$$ = new ExpressionStatement(
				static_cast<Expression*>($1)
			);
		}
;

assignment_statement: assignment_object OP_EQUALS expression {
			$$ = new AssignmentStatement(
				static_cast<Expression*>($1),
				static_cast<Expression*>($3)
			);
		}
;

assignment_object : IDENTIFIER {
		$$ = new IdentifierExpression(*($1));
		delete $1;
	}
;

expression: additive_expression
;

additive_expression: multiplicative_expression
    | additive_expression OP_PLUS multiplicative_expression {
			$$ = new BinaryExpression(
				BinaryOp::PLUS,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3)
			);
		}
    | additive_expression OP_MINUS multiplicative_expression {
			$$ = new BinaryExpression(
				BinaryOp::MINUS,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3)
			);
		}
    ;

multiplicative_expression: unary_expression
    | multiplicative_expression OP_MULTIPLY unary_expression {
			$$ = new BinaryExpression(
				BinaryOp::MULTIPLY,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3)
			);
		}
    | multiplicative_expression OP_DIVIDE unary_expression {
			$$ = new BinaryExpression(
				BinaryOp::DIVIDE,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3)
			);
		}
    ;

unary_expression: primary_expression
		| OP_MINUS primary_expression %prec UMINUS {
			$$ = new UnaryExpression(
				UnaryOp::UMINUS,
				static_cast<Expression*>($2)
			);
		};

primary_expression: LT_INTEGER {
			$$ = new IntegerLiteral(*($1));
			delete $1;
		}
		| OP_LPARENTHESES expression OP_RPARENTHESES {
			$$ = $2;
		}
		| IDENTIFIER {
			$$ = new IdentifierExpression(*($1));
			delete $1;
		}
		;

%%

void Pycperror(Node*& _, const char *s) {
	std::cerr << "Error: " << s << std::endl;
}

Node* parse(const std::string& text){
	Node* _final_asttree = nullptr;

	YY_BUFFER_STATE buffer = Pycp_scan_string(text.c_str());

	Pycpparse(_final_asttree);

	Pycp_delete_buffer(buffer);

	return _final_asttree;
}

int main() {
	std::string code = "a = 1 + 2 * 3\nb = a + 4";
	
	Node* asttree = parse(code);
	if (!asttree) {
		std::cerr << "Error: No asttree" << std::endl;
		return 1;
	}

	std::cout << asttree->to_string() << std::endl;
	return 0;
}

