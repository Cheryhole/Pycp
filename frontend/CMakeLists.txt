# =====================================================================
# Pycp Compiler 前端构建脚本
# ---------------------------------------------------------------------
# 构建 pycp 可执行文件：整合 Flex/Bison 解析器、AST、Codegen、AOT
# 与 backend 运行时，提供完整的编译 / 执行命令行接口。
# =====================================================================

cmake_minimum_required(VERSION 3.10)

project(PycpCompiler
    VERSION 1.0.0
    LANGUAGES CXX
)

# =====================================================================
# C++ 标准
# =====================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# =====================================================================
# Flex & Bison
# =====================================================================
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

message(STATUS "Bison : ${BISON_EXECUTABLE}")
message(STATUS "Flex  : ${FLEX_EXECUTABLE}")

# =====================================================================
# 源文件
# =====================================================================
set(AST_SOURCES
    src/PycpAstNode.cpp
    src/escape_handler.cpp
)

set(CODGEN_SOURCES
    src/PycpCodegen.cpp
)

set(LOADER_SOURCES
    src/PycpModuleLoader.cpp
)

set(AOT_SOURCES
    src/aot/PycpAot.cpp
    src/aot/PycpAotSdkLocator.cpp
    src/aot/PycpModulePlan.cpp
    src/aot/PycpProjectSpec.cpp
    src/aot/PycpBuildScriptGenerator.cpp
    src/aot/PycpCMakeGenerator.cpp
    src/aot/PycpAotProject.cpp
)

set(PREPROCESSOR_SOURCES
    src/preprocessor/PycpPreprocessor.cpp
)

set(PREPROCESSOR_BISON_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/src/preprocessor/PycpPreprocessorParser.y
)

set(PREPROCESSOR_FLEX_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/src/preprocessor/PycpPreprocessorLexer.l
)

# 主程序入口：位于项目顶层目录
set(MAIN_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../PycpMain.cpp
)

# 行编辑库：统一使用 cpp-linenoise（C++17 头文件库，跨平台，原生支持 Windows 控制台）。
# 仅用于无参数 REPL 路径，header-only，无需编译源码。
set(LINENOISE_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cpp-linenoise
)

include_directories(include)

# =====================================================================
# Flex / Bison 输入文件
# =====================================================================
set(BISON_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpParser.y
)

set(FLEX_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpLexer.l
)

# =====================================================================
# 生成解析器与词法分析器（Flex / Bison）
# ---------------------------------------------------------------------
# 与顶层 CMakeLists.txt 保持同一套写法：手写 add_custom_command，不用
# bison_target / flex_target / add_flex_bison_dependency。
# 原因：Windows 下的 win_flex / win_bison（winflexbison）并发不安全，它用
# _tempnam + fopen 申请临时文件，并行运行的多个实例会共用同一临时文件互相
# 覆盖，产出"未展开的 m4 骨架"（残留 M4_YY_PREFIX / m4_ifdef），编译时表现
# 为数百行 '#endif without #if' / "stray '#'" 等令人困惑的错误。
# 上游已知缺陷：lexxmark/winflexbison#86、westes/flex#580。
# 因此把 4 步生成串成一条依赖链（仅 WIN32；GNU flex/bison 用 mkstemp，
# 并发安全，保持并行），并在每条命令末尾执行生成物自检。
# =====================================================================
message(STATUS "binary : ${CMAKE_CURRENT_BINARY_DIR}")

# 生成产物（显式列出，便于建立依赖链与自检）
set(PYCP_GEN_PARSER_CPP    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.cpp)
set(PYCP_GEN_PARSER_HPP    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.hpp)
set(PYCP_GEN_PARSER_REPORT ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.output)

set(PYCP_GEN_LEXER_CPP     ${CMAKE_CURRENT_BINARY_DIR}/PycpLexer.cpp)
set(PYCP_GEN_LEXER_HPP     ${CMAKE_CURRENT_BINARY_DIR}/PycpLexer.hpp)

set(PYCP_GEN_PP_PARSER_CPP    ${CMAKE_CURRENT_BINARY_DIR}/PycpPreprocessorParser.cpp)
set(PYCP_GEN_PP_PARSER_HPP    ${CMAKE_CURRENT_BINARY_DIR}/PycpPreprocessorParser.hpp)
set(PYCP_GEN_PP_PARSER_REPORT ${CMAKE_CURRENT_BINARY_DIR}/PycpPreprocessorParser.output)

set(PYCP_GEN_PP_LEXER_CPP     ${CMAKE_CURRENT_BINARY_DIR}/PycpPreprocessorLexer.cpp)
set(PYCP_GEN_PP_LEXER_HPP     ${CMAKE_CURRENT_BINARY_DIR}/PycpPreprocessorLexer.hpp)

