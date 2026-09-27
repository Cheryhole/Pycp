// =============================================================
// json 标准库实现（原生 C++ 扩展）
//
// 提供两个字符串级 API，语义对齐 Python 的 json 模块：
//   - json.load(s)   : 把 JSON 字符串解析为 Pycp 对象并返回。
//   - json.dump(obj) : 把 Pycp 对象序列化为 JSON 字符串并返回。
//
// 双向类型映射：
//   {}  <-> Map        []  <-> List         "..." <-> String
//   整数 <-> Integer   小数/指数 <-> Decimal
//   true/false <-> Boolean    null <-> None
//
// 数字解析：无小数点/指数 -> Integer；含小数点或指数 -> Decimal（精确十进制）。
//
// dump 格式化标志（关键字参数）：
//   indent     : 整数=空格缩进数；或 "tab"/含 \t 的字符串=用制表符缩进；None=紧凑。
//   sort_keys  : bool，Map 按字符串键排序输出（稳定）。
//   separators : 二元素序列 (item_sep, key_sep)，自定义分隔符。
//
// 错误行为：
//   load 遇到非法 JSON -> ValueError（文案含出错位置）。
//   dump 遇到不可序列化类型或非字符串键 -> TypeError。
//   dump 遇到非有限 Float -> ValueError。
// 用户可见文案一律英文（见项目约定）。
// =============================================================

#include "json.hpp"

#include "object/PycpModule.hpp"
#include "object/PycpFunction.hpp"
#include "object/PycpString.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpFloat.hpp"
#include "object/PycpDecimal.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpList.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpMap.hpp"
#include "object/PycpException.hpp"
#include "object/PycpExtension.hpp"
#include "object/PycpGC.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace Pycp {

namespace {

const char* kModuleName = "json";

// =============================================================
// 递归下降解析器（load）
// =============================================================
class JsonParser {
public:
	explicit JsonParser(const std::string& s) : s_(s), pos_(0) {}

	// 解析整个输入为单个 JSON 值；尾部多余数据视为错误。
	Object* parse() {
		skip_ws();
		Object* v = parse_value();
		skip_ws();
		if (pos_ != s_.size()) {
			fail("trailing data after JSON value");
		}
		return v;
	}

private:
	// 注：按值持有，避免绑定 get_value() 返回的临时对象（悬空引用）。
	std::string s_;
	size_t      pos_;

	[[noreturn]] void fail(const std::string& msg) {
		throw ValueError("json.load(): " + msg + " at position " +
		                 std::to_string(pos_));
	}

	void skip_ws() {
		while (pos_ < s_.size()) {
			char c = s_[pos_];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				++pos_;
			} else {
				break;
			}
		}
	}

	char peek() {
		if (pos_ >= s_.size()) fail("unexpected end of input");
		return s_[pos_];
	}

	char get() {
		if (pos_ >= s_.size()) fail("unexpected end of input");
		return s_[pos_++];
	}

	Object* parse_value() {
		skip_ws();
		if (pos_ >= s_.size()) fail("unexpected end of input");
		char c = s_[pos_];
		switch (c) {
			case '{': return parse_object();
			case '[': return parse_array();
			case '"': return parse_string();
			case 't': return parse_literal("true", Boolean::True());
			case 'f': return parse_literal("false", Boolean::False());
			case 'n': return parse_literal("null", None::instance);
			case '-':
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				return parse_number();
			default:
				fail(std::string("unexpected character '") + c + "'");
		}
	}

	Object* parse_literal(const char* lit, Object* val) {
		size_t n = std::strlen(lit);
		if (pos_ + n > s_.size() || s_.compare(pos_, n, lit) != 0) {
			fail(std::string("invalid literal, expected '") + lit + "'");
		}
		pos_ += n;
		Incref(val);
		return val;
	}

	Object* parse_object() {
		get(); // 消费 '{'
		Map* m = Map::New();
		skip_ws();
		if (peek() == '}') { get(); return m; }
		while (true) {
			skip_ws();
			if (peek() != '"') fail("expected string key in object");
			Object* key = parse_string(); // Owned
			skip_ws();
			if (get() != ':') fail("expected ':' after object key");
			Object* val = parse_value(); // Owned
			m->put_item(key, val);       // 内部 Incref，故释放本地引用
			Decref(key);
			Decref(val);
			skip_ws();
			char c = get();
			if (c == ',') continue;
			if (c == '}') break;
			fail("expected ',' or '}' in object");
		}
		return m;
	}

