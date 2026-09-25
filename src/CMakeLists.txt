# =====================================================================
# Pycp 组件分层构建脚本
# ---------------------------------------------------------------------
# 定义两个库：
#   PycpRuntime  = object + bytecode + vm + abi（静态库 PycpRuntime_static
#                  + 动态库 PycpRuntime_shared，输出名均为 PycpRuntime）
#   PycpFrontend = parser + codegen + loader + aot（OBJECT 库 PycpFrontend_obj，
#                  由宿主 pycp 直接并入——OBJECT 库保证静态自注册对象
#                  （如 CMake 生成器）不被静态库链接期裁撤）
#
# 并由本文件完成 Flex/Bison 解析器/词法器生成（源位于 src/parser/）。
#
# 组件前缀 include 约定：所有 #include 形如 "object/PycpObject.hpp"、
# "bytecode/PycpBytecode.hpp"，以 src/ 作为搜索根（-I ${CMAKE_CURRENT_SOURCE_DIR}）。
# =====================================================================

# =====================================================================
# 输出目录
# ---------------------------------------------------------------------
# 运行时库输出到 build/backend/（保持既有构建树布局，使 pycp / stdlib 的
# rpath 与 dist 收集逻辑零改动）。mpdecimal 也随之输出到此。
# =====================================================================
set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/backend)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR})

set(PYCP_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PYCP_GENERATED_DIR})

# =====================================================================
# 第三方库：mpdecimal（libmpdec，CPython decimal 模块底层实现）
# ---------------------------------------------------------------------
# PycpDecimal（精确小数）依赖。以源码形式 vendor 在 third_party/mpdecimal，
# 编译为 mpdec 静态库。MSVC 走 CONFIG_64 + ANSI（纯 C，无 MASM 依赖）。
# =====================================================================
set(PYCP_MPDECIMAL_DIR ${CMAKE_SOURCE_DIR}/third_party/mpdecimal)
if(EXISTS ${PYCP_MPDECIMAL_DIR}/CMakeLists.txt)
    add_subdirectory(${PYCP_MPDECIMAL_DIR} ${CMAKE_BINARY_DIR}/mpdecimal)
else()
    message(FATAL_ERROR
        "未找到 third_party/mpdecimal（PycpDecimal 依赖的 mpdecimal 源码）。"
        "请确认仓库完整（third_party/mpdecimal/libmpdec/*.c 应存在）。")
endif()

# =====================================================================
# Flex / Bison 输入文件
# =====================================================================
set(BISON_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/parser/PycpParser.y
)
set(FLEX_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/parser/PycpLexer.l
)
set(PREPROCESSOR_BISON_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/parser/preprocessor/PycpPreprocessorParser.y
)
set(PREPROCESSOR_FLEX_INPUT
    ${CMAKE_CURRENT_SOURCE_DIR}/parser/preprocessor/PycpPreprocessorLexer.l
)

# =====================================================================
# 生成解析器与词法分析器（Flex / Bison）
# ---------------------------------------------------------------------
# 不使用 bison_target / flex_target / add_flex_bison_dependency，改为手写
# add_custom_command。原因：Windows 下的 win_flex / win_bison
# （winflexbison）并发不安全——它用 _tempnam + fopen 申请临时文件（TOCTOU），
# 并行运行的多个实例会共用同一个临时文件并互相覆盖，产出"未展开的 m4 骨架"
# （残留 M4_YY_PREFIX / m4_ifdef / [[#ifndef 等模板文本）。拿去编译会得到
# 数百行毫无意义的错误。上游已知缺陷：
#   https://github.com/lexxmark/winflexbison/issues/86
#   https://github.com/westes/flex/issues/580
# Linux/WSL 的 GNU flex/bison 用 mkstemp，并发安全，故只有 WIN32 需要串行化。
# =====================================================================

set(PYCP_GEN_PARSER_CPP    ${PYCP_GENERATED_DIR}/PycpParser.cpp)
set(PYCP_GEN_PARSER_HPP    ${PYCP_GENERATED_DIR}/PycpParser.hpp)
set(PYCP_GEN_PARSER_REPORT ${PYCP_GENERATED_DIR}/PycpParser.output)

