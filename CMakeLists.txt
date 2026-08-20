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
# Backend 运行时（静态库 / 动态库，可在构建时选择）
# ---------------------------------------------------------------------
# 通过 BUILD_RUNTIME_STATIC / BUILD_RUNTIME_SHARED 两个布尔选项选择
# 构建静态库还是动态库（默认静态：static ON、shared OFF）。
# pycp 可执行文件跟随选择进行链接（shared ON 时动态链接，否则静态）。
# 产物统一输出到 backend/bin/；关闭非必需的 Python 绑定 / 测试目标。
# =====================================================================
set(BUILD_RUNTIME_STATIC ON CACHE BOOL "Build static PycpRuntime library")
set(BUILD_RUNTIME_SHARED OFF CACHE BOOL "Build shared PycpRuntime library")
set(BUILD_PYTHON_BINDING OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)

# 合法性校验：至少构建一种库，否则无链接目标
if(NOT BUILD_RUNTIME_STATIC AND NOT BUILD_RUNTIME_SHARED)
    message(FATAL_ERROR "At least one of BUILD_RUNTIME_STATIC / BUILD_RUNTIME_SHARED must be ON.")
endif()

add_subdirectory(backend)

# =====================================================================
# 标准库动态库（io / Pycp）：编译为 .so/.dll，import 时动态加载
# ---------------------------------------------------------------------
# 在 backend 之后引入，使动态库可链接 PycpRuntime。产物输出到
# ${CMAKE_BINARY_DIR}/stdlib/，运行时可执行文件旁的 stdlib/ 目录加载。
# =====================================================================
add_subdirectory(builtin_libraries)

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

set(LOADER_SOURCES
    ${PYCP_FRONTEND_DIR}/src/PycpModuleLoader.cpp
)

set(LEXER_SOURCES
    ${PYCP_FRONTEND_DIR}/src/escape_handler.cpp
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
    ${LOADER_SOURCES}
    ${LEXER_SOURCES}
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
    # flex 库（libfl）提供 yywrap/main，但本工程 lexer 已 noyywrap 且自带
    # main，故通常无需链接；仅当 find_package 显式给出且非空时才链接，
    # 且允许通过 -DPYCP_SKIP_FLEX_LINK=ON 跳过（ASan 下 libfl 符号异常）。
    if(FLEX_LIBRARIES AND NOT PYCP_SKIP_FLEX_LINK)
        target_link_libraries(pycp PRIVATE ${FLEX_LIBRARIES})
    endif()
endif()

# 根据选项选择 runtime 库：shared ON 时动态链接，否则静态链接（默认）
if(BUILD_RUNTIME_SHARED)
    set(PYCP_RUNTIME_TARGET PycpRuntime_shared)
else()
    set(PYCP_RUNTIME_TARGET PycpRuntime_static)
endif()

# io/Pycp 库已改为动态库（stdlib/），import 时通过 LoadNativeModule 动态加载，
# 不再静态链接进 pycp。pycp 仅链接 PycpRuntime。
target_link_libraries(pycp PRIVATE ${PYCP_RUNTIME_TARGET})

# 原生扩展（dlopen 加载的 .so）需解析主程序的 C++ 符号（Module::New/New 等），
# 因此以 -rdynamic 导出全局符号（POSIX）。Windows 下静态链接需 --export-all-symbols。
if(UNIX AND NOT APPLE)
    target_link_options(pycp PRIVATE -rdynamic)
endif()

# 动态链接时设置 rpath：构建树内指向 backend/bin，安装后用 $ORIGIN
if(BUILD_RUNTIME_SHARED)
    set_target_properties(pycp PROPERTIES
        BUILD_RPATH "${PYCP_BACKEND_DIR}/bin"
        INSTALL_RPATH "$ORIGIN"
    )
endif()

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

# 动态链接时需同时安装动态库（与 pycp 同目录，配合 $ORIGIN rpath）
if(BUILD_RUNTIME_SHARED)
    install(TARGETS PycpRuntime_shared
        LIBRARY DESTINATION bin
    )
endif()

# =====================================================================
# SDK 打包：将 runtime 库与 AOT 所需头文件复制到 build/bin/
# ---------------------------------------------------------------------
# 形成自包含 SDK 目录，便于编译 AOT 生成的 .cpp：
#   g++ -std=c++17 -I build/bin <gen>.cpp -L build/bin -lPycpRuntime -o out
# =====================================================================
set(PYCP_SDK_DIR ${CMAKE_BINARY_DIR}/bin)

# AOT 生成代码编译所需的头文件（不含 VM/字节码解释专用头）
set(PYCP_AOT_HEADERS
    PycpConfig.hpp
    PycpObject.hpp
    PycpException.hpp
    PycpGC.hpp
    PycpEnvironment.hpp
    PycpFunction.hpp
    PycpNone.hpp
    PycpInteger.hpp
    PycpString.hpp
    PycpList.hpp
    PycpManager.hpp
    PycpModule.hpp
    PycpClass.hpp
    PycpBytecode.hpp
    PycpBytecodeVM.hpp
    PycpNativeExt.hpp
    PycpABI.hpp
)

# 仅复制实际构建的库到 SDK 目录
add_custom_command(TARGET pycp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PYCP_SDK_DIR}"
    COMMENT "Packaging runtime libraries and AOT headers into ${PYCP_SDK_DIR}"
)

if(BUILD_RUNTIME_STATIC)
    add_custom_command(TARGET pycp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:PycpRuntime_static>" "${PYCP_SDK_DIR}/"
    )
endif()

if(BUILD_RUNTIME_SHARED)
    # 确保动态库也被构建（pycp 静态链接时不依赖 shared 目标）
    add_dependencies(pycp PycpRuntime_shared)
    add_custom_command(TARGET pycp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:PycpRuntime_shared>" "${PYCP_SDK_DIR}/"
    )
endif()

foreach(hdr IN LISTS PYCP_AOT_HEADERS)
    add_custom_command(TARGET pycp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${PYCP_BACKEND_DIR}/include/${hdr}" "${PYCP_SDK_DIR}/"
    )
endforeach()

# =====================================================================
# 构建信息
# =====================================================================
message(STATUS "================================================")
message(STATUS "Project: Pycp v${PROJECT_VERSION}")
message(STATUS "Output executable: ${CMAKE_BINARY_DIR}/pycp")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "================================================")