	Object* parse_array() {
		get(); // 消费 '['
		List* l = List::New();
		skip_ws();
		if (peek() == ']') { get(); return l; }
		while (true) {
			Object* val = parse_value(); // Owned
			l->append(val);              // 内部 Incref，故释放本地引用
			Decref(val);
			skip_ws();
			char c = get();
			if (c == ',') continue;
			if (c == ']') break;
			fail("expected ',' or ']' in array");
		}
		return l;
	}

	Object* parse_string() {
		get(); // 消费 '"'
		std::string out;
		out.reserve(32);
		while (true) {
			if (pos_ >= s_.size()) fail("unterminated string");
			char c = s_[pos_++];
			if (c == '"') break;
			if (c == '\\') {
				if (pos_ >= s_.size()) fail("unterminated escape");
				char e = s_[pos_++];
				switch (e) {
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					case '/': out.push_back('/'); break;
					case 'b': out.push_back('\b'); break;
					case 'f': out.push_back('\f'); break;
					case 'n': out.push_back('\n'); break;
					case 'r': out.push_back('\r'); break;
					case 't': out.push_back('\t'); break;
					case 'u': {
						uint32_t cp = parse_hex4();
						// 代理对：高代理后须紧跟低代理
						if (cp >= 0xD800 && cp <= 0xDBFF) {
							if (pos_ + 2 > s_.size() ||
							    s_[pos_] != '\\' || s_[pos_ + 1] != 'u') {
								fail("invalid surrogate pair in string");
							}
							pos_ += 2;
							uint32_t lo = parse_hex4();
							if (lo < 0xDC00 || lo > 0xDFFF) {
								fail("invalid low surrogate in string");
							}
							cp = 0x10000 + ((cp - 0xD800) << 10) +
							     (lo - 0xDC00);
						} else if (cp >= 0xDC00 && cp <= 0xDFFF) {
							fail("unexpected low surrogate in string");
						}
						append_utf8(out, cp);
						break;
					}
					default:
						fail(std::string("invalid escape '\\") + e + "'");
				}
			} else if (static_cast<unsigned char>(c) < 0x20) {
				fail("control character must be escaped in string");
			} else {
				out.push_back(c);
			}
		}
		return String::FromCString(out.c_str());
	}

	uint32_t parse_hex4() {
		if (pos_ + 4 > s_.size()) fail("invalid \\u escape");
		uint32_t v = 0;
		for (int i = 0; i < 4; ++i) {
			char c = s_[pos_++];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
			else fail("invalid hex digit in \\u escape");
		}
		return v;
	}

	static void append_utf8(std::string& out, uint32_t cp) {
		if (cp <= 0x7F) {
			out.push_back(static_cast<char>(cp));
		} else if (cp <= 0x7FF) {
			out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else if (cp <= 0xFFFF) {
			out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		} else {
			out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
		}
	}

	Object* parse_number() {
		size_t start = pos_;
		if (peek() == '-') get();
		// 整数部分
		if (peek() == '0') {
			get();
		} else if (peek() >= '1' && peek() <= '9') {
			while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') get();
		} else {
			fail("invalid number");
		}
		bool is_decimal = false;
		// 小数部分
		if (pos_ < s_.size() && s_[pos_] == '.') {
			is_decimal = true;
			get();
			if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') {
				fail("invalid fraction");
			}
			while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') get();
		}
		// 指数部分
		if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
			is_decimal = true;
			get();
			if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) get();
			if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') {
				fail("invalid exponent");
			}
			while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') get();
		}
		std::string lit = s_.substr(start, pos_ - start);
		if (is_decimal) {
			return Decimal::FromString(lit); // 精确十进制（含 1.5e10 等）
		}
		try {
			long long iv = std::stoll(lit);
			return Integer::FromLong(iv);
		} catch (const std::exception&) {
			fail("integer out of range: " + lit);
		}
	}
};

// =============================================================
// 序列化器（dump）
// =============================================================

