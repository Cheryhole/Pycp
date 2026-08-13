# =====================================================================
# Pycp 项目总构建脚本
# ---------------------------------------------------------------------
# 统一构建入口：编排 backend 运行时（静态库）、frontend 前端源码
# （AST / Codegen / AOT / Flex/Bison 生成）与顶层 PycpMain.cpp，
# 最终产出 pycp 可执行文件。
#
# 用法：
#   cmake -S . -B build
#   cmake --build build
# =====================================================================

cmake_minimum_required(VERSION 3.15)

project(Pycp
    VERSION 1.0.0
    LANGUAGES CXX
)

# =====================================================================
# 基础编译配置
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
# Backend 运行时（仅需静态库）
# ---------------------------------------------------------------------
# 作为子目录引入 backend，仅构建 PycpRuntime_static 静态库，
# 关闭非必需的动态库 / Python 绑定 / 测试目标以加速全量编译。
# =====================================================================
set(BUILD_RUNTIME_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_PYTHON_BINDING OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(backend)

# =====================================================================
# 目录路径
# =====================================================================
set(PYCP_FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/frontend)
set(PYCP_BACKEND_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/backend)
set(PYCP_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

file(MAKE_DIRECTORY ${PYCP_GENERATED_DIR})

# =====================================================================
# 源文件
# =====================================================================
set(AST_SOURCES
    ${PYCP_FRONTEND_DIR}/src/PycpAstNode.cpp
)

set(CODGEN_SOURCES
    ${PYCP_FRONTEND_DIR}/src/PycpCodegen.cpp
)

set(AOT_SOURCES
    ${PYCP_FRONTEND_DIR}/src/PycpAot.cpp
)

# 主程序入口：位于项目顶层目录
set(MAIN_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/PycpMain.cpp
)

# =====================================================================
# Flex / Bison 输入文件
# =====================================================================
set(BISON_INPUT
    ${PYCP_FRONTEND_DIR}/src/PycpParser.y
)

set(FLEX_INPUT
    ${PYCP_FRONTEND_DIR}/src/PycpLexer.l
)

# =====================================================================
# 生成解析器
# =====================================================================
bison_target(
    PycpParser

    ${BISON_INPUT}

    ${PYCP_GENERATED_DIR}/PycpParser.cpp

    DEFINES_FILE
    ${PYCP_GENERATED_DIR}/PycpParser.hpp

    COMPILE_FLAGS
    "-v -Wcounterexamples"
)

# =====================================================================
# 生成词法分析器
# =====================================================================
flex_target(
    PycpLexer

    ${FLEX_INPUT}

    ${PYCP_GENERATED_DIR}/PycpLexer.cpp

    DEFINES_FILE
    ${PYCP_GENERATED_DIR}/PycpLexer.hpp
)

# =====================================================================
# 词法分析器依赖解析器
# =====================================================================
add_flex_bison_dependency(
    PycpLexer
    PycpParser
)

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
    ${PYCP_FRONTEND_DIR}/include
    ${PYCP_BACKEND_DIR}/include
    ${PYCP_GENERATED_DIR}
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
# 构建信息
# =====================================================================
message(STATUS "================================================")
message(STATUS "Project: Pycp v${PROJECT_VERSION}")
message(STATUS "Output executable: ${CMAKE_BINARY_DIR}/pycp")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "================================================")
