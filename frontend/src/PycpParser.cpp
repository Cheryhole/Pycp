#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "PycpParser.tab.hpp"
#include "PycpAstNode.hpp"

using namespace Pycp::Ast;

// 外部声明
extern FILE* yyin;
extern int yyparse();
extern Program* parse_result;

// 解析字符串
Program* parse_string(const std::string& source) {
    // 使用内存缓冲区
    yy_scan_string(source.c_str());
    yyparse();
    yylex_destroy();
    return parse_result;
}

int main(int argc, char* argv[]) {
  Program* program = nullptr;
    
	std::string source = "1 + (2 - -3) * 4 / 5 ";

	program = parse_string(source);
	
	
	if (program) {
			std::cout << "\n=== AST ===" << std::endl;
			std::cout << program->to_string() << std::endl;
			delete program;
			return 0;
	}
    
    return 1;
}