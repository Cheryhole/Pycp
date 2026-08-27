// PycpPreprocessor.cpp — 预处理接口文件（角色等同 PycpParser.cpp 之于主解析器）。
//
// 预处理分两层：
//   1) 指令行（# replace / # define / # set / # expand / # if / # send 等）
//      交给 Flex/Bison 生成的 PycpPreprocessorLexer / PycpPreprocessorParser
//      解析执行（与主语言前端同构），见 PycpPreprocessorParser.y；
//   2) 普通代码行由本文件的文本状态机 ExpandText 做标识符级宏展开：
//      字符串字面量、// 行注释、/* */ 块注释内不替换；替换结果递归展开；
//      代码中的 #lineno / #filename 展开为当前物理行号 / 当前文件路径。
//      预处理产出的注释（// 行尾、/* */ 块）会被剥离，空行会被删除，
//      输出末尾对相邻且完全相同的行指令（# <lineno> "<file>"）做去重合并。
//
// 条件编译（# if / # elif / # else / # end）跨多行、可嵌套：bison 仅解析单条
// 指令并维护 g_ctx->cond_stack，主循环每轮据栈顶状态决定代码行去留。
//
// 对外唯一接口：Pycp::Preprocessor::process(source, source_path, out)，
// 由 PycpParser.y 的 parsef 与 PycpMain.cpp 的 -p/--preprocess 分支调用。

#include "preprocessor/PycpPreprocessor.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Flex/Bison 生成的解析器接口（定义于 ${CMAKE_BINARY_DIR}/generated/）。
#include "PycpPreprocessorLexer.hpp"
#include "PycpPreprocessorParser.hpp"

// 定义于 PycpPreprocessorLexer.l 的用户代码区（flex 头文件不含其声明）。
void PycppResetScanner();

// 前向声明：processImpl 定义于下方 namespace Pycp（# expand 递归时调用）。
namespace Pycp {
static bool processImpl(const std::string& source, const std::string& source_path,
                        std::string& out, std::set<std::string>& expanding);
}

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

// 消息插值：扫描 text 中的 #{...} 与 \#{...}。
//   #{lineno}   → 当前物理行号（ctx.line）
//   #{filename} → 当前文件名（ctx.source_path，带引号）
//   #{MACRO}    → 宏内容（宏表或替换表中 MACRO 的值，优先替换表）
//   \#{...}     → 转义：原样保留（即字面的 \#{}）
// 未识别的 #{...} 原样保留，便于调试。
std::string InterpolateMessage(const std::string& text, const Context& ctx) {
    std::string out;
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const char c = text[i];
        if (c == '\\' && i + 1 < n && text[i + 1] == '#') {
            // 转义：保留字面的 "\#" 及其后内容（交还后续解析）。
            out += "\\#";
            i += 2;
            continue;
        }
        if (c == '#' && i + 1 < n && text[i + 1] == '{') {
            std::size_t j = i + 2;
            while (j < n && text[j] != '}') ++j;
            if (j < n) {
                const std::string key = text.substr(i + 2, j - (i + 2));
                if (key == "lineno") {
                    out += std::to_string(ctx.line);
                } else if (key == "filename") {
                    const std::string& path = ctx.source_path ? *ctx.source_path : "";
                    out += '"';
                    for (char ch : path) {
                        if (ch == '"' || ch == '\\') out += '\\';
                        out += ch;
                    }
                    out += '"';
                } else {
                    auto it = ctx.replacements->find(key);
                    if (it != ctx.replacements->end()) {
                        out += it->second;
                    } else {
                        auto mit = ctx.defined_macros->find(key);
                        if (mit != ctx.defined_macros->end()) {
                            out += key; // 已定义宏（无值）展开为其名
                        } else {
                            out += "#{" + key + "}"; // 未知键原样保留
                        }
                    }
                }
                i = j + 1;
                continue;
            }
        }
        out += c;
        ++i;
    }
    return out;
}

