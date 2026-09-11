%{
#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include "PycpAstNode.hpp"
#include "PycpLexer.hpp"
#include "preprocessor/PycpPreprocessor.hpp"

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

// 构建函数表达式节点：把解析期收集的 Param* 列表折叠为按值持有的
// std::vector<Param>（转移成员所有权后释放外壳），再构造 FunctionExpression。
// 顶层函数 / 匿名函数 / 类内方法三处共用，统一所有权管理。
static Pycp::Ast::FunctionExpression* make_func_expr(
	std::vector<Pycp::Ast::Param*>* params,
	Pycp::Ast::Program* body,
	const std::string& name,
	int line,
	std::vector<Pycp::Ast::Expression*>* decos = nullptr) {
	std::vector<Pycp::Ast::Param> pv;
	if (params != nullptr) {
		pv.reserve(params->size());
		for (Pycp::Ast::Param* item : *params) {
			pv.push_back(std::move(*item));
			delete item;
		}
		delete params;
	}
	return new Pycp::Ast::FunctionExpression(std::move(pv), body, name, line, decos);
}

// 语法错误计数（定义于本文件末尾）；解析期语义动作据此标记“已报错”，
// 使后续 ModuleLoader 抛出空消息异常中止编译（错误内容已在此直接输出）。
extern int Pycp_parse_error_count;

// 形参顺序错误：带默认值的形参之后又出现无默认值的普通形参
//（如 func f(a, b = 1, c)）。与 Python 一致在【解析期】报为 SyntaxError，
// 输出两行格式：File "<file>", line N[, column M]  +  SyntaxError: ...
// 不回显源码行（lexer/parser 未保留原始行文本），故无 caret 指示。
static void report_param_order_error(int line, int column) {
	++Pycp_parse_error_count;
	std::cerr << "File \"" << g_current_source_path << "\", line " << line;
	if (column >= 0) std::cerr << ", column " << column;
	std::cerr << "\n"
	          << "SyntaxError: parameter without a default follows parameter with a default"
	          << std::endl;
}

// parameter_defs 容器中是否已存在“带默认值”的形参。
static bool has_default_param(const std::vector<Pycp::Ast::Param*>* ps) {
	if (ps == nullptr) return false;
	for (Pycp::Ast::Param* p : *ps) {
		if (p != nullptr && p->default_value != nullptr) return true;
	}
	return false;
}
%}

%union {
	std::string* text; // for Lexer

	Pycp::Ast::Node* node; // for Parser
	Pycp::Ast::IfBranch* if_branch;            // single if/elif branch
	std::vector<Pycp::Ast::IfBranch*>* if_branches; // elif branch list
	std::vector<Pycp::Ast::Statement*>* statements;
	std::vector<std::string>* identifiers;
	std::vector<std::string*>* string_ptrs;   // identifier lists (from_import_names)
	Pycp::Ast::Param* param_def_item;         // single parameter (name + optional default)
	std::vector<Pycp::Ast::Param*>* param_defs; // parameter items (owning Param*)
	std::vector<Pycp::Ast::Expression*>* expressions; // call arguments
	std::vector<std::pair<Pycp::Ast::Expression*, Pycp::Ast::Expression*>>* pair_list; // map 键值对列表
	std::pair<Pycp::Ast::Expression*, Pycp::Ast::Expression*>* key_value_pair;
	// if 语句可选后缀：elif 分支列表 + 可选 else 代码块
	IfSuffix if_suffix;
}

%define api.prefix {Pycp}
%parse-param { Pycp::Ast::Node*& _final_asttree }

// 启用位置跟踪（%locations）：每个符号携带 first_line/last_line/first_column/last_column。
// 语义动作统一使用 @$.first_line 记录语句/表达式行号——它是【规则内第一个符号】的
// 起始行，不受 bison 归约时刻 lookahead 已读入的影响（此前直接用 @$.first_line 会在
// 归约时被后续换行等 lookahead 推进，导致行号 +1 偏移，例如 import 报 48 而非 47）。
%locations

