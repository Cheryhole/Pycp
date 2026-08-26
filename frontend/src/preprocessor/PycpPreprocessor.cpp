// PycpPreprocessor.cpp — 预处理接口文件（角色等同 PycpParser.cpp 之于主解析器）。
//
// 预处理分两层：
//   1) 指令行（# replace / # define / # stop replacing / # undefine 等）
//      交给 Flex/Bison 生成的 PycpPreprocessorLexer / PycpPreprocessorParser
//      解析执行（与主语言前端同构），见 PycpPreprocessorParser.y；
//   2) 普通代码行由本文件的文本状态机 ExpandText 做标识符级宏展开：
//      字符串字面量、// 行注释、/* */ 块注释内不替换；替换结果递归展开；
//      代码中的 #lineno / #filename 展开为当前物理行号 / 当前文件路径。
//
// 对外唯一接口：Pycp::Preprocessor::process(source, source_path, out)，
// 由 PycpParser.y 的 parsef 与 PycpMain.cpp 的 -p/--preprocess 分支调用。

#include "preprocessor/PycpPreprocessor.hpp"

#include <cctype>
#include <iostream>
#include <string>
#include <vector>

// Flex/Bison 生成的解析器接口（定义于 ${CMAKE_BINARY_DIR}/generated/）。
#include "PycpPreprocessorLexer.hpp"
#include "PycpPreprocessorParser.hpp"

// 定义于 PycpPreprocessorLexer.l 的用户代码区（flex 头文件不含其声明）。
void PycppResetScanner();

namespace Pycpp {

Context* g_ctx = nullptr;

bool IsValidMacroName(const std::string& name) {
    if (name.empty()) return false;
    const unsigned char c0 = static_cast<unsigned char>(name[0]);
    if (!(std::isalpha(c0) || c0 == '_')) return false;
    for (std::size_t i = 1; i < name.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (!(std::isalnum(c) || c == '_')) return false;
    }
    return true;
}

void ReportDirectiveError(Context& ctx, const std::string& msg) {
    ctx.error = true;
    std::cerr << "File \"" << (ctx.source_path ? *ctx.source_path : "") << "\", line "
              << ctx.line << "\n";
    std::cerr << msg << "\n";
}

} // namespace Pycpp

namespace {

using Pycp::Preprocessor;

// ---- 基础字符判断 ----
bool IsIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool IsIdentChar(char c) {
    return IsIdentStart(c) || (c >= '0' && c <= '9');
}

std::string TrimLeft(const std::string& s) {
    const std::size_t b = s.find_first_not_of(" \t\r");
    return b == std::string::npos ? std::string() : s.substr(b);
}

// "# <lineno> \"<filename>\"" 行指令判定：'#' 后第一个词为纯数字。
bool IsLineDirective(const std::string& line) {
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] != '#') return false;
    ++i;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    const std::size_t j = i;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    return i > j;
}

// '#' 后的第一个标识符（#lineno / # replace ... 的首词）。
std::string FirstWordAfterHash(const std::string& trimmed) {
    std::size_t i = 1;
    while (i < trimmed.size() && (trimmed[i] == ' ' || trimmed[i] == '\t')) ++i;
    const std::size_t j = i;
    while (i < trimmed.size() && IsIdentChar(trimmed[i])) ++i;
    return trimmed.substr(j, i - j);
}

// 判断一行是否为预处理指令行（# replace / # define / # stop replacing /
// # undefine 等，需交给 Flex/Bison 解析器消费）：行首 '#' 且 非行指令
// "# <lineno> \"<file>\"" 且 首词非 #lineno/#filename。
// 指令行被整体消费、不产生任何输出（不留空行补行号）。
bool IsDirectiveLine(const std::string& line) {
    const std::string trimmed = TrimLeft(line);
    if (trimmed.empty() || trimmed[0] != '#') return false;
    if (IsLineDirective(line)) return false;
    const std::string first = FirstWordAfterHash(trimmed);
    if (first == "lineno" || first == "filename") return false;
    return true;
}

bool Contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) {
        if (x == s) return true;
    }
    return false;
}

// 标识符级宏展开状态机。text 为一行普通代码（可含 '#' 开头的 #lineno/#filename）。
// in_block_comment 跨行保持；active 为当前展开链（A->B->A 防循环）。
void ExpandText(const std::string& text,
                bool& in_block_comment,
                const std::unordered_map<std::string, std::string>& replacements,
                int line_no,
                const std::string& source_path,
                std::string& out,
                std::vector<std::string>& active) {
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = text[i];

        if (in_block_comment) {
            if (c == '*' && i + 1 < n && text[i + 1] == '/') {
                in_block_comment = false;
                out += "*/";
                i += 2;
            } else {
                out += c;
                ++i;
            }
            continue;
        }

        // 字符串字面量：整体原样输出（含转义），内部不替换。
        if (c == '"') {
            out += c;
            ++i;
            while (i < n) {
                const char d = text[i];
                out += d;
                if (d == '\\' && i + 1 < n) {
                    out += text[i + 1];
                    i += 2;
                    continue;
                }
                ++i;
                if (d == '"') break;
            }
            continue;
        }

        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n) {
                out += text[i];
                ++i;
            }
            continue;
        }

        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            in_block_comment = true;
            out += "/*";
            i += 2;
            continue;
        }

        // #lineno / #filename：'#' 后紧跟完整标识符时展开；否则原样保留。
        if (c == '#') {
            if (i + 1 < n && IsIdentStart(text[i + 1])) {
                std::size_t j = i + 1;
                while (j < n && IsIdentChar(text[j])) ++j;
                const std::string word = text.substr(i + 1, j - i - 1);
                if (word == "lineno") {
                    out += std::to_string(line_no);
                    i = j;
                    continue;
                }
                if (word == "filename") {
                    out += '"';
                    for (char ch : source_path) {
                        if (ch == '"' || ch == '\\') out += '\\';
                        out += ch;
                    }
                    out += '"';
                    i = j;
                    continue;
                }
            }
            out += c;
            ++i;
            continue;
        }

        if (IsIdentStart(c)) {
            std::size_t j = i + 1;
            while (j < n && IsIdentChar(text[j])) ++j;
            const std::string word = text.substr(i, j - i);
            const auto it = replacements.find(word);
            if (it != replacements.end() && !Contains(active, word)) {
                active.push_back(word);
                // 宏值作为普通文本递归展开；其内部块注释状态独立（不串到本行）。
                bool dummy = false;
                ExpandText(it->second, dummy, replacements, line_no, source_path, out, active);
                active.pop_back();
            } else {
                out += word;
            }
            i = j;
            continue;
        }

        out += c;
        ++i;
    }
}

