%{
#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include "PycpAstNode.hpp"
#include "PycpLexer.hpp"

using namespace Pycp::Ast;


//Pycp::Ast::Node* _final_asttree;

// 当前正在解析的源文件路径（供词法/语法错误输出 File "<file>", line <lineno> 两行格式）。
// 由 parsef(path) 在解析前设置，Pycperror 与 lexer 的诊断据此输出文件路径。
std::string g_current_source_path;

// 词法错误标志：lexer 遇到错误时置为 true 并立即停止扫描；
// parse() 据此返回 nullptr 以中止解析。每次 parse 前由 parse() 重置。
bool g_lexer_error = false;

void Pycperror(Pycp::Ast::Node*&, const char *);
extern int Pycplex();
extern int Pycplineno;
extern YY_BUFFER_STATE Pycp_scan_string(const char*);
extern void Pycp_delete_buffer(YY_BUFFER_STATE);
%}

%union {
	std::string* text; // for Lexer

	Pycp::Ast::Node* node; // for Parser
	Pycp::Ast::IfBranch* if_branch;            // single if/elif branch
	std::vector<Pycp::Ast::IfBranch*>* if_branches; // elif branch list
	std::vector<Pycp::Ast::Statement*>* statements;
	std::vector<std::string>* identifiers;
	std::vector<std::string*>* string_ptrs;   // parameter names (owning pointers)
	std::vector<Pycp::Ast::Expression*>* expressions; // call arguments
	// if 语句可选后缀：elif 分支列表 + 可选 else 代码块
	IfSuffix if_suffix;
}

%define api.prefix {Pycp}
%parse-param { Pycp::Ast::Node*& _final_asttree }

// 该块内容会注入生成的头文件，确保 IfSuffix 在 union 与所有包含
// PycpParser.hpp 的翻译单元中均可见。
%code requires {
	#include <vector>
	#include "PycpAstNode.hpp"
	// if 语句的可选后缀：承载 elif 分支列表与可选的 else 代码块。
	// 仅含裸指针，平凡可拷贝，可直接作为 Bison %union 成员。
	struct IfSuffix {
		std::vector<Pycp::Ast::IfBranch*>* elif_branches;
		Pycp::Ast::Program* else_body;
	};
}

%token NEWLINE
%token <text> LT_INTEGER LT_STRING IDENTIFIER
%token KW_FUNC KW_RETURN KW_IF KW_ELIF KW_ELSE KW_NONE
%token OP_PLUS OP_MINUS OP_MULTIPLY OP_DIVIDE OP_POWER
%token OP_LPARENTHESES OP_RPARENTHESES
%token OP_LBRACKET OP_RBRACKET
%token OP_LBRACE OP_RBRACE
%token OP_EQUALS OP_COMMA
%token OP_LT OP_GT OP_LE OP_GE OP_EQ OP_NE

%left OP_PLUS OP_MINUS
%left OP_MULTIPLY OP_DIVIDE
%right OP_POWER
%right UMINUS
%left OP_LPARENTHESES OP_RPARENTHESES
%nonassoc OP_LT OP_GT OP_LE OP_GE OP_EQ OP_NE

%type <node> program 
%type <statements> statements
%type <node> statement
%type <node> return_statement
%type <node> assignment_statement
%type <node> assignment_object

%type <node> function_def_statement
%type <node> function_expr
%type <string_ptrs> parameter_list
%type <statements> code_block
%type <expressions> arguments

%type <node> if_statement
%type <if_branches> elif_clauses
%type <if_suffix> opt_elif_else

%type <node> expression
%type <node> comparison_expression
%type <node> additive_expression
%type <node> multiplicative_expression
%type <node> unary_expression
%type <node> power_expression
%type <node> primary_expression

%start program

%%

program: statements {
			$$ = new Program(
				static_cast<std::vector<Statement*>*>($1)
			);
			_final_asttree = static_cast<Node*>($$);
		}
		| newlines statements {
			$$ = new Program(
				static_cast<std::vector<Statement*>*>($2)
			);
			_final_asttree = static_cast<Node*>($$);
		}
		| statements newlines {
			$$ = new Program(
				static_cast<std::vector<Statement*>*>($1)
			);
			_final_asttree = static_cast<Node*>($$);
		}
		| newlines statements newlines {
			$$ = new Program(
				static_cast<std::vector<Statement*>*>($2)
			);
			_final_asttree = static_cast<Node*>($$);
		}
