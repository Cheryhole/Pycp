// PycpParser.cpp — frontend entry point.
//
// This file is the main program for the `pycp` executable. It drives the
// Flex/Bison based parser (generated from PycpLexer.l / PycpParser.y) through
// the parsef() helper, which reads a source file and returns the AST.
//
// Statement separation policy (enforced by the grammar in PycpParser.y):
//   * A newline is the ONLY separator between statements.
//   * Leading/trailing newlines are optional.
//   * Consecutive newlines (blank lines) are allowed and collapsed.

#include <iostream>
#include <string>

#include "PycpAstNode.hpp"

// Provided by the generated parser (PycpParser.cpp from PycpParser.y).
extern Pycp::Ast::Node* parsef(const std::string& path);
extern int Pycp_parse_error_count;

int main(int argc, char* argv[]) {
    // Default to the first command-line argument; if none is given, the
    // caller must specify an input file.
    if (argc < 2) {
        std::cerr << "Error: no input file specified." << std::endl;
        return 1;
    }

    const std::string path = argv[1];
    Pycp::Ast::Node* asttree = parsef(path);

    if (!asttree) {
        std::cerr << "Error: failed to build AST." << std::endl;
        return 1;
    }

    std::cout << asttree->to_string() << std::endl;

    return Pycp_parse_error_count > 0 ? 1 : 0;
}
