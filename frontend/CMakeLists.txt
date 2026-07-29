cmake_minimum_required(VERSION 3.10)

project(PycpCompiler
    VERSION 1.0.0
    LANGUAGES CXX
)

############################################################
# C++ Standard
############################################################

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

############################################################
# Flex & Bison
############################################################

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

#include(${BISON_USE_FILE})
#include(${FLEX_USE_FILE})

message(STATUS "Bison : ${BISON_EXECUTABLE}")
message(STATUS "Flex  : ${FLEX_EXECUTABLE}")

############################################################
# Source Files
############################################################

set(AST_SOURCES
    src/PycpAstNode.cpp
)

include_directories(include)

############################################################
# Flex / Bison
############################################################

set(BISON_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpParser.y
)

set(FLEX_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpLexer.l
)
############################################################
# Generate Parser
############################################################

message(STATUS "binary : ${CMAKE_CURRENT_BINARY_DIR}")

BISON_TARGET(
    PycpParser

    ${BISON_INPUT}

    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.cpp

    DEFINES_FILE
    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.hpp

    COMPILE_FLAGS
    "-v -Wcounterexamples"
)

############################################################
# Generate Lexer
############################################################

FLEX_TARGET(
    PycpLexer

    ${FLEX_INPUT}

    ${CMAKE_CURRENT_BINARY_DIR}/PycpLexer.cpp

    DEFINES_FILE
    ${CMAKE_CURRENT_BINARY_DIR}/PycpLexer.hpp
)

############################################################
# Lexer depends on Parser
############################################################

ADD_FLEX_BISON_DEPENDENCY(
    PycpLexer
    PycpParser
)

############################################################
# Executable
############################################################

add_executable(pycp

    ${AST_SOURCES}

    ${BISON_PycpParser_OUTPUTS}

    ${FLEX_PycpLexer_OUTPUTS}
)

############################################################
# Include Directories
############################################################

target_include_directories(pycp
    PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}

    ${CMAKE_CURRENT_BINARY_DIR}
)

############################################################
# Compile Options
############################################################

if(MSVC)

    target_compile_options(pycp
        PRIVATE
        /W4
        /MP
    )

else()

    target_compile_options(pycp
        PRIVATE

        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )

endif()

############################################################
# Link Flex Library (Linux only)
############################################################

if(UNIX AND NOT APPLE)
    target_link_libraries(pycp PRIVATE ${FLEX_LIBRARIES})
endif()

############################################################
# Debug Macro
############################################################

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(pycp PRIVATE DEBUG=1)
else()
    target_compile_definitions(pycp PRIVATE NDEBUG=1)
endif()

############################################################
# Install
############################################################

install(TARGETS pycp
    RUNTIME DESTINATION bin
)

############################################################
# Testing
############################################################

enable_testing()

add_test(
    NAME ParserTest
    COMMAND pycp
)

############################################################
# Utility Targets
############################################################

add_custom_target(show-generated

    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "========== Generated Files =========="
    COMMAND ${CMAKE_COMMAND} -E echo "Parser : ${BISON_PycpParser_OUTPUT_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E echo "Header : ${BISON_PycpParser_OUTPUT_HEADER}"
    COMMAND ${CMAKE_COMMAND} -E echo "Lexer  : ${FLEX_PycpLexer_OUTPUTS}"
    COMMAND ${CMAKE_COMMAND} -E echo "===================================="
)

add_custom_target(clean-generated

    COMMAND ${CMAKE_COMMAND} -E remove

    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.hpp
    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.output

    ${CMAKE_CURRENT_BINARY_DIR}/PycpLexer.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/PycpLexer.hpp
)