set(PYCP_GEN_LEXER_CPP     ${PYCP_GENERATED_DIR}/PycpLexer.cpp)
set(PYCP_GEN_LEXER_HPP     ${PYCP_GENERATED_DIR}/PycpLexer.hpp)

set(PYCP_GEN_PP_PARSER_CPP    ${PYCP_GENERATED_DIR}/PycpPreprocessorParser.cpp)
set(PYCP_GEN_PP_PARSER_HPP    ${PYCP_GENERATED_DIR}/PycpPreprocessorParser.hpp)
set(PYCP_GEN_PP_PARSER_REPORT ${PYCP_GENERATED_DIR}/PycpPreprocessorParser.output)

set(PYCP_GEN_PP_LEXER_CPP     ${PYCP_GENERATED_DIR}/PycpPreprocessorLexer.cpp)
set(PYCP_GEN_PP_LEXER_HPP     ${PYCP_GENERATED_DIR}/PycpPreprocessorLexer.hpp)

set(PYCP_GEN_CHECK_SCRIPT ${CMAKE_SOURCE_DIR}/cmake/PycpCheckGenerated.cmake)

# Windows 串行化：每一步额外依赖上一步的全部产物。
# 词法器对语法头文件的依赖（token 常量）在所有平台都保留；
# 跨解析器的依赖仅在 WIN32 下追加，Linux/WSL 仍是并行。
if(WIN32)
    set(PYCP_GEN_PP_PARSER_EXTRA_DEPS
        ${PYCP_GEN_LEXER_CPP} ${PYCP_GEN_LEXER_HPP})
else()
    set(PYCP_GEN_PP_PARSER_EXTRA_DEPS)
endif()

# ---------------------------------------------------------------------
# 1) 主解析器：PycpParser.y -> PycpParser.cpp / .hpp（+ .output 报告）
# ---------------------------------------------------------------------
add_custom_command(
    OUTPUT
        ${PYCP_GEN_PARSER_CPP}
        ${PYCP_GEN_PARSER_HPP}
        ${PYCP_GEN_PARSER_REPORT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PYCP_GENERATED_DIR}
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
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "[BISON][PycpParser] Generating parser"
    VERBATIM
)

# ---------------------------------------------------------------------
# 2) 主词法器：PycpLexer.l -> PycpLexer.cpp / .hpp（依赖解析器头文件）
# ---------------------------------------------------------------------
add_custom_command(
    OUTPUT
        ${PYCP_GEN_LEXER_CPP}
        ${PYCP_GEN_LEXER_HPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PYCP_GENERATED_DIR}
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
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "[FLEX][PycpLexer] Generating scanner"
    VERBATIM
)

# ---------------------------------------------------------------------
# 3) 预处理指令解析器（独立于主解析器）
# ---------------------------------------------------------------------
add_custom_command(
    OUTPUT
        ${PYCP_GEN_PP_PARSER_CPP}
        ${PYCP_GEN_PP_PARSER_HPP}
        ${PYCP_GEN_PP_PARSER_REPORT}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PYCP_GENERATED_DIR}
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
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "[BISON][PycpPreprocessorParser] Generating parser"
    VERBATIM
)

# ---------------------------------------------------------------------
# 4) 预处理指令词法器（依赖其解析器头文件）
# ---------------------------------------------------------------------
add_custom_command(
    OUTPUT
        ${PYCP_GEN_PP_LEXER_CPP}
        ${PYCP_GEN_PP_LEXER_HPP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PYCP_GENERATED_DIR}
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
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "[FLEX][PycpPreprocessorLexer] Generating scanner"
    VERBATIM
)