// 把科学计数法（mpd_to_sci）的 Decimal 字符串规整为普通十进制串，
// 保证 round-trip 友好且不含 'E'（仍为合法 JSON 数字）。
std::string normalize_decimal_string(const std::string& sci) {
	size_t epos = sci.find('E');
	if (epos == std::string::npos) epos = sci.find('e');
	if (epos == std::string::npos) return sci;

	std::string sign;
	std::string mant = sci.substr(0, epos);
	if (!mant.empty() && mant[0] == '-') {
		sign = "-";
		mant = mant.substr(1);
	}
	int exp = std::stoi(sci.substr(epos + 1));

	size_t dot = mant.find('.');
	std::string digits;
	if (dot == std::string::npos) {
		digits = mant;
	} else {
		std::string intpart = mant.substr(0, dot);
		std::string fracpart = mant.substr(dot + 1);
		exp -= static_cast<int>(fracpart.size());
		digits = intpart + fracpart;
	}

	// value = digits × 10^exp
	if (exp >= 0) {
		return sign + digits + std::string(static_cast<size_t>(exp), '0');
	}
	int frac = -exp;
	if (frac >= static_cast<int>(digits.size())) {
		return sign + "0." + std::string(static_cast<size_t>(frac) - digits.size(), '0') + digits;
	}
	size_t cut = static_cast<size_t>(digits.size()) - static_cast<size_t>(frac);
	return sign + digits.substr(0, cut) + "." + digits.substr(cut);
}

// 字符串转义（ensure_ascii=true：非 ASCII 按 UTF-8 解码为码点后转 \uXXXX）。
void json_escape_string(const std::string& s, std::string& out) {
	out.push_back('"');
	size_t i = 0;
	size_t n = s.size();
	while (i < n) {
		unsigned char c = static_cast<unsigned char>(s[i]);
		switch (c) {
			case '"': out += "\\\""; i++; break;
			case '\\': out += "\\\\"; i++; break;
			case '\b': out += "\\b"; i++; break;
			case '\f': out += "\\f"; i++; break;
			case '\n': out += "\\n"; i++; break;
			case '\r': out += "\\r"; i++; break;
			case '\t': out += "\\t"; i++; break;
			default:
				if (c < 0x20) {
					char buf[16];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
					i++;
				} else if (c < 0x80) {
					out.push_back(static_cast<char>(c));
					i++;
				} else {
					// 解码一个 UTF-8 码点
					uint32_t cp;
					int extra;
					if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
					else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
					else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
					else { cp = 0xFFFD; extra = 0; } // 非法前导字节
					bool ok = (extra > 0);
					for (int k = 0; k < extra; ++k) {
						if (i + 1 >= n) { ok = false; break; }
						i++;
						unsigned char cont = static_cast<unsigned char>(s[i]);
						if ((cont & 0xC0) != 0x80) { ok = false; break; }
						cp = (cp << 6) | (cont & 0x3F);
					}
					i++;
					if (!ok) cp = 0xFFFD;
					if (cp <= 0xFFFF) {
						char buf[16];
						std::snprintf(buf, sizeof(buf), "\\u%04x", cp);
						out += buf;
					} else {
						uint32_t u = cp - 0x10000;
						uint32_t hi = 0xD800 + (u >> 10);
						uint32_t lo = 0xDC00 + (u & 0x3FF);
						char buf[16];
						std::snprintf(buf, sizeof(buf), "\\u%04x", hi);
						out += buf;
						std::snprintf(buf, sizeof(buf), "\\u%04x", lo);
						out += buf;
					}
				}
		}
	}
	out.push_back('"');
}

struct DumpOptions {
	bool        pretty     = false; // 是否美化（indent 非空）
	std::string indent_unit;        // 每层缩进串（"  " / "\t" / 自定义）
	bool        sort_keys  = false;
	bool        has_sep    = false; // separators 是否显式提供
	std::string item_sep    = ", ";  // 默认紧凑分隔符
	std::string key_sep     = ": ";  // 默认键分隔符
};

std::string indent_of(int depth, const DumpOptions& opt) {
	std::string r;
	for (int i = 0; i < depth; ++i) r += opt.indent_unit;
	return r;
}