// 该块内容会注入生成的头文件，确保 IfSuffix 在 union 与所有包含
// PycpParser.hpp 的翻译单元中均可见。
%code requires {
	#include <vector>
	#include <utility>
	#include "PycpConfig.hpp"
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
%token KW_FUNC KW_RETURN KW_IF KW_ELIF KW_ELSE KW_NONE KW_IMPORT KW_AS KW_CLASS KW_TRUE KW_FALSE
%token KW_FROM KW_INHERITS KW_REPEAT KW_TO KW_BREAK KW_BY KW_DELETE
%token KW_FOR KW_IN
%token OP_PLUS OP_MINUS OP_MULTIPLY OP_DIVIDE OP_POWER
%token OP_LPARENTHESES OP_RPARENTHESES
%token OP_LBRACKET OP_RBRACKET
%token OP_LBRACE OP_RBRACE
%token OP_EQUALS OP_COMMA OP_COLON
%token OP_LT OP_GT OP_LE OP_GE OP_EQ OP_NE
%token OP_DOT OP_AT

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
%type <node> import_statement
%type <node> from_import_statement
%type <string_ptrs> from_import_names

%type <node> function_def_statement
%type <node> function_expr
%type <param_def_item> parameter_def
%type <param_defs> parameter_list
%type <statements> code_block
%type <expressions> arguments
%type <pair_list> map_literal
%type <pair_list> map_pairs
%type <key_value_pair> map_pair

%type <node> class_def_statement
%type <node> class_expr
%type <text> opt_inherits
%type <statements> class_body
%type <statements> class_members
%type <node> class_member
%type <node> member_variable
%type <node> method_definition
%type <node> class_member_with_modifier
%type <node> decorator_expr
%type <expressions> decorator_list

%type <node> if_statement
%type <if_branches> elif_clauses
%type <if_suffix> opt_elif_else
%type <node> repeat_statement
%type <node> break_statement
%type <node> delete_statement
%type <node> for_statement

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
		| import_statement {
			$$ = $1;
		}
		| from_import_statement {
			$$ = $1;
		}
		| return_statement {
			$$ = $1;
		}
		| function_def_statement {
			$$ = $1;
		}
		| class_def_statement {
			$$ = $1;
		}
		| if_statement {
			$$ = $1;
		}
		| repeat_statement {
			$$ = $1;
		}
		| break_statement {
			$$ = $1;
		}
		| for_statement {
			$$ = $1;
		}
		| delete_statement {
			$$ = $1;
		}
		| expression {
			$$ = new ExpressionStatement(
				static_cast<Expression*>($1),
				@$.first_line
			);
		}
		// 装饰器列表 + name = expr：变量声明装饰（@readonly x = expr 常量绑定，可叠加）。
		| decorator_list IDENTIFIER OP_EQUALS expression {
			AssignmentStatement* as = new AssignmentStatement(
				new IdentifierExpression(new std::string(*$2), @$.first_line),
				static_cast<Expression*>($4),
				@$.first_line,
				$1                               // decorators
			);
			delete $2;
			$$ = as;
		}
;

// ============================================================
// delete 语句：delete obj / delete obj.attr / delete obj[key]
// ============================================================
delete_statement: KW_DELETE expression {
			$$ = new DeleteStatement(
				static_cast<Expression*>($2),
				@$.first_line
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

// 参数定义项：形参名，或带默认值的形参（name = default_expr）。
// 默认值可为任意表达式（Python 语义：函数定义时求值一次）。
parameter_def: IDENTIFIER {
				$$ = new Pycp::Ast::Param($1);
		}
		| IDENTIFIER OP_EQUALS expression {
				$$ = new Pycp::Ast::Param($1, static_cast<Expression*>($3));
		}
;

// 参数列表：逗号分隔的形参项。形如 (a, b = 1, c) 或空 ()。
// 「默认值形参后不得再接必填普通形参」的顺序校验在【解析期】执行
//（与 Python 一致，表现为 SyntaxError），见下方 OP_COMMA 规则。
parameter_list: %empty {
				$$ = new std::vector<Pycp::Ast::Param*>();
		}
		| parameter_def {
				$$ = new std::vector<Pycp::Ast::Param*>();
				$$->push_back($1);
		}
		| parameter_list OP_COMMA parameter_def {
				// 顺序校验：若前面已出现过带默认值的形参，新加入的无默认值
				// 普通形参即非法（func f(a, b = 1, c)）。在解析期直接以
				// SyntaxError 报出，并标记错误计数使编译中止。
				if ($3 != nullptr && $3->default_value == nullptr &&
				    has_default_param($1)) {
					report_param_order_error(@3.first_line, -1);
				}
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
		| arguments OP_COMMA {
				// 尾逗号（Python 风格）：f(a, ) / [a, ]
				$$ = $1;
		}
;

// map 字面量：{k1: v1, k2: v2, ...} 或空 {}。
// 每个键值对 key/value 均为普通表达式（键的可哈希性由运行时 SetItem 校验）。
// 跨行定义由 lexer 的 BRACKET_BODY 状态抑制括号内换行实现，此处无需 newlines。
map_literal: %empty {
				$$ = new std::vector<std::pair<Expression*, Expression*>>();
		}
		| map_pairs {
				$$ = $1;
		}
;

map_pairs: map_pair {
				$$ = new std::vector<std::pair<Expression*, Expression*>>();
				$$->push_back(*$1);
				delete $1;
		}
		| map_pairs OP_COMMA map_pair {
				$1->push_back(*$3);
				delete $3;
				$$ = $1;
		}
		| map_pairs OP_COMMA {
				// 尾逗号（Python 风格）：{k: v, }
				$$ = $1;
		}
;

map_pair: expression OP_COLON expression {
				$$ = new std::pair<Expression*, Expression*>(
					static_cast<Expression*>($1),
					static_cast<Expression*>($3)
				);
		}
;

// 函数定义语句：func name(params) { body }
// 语法糖 —— 等价于将匿名函数表达式赋值给 name，
// 与 .old PLY 版本的实现一致。
// 可选装饰器前缀：@decorator [换行] @decorator ... func name(...){...}，
// 装饰器列表存入 FunctionExpression::decorators，运行时由 codegen 逐个发射
// 装饰器调用替换（最靠近函数的装饰器最先应用）。
function_def_statement: KW_FUNC IDENTIFIER OP_LPARENTHESES parameter_list OP_RPARENTHESES code_block {
			FunctionExpression* func = make_func_expr(
				$4,                                  // params
				new Program($6),                     // body
				*($2),                               // name
				@$.first_line
			);
			delete $2;

			$$ = new AssignmentStatement(
				new IdentifierExpression(new std::string(func->name)),
				static_cast<Expression*>(func),
				@$.first_line
			);
		}
	| decorator_list KW_FUNC IDENTIFIER OP_LPARENTHESES parameter_list OP_RPARENTHESES code_block {
			FunctionExpression* func = make_func_expr(
				$5,                                  // params
				new Program($7),                     // body
				*($3),                               // name
				@$.first_line,
				$1                                   // decorators
			);
			delete $3;

			$$ = new AssignmentStatement(
				new IdentifierExpression(new std::string(func->name)),
				static_cast<Expression*>(func),
				@$.first_line
			);
		}
;

// 匿名函数表达式：func(params) { body }
function_expr: KW_FUNC OP_LPARENTHESES parameter_list OP_RPARENTHESES code_block {
			$$ = make_func_expr(
				$3,                        // params
				new Program($5),           // body
				Pycp::ANONYMOUS_FUNCTION,  // name（config 常量）
				@$.first_line
			);
		}
;

// ============================================================
// 类定义：class name [inherits parent] { 成员变量... 方法... }
//
// 语法：
//   class name [inherits parent]{
//       variables...
//       functions...
//       func __initialize__(self, ...){...}
//       func __string__(self){...}
//       magic_methods...
//   }
//
// 成员变量声明：`name = expr` 或 `name`（仅声明）。
// 方法定义：`func name(params){ body }`（复用 function 语法）。
// 成员可用 @private / @public 修饰（可叠加多个装饰器修饰一个成员）。
// ============================================================
class_def_statement: KW_CLASS IDENTIFIER opt_inherits OP_LBRACE class_body OP_RBRACE {
			// 分离成员变量与方法。
			std::vector<Statement*>* members = static_cast<std::vector<Statement*>*>($5);
			std::vector<Statement*>* vars = new std::vector<Statement*>();
			std::vector<Statement*>* methods = new std::vector<Statement*>();
			for (Statement* m : *members) {
				if (m->get_type() == NodeType::MEMBER_VARIABLE) {
					vars->push_back(m);
				} else if (m->get_type() == NodeType::METHOD_DEFINITION) {
					methods->push_back(m);
				} else {
					delete m;
				}
			}
			delete members;
			$$ = new ClassDefinition($2, $3, vars, methods, @$.first_line);
		}
	// 装饰器列表 + class name {...}：类定义装饰（@readonly class B{} 常量绑定，可叠加）。
	| decorator_list KW_CLASS IDENTIFIER opt_inherits OP_LBRACE class_body OP_RBRACE {
			std::vector<Statement*>* members = static_cast<std::vector<Statement*>*>($6);
			std::vector<Statement*>* vars = new std::vector<Statement*>();
			std::vector<Statement*>* methods = new std::vector<Statement*>();
			for (Statement* m : *members) {
				if (m->get_type() == NodeType::MEMBER_VARIABLE) {
					vars->push_back(m);
				} else if (m->get_type() == NodeType::METHOD_DEFINITION) {
					methods->push_back(m);
				} else {
					delete m;
				}
			}
			delete members;
			$$ = new ClassDefinition($3, $4, vars, methods, @$.first_line,
			                         $1); // decorators
		}
;

// 匿名类表达式：class [inherits parent]{...}，无名字，内部名由 Codegen 填
// 入 config 常量 ANONYMOUS_CLASS。用于 `X = class inherits pycp.Object{...}`。
class_expr: KW_CLASS opt_inherits OP_LBRACE class_body OP_RBRACE {
			std::vector<Statement*>* members = static_cast<std::vector<Statement*>*>($4);
			std::vector<Statement*>* vars = new std::vector<Statement*>();
			std::vector<Statement*>* methods = new std::vector<Statement*>();
			for (Statement* m : *members) {
				if (m->get_type() == NodeType::MEMBER_VARIABLE) {
					vars->push_back(m);
				} else if (m->get_type() == NodeType::METHOD_DEFINITION) {
					methods->push_back(m);
				} else {
					delete m;
				}
			}
			delete members;
			// 匿名类：name 为 nullptr，Codegen 使用 ANONYMOUS_CLASS 内部名。
			$$ = new ClassExpression($2, vars, methods, @$.first_line);
		}
;

// 可选继承子句：inherits parent，无则 nullptr。
// parent 可为单个标识符（inherits A）或属性访问路径（inherits pycp.Object）。
opt_inherits: %empty {
			$$ = nullptr;
		}
		| KW_INHERITS IDENTIFIER {
			$$ = $2;
		}
		| KW_INHERITS IDENTIFIER OP_DOT IDENTIFIER {
			// 属性访问路径：拼接为 "A.B"。
			std::string* path = new std::string(*$2 + "." + *$4);
			delete $2;
			delete $4;
			$$ = path;
		}
;

// 类体：成员列表（成员变量或方法），可为空。
// 允许前导/尾部换行（与 code_block 一致）。
class_body: %empty {
			$$ = new std::vector<Statement*>();
		}
		| newlines {
			$$ = new std::vector<Statement*>();
		}
		| class_members {
			$$ = $1;
		}
		| class_members newlines {
			$$ = $1;
		}
		| newlines class_members {
			$$ = $2;
		}
		| newlines class_members newlines {
			$$ = $2;
		}
;

class_members: class_member {
			$$ = new std::vector<Statement*>();
			$$->push_back(static_cast<Statement*>($1));
		}
		| class_members newlines class_member {
			$1->push_back(static_cast<Statement*>($3));
			$$ = $1;
		}
;

class_member: member_variable {
			$$ = $1;
		}
		| method_definition {
			$$ = $1;
		}
		| class_member_with_modifier {
			$$ = $1;
		}
;

// 带装饰器的成员：装饰器列表后跟一个成员变量或方法。装饰器表达式可为单个
// 标识符（@private）或点分名称路径（@classtools.private），运行时求值得到
// 装饰器函数（如 classtools 导出的 private/public），由 MAKE_CLASS 调用该函数
// 设置被装饰对象的可见性；多个装饰器叠加时由内向外依次应用。
// 装饰器与成员之间、装饰器之间均允许换行（与 class_object 示例一致）。
class_member_with_modifier: decorator_list member_variable {
		MemberVariable* mv = static_cast<MemberVariable*>($2);
		mv->decorators = $1;
		$$ = $2;
	}
	| decorator_list method_definition {
		MethodDefinition* md = static_cast<MethodDefinition*>($2);
		md->decorators = $1;
		$$ = $2;
	}
;

// 装饰器表达式：单个标识符（@private）或点分名称路径（@classtools.private）。
// 前者解析为 IdentifierExpression，后者解析为链式 AttributeExpression，
// 复用现有属性访问表达式语义（运行时 LOAD_VAR + LOAD_ATTR 求值）。
decorator_expr: IDENTIFIER {
		$$ = new IdentifierExpression($1, @$.first_line);
	}
	| decorator_expr OP_DOT IDENTIFIER {
		$$ = new AttributeExpression(
			static_cast<Expression*>($1), $3, @$.first_line);
	}
;

// 装饰器列表：@d0 [换行] @d1 [换行] ...，按源码【自上而下】顺序收集。
// 列表长度即装饰器个数（单个装饰器为长度 1 的列表，与旧语法等价）；
// 应用语义与 Python 一致：列表末尾（最靠近被装饰对象）的装饰器最先应用，
// 即 @a 换行 @b 换行 target 等价于 a(b(target))。
decorator_list: OP_AT decorator_expr opt_newlines {
		std::vector<Expression*>* list = new std::vector<Expression*>();
		list->push_back(static_cast<Expression*>($2));
		$$ = list;
	}
	| decorator_list OP_AT decorator_expr opt_newlines {
		$1->push_back(static_cast<Expression*>($3));
		$$ = $1;
	}
;

// 可选换行（0 或多个 NEWLINE）。
opt_newlines: %empty
		| newlines
;

// 成员变量声明：可选初始值（var1 = Pycp.None()）。
// 无初始值则 value 为 nullptr，初始化由 __initialize__ 负责；
// 有初始值则实例化时自动求值并赋值给实例字段。
member_variable: IDENTIFIER {
			$$ = new MemberVariable($1, nullptr, @$.first_line);
		}
		| IDENTIFIER OP_EQUALS expression {
			$$ = new MemberVariable($1, static_cast<Expression*>($3), @$.first_line);
		}
;

// 方法定义：func name(params){ body }
method_definition: KW_FUNC IDENTIFIER OP_LPARENTHESES parameter_list OP_RPARENTHESES code_block {
			FunctionExpression* func = make_func_expr(
				$4,                                  // params
				new Program($6),                     // body
				*($2),                               // name
				@$.first_line
			);
			delete $2;
			$$ = new MethodDefinition(func, @$.first_line);
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
					@$.first_line
				),
				$4.elif_branches,   // elif 分支列表（可能为空）
				$4.else_body,       // else 代码块（可能为空指针）
				@$.first_line
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
				@$.first_line,
				/*is_elif=*/true
			));
		}
		| elif_clauses KW_ELIF expression code_block {
			$1->push_back(new IfBranch(
				static_cast<Expression*>($3),
				new Program($4),
				@$.first_line,
				/*is_elif=*/true
			));
			$$ = $1;
		}
;

/// ============================================================
// repeat 循环语句（结构化循环）
//
// 语法形式（as 必须位于最后，紧邻 code_block）：
//   repeat if <cond> { }                         while：cond 为假退出
//   repeat <N> { }                               计数 N 次（i 不绑定）
//   repeat <N> as <i> { }                        计数 N 次，i = 0..N-1
//   repeat from <a> to <b> { }                   范围 [a, b]，步长自动
//   repeat from <a> to <b> as <i> { }            范围，i 取端点值
//   repeat from <a> to <b> by <s> { }            范围，显式步长
//   repeat from <a> to <b> by <s> as <i> { }     范围，显式步长 + i
//
// 【注意】无限循环形式 repeat { } 已移除，请使用 repeat if true { }
//        或 while 的语义替代。
// ============================================================
repeat_statement: KW_REPEAT KW_IF expression code_block {
			$$ = new RepeatStatement(
				RepeatMode::WHILE,
				nullptr, nullptr, nullptr, nullptr,
				static_cast<Expression*>($3),
				nullptr,
				new Program($4),
				@$.first_line
			);
		}
		| KW_REPEAT expression code_block {
			// 计数循环（无 as 变量）
			$$ = new RepeatStatement(
				RepeatMode::COUNT,
				static_cast<Expression*>($2), nullptr, nullptr, nullptr, nullptr,
				nullptr,
				new Program($3),
				@$.first_line
			);
		}
		| KW_REPEAT expression KW_AS IDENTIFIER code_block {
			// 计数循环（绑定 i）
			$$ = new RepeatStatement(
				RepeatMode::COUNT,
				static_cast<Expression*>($2), nullptr, nullptr, nullptr, nullptr,
				$4,
				new Program($5),
				@$.first_line
			);
		}
		| KW_REPEAT KW_FROM expression KW_TO expression code_block {
			// 范围循环（无 by/as）
			$$ = new RepeatStatement(
				RepeatMode::RANGE,
				nullptr,
				static_cast<Expression*>($3), static_cast<Expression*>($5),
				nullptr, nullptr,
				nullptr,
				new Program($6),
				@$.first_line
			);
		}
		| KW_REPEAT KW_FROM expression KW_TO expression KW_AS IDENTIFIER code_block {
			// 范围循环（as i，无 by）
			$$ = new RepeatStatement(
				RepeatMode::RANGE,
				nullptr,
				static_cast<Expression*>($3), static_cast<Expression*>($5),
				nullptr, nullptr,
				$7,
				new Program($8),
				@$.first_line
			);
		}
		| KW_REPEAT KW_FROM expression KW_TO expression KW_BY expression code_block {
			// 范围循环（by 步长，无 as）
			$$ = new RepeatStatement(
				RepeatMode::RANGE,
				nullptr,
				static_cast<Expression*>($3), static_cast<Expression*>($5),
				static_cast<Expression*>($7), nullptr,
				nullptr,
				new Program($8),
				@$.first_line
			);
		}
		| KW_REPEAT KW_FROM expression KW_TO expression KW_BY expression KW_AS IDENTIFIER code_block {
			// 范围循环（by 步长 + as i）
			$$ = new RepeatStatement(
				RepeatMode::RANGE,
				nullptr,
				static_cast<Expression*>($3), static_cast<Expression*>($5),
				static_cast<Expression*>($7), nullptr,
				$9,
				new Program($10),
				@$.first_line
			);
		}
;

// break 语句：退出当前一层循环（无代码块，独立语句）。
break_statement: KW_BREAK {
			$$ = new BreakStatement(@$.first_line);
		}
;

// foreach 语句：for 变量 in 表达式 { 语句体 }
// 遍历可迭代对象（List/String）的每个元素绑定到变量。expression 不以
// OP_LBRACE 起始、code_block 以 OP_LBRACE 起始，两者边界无歧义。
for_statement: KW_FOR IDENTIFIER KW_IN expression code_block {
			$$ = new ForeachStatement(
				$2,
				static_cast<Expression*>($4),
				new Program($5),
				@$.first_line
			);
		}
;

// import 语句：import foo 或 import foo as bar
// 模块名暂为单标识符（不含点，对应"仅入口目录查找"的第一阶段实现）。
import_statement: KW_IMPORT IDENTIFIER {
			$$ = new ImportStatement($2, nullptr, @$.first_line);
		}
		| KW_IMPORT IDENTIFIER KW_AS IDENTIFIER {
			$$ = new ImportStatement($2, $4, @$.first_line);
		}
;

// from ... import ... 语句：from module import name1, name2, ...
// 名称均为普通标识符（含 super/private/public，它们不再是关键字）。
from_import_statement: KW_FROM IDENTIFIER KW_IMPORT from_import_names {
		$$ = new FromImportStatement($2, $4, @$.first_line);
	}
;

// 导入名称列表：逗号分隔的标识符。
from_import_names: IDENTIFIER {
		$$ = new std::vector<std::string*>();
		$$->push_back($1);
	}
	| from_import_names OP_COMMA IDENTIFIER {
		$1->push_back($3);
		$$ = $1;
	}
;

return_statement: KW_RETURN expression {
		$$ = new ReturnStatement(
			static_cast<Expression*>($2),
			@$.first_line
		);
};

assignment_statement: primary_expression OP_EQUALS expression {
		// 赋值目标复用 primary_expression（标识符或属性访问）。
		// 非法的目标（字面量、调用、函数等）在 Codegen 中校验拒绝。
		$$ = new AssignmentStatement(
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			@$.first_line
		);
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
			@$.first_line
		);
	}
	| comparison_expression OP_GT additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::GREATER_THAN,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			@$.first_line
		);
	}
	| comparison_expression OP_LE additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::LESS_EQUAL,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			@$.first_line
		);
	}
	| comparison_expression OP_GE additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::GREATER_EQUAL,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			@$.first_line
		);
	}
	| comparison_expression OP_EQ additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::EQUAL,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			@$.first_line
		);
	}
	| comparison_expression OP_NE additive_expression {
		$$ = new BinaryExpression(
			BinaryOp::NOT_EQUAL,
			static_cast<Expression*>($1),
			static_cast<Expression*>($3),
			@$.first_line
		);
	}