;

// 语句列表：首条语句直接书写，其后每条语句必须由至少一个换行符分隔。
// 不提供“无换行直接续接”的推导，从而强制换行作为唯一分隔符；
// 同一行书写多条语句（缺少 NEWLINE）将无可推导路径，触发语法错误。
// 换行语义由词法器的 NEWLINE（\n / \r\n / \r）统一产出，横向空白 [ \t ] 被忽略，
// 因此换行是语句之间唯一的边界标记，空行（连续 NEWLINE）在此被压缩合并，不入 AST。
statements: statement {
				$$ = new std::vector<Statement*>();
				$$->push_back(static_cast<Statement*>($1));
		}
		| statements newlines statement {
				$1->push_back(static_cast<Statement*>($3));
				$$ = $1;
		}
		// 错误恢复：捕获“同一行无换行续接”的非法写法（如 a = 1 b = 2）。
		// 恢复锚点强制为下一个 NEWLINE —— 即跳过违规 token 直到遇到换行。
		// 以 NEWLINE 作为恢复终止条件可消除结尾 $end 处的 shift/reduce 歧义，
		// 避免 Bison 在文件末尾错误地处理 error。
		// 两个分支分别处理两种情形：
		//   (a) statements error NEWLINE
		//         同行非法且其后已无更多语句，丢弃违规片段并结束。
		//   (b) statements error NEWLINE statement
		//         同行非法，但换行后仍有合法语句（如 `a = 1 b = 2\nc = 3`），
		//         丢弃违规片段后继续解析下一行的 statement，实现真正恢复。
		| statements error NEWLINE {
				Pycperror(_final_asttree, "unexpected token after statement (missing newline separator)");
				yyerrok;
				$$ = $1;
		}
		| statements error NEWLINE statement {
				Pycperror(_final_asttree, "unexpected token after statement (missing newline separator)");
				yyerrok;
				$1->push_back(static_cast<Statement*>($4));
				$$ = $1;
		}
;

newlines: NEWLINE
		| newlines NEWLINE
;

statement: assignment_statement {
			$$ = $1;
		}
		| return_statement {
			$$ = $1;
		}
		| function_def_statement {
			$$ = $1;
		}
		| if_statement {
			$$ = $1;
		}
		| expression {
			$$ = new ExpressionStatement(
				static_cast<Expression*>($1),
				Pycplineno
			);
		}
;

// 代码块：大括号包裹的语句序列。
// 函数体内部的语句同样以换行作为唯一分隔符，
// 与顶层 statements 保持一致；空代码块等价于空语句列表。
// 大括号与内部语句之间允许出现任意数量换行（即允许空行/前导换行/尾部换行）。
code_block: OP_LBRACE OP_RBRACE {
			$$ = new std::vector<Statement*>();
		}
		| OP_LBRACE newlines OP_RBRACE {
			$$ = new std::vector<Statement*>();
		}
		| OP_LBRACE statements OP_RBRACE {
			$$ = static_cast<std::vector<Statement*>*>($2);
		}
		| OP_LBRACE newlines statements OP_RBRACE {
			$$ = static_cast<std::vector<Statement*>*>($3);
		}
		| OP_LBRACE statements newlines OP_RBRACE {
			$$ = static_cast<std::vector<Statement*>*>($2);
		}
		| OP_LBRACE newlines statements newlines OP_RBRACE {
			$$ = static_cast<std::vector<Statement*>*>($3);
		}
;

// 参数列表：逗号分隔的标识符（参数名）。
// 形如 (a, b, c) 或空 ()。
parameter_list: %empty {
				$$ = new std::vector<std::string*>();
		}
		| IDENTIFIER {
				$$ = new std::vector<std::string*>();
				$$->push_back($1);
		}
		| parameter_list OP_COMMA IDENTIFIER {
				$1->push_back($3);
				$$ = $1;
		}
;

