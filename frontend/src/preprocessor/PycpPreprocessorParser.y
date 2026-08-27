%{
// PycpPreprocessorParser.y — 预处理指令的 Bison 文法。
//
// 与主语言解析器（PycpParser.y / PycpLexer.l）对称：指令行由本文法解析，
// 词法由 PycpPreprocessorLexer.l（prefix="Pycpp"）提供。两种生成物：
//   PycpPreprocessorParser.cpp / PycpPreprocessorLexer.cpp
// 仅由 PycpPreprocessor.cpp 驱动（逐行切分 + 宏展开），不参与主解析流程。
//
// 支持的指令：
//   # replace NAME with VALUE   定义标识符替换（C #define 的文本替换语义）
//   # define NAME [VALUE]       定义宏（仅记录定义，供后续条件编译判断，
//                               不参与文本替换）
//   # stop replacing NAME       停止对 NAME 的文本替换（等价 C #undef 作用于替换表）
//   # undefine NAME             取消宏定义（等价 C #undef 作用于宏表）
//   # set NAME to VALUE         设置输出元数据：NAME=lineno 覆盖行指令行号，
//                               NAME=filename 覆盖行指令文件名；其他名忽略
//   # expand NAME               NAME 经 # replace 定义为文件路径，将文件内容
//                               完整展开插入输出（文件缺失报 file not found 终止）
//   # if COND / # elif COND     条件编译：COND 为真时激活当前块
//     # else / # end            块内普通代码行仅当所属条件激活时保留
//   # send error|warning|message "TEXT"
//                               error 报错终止；warning/message 输出消息
//                               （TEXT 支持 #{lineno}/#{filename}/#{MACRO} 插值）
//
// 指令执行时直接操作 Pycpp::g_ctx 指向的预处理状态（替换表 / 宏表 / 条件栈 /
// 输出元数据 / expand 回调），类似主解析器通过全局 g_current_source_path
// 共享上下文的风格。跨行的 # if/#elif/#else/#end 块流控由主循环读取
// g_ctx->cond_stack 决定代码行去留。

#include <iostream>
#include <string>
#include <set>

#include "preprocessor/PycpPreprocessor.hpp"

namespace Pycpp {
// Context 定义见 PycpPreprocessor.hpp；g_ctx 定义于 PycpPreprocessor.cpp。
extern Context* g_ctx;
// 消息插值（定义于 PycpPreprocessor.cpp）。
std::string InterpolateMessage(const std::string& text, const Context& ctx);
// 读取并展开文件（定义于 PycpPreprocessor.cpp），返回 false 表示失败（已报错）。
bool ExpandFileInto(const std::string& path, const Context& ctx, std::string& out);
} // namespace Pycpp

// 指令错误的诊断输出（两行格式，与主解析器一致）。
void Pycpperror(const char* msg);
extern int Pycpplex();
%}

%define api.prefix {Pycpp}
%define parse.error verbose

%union {
    std::string* str;
    bool cond;
}

%token PP_REPLACE "replace"
%token PP_DEFINE "define"
%token PP_STOP "stop"
%token PP_REPLACING "replacing"
%token PP_UNDEFINE "undefine"
%token PP_WITH "with"
%token PP_SET "set"
%token PP_TO "to"
// expand 指令在主循环中处理，避免 bison 重入；此处不进入 bison 文法。
// %token PP_EXPAND "expand"
%token PP_IF "if"
%token PP_ELIF "elif"
%token PP_ELSE "else"
%token PP_END "end"
%token PP_SEND "send"
%token PP_ERROR "error"
%token PP_WARNING "warning"
%token PP_MESSAGE "message"
%token PP_DEFINED "defined"
%token PP_NOT "not"

%token <str> PP_IDENTIFIER
%token <str> PP_VALUE

%type <str> replace_value
%type <str> optional_value
%type <str> set_value
%type <str> send_text
%type <cond> cond

%destructor { delete $$; } <str>

%%

directive:
      replace_directive
    | define_directive
    | stop_replacing_directive
    | undefine_directive
    | set_directive
    | if_directive
    | elif_directive
    | else_directive
    | end_directive
    | send_directive
    | unknown_directive
    ;

// # replace NAME with VALUE
// VALUE 为 with 之后到行尾的文本（词法层已去首尾空白）。
replace_directive:
    PP_REPLACE PP_IDENTIFIER PP_WITH replace_value
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
    PP_DEFINE PP_IDENTIFIER optional_value
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
    PP_STOP PP_REPLACING PP_IDENTIFIER
        {
            // 幂等：NAME 未定义时静默（同 C #undef 对未定义宏不报错）。
            Pycpp::g_ctx->replacements->erase(*$3);
            delete $3;
        }
    ;