;

additive_expression: multiplicative_expression
    | additive_expression OP_PLUS multiplicative_expression {
			$$ = new BinaryExpression(
				BinaryOp::PLUS,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				@$.first_line
			);
		}
    | additive_expression OP_MINUS multiplicative_expression {
			$$ = new BinaryExpression(
				BinaryOp::MINUS,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				@$.first_line
			);
		}
    ;

multiplicative_expression: unary_expression
    | multiplicative_expression OP_MULTIPLY unary_expression {
			$$ = new BinaryExpression(
				BinaryOp::MULTIPLY,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				@$.first_line
			);
		}
    | multiplicative_expression OP_DIVIDE unary_expression {
			$$ = new BinaryExpression(
				BinaryOp::DIVIDE,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				@$.first_line
			);
		}
    ;

// 乘方：右结合，优先级高于一元取负（-2**2 == -(2**2)）
unary_expression: power_expression
		| OP_MINUS power_expression %prec UMINUS {
			$$ = new UnaryExpression(
				UnaryOp::UMINUS,
				static_cast<Expression*>($2),
				@$.first_line
			);
		};

power_expression: primary_expression
		| primary_expression OP_POWER unary_expression {
			$$ = new BinaryExpression(
				BinaryOp::POWER,
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				@$.first_line
			);
		};