// 调用实参列表：逗号分隔的表达式。
// 形如 (1, a + 2) 或空 ()。
arguments: %empty {
				$$ = new std::vector<Expression*>();
		}
		| expression {
				$$ = new std::vector<Expression*>();
				$$->push_back(static_cast<Expression*>($1));
		}
		| arguments OP_COMMA expression {
				$1->push_back(static_cast<Expression*>($3));
				$$ = $1;
		}
;

// 函数定义语句：func name(params) { body }
// 语法糖 —— 等价于将匿名函数表达式赋值给 name，
// 与 .old PLY 版本的实现一致。
function_def_statement: KW_FUNC IDENTIFIER OP_LPARENTHESES parameter_list OP_RPARENTHESES code_block {
			FunctionExpression* func = new FunctionExpression(
				*$4,                                  // params
				new Program($6),                     // body
				*($2),                               // name
				Pycplineno
			);
			delete $4; // 参数已移动进 FunctionExpression
			delete $2;

			$$ = new AssignmentStatement(
				new IdentifierExpression(new std::string(func->name)),
				static_cast<Expression*>(func),
				Pycplineno
			);
		}
;

// 匿名函数表达式：func(params) { body }
function_expr: KW_FUNC OP_LPARENTHESES parameter_list OP_RPARENTHESES code_block {
			$$ = new FunctionExpression(
				*$3,                       // params
				new Program($5),           // body
				"@anonymous",              // name
				Pycplineno
			);
			delete $3; // 参数已移动进 FunctionExpression
		}
;

// ============================================================
// 条件判断语句
//   条件表达式不要求括号括起（如 `if a > b { ... }`）。
//   每个分支的执行块使用与函数体相同的 code_block 语法（大括号包裹，
//   内部以换行作为语句唯一分隔符）。
//   if / elif / else 是同一个条件语句的组成部分，关键字之间直接相邻，
//   不使用换行分隔（与函数定义 `func name(params) {}` 的单行结构一致）。
//   嵌套条件通过在 code_block 内再出现 if_statement 自然支持。
// ============================================================
if_statement: KW_IF expression code_block opt_elif_else {
			$$ = new IfStatement(
				new IfBranch(
					static_cast<Expression*>($2),
					new Program($3),
					Pycplineno
				),
				$4.elif_branches,   // elif 分支列表（可能为空）
				$4.else_body,       // else 代码块（可能为空指针）
				Pycplineno
			);
		}
;

// 可选的 elif / else 后缀。
// 返回一个 pair：第一项为 elif 分支列表，第二项为 else 代码块（无则 nullptr）。
// 采用非终结符承载返回值是 Bison 的惯用法；此处用一个辅助结构避免
// 多条 if_statement 分支造成的 shift/reduce 歧义。
opt_elif_else: %empty {
			$$ = IfSuffix{ new std::vector<IfBranch*>(), nullptr };
		}
		| elif_clauses {
			$$ = IfSuffix{ $1, nullptr };
		}
		| KW_ELSE code_block {
			$$ = IfSuffix{ new std::vector<IfBranch*>(), new Program($2) };
		}
		| elif_clauses KW_ELSE code_block {
			$$ = IfSuffix{ $1, new Program($3) };
		}
;

// elif 分支序列：一个或多个 elif，每个带条件表达式与大括号代码块。
elif_clauses: KW_ELIF expression code_block {
			$$ = new std::vector<IfBranch*>();
			$$->push_back(new IfBranch(
				static_cast<Expression*>($2),
				new Program($3),
				Pycplineno,
				/*is_elif=*/true
			));
		}
		| elif_clauses KW_ELIF expression code_block {
			$1->push_back(new IfBranch(
				static_cast<Expression*>($3),
				new Program($4),
				Pycplineno,
				/*is_elif=*/true
			));
			$$ = $1;
		}
;

return_statement: KW_RETURN expression {
		$$ = new ReturnStatement(
			static_cast<Expression*>($2),
			Pycplineno
		);
};

assignment_statement: assignment_object OP_EQUALS expression {
		$$ = new AssignmentStatement(
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			Pycplineno
		);
	}
;

assignment_object : IDENTIFIER {
	$$ = new IdentifierExpression($1, Pycplineno);
}
;

expression: comparison_expression
;