// # undefine NAME
undefine_directive:
    PP_UNDEFINE PP_IDENTIFIER
        {
            // 幂等：NAME 未定义时静默。
            Pycpp::g_ctx->defined_macros->erase(*$2);
            delete $2;
        }
    ;

// # set NAME to VALUE
// NAME=lineno  → 覆盖后续行指令的行号（out_lineno）
// NAME=filename → 覆盖后续行指令的文件名（out_filename），VALUE 可为带引号字符串
// 其他 NAME 静默忽略。
set_directive:
    PP_SET PP_IDENTIFIER PP_TO set_value
        {
            const std::string* name = $2;
            const std::string* raw = $4;
            if (*name == "lineno") {
                try {
                    Pycpp::g_ctx->out_lineno = std::stoi(*raw);
                } catch (...) {
                    Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
                        "set lineno requires an integer, got: " + *raw);
                    delete $2; delete $4;
                    YYERROR;
                }
            } else if (*name == "filename") {
                std::string v = *raw;
                // 去首尾引号（与代码层 #filename 输出一致，行指令要求带引号）。
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                    v = v.substr(1, v.size() - 2);
                }
                Pycpp::g_ctx->out_filename = v;
            }
            // 其他 NAME：忽略（保留为未来扩展点）。
            delete $2; delete $4;
        }
    ;

// # expand NAME 在主循环（PycpPreprocessor.cpp）中处理，避免 bison 重入；
// 此处不进入 bison 文法（IsDirectiveLine 仍将其识别为指令行，由主循环拦截）。

// # if COND  → 压栈；active = matched = COND
if_directive:
    PP_IF cond
        {
            Pycpp::CondFrame fr;
            fr.active = $2;
            fr.matched = $2;
            fr.line = Pycpp::g_ctx->line;
            Pycpp::g_ctx->cond_stack.push_back(fr);
        }
    ;

// # elif COND → 本帧尚未 matched 时重新求值；active = !matched && COND
elif_directive:
    PP_ELIF cond
        {
            if (Pycpp::g_ctx->cond_stack.empty()) {
                Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
                    "elif without matching #if");
                YYERROR;
            }
            Pycpp::CondFrame& fr = Pycpp::g_ctx->cond_stack.back();
            if (!fr.matched) {
                fr.active = $2;
                fr.matched = fr.matched || $2;
            } else {
                fr.active = false;
            }
        }
    ;

// # else → active = !matched
else_directive:
    PP_ELSE
        {
            if (Pycpp::g_ctx->cond_stack.empty()) {
                Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
                    "else without matching #if");
                YYERROR;
            }
            Pycpp::CondFrame& fr = Pycpp::g_ctx->cond_stack.back();
            fr.active = !fr.matched;
        }
    ;

// # end → 弹栈
end_directive:
    PP_END
        {
            if (Pycpp::g_ctx->cond_stack.empty()) {
                Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
                    "end without matching #if");
                YYERROR;
            }
            Pycpp::g_ctx->cond_stack.pop_back();
        }
    ;

// # send error|warning|message "TEXT"
// error  → 报错终止；warning/message → 输出消息（TEXT 支持插值）。
send_directive:
      PP_SEND PP_ERROR send_text
        {
            Pycpp::ReportDirectiveError(*Pycpp::g_ctx,
                Pycpp::InterpolateMessage(*$3, *Pycpp::g_ctx));
            delete $3;
            YYERROR;
        }
    | PP_SEND PP_WARNING send_text
        {
            std::cerr << "warning: " << Pycpp::InterpolateMessage(*$3, *Pycpp::g_ctx) << "\n";
            delete $3;
        }
    | PP_SEND PP_MESSAGE send_text
        {
            std::cerr << Pycpp::InterpolateMessage(*$3, *Pycpp::g_ctx) << "\n";
            delete $3;
        }
    ;

// 条件表达式（消除 reduce/reduce 冲突的简化文法）：
//   defined NAME          → NAME 是否在宏表中定义
//   not defined NAME      → 取反
//   ( cond )             → 分组
//   not ( cond )         → 取反分组
cond:
      PP_DEFINED PP_IDENTIFIER
        { $$ = Pycpp::g_ctx->defined_macros->count(*$2) > 0; delete $2; }
    | PP_NOT PP_DEFINED PP_IDENTIFIER
        { $$ = Pycpp::g_ctx->defined_macros->count(*$3) == 0; delete $3; }
    | "(" cond ")"
        { $$ = $2; }
    | PP_NOT "(" cond ")"
        { $$ = !$3; }
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

set_value:
    PP_VALUE { $$ = $1; }
    ;

send_text:
    PP_VALUE { $$ = $1; }
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