primary_expression: LT_INTEGER {
			$$ = new IntegerLiteral($1, @$.first_line);
		}
		| LT_STRING {
			$$ = new StringLiteral($1, @$.first_line);
		}
		| KW_NONE {
			$$ = new NoneLiteral(@$.first_line);
		}
		| KW_TRUE {
			$$ = new BooleanLiteral(true, @$.first_line);
		}
		| KW_FALSE {
			$$ = new BooleanLiteral(false, @$.first_line);
		}
		| OP_LPARENTHESES expression OP_RPARENTHESES {
			$$ = $2;
		}
		| OP_LBRACKET arguments OP_RBRACKET {
			// 列表字面量：[a, b, c] 或空 []
			$$ = new ListLiteral(
				static_cast<std::vector<Expression*>*>($2),
				@$.first_line
			);
		}
		| OP_LBRACE map_literal OP_RBRACE {
			// map 字面量：{k: v, ...} 或空 {}
			$$ = new MapLiteral(
				static_cast<std::vector<std::pair<Expression*, Expression*>>*>($2),
				@$.first_line
			);
		}
		| IDENTIFIER {
			$$ = new IdentifierExpression($1, @$.first_line);
		}
		| function_expr {
			$$ = $1;
		}
		| class_expr {
			$$ = $1;
		}
		| primary_expression OP_LPARENTHESES arguments OP_RPARENTHESES {
			$$ = new CallExpression(
				static_cast<Expression*>($1),
				*$3,                 // arguments
				@$.first_line
			);
			delete $3; // 实参已移动进 CallExpression
		}
		| primary_expression OP_DOT IDENTIFIER {
			$$ = new AttributeExpression(
				static_cast<Expression*>($1),
				$3,
				@$.first_line
			);
		}
		| primary_expression OP_LBRACKET expression OP_RBRACKET {
			// 下标访问：obj[key]
			$$ = new IndexExpression(
				static_cast<Expression*>($1),
				static_cast<Expression*>($3),
				@$.first_line
			);
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
	// Bison 内部默认 "syntax error" 统一规范为 Python 风格的语法错误文案。
	if (std::strcmp(s, "syntax error") == 0) {
		s = "SyntaxError: invalid syntax";
	}
	// 两行格式：File "<file>", line <lineno>\n<error>
	std::cerr << "File \"" << g_current_source_path << "\", line " << Pycplineno << "\n" << s << std::endl;
}

