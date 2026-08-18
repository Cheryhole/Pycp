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
)

set(CODGEN_SOURCES
    src/PycpCodegen.cpp
)

set(AOT_SOURCES
    src/PycpAot.cpp
)

# 主程序入口：位于项目顶层目录
set(MAIN_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../PycpMain.cpp
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
# 生成解析器
# =====================================================================
message(STATUS "binary : ${CMAKE_CURRENT_BINARY_DIR}")

bison_target(
    PycpParser

    ${BISON_INPUT}

    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.cpp

    DEFINES_FILE
    ${CMAKE_CURRENT_BINARY_DIR}/PycpParser.hpp

    COMPILE_FLAGS
    "-v -Wcounterexamples"
)

# =====================================================================
# 生成词法分析器
# =====================================================================
flex_target(
    PycpLexer

    ${FLEX_INPUT}

    ${CMAKE_CURRENT_BINARY_DIR}/PycpLexer.cpp

    DEFINES_FILE
    ${CMAKE_CURRENT_BINARY_DIR}/PycpLexer.hpp
)

# =====================================================================
# 词法分析器依赖解析器
# =====================================================================
add_flex_bison_dependency(
    PycpLexer
    PycpParser
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
    ${AOT_SOURCES}
    ${BISON_PycpParser_OUTPUTS}
    ${FLEX_PycpLexer_OUTPUTS}
)

# =====================================================================
# 头文件搜索路径
# =====================================================================
target_include_directories(pycp
    PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}

    # backend 头文件（Pycp.hpp / PycpBytecode.hpp / PycpBytecodeVM.hpp 等）
    ${CMAKE_CURRENT_SOURCE_DIR}/../backend/include
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