// 将相对路径解析为基于 base_dir 的绝对/相对路径（base_dir 为空则用原路径）。
std::string ResolveIncludePath(const std::string& path,
                               const std::string& base_file) {
    if (path.empty() || path.find('/') == 0) return path; // 绝对路径直接用
    const std::size_t slash = base_file.find_last_of("/\\");
    if (slash == std::string::npos) return path;            // 无目录前缀
    return base_file.substr(0, slash + 1) + path;
}

// 读取 path 指定的文件并递归预处理（# expand 用）。
// 成功将结果写入 out、返回 true；文件缺失或处理失败已报错并返回 false。
bool ExpandFileInto(const std::string& path, Context& ctx, std::string& out) {
    const std::string resolved = ResolveIncludePath(path, ctx.source_path
        ? *ctx.source_path : std::string());
    if (ctx.expanding->count(resolved)) {
        // 已在展开链中：防止递归环。
        std::cerr << "File \"" << (ctx.source_path ? *ctx.source_path : "") << "\", line "
                  << ctx.line << "\n";
        std::cerr << "expand: circular include detected for file: " << resolved << "\n";
        return false;
    }
    std::ifstream f(resolved, std::ios::binary);
    if (!f) {
        std::cerr << "File \"" << (ctx.source_path ? *ctx.source_path : "") << "\", line "
                  << ctx.line << "\n";
        std::cerr << "expand: file not found: " << resolved << "\n";
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    // 递归预处理该文件内容（独立的条件栈 / 元数据上下文，但共享替换表与宏表）。
    std::set<std::string> child_expanding = *ctx.expanding;
    child_expanding.insert(resolved);

    std::string inner;
    Context child = ctx;
    child.source_path = &resolved;
    child.line = 1;
    child.error = false;
    child.out_lineno = -1;
    child.out_filename.clear();
    child.expanding = &child_expanding;
    child.out = &inner;

    Context* saved = g_ctx;
    g_ctx = &child;
    const bool ok = ::Pycp::processImpl(content, resolved, inner, child_expanding);
    g_ctx = saved;

    if (!ok) return false;
    out += inner;
    return true;
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

// 行指令判定（新格式）：行首 "//" 注释且内容为 "__pycp_pp# <数字> ..."。
// 形如 "// __pycp_pp# 20 \"file.pycp\""；仅当注释以此前缀 + 纯数字开头才视为行指令。
bool IsLineDirective(const std::string& line) {
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i + 1 >= line.size() || line[i] != '/' || line[i + 1] != '/') return false;
    i += 2; // 跳过 "//"
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    const std::string marker = "__pycp_pp#";
    if (line.compare(i, marker.size(), marker) != 0) return false;
    i += marker.size();
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

// 判断一行是否为预处理指令行（需交给 Flex/Bison 解析器消费）：
// 行首 '#' 且 非行指令 "# <lineno> \"<file>\"" 且 首词为已知指令关键字
// （或未知标识符 —— 交由 bison 报 unknown 终止）。
// 指令行被整体消费、不产生任何输出（不留空行补行号）。
bool IsDirectiveLine(const std::string& line) {
    const std::string trimmed = TrimLeft(line);
    if (trimmed.empty() || trimmed[0] != '#') return false;
    if (IsLineDirective(line)) return false;
    const std::string first = FirstWordAfterHash(trimmed);
    if (first == "lineno" || first == "filename") return false;
    // 行首 #lineno / #filename（无空格）按普通代码处理，不在此识别。
    // 其他首词（已知指令或未知标识符）均视为指令行。
    return true;
}

// 输出压缩：删除纯空行，并合并内容（行号+文件名）完全相同的相邻 "# <lineno> "<file>""
// 行指令（被空行分隔的相同行指令，因空行被删而相邻，顺带合并）。
// 行指令本身保留——主 lexer 靠它对齐行号，不可删；被真实代码行打断的相同行指令
// 不合并以保留行号对齐信息。O(行数) 单趟扫描。
void CompactOutput(std::string& out) {
    std::vector<std::string> lines;
    std::string cur;
    cur.reserve(64);
    for (char ch : out) {
        if (ch == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) lines.push_back(cur); // 末尾无换行的内容行

    const auto is_blank = [](const std::string& s) {
        for (char c : s) if (c != ' ' && c != '\t' && c != '\r') return false;
        return true;
    };

    std::string result;
    result.reserve(out.size());
    bool have_last_dir = false;
    std::string last_dir;
    for (const auto& line : lines) {
        if (is_blank(line)) continue; // 删除空行（不影响行号对齐）
        const bool is_dir = IsLineDirective(line);
        if (is_dir && have_last_dir && line == last_dir) {
            continue; // 与上一个保留的行指令完全相同：合并（删除重复行）
        }
        result += line;
        result += '\n';
        if (is_dir) {
            have_last_dir = true;
            last_dir = line;
        } else {
            have_last_dir = false; // 被真实代码行打断，不再与更早的行指令合并
        }
    }
    // 末尾换行：若原 out 不以 '\n' 结尾，去掉多余换行以保持一致。
    if (!out.empty() && out.back() != '\n' && !result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    out = std::move(result);
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

        // 块注释 /* */：跨行由 in_block_comment 维持；注释内容（含 /* 与 */ 边界）全部丢弃。
        if (in_block_comment) {
            if (c == '*' && i + 1 < n && text[i + 1] == '/') {
                in_block_comment = false;
                i += 2;
            } else {
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

        // 行尾 // 注释：默认丢弃到行尾（不输出）。但若内容以 " __pycp_pp#" 开头，
        // 这是预处理行指令（// __pycp_pp# <lineno> "<file>"），需保留原样交给主 lexer 对齐。
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            std::size_t j = i + 2;
            while (j < n && (text[j] == ' ' || text[j] == '\t')) ++j;
            if (text.compare(j, std::string_view("__pycp_pp#").size(), "__pycp_pp#") == 0) {
                while (i < n) { out += text[i]; ++i; } // 保留整行行指令注释
                continue;
            }
            while (i < n) ++i; // 普通注释：丢弃
            continue;
        }

        // 块注释 /* */：跨行由 in_block_comment 维持；注释内容（含 /* 与 */ 边界）全部丢弃。
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            in_block_comment = true;
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

// 条件栈：所有帧均激活（当前代码行应保留到输出）？
bool IsOutputActive(const std::vector<Pycpp::CondFrame>& stack) {
    for (const auto& fr : stack) {
        if (!fr.active) return false;
    }
    return true;
}

// 处理 # expand NAME（在主循环处理，避免 bison 重入）：
// NAME 须在替换表中定义为文件路径；读取并完整展开插入输出（递归 process）。
// 文件缺失 / 未定义报 file not found 并置 ctx.error。
void HandleExpand(const std::string& directive,  // 已 TrimLeft、去 '#'，形如 "expand NAME"
                  Pycpp::Context& ctx,
                  const std::unordered_map<std::string, std::string>& replacements,
                  std::string& out) {
    std::size_t i = 0;
    const std::size_t n = directive.size();
    while (i < n && (directive[i] == ' ' || directive[i] == '\t')) ++i;
    while (i < n && IsIdentChar(directive[i])) ++i; // 跳过 "expand"
    while (i < n && (directive[i] == ' ' || directive[i] == '\t')) ++i;
    const std::size_t j = i;
    while (i < n && IsIdentChar(directive[i])) ++i;
    const std::string name = directive.substr(j, i - j);

    auto it = replacements.find(name);
    if (it == replacements.end()) {
        Pycpp::ReportDirectiveError(ctx,
            "expand: '" + name + "' is not defined as a file path "
            "(use # replace " + name + " with \"path\").");
        return;
    }
    std::string path = it->second;
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"') {
        path = path.substr(1, path.size() - 2);
    }
    std::string expanded;
    if (!Pycpp::ExpandFileInto(path, ctx, expanded)) {
        ctx.error = true; // 错误已由 ExpandFileInto 输出
        return;
    }
    out += expanded;
}

// 处理一行（普通行 / 指令行 / 行指令）。
// is_block_skipped：当前处于被跳过的条件块内（非指令行不输出，但仍解析指令）。
// 返回 true 表示本行向 out 产生了非空内容（主循环据此推进输出行号）。
bool ProcessLine(const std::string& line,
                 int line_no,
                 const std::string& source_path,
                 Pycpp::Context& ctx,
                 const std::unordered_map<std::string, std::string>& replacements,
                 bool& in_block_comment,
                 bool is_block_skipped,
                 std::string& out) {
    const std::string trimmed = TrimLeft(line);
    if (trimmed.empty() || trimmed[0] != '#') {
        // 普通代码行：仅在激活时展开输出。空行 / 纯注释行不产出内容。
        if (!is_block_skipped) {
            const std::size_t before = out.size();
            std::vector<std::string> active;
            ExpandText(line, in_block_comment, replacements, line_no, source_path, out, active);
            return out.size() > before;
        }
        return false;
    }

    // 行指令 "# <lineno> \"<filename>\""：原样保留（主 lexer 据此调整行号/文件名）。
    if (IsLineDirective(line)) {
        if (!is_block_skipped) out += line;
        return !is_block_skipped;
    }

    // 行首 #lineno / #filename（无空格）：作为普通表达式展开，而非指令。
    const std::string first = FirstWordAfterHash(trimmed);
    if (first == "lineno" || first == "filename") {
        if (!is_block_skipped) {
            const std::size_t before = out.size();
            std::vector<std::string> active;
            ExpandText(line, in_block_comment, replacements, line_no, source_path, out, active);
            return out.size() > before;
        }
        return false;
    }

    // 其余指令行：即使处于被跳过的块内也需解析（否则 # if 内的嵌套 # replace
    // 不生效、# end 无法正确闭合）。# expand 已由主循环拦截，不会到达此处。
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
    return false;
}

} // namespace

namespace Pycp {

// 内部实现：与 process 相同，但接收"防环展开集合"以便递归 # expand 沿用父级集合。
static bool processImpl(const std::string& source,
                        const std::string& source_path,
                        std::string& out,
                        std::set<std::string>& expanding);

bool Preprocessor::process(const std::string& source,
                           const std::string& source_path,
                           std::string& out) {
    std::set<std::string> expanding; // 防环展开集合（顶层为空）
    return processImpl(source, source_path, out, expanding);
}

bool processImpl(const std::string& source,
                 const std::string& source_path,
                 std::string& out,
                 std::set<std::string>& expanding) {
    std::unordered_map<std::string, std::string> replacements; // # replace 表
    std::unordered_set<std::string> defined_macros;            // # define 宏表

    Pycpp::Context ctx;
    ctx.replacements = &replacements;
    ctx.defined_macros = &defined_macros;
    ctx.source_path = &source_path;
    ctx.line = 0;
    ctx.error = false;
    ctx.expanding = &expanding;
    ctx.out = &out;
    Pycpp::g_ctx = &ctx;

    out.clear();
    out.reserve(source.size());

    bool in_block_comment = false;
    int line_no = 1;   // 输入端物理行号
    int out_line = 1;  // 输出端下一行将占用的行号（主 lexer 视角）
    std::size_t pos = 0;
    const std::size_t n = source.size();

    // 输出端行号与物理行号不一致时插入 "# <lineno> \"<path>\"" 行指令，
    // 让主 lexer 把后续代码行号对齐到物理行号（C #line 语义），
    // 替代"指令行留空行补行号"的做法。
    // 行指令中的 lineno/filename 优先取自 # set 设置的输出元数据
    // （out_lineno / out_filename），未设置时回退物理值。
    // out_line 维护"主 lexer 视角的下一行行号"：输出一行后 +1；
    // 输出行指令后置为 target（行指令声明下一行是源第 target 行）。
    // 构造行指令文本（形如 "// __pycp_pp# <lineno> \"<file>\"\n"），不立即写入 out。
    // target 为当前物理行号；若 # set lineno 已设置，则取其一一次性基准值（用后清空，
    // 之后行号随物理行递增）。返回文本的同时更新 out_line（主 lexer 视角的下一输出行号）。
    const auto make_line_directive = [&](int target) -> std::string {
        int lineno;
        if (ctx.out_lineno >= 0) {
            lineno = ctx.out_lineno;   // 一次性：仅影响其后第一个输出行
            ctx.out_lineno = -1;
        } else {
            lineno = target;
        }
        const std::string& fname = ctx.out_filename.empty()
            ? source_path : ctx.out_filename;
        std::string d = "// __pycp_pp# ";
        d += std::to_string(lineno);
        d += " \"";
        for (char ch : fname) {
            if (ch == '"' || ch == '\\') d += '\\';
            d += ch;
        }
        d += "\"\n";
        out_line = target;
        return d;
    };

    while (pos < n) {
        std::size_t eol = source.find('\n', pos);
        if (eol == std::string::npos) eol = n;
        const std::string line = source.substr(pos, eol - pos);

        const bool is_directive = IsDirectiveLine(line);
        const bool block_skipped = !IsOutputActive(ctx.cond_stack);

        // # expand 在主循环处理（避免 bison 重入）：注入文件内容后继续后续行。
        if (is_directive) {
            const std::string trimmed = TrimLeft(line);
            const std::string fw = FirstWordAfterHash(trimmed);
            if (fw == "expand") {
                if (!block_skipped) {
                    HandleExpand(trimmed.substr(1), ctx, replacements, out);
                    if (ctx.error) { Pycpp::g_ctx = nullptr; return false; }
                }
                // 注入内容自带行指令对齐；后续外行从当前物理行继续。
                out_line = line_no + 1;
                ++line_no;
                pos = eol + 1;
                continue;
            }
        }

        // 先展开本行；仅当真正产出非空内容（produced）且输出行号与物理行号错位时，
        // 才在该行产出内容前插入行指令对齐。空行 / 纯注释行（produced=false）不插入、
        // 不推进行号，从而保持输出行号与物理代码行严格对应（空行不影响行号对齐）。
        // 指令行（is_directive）整体被消费，也不在此处理。
        const std::size_t out_before = out.size();
        const bool produced = ProcessLine(line, line_no, source_path, ctx,
                                            replacements, in_block_comment,
                                            block_skipped, out);

        if (ctx.error) {
            Pycpp::g_ctx = nullptr;
            return false;
        }

        if (produced && line_no != out_line) {
            out.insert(out_before, make_line_directive(line_no));
        }

        // 仅当本行真正产出非空内容时，才追加换行并推进行号。
        if (produced && eol < n) {
            out.push_back('\n');
            ++out_line;
        }
        ++line_no;
        pos = eol + 1;
    }

    // 文件结束但仍有未闭合的 # if：报告缺失 #end。
    if (!ctx.cond_stack.empty()) {
        ctx.line = line_no;
        Pycpp::ReportDirectiveError(ctx, "unexpected end of file: missing #end for #if");
        Pycpp::g_ctx = nullptr;
        return false;
    }

    // 后处理：删除空行，并合并相邻且完全相同的行指令（# <lineno> "<file>"）。
    CompactOutput(out);

    Pycpp::g_ctx = nullptr;
    return true;
}

} // namespace Pycp