Node* parse(const std::string& text){
	Node* _final_asttree = nullptr;
	g_lexer_error = false;
	Pycplineno = 1; // 每个文件独立计数，避免多文件 import 时行号累积。

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

	// 文本层预处理（# replace / # define / 行指令保留）：
	// 失败时错误已按 "File \"<path>\", line <n>" 两行格式输出，返回 nullptr 中止。
	std::string processed;
	if (!Pycp::Preprocessor::process(text, path, processed)) {
		return nullptr;
	}

	return parse(processed);
}

// 单语句解析 ABI（REPL 逐条执行 / 外部复用）。
// 语义对标 Python 的 "single" 解析模式：解析【一段完整语句】
// （可能跨多行，如类/函数/列表定义），整体作为一个解析单元返回。
// 实现上复用整段解析（parse），将 buffer 解析为 Program；REPL 的续行
// 启发式已保证 buffer 在调用本函数时是完整的单条语句单元。
// 注意：调用方需自行在调用前重置 Pycp_parse_error_count 与
// g_current_source_path（见 ModuleLoader::compile_statement），本函数与
// parse() 对称地重置词法错误标志。
Node* parse_statement(const std::string& text, int initial_line){
	Node* _final_asttree = nullptr;
	g_lexer_error = false;
	Pycplineno = initial_line; // 支持 REPL 会话级连续行号：起始行由调用方（如会话计数）指定。

	YY_BUFFER_STATE buffer = Pycp_scan_string(text.c_str());

	Pycpparse(_final_asttree);

	Pycp_delete_buffer(buffer);

	// 词法错误已由 lexer 输出并停止扫描，此处返回 nullptr 中止解析。
	if (g_lexer_error) {
		return nullptr;
	}

	return _final_asttree;
}