// 序列化一个值到 out（depth 仅用于美化缩进）。
void serialize(Object* obj, int depth, const DumpOptions& opt, std::string& out) {
	if (obj == None::instance) { out += "null"; return; }
	// Boolean 须先于 Integer 判定（Boolean 是 Integer 的 C++ 子类）。
	if (IsExact<Boolean>(obj)) {
		out += (static_cast<Integer*>(obj)->get_value() != 0 ? "true" : "false");
		return;
	}
	if (IsIntegerExact(obj)) {
		out += std::to_string(static_cast<Integer*>(obj)->get_value());
		return;
	}
	if (IsExact<Decimal>(obj)) {
		out += normalize_decimal_string(static_cast<Decimal*>(obj)->to_string());
		return;
	}
	if (IsExact<Float>(obj)) {
		double v = static_cast<Float*>(obj)->get_value();
		if (!std::isfinite(v)) {
			throw ValueError("json.dump(): cannot serialize non-finite float (inf/nan)");
		}
		std::string s;
		{
			char buf[64];
			auto res = std::to_chars(buf, buf + sizeof(buf), v);
			s.assign(buf, res.ptr);
		}
		// 保证含 '.' 或 'e'，对齐 Python（如 1.0 -> "1.0"）。
		if (s.find('.') == std::string::npos &&
		    s.find('e') == std::string::npos &&
		    s.find('E') == std::string::npos) {
			s += ".0";
		}
		out += s;
		return;
	}
	if (IsString(obj)) {
		json_escape_string(static_cast<String*>(obj)->get_value(), out);
		return;
	}
	if (IsExact<List>(obj) || IsExact<FixedList>(obj)) {
		bool is_list = IsExact<List>(obj);
		size_t count = is_list ? static_cast<List*>(obj)->size()
		                       : static_cast<FixedList*>(obj)->size();
		if (count == 0) { out += "[]"; return; }
		if (opt.pretty) {
			out += "[";
			for (size_t i = 0; i < count; ++i) {
				if (i > 0) out += opt.item_sep;
				out += "\n" + indent_of(depth + 1, opt);
				Object* el = is_list ? static_cast<List*>(obj)->at(i)
				                     : static_cast<FixedList*>(obj)->at(i);
				serialize(el, depth + 1, opt, out);
			}
			out += "\n" + indent_of(depth, opt) + "]";
		} else {
			out += "[";
			for (size_t i = 0; i < count; ++i) {
				if (i > 0) out += opt.item_sep;
				Object* el = is_list ? static_cast<List*>(obj)->at(i)
				                     : static_cast<FixedList*>(obj)->at(i);
				serialize(el, depth, opt, out);
			}
			out += "]";
		}
		return;
	}
	if (IsExact<Map>(obj)) {
		Map* m = static_cast<Map*>(obj);
		std::vector<std::pair<Object*, Object*>> entries = m->entries();
		if (entries.empty()) { out += "{}"; return; }
		if (opt.sort_keys) {
			// 先取出各键的字符串表示，再按字符串稳定排序。
			// 比较器只做平凡字符串比较（无虚调用、不抛异常），
			// 避免 std::stable_sort 在比较器内触发未定义行为。
			std::vector<std::pair<std::string, size_t>> kv;
			kv.reserve(entries.size());
			for (size_t i = 0; i < entries.size(); ++i) {
				Object* k = entries[i].first;
				std::string ks;
				if (IsString(k)) {
					// String 键直接取内部值（无需额外引用管理）。
					ks = static_cast<String*>(k)->get_value();
				} else {
					// 非 String 键（序列化时会抛 TypeError）：用 __raw_string__
					// 取得新 String 并正确释放，避免误 Decref __string__() 返回的 this。
					Object* r = k->__raw_string__();
					ks = static_cast<String*>(r)->get_value();
					Decref(r);
				}
				kv.emplace_back(std::move(ks), i);
			}
			std::stable_sort(kv.begin(), kv.end(),
				[](const std::pair<std::string, size_t>& a,
				   const std::pair<std::string, size_t>& b) {
					return a.first < b.first;
				});
			std::vector<std::pair<Object*, Object*>> sorted;
			sorted.reserve(entries.size());
			for (auto& p : kv) sorted.push_back(entries[p.second]);
			entries = std::move(sorted);
		}
		if (opt.pretty) {
			out += "{";
			for (size_t i = 0; i < entries.size(); ++i) {
				if (i > 0) out += opt.item_sep;
				out += "\n" + indent_of(depth + 1, opt);
				Object* k = entries[i].first;
				if (!IsString(k)) {
					throw TypeError("json.dump(): keys must be str");
				}
				json_escape_string(static_cast<String*>(k)->get_value(), out);
				out += opt.key_sep;
				serialize(entries[i].second, depth + 1, opt, out);
			}
			out += "\n" + indent_of(depth, opt) + "}";
		} else {
			out += "{";
			for (size_t i = 0; i < entries.size(); ++i) {
				if (i > 0) out += opt.item_sep;
				Object* k = entries[i].first;
				if (!IsString(k)) {
					throw TypeError("json.dump(): keys must be str");
				}
				json_escape_string(static_cast<String*>(k)->get_value(), out);
				out += opt.key_sep;
				serialize(entries[i].second, depth, opt, out);
			}
			out += "}";
		}
		return;
	}
	throw TypeError("json.dump() cannot serialize '" + obj->type_name() + "' object");
}

// =============================================================
// 入口函数
// =============================================================