set(PYCP_GEN_CHECK_SCRIPT
    ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/PycpCheckGenerated.cmake)

if(WIN32)
    set(PYCP_GEN_PP_PARSER_EXTRA_DEPS
        ${PYCP_GEN_LEXER_CPP} ${PYCP_GEN_LEXER_HPP})
else()
    set(PYCP_GEN_PP_PARSER_EXTRA_DEPS)
endif()

# 1) 主解析器：PycpParser.y -> PycpParser.cpp / .hpp（+ .output 报告）
add_custom_command(
    OUTPUT
        ${PYCP_GEN_PARSER_CPP}
        ${PYCP_GEN_PARSER_HPP}
        ${PYCP_GEN_PARSER_REPORT}
    # bison / flex 不会自建输出目录，故每条生成命令自带建目录步骤。
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${BISON_EXECUTABLE}
        -v -Wcounterexamples
        --defines=${PYCP_GEN_PARSER_HPP}
        -o ${PYCP_GEN_PARSER_CPP}
        ${BISON_INPUT}
    COMMAND ${CMAKE_COMMAND}
        -DFILE=${PYCP_GEN_PARSER_CPP}
        -DEXPECT=Pycpparse
        -DLABEL=PycpParser.cpp
        -P ${PYCP_GEN_CHECK_SCRIPT}
    DEPENDS ${BISON_INPUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "[BISON][PycpParser] Generating parser"
    VERBATIM
)

# 2) 主词法器：PycpLexer.l -> PycpLexer.cpp / .hpp（依赖解析器头文件）
add_custom_command(
    OUTPUT
        ${PYCP_GEN_LEXER_CPP}
        ${PYCP_GEN_LEXER_HPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${FLEX_EXECUTABLE}
        --header-file=${PYCP_GEN_LEXER_HPP}
        -o${PYCP_GEN_LEXER_CPP}
        ${FLEX_INPUT}
    COMMAND ${CMAKE_COMMAND}
        -DFILE=${PYCP_GEN_LEXER_CPP}
        -DEXPECT=Pycp_create_buffer
        -DLABEL=PycpLexer.cpp
        -P ${PYCP_GEN_CHECK_SCRIPT}
    DEPENDS ${FLEX_INPUT} ${PYCP_GEN_PARSER_HPP}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "[FLEX][PycpLexer] Generating scanner"
    VERBATIM
)

# 3) 预处理指令解析器（独立于主解析器）
add_custom_command(
    OUTPUT
        ${PYCP_GEN_PP_PARSER_CPP}
        ${PYCP_GEN_PP_PARSER_HPP}
        ${PYCP_GEN_PP_PARSER_REPORT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${BISON_EXECUTABLE}
        -v -Wcounterexamples
        --defines=${PYCP_GEN_PP_PARSER_HPP}
        -o ${PYCP_GEN_PP_PARSER_CPP}
        ${PREPROCESSOR_BISON_INPUT}
    COMMAND ${CMAKE_COMMAND}
        -DFILE=${PYCP_GEN_PP_PARSER_CPP}
        -DEXPECT=Pycppparse
        -DLABEL=PycpPreprocessorParser.cpp
        -P ${PYCP_GEN_CHECK_SCRIPT}
    DEPENDS ${PREPROCESSOR_BISON_INPUT} ${PYCP_GEN_PP_PARSER_EXTRA_DEPS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "[BISON][PycpPreprocessorParser] Generating parser"
    VERBATIM
)

# 4) 预处理指令词法器（依赖其解析器头文件）
add_custom_command(
    OUTPUT
        ${PYCP_GEN_PP_LEXER_CPP}
        ${PYCP_GEN_PP_LEXER_HPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${FLEX_EXECUTABLE}
        --header-file=${PYCP_GEN_PP_LEXER_HPP}
        -o${PYCP_GEN_PP_LEXER_CPP}
        ${PREPROCESSOR_FLEX_INPUT}
    COMMAND ${CMAKE_COMMAND}
        -DFILE=${PYCP_GEN_PP_LEXER_CPP}
        -DEXPECT=Pycpp_create_buffer
        -DLABEL=PycpPreprocessorLexer.cpp
        -P ${PYCP_GEN_CHECK_SCRIPT}
    DEPENDS
        ${PREPROCESSOR_FLEX_INPUT}
        ${PYCP_GEN_PP_PARSER_HPP}
        ${PYCP_GEN_PP_PARSER_CPP}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "[FLEX][PycpPreprocessorLexer] Generating scanner"
    VERBATIM
)

# =====================================================================
# Backend 运行时（通过 add_subdirectory 引入）
# ---------------------------------------------------------------------
# 引入 backend 运行时：提供 Object/GC/ABI/Bytecode/VM 实现。
# backend 会构建 PycpRuntime_static 静态库目标。
# =====================================================================
set(BUILD_PYTHON_BINDING OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../backend
                 ${CMAKE_CURRENT_BINARY_DIR}/backend)

# =====================================================================
# 可执行文件
# =====================================================================
add_executable(pycp
    ${MAIN_SOURCE}
    ${AST_SOURCES}
    ${CODGEN_SOURCES}
    ${LOADER_SOURCES}
    ${AOT_SOURCES}
    ${PREPROCESSOR_SOURCES}

    # Flex / Bison 生成物（见上方"生成解析器与词法分析器"一节）
    ${PYCP_GEN_PARSER_CPP}
    ${PYCP_GEN_PARSER_HPP}
    ${PYCP_GEN_LEXER_CPP}
    ${PYCP_GEN_LEXER_HPP}
    ${PYCP_GEN_PP_PARSER_CPP}
    ${PYCP_GEN_PP_PARSER_HPP}
    ${PYCP_GEN_PP_LEXER_CPP}
    ${PYCP_GEN_PP_LEXER_HPP}
)

# PycpParser.cpp（生成物）会 #include "PycpLexer.hpp"（PycpParser.y 顶部），
# 而该头文件也是生成物：显式声明依赖，避免并行构建下"编译先于生成"。
set_source_files_properties(${PYCP_GEN_PARSER_CPP} PROPERTIES
    OBJECT_DEPENDS ${PYCP_GEN_LEXER_HPP}
)

# =====================================================================
# 头文件搜索路径
# =====================================================================
target_include_directories(pycp
    PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}

    # 预处理头文件（include/preprocessor）
    ${CMAKE_CURRENT_SOURCE_DIR}/include

    # backend 头文件（Pycp.hpp / PycpBytecode.hpp / PycpBytecodeVM.hpp 等）
    ${CMAKE_CURRENT_SOURCE_DIR}/../backend/include

    # cpp-linenoise 头文件（REPL 行编辑，header-only）
    ${LINENOISE_INCLUDE_DIR}
)

# =====================================================================
# 编译选项
# =====================================================================
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

# =====================================================================
# 链接 Flex 库（仅 Linux）与 backend 运行时
# =====================================================================
if(UNIX AND NOT APPLE)
    target_link_libraries(pycp PRIVATE ${FLEX_LIBRARIES})
endif()

target_link_libraries(pycp PRIVATE PycpRuntime_static)

# =====================================================================
# 调试宏
# =====================================================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(pycp PRIVATE DEBUG=1)
else()
    target_compile_definitions(pycp PRIVATE NDEBUG=1)
endif()

# =====================================================================
# 安装
# =====================================================================
install(TARGETS pycp
    RUNTIME DESTINATION bin
)

# =====================================================================
# 测试
# =====================================================================
enable_testing()

add_test(
    NAME ParserTest
    COMMAND pycp
)

# =====================================================================
# 实用目标
# =====================================================================
add_custom_target(show-generated
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "========== Generated Files =========="
    COMMAND ${CMAKE_COMMAND} -E echo "Parser : ${PYCP_GEN_PARSER_CPP}"
    COMMAND ${CMAKE_COMMAND} -E echo "Header : ${PYCP_GEN_PARSER_HPP}"
    COMMAND ${CMAKE_COMMAND} -E echo "Lexer  : ${PYCP_GEN_LEXER_CPP}"
    COMMAND ${CMAKE_COMMAND} -E echo "PPParser : ${PYCP_GEN_PP_PARSER_CPP}"
    COMMAND ${CMAKE_COMMAND} -E echo "PPLexer  : ${PYCP_GEN_PP_LEXER_CPP}"
    COMMAND ${CMAKE_COMMAND} -E echo "===================================="
)

add_custom_target(clean-generated
    COMMAND ${CMAKE_COMMAND} -E remove

    ${PYCP_GEN_PARSER_CPP}
    ${PYCP_GEN_PARSER_HPP}
    ${PYCP_GEN_PARSER_REPORT}

    ${PYCP_GEN_LEXER_CPP}
    ${PYCP_GEN_LEXER_HPP}

    ${PYCP_GEN_PP_PARSER_CPP}
    ${PYCP_GEN_PP_PARSER_HPP}
    ${PYCP_GEN_PP_PARSER_REPORT}

    ${PYCP_GEN_PP_LEXER_CPP}
    ${PYCP_GEN_PP_LEXER_HPP}
)