// 比较表达式：用于条件判断。优先级低于算术运算，
// 因此 `a + 1 > b * 2` 会被正确解析为 (a+1) > (b*2)。
comparison_expression: additive_expression
	| comparison_expression OP_LT additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::LESS_THAN,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			Pycplineno
		);
	}
	| comparison_expression OP_GT additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::GREATER_THAN,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			Pycplineno
		);
	}
	| comparison_expression OP_LE additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::LESS_EQUAL,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			Pycplineno
		);
	}
	| comparison_expression OP_GE additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::GREATER_EQUAL,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			Pycplineno
		);
	}
	| comparison_expression OP_EQ additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::EQUAL,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			Pycplineno
		);
	}
	| comparison_expression OP_NE additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::NOT_EQUAL,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			Pycplineno
		);
	}
;

additive_expression: multiplicative_expression
    | additive_expression OP_PLUS multiplicative_expression {
			$$ = new BinaryExpression(
				BinaryOp::PLUS,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				Pycplineno
			);
		}
    | additive_expression OP_MINUS multiplicative_expression {
			$$ = new BinaryExpression(
				BinaryOp::MINUS,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				Pycplineno
			);
		}
    ;

multiplicative_expression: unary_expression
    | multiplicative_expression OP_MULTIPLY unary_expression {
			$$ = new BinaryExpression(
				BinaryOp::MULTIPLY,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				Pycplineno
			);
		}
    | multiplicative_expression OP_DIVIDE unary_expression {
			$$ = new BinaryExpression(
				BinaryOp::DIVIDE,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				Pycplineno
			);
		}
    ;

// 乘方：右结合，优先级高于一元取负（-2**2 == -(2**2)）
unary_expression: power_expression
		| OP_MINUS power_expression %prec UMINUS {
			$$ = new UnaryExpression(
				UnaryOp::UMINUS,
				static_cast<Expression*>($2),
				Pycplineno
			);
		};

power_expression: primary_expression
		| primary_expression OP_POWER unary_expression {
			$$ = new BinaryExpression(
				BinaryOp::POWER,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				Pycplineno
			);
		};

primary_expression: LT_INTEGER {
			$$ = new IntegerLiteral($1, Pycplineno);
		}
		| LT_STRING {
			$$ = new StringLiteral($1, Pycplineno);
		}
		| OP_LPARENTHESES expression OP_RPARENTHESES {
			$$ = $2;
		}
		| IDENTIFIER {
			$$ = new IdentifierExpression($1, Pycplineno);
		}
		| function_expr {
			$$ = $1;
		}
		| primary_expression OP_LPARENTHESES arguments OP_RPARENTHESES {
			$$ = new CallExpression(
				static_cast<Expression*>($1),
				*$3,                 // arguments
				Pycplineno
			);
			delete $3; // 实参已移动进 CallExpression
		}
;

%%

int Pycp_parse_error_count = 0;

void Pycperror(Node*& _, const char *s) {
	// 词法错误已由 lexer 输出并停止扫描，此时 parser 因提前 EOF 触发的
	// 语法错误是次生的，直接忽略，避免重复输出。
	if (g_lexer_error) {
		return;
	}
	++Pycp_parse_error_count;
	// Bison reports an internal "syntax error" first; replace it with a
	// meaningful, newline-focused English diagnostic.
	if (std::strcmp(s, "syntax error") == 0) {
		s = "newline is the only valid separator between statements";
	}
	// 两行格式：File "<file>", line <lineno>\n<error>
	std::cerr << "File \"" << g_current_source_path << "\", line " << Pycplineno << "\n" << s << std::endl;
}

Node* parse(const std::string& text){
	Node* _final_asttree = nullptr;
	g_lexer_error = false;

	YY_BUFFER_STATE buffer = Pycp_scan_string(text.c_str());

	Pycpparse(_final_asttree);

	Pycp_delete_buffer(buffer);

	// 词法错误已由 lexer 输出并停止扫描，此处返回 nullptr 中止解析。
	if (g_lexer_error) {
		return nullptr;
	}

	return _final_asttree;
}

Node* parsef(const std::string& path){
	g_current_source_path = path;

	std::ifstream file(path);
	std::string text((std::istreambuf_iterator<char>(file)),
					 std::istreambuf_iterator<char>());

	file.close();

	return parse(text);
}



