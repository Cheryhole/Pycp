#ifndef PYCP_PREPROCESSOR_HPP
#define PYCP_PREPROCESSOR_HPP

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Pycp {

// 预处理解析器（接口入口，位于 frontend/src/preprocessor/PycpPreprocessor.cpp）。
//
// 指令行由 Flex/Bison 生成的解析器（PycpPreprocessorLexer / PycpPreprocessorParser）
// 解析执行（与主语言前端同构）；普通代码行由本文件的文本状态机做宏展开。
//
// 支持的指令：
//   # replace NAME with VALUE        标识符文本替换（同 C #define 的替换语义，
//                                      字符串字面量 / 注释 / 更长标识符子串内不替换）
//   # define NAME [VALUE]            定义宏：仅记录定义状态，供后续 # if defined 判断，
//                                      不参与文本替换
//   # stop replacing NAME            停止对 NAME 的替换（作用于替换表，幂等）
//   # undefine NAME                  取消 NAME 的宏定义（作用于宏表，幂等）
//   # set lineno to N                覆盖后续行指令行号（输出元数据，不改代码文本）
//   # set filename to "PATH"         覆盖后续行指令文件名（输出元数据）
//   # expand NAME                    NAME 经 # replace 定义为文件路径，将文件内容
//                                      完整展开插入输出（缺失报 file not found 终止）
//   # if COND / # elif COND          条件编译：COND 成立时激活当前块，块内代码行保留
//     # else / # end                 不成立则整块跳过（支持多行嵌套）
//   # send error|warning|message "TEXT"
//                                      error 报错终止；warning/message 输出消息
//                                      TEXT 支持 #{lineno}/#{filename}/#{MACRO} 插值，
//                                      \#{MACRO} 转义为原样
//   # <lineno> "<filename>"          行指令，原样保留（供主 lexer 调整行号/文件名）
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

// 条件编译块的一帧：记录当前分支是否已激活（active）与是否已有分支成立（matched）。
struct CondFrame {
    bool active = false;   // 当前块是否处于激活状态（代码行是否保留）
    bool matched = false;  // 本 # if/#elif 链中是否已有分支成立
    int line = 0;          // # if 所在物理行号（用于错误诊断）
};

// 预处理上下文：PycpPreprocessor.cpp 在解析每条指令前设置，
// Bison action（PycpPreprocessorParser.y）通过 g_ctx 直接操作。
struct Context {
    std::unordered_map<std::string, std::string>* replacements = nullptr; // # replace 表
    std::unordered_set<std::string>* defined_macros = nullptr;            // # define 宏表
    const std::string* source_path = nullptr;                             // 当前文件路径
    int line = 0;                                                         // 指令所在物理行号
    bool error = false;                                                   // 出错标志

    // 条件编译块栈（# if 压栈，# end 弹栈），支持多行嵌套。
    std::vector<CondFrame> cond_stack;

    // 输出元数据（由 # set 设置）：out_lineno < 0 表示用物理行号；
    // out_filename 为空表示用物理路径。仅影响行指令，不改代码文本。
    int out_lineno = -1;
    std::string out_filename;

    // 防环：当前正在展开的文件路径集合（# expand 递归展开时记录）。
    std::set<std::string>* expanding = nullptr;

    // 输出缓冲区（expand 时追加内层展开结果）。
    std::string* out = nullptr;
};

// 定义于 PycpPreprocessor.cpp。
extern Context* g_ctx;

// 宏名合法性校验（首字符字母/下划线，其余字母/数字/下划线）。
bool IsValidMacroName(const std::string& name);

// 指令级错误：以两行格式输出诊断并置 ctx.error = true。
void ReportDirectiveError(Context& ctx, const std::string& msg);

// 消息插值：扫描 TEXT 中的 #{lineno}/#{filename}/#{MACRO} 分别替换为当前行号 /
// 当前文件名 / 宏内容；\#{} 转义为原样保留。返回处理后的字符串。
std::string InterpolateMessage(const std::string& text, const Context& ctx);

// 读取 path 指定的文件并递归预处理（# expand 用）。成功将结果写入 out、返回 true；
// 文件缺失或处理失败则已按两行格式报错并返回 false。防环由 ctx.expanding 维护。
bool ExpandFileInto(const std::string& path, Context& ctx, std::string& out);

} // namespace Pycpp

#endif // PYCP_PREPROCESSOR_HPP