# =====================================================================
# PycpRuntime：object + bytecode + vm + abi
# =====================================================================
set(PYCP_RUNTIME_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpNone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpInteger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpBoolean.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpFloat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpDecimal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpString.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpFunction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpGC.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpClass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpFixedList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpMap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpIterator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpMagic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpExtension.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bytecode/PycpBytecode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bytecode/PycpBytecodeDump.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bytecode/PycpBytecodeObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm/PycpBytecodeVM.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm/PycpModuleRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/abi/PycpABI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/abi/PycpNativeExt.cpp
)

# 头文件（供 IDE 展示；GLOB 以自动纳入新增文件）
file(GLOB PYCP_RUNTIME_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/object/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bytecode/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vm/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/abi/*.hpp
)

# ---------------------------------------------------------------------
# 静态库：内部目标名 PycpRuntime_static，输出文件名 PycpRuntime
# ---------------------------------------------------------------------
if(BUILD_RUNTIME_STATIC)
    add_library(PycpRuntime_obj_static OBJECT ${PYCP_RUNTIME_SOURCES} ${PYCP_RUNTIME_HEADERS})
    target_include_directories(PycpRuntime_obj_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(PycpRuntime_obj_static PUBLIC PYCP_STATIC)
    target_link_libraries(PycpRuntime_obj_static PUBLIC mpdec)

    add_library(PycpRuntime_static STATIC $<TARGET_OBJECTS:PycpRuntime_obj_static>)
    target_include_directories(PycpRuntime_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # mpdecimal（PycpDecimal 依赖）必须直接链接到本库：OBJECT 库的链接接口
    # 不会经 $<TARGET_OBJECTS> 聚合传递，消费方需经此处 PUBLIC 接口获得 mpdec。
    target_link_libraries(PycpRuntime_static PUBLIC mpdec)
    set_target_properties(PycpRuntime_static PROPERTIES
        OUTPUT_NAME "PycpRuntime"
        ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )
    # 原生扩展加载依赖 dlopen/dlsym（POSIX）；Windows 无额外库。
    if(UNIX AND NOT APPLE)
        target_link_libraries(PycpRuntime_static PUBLIC ${CMAKE_DL_LIBS})
    endif()

    if(MSVC)
        target_compile_options(PycpRuntime_obj_static PRIVATE /W4 /EHsc /utf-8)
    else()
        target_compile_options(PycpRuntime_obj_static PRIVATE -Wall -Wextra -O2)
    endif()
endif()

# ---------------------------------------------------------------------
# 动态库：内部目标名 PycpRuntime_shared，输出文件名 PycpRuntime
# ---------------------------------------------------------------------
if(BUILD_RUNTIME_SHARED)
    add_library(PycpRuntime_obj_shared OBJECT ${PYCP_RUNTIME_SOURCES} ${PYCP_RUNTIME_HEADERS})
    target_include_directories(PycpRuntime_obj_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(PycpRuntime_obj_shared PRIVATE PYCP_BUILD_DLL)
    target_link_libraries(PycpRuntime_obj_shared PUBLIC mpdec)

    add_library(PycpRuntime_shared SHARED $<TARGET_OBJECTS:PycpRuntime_obj_shared>)
    target_include_directories(PycpRuntime_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    # 同静态库：mpdec 直接并入运行时 DLL，stdlib 扩展 DLL 无需单独链接。
    target_link_libraries(PycpRuntime_shared PUBLIC mpdec)
    set_target_properties(PycpRuntime_shared PROPERTIES
        OUTPUT_NAME "PycpRuntime"
        LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )

    if(MSVC)
        target_compile_options(PycpRuntime_obj_shared PRIVATE /W4 /EHsc /utf-8)
    else()
        target_compile_options(PycpRuntime_obj_shared PRIVATE -Wall -Wextra -O2)
    endif()

    # MinGW 下导出 DLL 的全部符号（__declspec 未逐符号标注的也一并导出），
    # 保证主程序与内置扩展 DLL 跨 DLL 解析所有运行时 C++ 符号。
    if(MINGW)
        target_link_options(PycpRuntime_shared PRIVATE -Wl,--export-all-symbols)
    endif()

    # 动态库必须显式链接 libdl（glibc < 2.34 上 dlopen 尚未并入 libc）。
    if(UNIX AND NOT APPLE)
        target_link_libraries(PycpRuntime_shared PUBLIC ${CMAKE_DL_LIBS})
    endif()
endif()

# =====================================================================
# PycpFrontend：parser + codegen + loader + aot（OBJECT 库）
# ---------------------------------------------------------------------
# 由宿主（pycp 可执行文件）以 $<TARGET_OBJECTS> 直接并入。stdlib 原生扩展
# 【不】使用本库——它们只链接 PycpRuntime，编译能力经宿主注册的源码钩子
# 获取（避免 stdlib .so 反向依赖前端，Windows 下 DLL 无法解析宿主静态符号）。
#
# 采用 OBJECT 库而非静态库：CMake/构建脚本生成器经静态初始化自注册，
# 静态库会因“无符号被引用”而裁撤对应 .o，导致注册丢失。
# =====================================================================
set(PYCP_FRONTEND_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/parser/PycpAstNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parser/PycpAstReflect.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parser/escape_handler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parser/preprocessor/PycpPreprocessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/codegen/PycpCodegen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/PycpModuleLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/PycpSourceBridge.cpp
    # 原生编译（AOT）已自举为 pycp 版（src/stdlib/aot 下的 .pycp 模块），
    # 由 CLI --emit-cpp 经 execute_program 驱动；C++ 实现已于阶段 3 删除。
    # Flex / Bison 生成物
    ${PYCP_GEN_PARSER_CPP}
    ${PYCP_GEN_LEXER_CPP}
    ${PYCP_GEN_PP_PARSER_CPP}
    ${PYCP_GEN_PP_LEXER_CPP}
)

# PycpParser.cpp（生成物）会 #include "PycpLexer.hpp"（PycpParser.y 顶部），
# 而该头文件也是生成物：显式声明依赖，避免并行构建下"编译先于生成"。
set_source_files_properties(${PYCP_GEN_PARSER_CPP} PROPERTIES
    OBJECT_DEPENDS ${PYCP_GEN_LEXER_HPP}
)

add_library(PycpFrontend_obj OBJECT ${PYCP_FRONTEND_SOURCES})
target_include_directories(PycpFrontend_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PYCP_GENERATED_DIR}
)

if(MSVC)
    target_compile_options(PycpFrontend_obj PRIVATE /W4 /EHsc /utf-8)
else()
    # 生成解析器的 Pycperror 带未使用形参，与 pycp 目标保持同一抑制。
    target_compile_options(PycpFrontend_obj PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

message(STATUS "Pycp src: runtime=[${PYCP_RUNTIME_TARGET}] frontend=PycpFrontend_obj")

# =====================================================================
# 运行时自测程序（可选）
# ---------------------------------------------------------------------
# 经统一 ABI 创建对象 / 调用内建 / GC 计数的样板演示。默认不构建；
# 需要时配置 -DBUILD_TEST=ON 生成 pycp-selftest。
# =====================================================================
if(BUILD_TEST)
    add_executable(pycp-selftest
        ${CMAKE_CURRENT_SOURCE_DIR}/object/PycpRuntimeSelfTest.cpp
    )
    target_include_directories(pycp-selftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(pycp-selftest PRIVATE ${PYCP_RUNTIME_TARGET})
    set_target_properties(pycp-selftest PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )

    # 源码桥接自测（校验 SetSourceStringCompiler / SetSourceExecutor 生效）。
    # 需要前端 OBJECT 一并并入。
    add_executable(pycp-hooks-test
        ${CMAKE_CURRENT_SOURCE_DIR}/loader/PycpSourceBridgeSelfTest.cpp
    )
    target_include_directories(pycp-hooks-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PYCP_GENERATED_DIR}
    )
    target_sources(pycp-hooks-test PRIVATE $<TARGET_OBJECTS:PycpFrontend_obj>)
    target_link_libraries(pycp-hooks-test PRIVATE ${PYCP_RUNTIME_TARGET})
    set_target_properties(pycp-hooks-test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )
endif()
