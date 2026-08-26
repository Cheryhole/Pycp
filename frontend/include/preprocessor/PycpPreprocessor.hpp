#ifndef PYCP_PREPROCESSOR_HPP
#define PYCP_PREPROCESSOR_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Pycp {

// 预处理解析器（接口入口，位于 frontend/src/preprocessor/PycpPreprocessor.cpp）。
//
// 指令行由 Flex/Bison 生成的解析器（PycpPreprocessorLexer / PycpPreprocessorParser）
// 解析执行（与主语言前端同构）；普通代码行由本文件的文本状态机做宏展开。
//
// 本轮支持的指令：
//   # replace NAME with VALUE     标识符文本替换（同 C #define 的替换语义，
//                                 字符串字面量 / 注释 / 更长标识符子串内不替换）
//   # define NAME [VALUE]         定义宏：仅记录定义状态，供后续条件编译
//                                 （# if defined ...）判断，不参与文本替换
//   # stop replacing NAME         停止对 NAME 的替换（作用于替换表，幂等）
//   # undefine NAME               取消 NAME 的宏定义（作用于宏表，幂等）
//   # <lineno> "<filename>"       行指令，原样保留（供主 lexer 调整行号/文件名）
// 代码中 #lineno / #filename 展开为当前物理行号（裸整数）/ 当前文件路径（带引号字符串）。
// 未实现的指令报 unknown preprocessor directive 并终止。
// 返回 false 表示失败（错误已按 "File \"<path>\", line <n>" 两行格式输出到 stderr）。
class Preprocessor {
public:
    static bool process(const std::string& source,
                        const std::string& source_path,
                        std::string& out);
};

} // namespace Pycp

namespace Pycpp {

// 预处理上下文：PycpPreprocessor.cpp 在解析每条指令前设置，
// Bison action（PycpPreprocessorParser.y）通过 g_ctx 直接操作。
struct Context {
    std::unordered_map<std::string, std::string>* replacements = nullptr; // # replace 表
    std::unordered_set<std::string>* defined_macros = nullptr;            // # define 宏表
    const std::string* source_path = nullptr;                             // 当前文件路径
    int line = 0;                                                         // 指令所在物理行号
    bool error = false;                                                   // 出错标志
};

// 定义于 PycpPreprocessor.cpp。
extern Context* g_ctx;

// 宏名合法性校验（首字符字母/下划线，其余字母/数字/下划线）。
bool IsValidMacroName(const std::string& name);

// 指令级错误：以两行格式输出诊断并置 ctx.error = true。
void ReportDirectiveError(Context& ctx, const std::string& msg);

} // namespace Pycpp

#endif // PYCP_PREPROCESSOR_HPP
