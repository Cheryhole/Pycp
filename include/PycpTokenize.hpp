#ifndef __PYCP_TOKENIZE_HPP__
#define __PYCP_TOKENIZE_HPP__ 1

#include "Pycp.hpp"
#include <fstream>
#include <stack>
#include <string>
#include "lexy/dsl.hpp"

namespace Pycp{
namespace Tokenize{

struct None{
	static constexpr auto rule = LEXY_LIT("None");
};

struct True{
	static constexpr auto rule = LEXY_LIT("True");
};

struct False{
	static constexpr auto rule = LEXY_LIT("False");
};

struct Decimal{
	static constexpr auto rule = lexy::dsl::decimal();
};

enum class Tokens : uint16_t{
	// constants
	T_NONE = PycpSize_0,
	T_TRUE,
	T_FALSE,

	// comment
	T_COMMENT,// //
	T_MUTILINE_COMMENT, // /* */

	// objects
	T_INTEGER, // 1 2 0x1 0b1 0o1
	T_DECIMAL, // 0.1 -0.2 0.2e-2
	T_STRING, // "" ''
	T_LIST, // []
	T_TUPLE, // ()
	T_OBJECT, // {}

	// identifier
	T_IDENTIFIER,

	// operators
	OP_PLUS,
	OP_MINUS,
	OP_MULTIPLY,
	OP_DIVIDE,

	// delimiters
	T_LPAREN,
	T_RPAREN,
	T_LBRACKET,
	T_RBRACKET,
	T_LBRACE,
	T_RBRACE,
	T_COMMA,
	T_COLON,
	T_SEMICOLON,
	T_DOT,
	T_OF,

	// keywords
	KW_IF,
	KW_ELIF,
	KW_ELSE,
	KW_FOR,
	KW_WHILE,
	KW_FUNC,
	KW_RETURN,
	KW_BREAK,
	KW_CONTINUE,
	KW_PASS,
	KW_AND,
	KW_OR,
	KW_NOT,
	KW_IN,
	KW_IS,
	KW_IMPORT,
};

enum class ByteCodes : uint8_t{
	LOAD_CONST = PycpSize_0,
	LOAD_GLOBAL,
	LOAD_LOCAL,
	STORE_GLOBAL,
	STORE_LOCAL,

	OPERATOR,
	CALL,
};

struct Token{
	Tokens token;
	std::string value;
};

class Tokenizer{
	private:
		Tokenizer(const Tokenizer&) = delete;
		Tokenizer& operator=(const Tokenizer&) = delete;

		PycpSize_t m_line;
		PycpSize_t m_column;
		Tokens m_token;
		std::ifstream m_file;
		std::stack<ByteCodes> m_stack;
		bool flag_instring;
		bool flag_inlist;
		bool flag_intuple;
		bool flag_inobject;
		bool flag_incomment;

	public:
		Tokenizer(const char* inputfile);
		~Tokenizer();

		void scan();
		Tokens next();
		
}; // class PycpTokenizer

} // namespace Tokenize
} // namespace Pycp

#endif // __PYCP_TOKENIZE_HPP__