// 处理一行（普通行 / 指令行 / 行指令）。
void ProcessLine(const std::string& line,
                 int line_no,
                 const std::string& source_path,
                 Pycpp::Context& ctx,
                 const std::unordered_map<std::string, std::string>& replacements,
                 bool& in_block_comment,
                 std::string& out) {
    const std::string trimmed = TrimLeft(line);
    if (trimmed.empty() || trimmed[0] != '#') {
        std::vector<std::string> active;
        ExpandText(line, in_block_comment, replacements, line_no, source_path, out, active);
        return;
    }

    // 行指令 "# <lineno> \"<filename>\""：原样保留（主 lexer 据此调整行号/文件名）。
    if (IsLineDirective(line)) {
        out += line;
        return;
    }

    // 行首 #lineno / #filename（无空格）：作为普通表达式展开，而非指令。
    const std::string first = FirstWordAfterHash(trimmed);
    if (first == "lineno" || first == "filename") {
        std::vector<std::string> active;
        ExpandText(line, in_block_comment, replacements, line_no, source_path, out, active);
        return;
    }

    // 其余指令行：交给 Flex/Bison 解析器执行（修改宏表 / 报错）。
    ctx.line = line_no;
    ctx.error = false;
    PycppResetScanner(); // 重置词法状态（防止上一行 VAL 状态残留）
    const std::string directive = trimmed.substr(1); // 去掉行首 '#'
    YY_BUFFER_STATE buf = Pycpp_scan_string(directive.c_str());
    const int r = Pycppparse();
    Pycpp_delete_buffer(buf);
    if (r != 0 && !ctx.error) {
        Pycpp::ReportDirectiveError(ctx, "invalid preprocessor directive.");
    }
    // 指令行整体被消费，不参与代码（不影响块注释状态）。
}

} // namespace

namespace Pycp {

bool Preprocessor::process(const std::string& source,
                           const std::string& source_path,
                           std::string& out) {
    std::unordered_map<std::string, std::string> replacements; // # replace 表
    std::unordered_set<std::string> defined_macros;            // # define 宏表

    Pycpp::Context ctx;
    ctx.replacements = &replacements;
    ctx.defined_macros = &defined_macros;
    ctx.source_path = &source_path;
    ctx.line = 0;
    ctx.error = false;
    Pycpp::g_ctx = &ctx;

    out.clear();
    out.reserve(source.size());

    bool in_block_comment = false;
    int line_no = 1;   // 输入端物理行号
    int out_line = 1;  // 输出端下一行将占用的行号
    std::size_t pos = 0;
    const std::size_t n = source.size();

    // 输出端行号与物理行号不一致时插入 "# <lineno> \"<path>\"" 行指令，
    // 让主 lexer 把后续代码行号对齐到物理行号（C #line 语义），
    // 替代"指令行留空行补行号"的做法。
    // out_line 维护"主 lexer 视角的下一行行号"：输出一行后 +1；
    // 输出行指令后置为 target（行指令声明下一行是源第 target 行）。
    const auto emit_line_directive = [&](int target) {
        out += "# ";
        out += std::to_string(target);
        out += " \"";
        for (char ch : source_path) {
            if (ch == '"' || ch == '\\') out += '\\';
            out += ch;
        }
        out += "\"\n";
        out_line = target;
    };

    while (pos < n) {
        std::size_t eol = source.find('\n', pos);
        if (eol == std::string::npos) eol = n;
        const std::string line = source.substr(pos, eol - pos);

        const bool is_directive = IsDirectiveLine(line);
        // 输出行前：若输出端行号与物理行号不一致（指令行被消费所致），
        // 先插入行指令对齐；指令行本身被整体消费，不留空行。
        if (!is_directive && line_no != out_line) {
            emit_line_directive(line_no);
        }

        ProcessLine(line, line_no, source_path, ctx, replacements,
                    in_block_comment, out);

        if (ctx.error) {
            Pycpp::g_ctx = nullptr;
            return false;
        }

        // 指令行被整体消费（不留空行补行号）；普通行/行指令才追加换行。
        if (!is_directive && eol < n) {
            out.push_back('\n');
            ++out_line;
        }
        ++line_no;
        pos = eol + 1;
    }

    Pycpp::g_ctx = nullptr;
    return true;
}

} // namespace Pycp
