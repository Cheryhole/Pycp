#include "escape_handler.hpp"

#include <cstdint>
#include <string>

namespace Pycp {
namespace Lexer {

namespace {

// 将 Unicode 码点编码为 UTF-8 多字节序列。
void append_utf8(std::string& out, uint32_t cp) {
	if (cp <= 0x7F) {
		out.push_back(static_cast<char>(cp));
	} else if (cp <= 0x7FF) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0xFFFF) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0x10FFFF) {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}
	// 超出 0x10FFFF 的非法码点：静默忽略（不追加）。
}

// 解析 4 位十六进制（\uXXXX），失败返回 -1。
int parse_hex4(const std::string& s, std::size_t start) {
	if (start + 4 > s.size()) return -1;
	int v = 0;
	for (std::size_t i = 0; i < 4; ++i) {
		char c = s[start + i];
		v <<= 4;
		if (c >= '0' && c <= '9')      v |= (c - '0');
		else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
		else return -1;
	}
	return v;
}

// 解析 2 位十六进制（\xHH），失败返回 -1。
int parse_hex2(const std::string& s, std::size_t start) {
	if (start + 2 > s.size()) return -1;
	int v = 0;
	for (std::size_t i = 0; i < 2; ++i) {
		char c = s[start + i];
		v <<= 4;
		if (c >= '0' && c <= '9')      v |= (c - '0');
		else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
		else return -1;
	}
	return v;
}

} // anonymous namespace

std::string decode_escape(const std::string& seq) {
	// 空序列：直接返回空串。
	if (seq.empty()) return "";

	// 常见单字符转义。
	switch (seq[0]) {
		case 'n': return "\n";
		case 't': return "\t";
		case 'r': return "\r";
		case '0': return std::string(1, '\0');
		case '\\': return "\\";
		case '"': return "\"";
		case '\'': return "'";
		case 'a': return "\a";
		case 'b': return "\b";
		case 'f': return "\f";
		case 'v': return "\v";
		default: break;
	}

	// Unicode 转义 \uXXXX。
	if (seq[0] == 'u') {
		int cp = parse_hex4(seq, 1);
		if (cp >= 0) {
			std::string out;
			append_utf8(out, static_cast<uint32_t>(cp));
			return out;
		}
	}

	// 十六进制字节转义 \xHH。
	if (seq[0] == 'x') {
		int b = parse_hex2(seq, 1);
		if (b >= 0) {
			return std::string(1, static_cast<char>(b));
		}
	}

	// 未知转义序列：保留反斜杠 + 原始字符（避免破坏既有脚本）。
	return "\\" + seq;
}

} // namespace Lexer
} // namespace Pycp
