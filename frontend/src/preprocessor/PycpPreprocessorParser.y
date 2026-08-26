%{
// PycpPreprocessorParser.y — 预处理指令的 Bison 文法。
//
// 与主语言解析器（PycpParser.y / PycpLexer.l）对称：指令行由本文法解析，
// 词法由 PycpPreprocessorLexer.l（prefix="Pycpp"）提供。两种生成物：
//   PycpPreprocessorParser.cpp / PycpPreprocessorLexer.cpp
// 仅由 PycpPreprocessor.cpp 驱动（逐行切分 + 宏展开），不参与主解析流程。
//
// 支持的指令（本轮）：
//   # replace NAME with VALUE   定义标识符替换（C #define 的文本替换语义）
//   # define NAME [VALUE]       定义宏（仅记录定义，供后续条件编译判断，
//                               不参与文本替换）
//   # stop replacing NAME       停止对 NAME 的文本替换（等价 C #undef 作用于替换表）
//   # undefine NAME             取消宏定义（等价 C #undef 作用于宏表）
// 未实现的指令（# if / # elif / # else / # end / # send / # set / # expand /
// # run by 等）一律报 unknown preprocessor directive 终止。
//
// 指令执行时直接操作 Pycpp::g_ctx 指向的预处理状态（替换表 / 宏表），
// 类似主解析器通过全局 g_current_source_path 共享上下文的风格。

#include <iostream>

#include "preprocessor/PycpPreprocessor.hpp"

namespace Pycpp {
// Context 定义见 PycpPreprocessor.hpp；g_ctx 定义于 PycpPreprocessor.cpp。
extern Context* g_ctx;
} // namespace Pycpp

// 指令错误的诊断输出（两行格式，与主解析器一致）。
void Pycpperror(const char* msg);
extern int Pycpplex();
%}

%define api.prefix {Pycpp}
%define parse.error verbose

%union {
    std::string* str;
}

%token PP_REPLACE "replace"
%token PP_DEFINE "define"
%token PP_STOP "stop"
%token PP_REPLACING "replacing"
%token PP_UNDEFINE "undefine"
%token PP_WITH "with"

%token <str> PP_IDENTIFIER
%token <str> PP_VALUE

%type <str> replace_value
%type <str> optional_value

%destructor { delete $$; } <str>

%%

directive:
      replace_directive
    | define_directive
    | stop_replacing_directive
    | undefine_directive
    | unknown_directive
    ;

// # replace NAME with VALUE
// VALUE 为 with 之后到行尾的文本（词法层已去首尾空白）。
replace_directive:
    "replace" PP_IDENTIFIER "with" replace_value
        {
            const std::string* name = $2;
            if (!Pycpp::IsValidMacroName(*name)) {
                Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
                    "invalid macro name: " + *name);
                delete $2; delete $4;
                YYERROR;
            }
            (*(Pycpp::g_ctx->replacements))[*name] = *$4;
            delete $2; delete $4;
        }
    ;

// # define NAME [VALUE]
// VALUE 可缺省；即使给出也不参与文本替换，仅记录定义。
define_directive:
    "define" PP_IDENTIFIER optional_value
        {
            const std::string* name = $2;
            if (!Pycpp::IsValidMacroName(*name)) {
                Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
                    "invalid macro name: " + *name);
                delete $2; delete $3;
                YYERROR;
            }
            Pycpp::g_ctx->defined_macros->insert(*name);
            delete $2; delete $3;
        }
    ;

// # stop replacing NAME
stop_replacing_directive:
    "stop" "replacing" PP_IDENTIFIER
        {
            // 幂等：NAME 未定义时静默（同 C #undef 对未定义宏不报错）。
            Pycpp::g_ctx->replacements->erase(*$3);
            delete $3;
        }
    ;

// # undefine NAME
undefine_directive:
    "undefine" PP_IDENTIFIER
        {
            // 幂等：NAME 未定义时静默。
            Pycpp::g_ctx->defined_macros->erase(*$2);
            delete $2;
        }
    ;

// 未知 / 未实现指令：报错终止（防止静默漏处理）。
// 词法层 [^\n]+ 已吞掉指令首词之后的整行，故只需两种形式即可覆盖。
unknown_directive:
      PP_IDENTIFIER
        { Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
              "unknown preprocessor directive: #" + *$1); delete $1; }
    | PP_IDENTIFIER PP_VALUE
        { Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
              "unknown preprocessor directive: #" + *$1); delete $1; delete $2; }
    ;

replace_value:
    PP_VALUE { $$ = $1; }
    ;

optional_value:
      %empty { $$ = new std::string(); }
    | PP_VALUE { $$ = $1; }
    ;

%%

// 语法错误诊断（bison 约定函数；msg 为 bison 生成的说明）。
void Pycpperror(const char* msg) {
    if (Pycpp::g_ctx == nullptr) {
        std::cerr << "preprocessor directive error: " << msg << std::endl;
        return;
    }
    Pycpp::ReportDirectiveError(*Pycpp::g_ctx, std::string(msg));
}
