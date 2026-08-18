#ifndef PYCP_ESCAPE_HANDLER_HPP
#define PYCP_ESCAPE_HANDLER_HPP

// =============================================================
// Pycp 转义序列处理模块（escape_handler）
//
// 负责字符串字面量中转义序列的解析与转换。词法分析器（PycpLexer.l）
// 在 STRING 状态遇到 `\` 引导的转义序列时，调用 decode_escape 将其
// 解码为实际字符（Unicode 转义解码为多字节 UTF-8）后追加到字符串缓冲区。
//
// 支持：
//   \n \t \r \0 \\ \" \'          常见转义序列
//   \uXXXX                         Unicode 码点（4 位十六进制，UTF-8 编码）
//   \xHH                           十六进制字节
// 未知转义序列：保留原样（反斜杠 + 字符），避免破坏既有脚本。
// =============================================================

#include <string>

namespace Pycp {
namespace Lexer {

// 解码一个转义序列，返回解码后的字符串（Unicode 转义可能为多字节 UTF-8）。
//
// seq 为去掉前导反斜杠后的剩余部分（由 lexer 的 `\\.` 规则捕获的 Pycptext
// 形如 "\n"、"\\"、"\u0041"，调用前剥离首个字符 `\`）。
// 例如：decode_escape("n") -> "\n"，decode_escape("u0041") -> "A"。
std::string decode_escape(const std::string& seq);

} // namespace Lexer
} // namespace Pycp

#endif // PYCP_ESCAPE_HANDLER_HPP