Object* _json_load(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("load", {
		Arg::Required("s"),
	});
	auto r = spec.Bind(args, kwargs);
	Object* s_o = r["s"];
	if (!IsString(s_o)) {
		throw TypeError("json.load(): argument 's' must be a string, got '" +
		                std::string(s_o->type_name()) + "'");
	}
	JsonParser parser(static_cast<String*>(s_o)->get_value());
	return parser.parse();
}

Object* _json_dump(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("dump", {
		Arg::Required("obj"),
		Arg::Optional("indent", None::instance),
		Arg::Optional("sort_keys", Boolean::False()),
		Arg::Optional("separators", None::instance),
	});
	auto r = spec.Bind(args, kwargs);
	Object* obj = r["obj"];
	// 接管参数所有权：保证递归序列化期间对象始终存活
	// （嵌套调用 json.dump(json.load(...)) 时 VM 可能提前释放该参数）。
	Incref(obj);

	DumpOptions opt;

	// indent：整数=空格数；"tab"/含 \t 的字符串=制表符；None=紧凑。
	Object* indent_o = r["indent"];
	if (indent_o != nullptr && indent_o != None::instance) {
		if (IsIntegerExact(indent_o)) {
			long long n = static_cast<Integer*>(indent_o)->get_value();
			if (n < 0) throw TypeError("json.dump(): indent must be >= 0");
			if (n > 16) n = 16;
			opt.pretty = true;
			opt.indent_unit = std::string(static_cast<size_t>(n), ' ');
		} else if (IsString(indent_o)) {
			std::string s = static_cast<String*>(indent_o)->get_value();
			opt.pretty = true;
			if (s == "tab" || s.find('\t') != std::string::npos) {
				opt.indent_unit = "\t";
			} else {
				opt.indent_unit = s;
			}
		} else {
			throw TypeError("json.dump(): indent must be an integer or string");
		}
	}

	// sort_keys：经 __boolean__ 真值化。
	Object* sk_o = r["sort_keys"];
	if (sk_o != nullptr && sk_o != None::instance) {
		Object* bt = sk_o->__boolean__();
		opt.sort_keys = (bt != nullptr && IsExact<Boolean>(bt) &&
		                 static_cast<Integer*>(bt)->get_value() != 0);
		if (bt != sk_o) Decref(bt);
	}

	// separators：长度为 2 的序列，元素均为 String。
	Object* sep_o = r["separators"];
	if (sep_o != nullptr && sep_o != None::instance) {
		bool is_seq = IsExact<List>(sep_o) || IsExact<FixedList>(sep_o);
		if (!is_seq) {
			throw TypeError("json.dump(): separators must be a sequence of 2 strings");
		}
		size_t n = IsExact<List>(sep_o) ? static_cast<List*>(sep_o)->size()
		                                : static_cast<FixedList*>(sep_o)->size();
		if (n != 2) {
			throw TypeError("json.dump(): separators must be a sequence of 2 strings");
		}
		Object* a = IsExact<List>(sep_o) ? static_cast<List*>(sep_o)->at(0)
		                                : static_cast<FixedList*>(sep_o)->at(0);
		Object* b = IsExact<List>(sep_o) ? static_cast<List*>(sep_o)->at(1)
		                                : static_cast<FixedList*>(sep_o)->at(1);
		if (!IsString(a) || !IsString(b)) {
			throw TypeError("json.dump(): separators must be a sequence of 2 strings");
		}
		opt.has_sep = true;
		opt.item_sep = static_cast<String*>(a)->get_value();
		opt.key_sep = static_cast<String*>(b)->get_value();
	}

	// 分隔符默认值：美化用紧凑分隔符 ("," / ": ")，压缩用 (", " / ": ")。
	if (!opt.has_sep) {
		opt.item_sep = opt.pretty ? "," : ", ";
		opt.key_sep = ": ";
	}

	std::string out;
	serialize(obj, 0, opt, out);
	Decref(obj);
	return String::FromCString(out.c_str());
}

// =============================================================
// 模块注册
// =============================================================
Module* make_json_module() {
	Module* mod = Module::New(kModuleName);

	// 规则：模块命名空间注入 __name__ = 模块名。
	mod->set_variable("__name__", String::FromCString(kModuleName));

	mod->set_function("load", _json_load, /*with_keywords=*/true);
	mod->set_function("dump", _json_dump, /*with_keywords=*/true);

	return mod;
}

} // namespace

// 动态库入口（符号名 PycpModule_json，按模块名导出）。
PYCP_EXPORT_MODULE(json) {
	return make_json_module();
}

} // namespace Pycp
