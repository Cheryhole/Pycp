%{
// PycpPreprocessorLexer.l — 预处理指令行的 Flex 词法分析器。
//
// 配合 PycpPreprocessorParser.y 使用（见该文件顶部注释）。每次由
// PycpPreprocessor.cpp 喂入一条完整的指令行（不含换行与行首的 '#'），
// 产出指令关键字 / 标识符 / 值文本 token。
//
// 关键点：值文本（# replace A with 3.14 中的 "3.14"）不是独立词法单元，
// 而是"值模式"（VAL start condition）下一次性匹配的整行剩余文本——
// 这样避免了 `[^\n]+` 贪婪吞掉关键字/标识符的问题：
//   - 读到 "with" 后进入 VAL 模式（replace 的值）；
//   - 读到 # define 的宏名后进入 VAL 模式（define 的可选值，可为空）；
//   - 读到 "to" 后进入 VAL 模式（set 的值）；
//   - 读到 "send" 的 kind 后进入 VAL 模式（send 的消息文本，可为带插值字符串）；
//   - 未知指令首词（非关键字标识符）后整行进入 VAL 模式，交由 bison 报 unknown。
// 命名前缀统一为 Pycpp，与 bison 端（%define api.prefix {Pycpp}）对齐。

#include <string>

#include "PycpPreprocessorParser.hpp"

// 值文本去除首尾空白。
namespace {
std::string PpTrim(const char* s, int len) {
    std::string r(s, static_cast<std::size_t>(len));
    const std::size_t b = r.find_first_not_of(" \t\r");
    if (b == std::string::npos) return std::string();
    const std::size_t e = r.find_last_not_of(" \t\r");
    return r.substr(b, e - b + 1);
}
} // namespace

static int pp_prev_token = 0;    // 上一个返回的 token（用于 define/set/send 后进入值模式）
static int pp_at_line_start = 1; // 当前是否为指令行首 token（未知指令首词后整行作值）
%}

%option noyywrap
%option nounput
%option noinput
%option prefix="Pycpp"

%x VAL

%%

[ \t\r]+            { /* 跳过空白 */ }
"replace"           { pp_at_line_start = 0; pp_prev_token = PP_REPLACE; return PP_REPLACE; }
"define"            { pp_at_line_start = 0; pp_prev_token = PP_DEFINE; return PP_DEFINE; }
"stop"              { pp_at_line_start = 0; pp_prev_token = PP_STOP; return PP_STOP; }
"replacing"         { pp_at_line_start = 0; pp_prev_token = PP_REPLACING; return PP_REPLACING; }
"undefine"          { pp_at_line_start = 0; pp_prev_token = PP_UNDEFINE; return PP_UNDEFINE; }
"with"              { pp_at_line_start = 0; pp_prev_token = PP_WITH; BEGIN(VAL); return PP_WITH; }
"set"               { pp_at_line_start = 0; pp_prev_token = PP_SET; return PP_SET; }
"to"                { pp_at_line_start = 0; pp_prev_token = PP_TO; BEGIN(VAL); return PP_TO; }
"if"                { pp_at_line_start = 0; pp_prev_token = PP_IF; return PP_IF; }
"elif"              { pp_at_line_start = 0; pp_prev_token = PP_ELIF; return PP_ELIF; }
"else"              { pp_at_line_start = 0; pp_prev_token = PP_ELSE; return PP_ELSE; }
"end"               { pp_at_line_start = 0; pp_prev_token = PP_END; return PP_END; }
"send"              { pp_at_line_start = 0; pp_prev_token = PP_SEND; return PP_SEND; }
"error"             { pp_at_line_start = 0; pp_prev_token = PP_ERROR; BEGIN(VAL); return PP_ERROR; }
"warning"           { pp_at_line_start = 0; pp_prev_token = PP_WARNING; BEGIN(VAL); return PP_WARNING; }
"message"           { pp_at_line_start = 0; pp_prev_token = PP_MESSAGE; BEGIN(VAL); return PP_MESSAGE; }
"defined"           { pp_at_line_start = 0; pp_prev_token = PP_DEFINED; return PP_DEFINED; }
"not"               { pp_at_line_start = 0; pp_prev_token = PP_NOT; return PP_NOT; }


[a-zA-Z_][a-zA-Z0-9_]* {
    if (pp_at_line_start) BEGIN(VAL); // 未知指令首词：整行剩余文本作值，bison 报 unknown
    pp_at_line_start = 0;
    Pycpplval.str = new std::string(Pycpptext);
    if (pp_prev_token == PP_DEFINE) BEGIN(VAL); // # define NAME [VALUE]
    pp_prev_token = PP_IDENTIFIER;
    return PP_IDENTIFIER;
}

<VAL>[^\n]* {
    BEGIN(INITIAL);
    Pycpplval.str = new std::string(PpTrim(Pycpptext, Pycppleng));
    pp_prev_token = PP_VALUE;
    return PP_VALUE;
}

\n                  { /* 忽略（调用方按行切分） */ }

.                   { /* 非法字符：交给 bison 报语法错误 */ return (int)(unsigned char)Pycpptext[0]; }

%%

// 每解析一条指令行前调用：重置 start condition 与行首标志，
// 防止 # define NAME（无值）扫描提前结束时 VAL 状态残留到下一行。
void PycppResetScanner() {
    BEGIN(INITIAL);
    pp_at_line_start = 1;
    pp_prev_token = 0;
}
