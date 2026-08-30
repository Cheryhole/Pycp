# =====================================================================
# Pycp 项目总构建脚本
# ---------------------------------------------------------------------
# 统一构建入口：编排 backend 运行时（静态库）、frontend 前端源码
# （AST / Codegen / AOT / Flex/Bison 生成）与顶层 PycpMain.cpp，
# 最终产出 pycp 可执行文件，并由 pycp-dist 目标把所有产物收集到
# 最终输出目录（默认 build/dist/，自包含可直接运行 / 部署）。
#
# 用法：
#   cmake -S . -B build
#   cmake --build build
#
# 常用配置：
#   -DPYCP_DIST_DIR=<path>           改最终输出目录位置
#   -DPYCP_DIST_PLATFORM_SUBDIR=ON   按 <system>-<arch> 分层放置产物
#   -DBUILD_RUNTIME_SHARED=ON        动态链接运行时（Windows 默认）
# =====================================================================

cmake_minimum_required(VERSION 3.15)

project(Pycp
    VERSION 1.0.0
    LANGUAGES C CXX
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
# ---------------------------------------------------------------------
# Windows 下系统通常不自带 bison/flex，需安装 win_flex_bison
# （https://sourceforge.net/projects/winflexbison/）并将其 bin 目录加入 PATH，
# 或显式指定 -DBISON_EXECUTABLE=... -DFLEX_EXECUTABLE=...。
# 找不到时给出明确提示而非晦涩的 fatal。
# =====================================================================
find_package(BISON)
find_package(FLEX)
if(NOT BISON_FOUND OR NOT FLEX_FOUND)
    if(WIN32)
        message(FATAL_ERROR
            "在 Windows 下未找到 Bison/Flex。请安装 win_flex_bison 并将其 bin 目录加入 PATH，"
            "或显式指定：\n"
            "  cmake -S . -B build -DBISON_EXECUTABLE=<path>/win_bison.exe "
            "-DFLEX_EXECUTABLE=<path>/win_flex.exe")
    else()
        message(FATAL_ERROR "未找到 Bison/Flex，请先安装（如 apt install bison flex）。")
    endif()
endif()

message(STATUS "Bison : ${BISON_EXECUTABLE}")
message(STATUS "Flex  : ${FLEX_EXECUTABLE}")

# =====================================================================
# Backend 运行时（静态库 / 动态库，可在构建时选择）
# ---------------------------------------------------------------------
# 默认同时构建静态库与动态库（static ON、shared ON），pycp 与三个 stdlib
# 扩展统一【动态链接】运行时：全进程只存在一份运行时状态（GC 池、小整数池、
# 扩展句柄缓存），避免每个扩展 .so 内嵌一份副本。静态库仍产出，供 AOT 生成
# 代码选择静态链接。想退回纯静态，配置 -DBUILD_RUNTIME_SHARED=OFF。
# 产物统一输出到 build/backend/；关闭非必需的 Python 绑定 / 测试目标。
# 所有产物最终由 pycp-dist 目标收集到最终输出目录（默认 build/dist）。
# =====================================================================
set(BUILD_RUNTIME_STATIC ON CACHE BOOL "Build static PycpRuntime library")
# 全平台默认构建 shared 运行时。Windows 下更是刚需：内置扩展 DLL
# （io/Pycp/classtools）需解析运行时 C++ 符号，而 Windows 不导出可执行文件
# 符号，必须靠 PycpRuntime_shared.dll 导出。
set(BUILD_RUNTIME_SHARED ON CACHE BOOL "Build shared PycpRuntime library")
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
add_subdirectory(stdlib)

# 取出各 stdlib 子库注册的全局目标清单（安装与 dist 收集共用）。
# 新增 stdlib 子库只需在其 CMakeLists 中 set_property 注册，此处无需改动。
get_property(PYCP_STDLIB_TARGETS GLOBAL PROPERTY PYCP_STDLIB_TARGETS)
# 静态版 stdlib 扩展（AOT --static 模式用，收集到 dist/lib/）。
get_property(PYCP_STDLIB_STATIC_TARGETS GLOBAL PROPERTY PYCP_STDLIB_STATIC_TARGETS)

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
    ${PYCP_FRONTEND_DIR}/src/aot/PycpAot.cpp
    ${PYCP_FRONTEND_DIR}/src/aot/PycpAotSdkLocator.cpp
    ${PYCP_FRONTEND_DIR}/src/aot/PycpProjectSpec.cpp
    ${PYCP_FRONTEND_DIR}/src/aot/PycpBuildScriptGenerator.cpp
    ${PYCP_FRONTEND_DIR}/src/aot/PycpCMakeGenerator.cpp
    ${PYCP_FRONTEND_DIR}/src/aot/PycpAotProject.cpp
)

set(LOADER_SOURCES
    ${PYCP_FRONTEND_DIR}/src/PycpModuleLoader.cpp
)

set(LEXER_SOURCES
    ${PYCP_FRONTEND_DIR}/src/escape_handler.cpp
)

set(PREPROCESSOR_SOURCES
    ${PYCP_FRONTEND_DIR}/src/preprocessor/PycpPreprocessor.cpp
)

set(PREPROCESSOR_BISON_INPUT
    ${PYCP_FRONTEND_DIR}/src/preprocessor/PycpPreprocessorParser.y
)

set(PREPROCESSOR_FLEX_INPUT
    ${PYCP_FRONTEND_DIR}/src/preprocessor/PycpPreprocessorLexer.l
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
# 生成解析器与词法分析器（Flex / Bison）
# ---------------------------------------------------------------------
# 不使用 bison_target / flex_target / add_flex_bison_dependency，改为手写
# add_custom_command。原因：Windows 下的 win_flex / win_bison
# （winflexbison）并发不安全——它用 _tempnam + fopen 申请临时文件（TOCTOU），
# 并行运行的多个实例会共用同一个临时文件并互相覆盖，产出"未展开的 m4 骨架"
# （残留 M4_YY_PREFIX / m4_ifdef / [[#ifndef 等模板文本）。拿去编译会得到
# 数百行毫无意义的错误，例如：
#   error: '#endif' without '#if'
#   error: stray '#' in program
#   error: 'out_ALREADY_DEFINED' does not name a type
#   error: expected unqualified-id before ']' token
# 而且产物比 .l/.y 新，不手动删除就不会重新生成，问题会一直卡住。
# 上游已知缺陷：
#   https://github.com/lexxmark/winflexbison/issues/86
#   https://github.com/westes/flex/issues/580
# Linux/WSL 的 GNU flex/bison 用 mkstemp，并发安全，故只有 WIN32 需要串行化。
#
# flex_target 不支持追加 DEPENDS，无法表达"第二个 flex 必须等第一个 flex
# 结束"，所以必须手写命令才能建立下面的依赖链：
#   PycpParser -> PycpLexer -> PycpPreprocessorParser -> PycpPreprocessorLexer
# 每条命令末尾再跑一次生成物自检（cmake/PycpCheckGenerated.cmake），在编译器
# 介入前把"几百行莫名报错"变成一条可操作的错误。
# =====================================================================

# 生成产物（显式列出，便于建立依赖链与自检）
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

set(PYCP_GEN_CHECK_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PycpCheckGenerated.cmake)

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
    # bison / flex 不会自建输出目录，且在 WIN32 下两条命令可能先于
    # file(MAKE_DIRECTORY) 被触发，故每条生成命令都自带建目录步骤。
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
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "[FLEX][PycpLexer] Generating scanner"
    VERBATIM
)

# ---------------------------------------------------------------------
# 3) 预处理指令解析器（独立于主解析器）：
#    PycpPreprocessorParser.y -> .cpp / .hpp（+ .output 报告）
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
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "[BISON][PycpPreprocessorParser] Generating parser"
    VERBATIM
)

# ---------------------------------------------------------------------
# 4) 预处理指令词法器：
#    PycpPreprocessorLexer.l -> .cpp / .hpp（依赖其解析器头文件）
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
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "[FLEX][PycpPreprocessorLexer] Generating scanner"
    VERBATIM
)

# =====================================================================
# 可执行文件
# =====================================================================
# 行编辑库（提供 REPL 方向键/历史，仅用于无参数 REPL 路径）。
# 统一使用 cpp-linenoise（C++17 头文件库，跨平台，原生支持 Windows 控制台）。
set(LINENOISE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-linenoise)
message(STATUS "Line editing: cpp-linenoise (header-only)")

add_executable(pycp
    ${MAIN_SOURCE}
    ${AST_SOURCES}
    ${CODGEN_SOURCES}
    ${AOT_SOURCES}
    ${LOADER_SOURCES}
    ${LEXER_SOURCES}
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

# PycpParser.y 顶部 #include "PycpLexer.hpp"，该头文件本身也是生成物。
# 显式声明这一编译单元的依赖，避免并行构建下"编译先于生成"
# （Ninja 下会退化成 missing and no known rule to make it）。
set_source_files_properties(${PYCP_GEN_PARSER_CPP} PROPERTIES
    OBJECT_DEPENDS ${PYCP_GEN_LEXER_HPP}
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
    # flex 库（libfl）提供 yywrap/main，但本工程 lexer 已 noyywrap 且自带
    # main，故通常无需链接；仅当 find_package 显式给出且非空时才链接，
    # 且允许通过 -DPYCP_SKIP_FLEX_LINK=ON 跳过（ASan 下 libfl 符号异常）。
    if(FLEX_LIBRARIES AND NOT PYCP_SKIP_FLEX_LINK)
        target_link_libraries(pycp PRIVATE ${FLEX_LIBRARIES})
    endif()
endif()

# 根据选项选择 runtime 库：shared ON 时动态链接（默认），否则静态链接
if(BUILD_RUNTIME_SHARED)
    set(PYCP_RUNTIME_TARGET PycpRuntime_shared)
else()
    set(PYCP_RUNTIME_TARGET PycpRuntime_static)
endif()

# io/Pycp 库已改为动态库（stdlib/），import 时通过 LoadNativeModule 动态加载，
# 不再静态链接进 pycp。pycp 仅链接 PycpRuntime。
target_link_libraries(pycp PRIVATE ${PYCP_RUNTIME_TARGET})

# 原生扩展（dlopen/LoadLibrary 加载的 .so/.dll）需解析运行时 C++ 符号
# （Module::New/New 等）。POSIX 下以 -rdynamic 导出全局符号；Windows 下
# 采用 shared 运行时（PycpRuntime_shared.dll），由 DLL 自身导出符号，
# 主程序无需导出，因此不添加 -rdynamic（MSVC 亦无此选项）。
if(UNIX AND NOT APPLE)
    target_link_options(pycp PRIVATE -rdynamic)
endif()

# Windows 下动态链接运行时时，将 PycpRuntime_shared.dll 复制到可执行文件所在
# 目录，保证 pycp.exe 启动即可定位该 DLL（stdlib 扩展 DLL 也链接同一份）。
if(WIN32 AND BUILD_RUNTIME_SHARED)
    add_custom_command(TARGET pycp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:PycpRuntime_shared>"
                "$<TARGET_FILE_DIR:pycp>/"
    )
endif()

# 动态链接时确保 shared 运行时被构建（pycp 静态链接时不依赖该目标），
# 并设置 rpath：
#   $ORIGIN/lib      -> dist/pycp 与安装目录（库在 <前缀>/lib 下）
#   $ORIGIN/backend  -> 构建树（build/pycp 对应 build/backend/libPycpRuntime.so）
# BUILD_RPATH 排在 CMake 自动追加的链接目录之前，故 dist 中的 pycp 不会被
# 构建树里的旧 .so 抢先命中，dist 保持自包含。
if(BUILD_RUNTIME_SHARED)
    add_dependencies(pycp PycpRuntime_shared)
    set_target_properties(pycp PROPERTIES
        BUILD_RPATH "$ORIGIN/lib;$ORIGIN/backend"
        INSTALL_RPATH "$ORIGIN/../lib"
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
# ---------------------------------------------------------------------
# 布局与 dist 保持一致：bin/pycp、bin/stdlib/*.so（必须与 exe 同级）、
# lib/libPycpRuntime.*、include/*.h*。rpath 均为 $ORIGIN/../lib。
# =====================================================================
install(TARGETS pycp
    RUNTIME DESTINATION bin
)

if(BUILD_RUNTIME_SHARED)
    install(TARGETS PycpRuntime_shared
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin   # Windows：DLL 需与 exe 同级才能被找到
    )
endif()

if(BUILD_RUNTIME_STATIC)
    install(TARGETS PycpRuntime_static
        ARCHIVE DESTINATION lib
    )
endif()

# 标准库原生扩展：运行时按可执行文件同级 stdlib/ 查找，必须装在 bin/stdlib
if(PYCP_STDLIB_TARGETS)
    install(TARGETS ${PYCP_STDLIB_TARGETS}
        LIBRARY DESTINATION bin/stdlib
        RUNTIME DESTINATION bin/stdlib
    )
endif()

# AOT / 原生扩展编译所需的后端头文件
install(DIRECTORY ${PYCP_BACKEND_DIR}/include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# =====================================================================
# 最终产物收集（dist）
# ---------------------------------------------------------------------
# 把散落在构建树各处的产物（build/pycp、build/backend/libPycpRuntime.*、
# build/stdlib/*.so、backend/include/*.h*）统一收集到一个自包含目录，
# 开箱即可运行与部署，无需再拼接其它路径：
#
#   <PYCP_DIST_DIR>/pycp                主程序
#   <PYCP_DIST_DIR>/stdlib/             标准库原生扩展（运行时按可执行文件
#                                       同级目录的 stdlib/ 查找，不可改名）
#   <PYCP_DIST_DIR>/lib/                运行时库（动态库 + 静态库 + 导入库）
#   <PYCP_DIST_DIR>/include/            AOT / 原生扩展所需的后端头文件
#
# 触发：默认随 ALL 自动执行；也可单独执行（幂等，仅覆盖同名文件、不清空目录）：
#   cmake --build build --target pycp-dist
# 自定义：
#   -DPYCP_DIST_DIR=<path>              改输出位置（默认 build/dist）
#   -DPYCP_DIST_PLATFORM_SUBDIR=ON      输出到 dist/<system>-<arch>/，
#                                       多平台/多架构产物共存不冲突
#   -DPYCP_DIST_WRITE_BUILD_INFO=OFF    不生成 BUILD_INFO.txt
# =====================================================================
set(PYCP_DIST_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH
    "Final output directory for the collected Pycp artifacts")
option(PYCP_DIST_PLATFORM_SUBDIR
    "Place artifacts under <PYCP_DIST_DIR>/<system>-<arch>" OFF)
option(PYCP_DIST_WRITE_BUILD_INFO
    "Write BUILD_INFO.txt into the dist directory" ON)

# 规范化输出目录（含平台分层子目录）
file(TO_CMAKE_PATH "${PYCP_DIST_DIR}" PYCP_DIST_DIR)
if(PYCP_DIST_PLATFORM_SUBDIR)
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _dist_system)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _dist_arch)
    set(PYCP_DIST_OUT_DIR "${PYCP_DIST_DIR}/${_dist_system}-${_dist_arch}")
else()
    set(PYCP_DIST_OUT_DIR "${PYCP_DIST_DIR}")
endif()

# 标准库目标清单：各子库经 GLOBAL 属性注册，新增子库无需改动此处
set(PYCP_DIST_STDLIB_FILES "")
foreach(_stdlib_target IN LISTS PYCP_STDLIB_TARGETS)
    string(APPEND PYCP_DIST_STDLIB_FILES "$<TARGET_FILE:${_stdlib_target}>|")
endforeach()

# 静态版 stdlib 扩展（libPycpExt_<name>.a）-> dist/lib，供 AOT --static 链接。
set(PYCP_DIST_STDLIB_STATIC_FILES "")
foreach(_stdlib_target IN LISTS PYCP_STDLIB_STATIC_TARGETS)
    string(APPEND PYCP_DIST_STDLIB_STATIC_FILES "$<TARGET_FILE:${_stdlib_target}>|")
endforeach()

# 运行时库（静态库 / 动态库 / Windows 导入库）-> dist/lib
set(PYCP_DIST_RUNTIME_TARGETS "")
set(PYCP_DIST_RUNTIME_FILES "")
if(BUILD_RUNTIME_STATIC)
    list(APPEND PYCP_DIST_RUNTIME_TARGETS PycpRuntime_static)
    string(APPEND PYCP_DIST_RUNTIME_FILES "$<TARGET_FILE:PycpRuntime_static>|")
endif()
if(BUILD_RUNTIME_SHARED)
    list(APPEND PYCP_DIST_RUNTIME_TARGETS PycpRuntime_shared)
    string(APPEND PYCP_DIST_RUNTIME_FILES "$<TARGET_FILE:PycpRuntime_shared>|")
    # Windows 导入库（MinGW: libPycpRuntime.dll.a；MSVC: PycpRuntime.lib），
    # 否则 g++ -lPycpRuntime 会回退选中静态库 .a，导致 __imp_ 符号 undefined reference。
    if(WIN32)
        string(APPEND PYCP_DIST_RUNTIME_FILES "$<TARGET_LINKER_FILE:PycpRuntime_shared>|")
    endif()
endif()

# 需与可执行文件同级的运行时文件：Windows 加载 DLL 只搜索 exe 所在目录，
# 不会搜索 lib/，因此 PycpRuntime.dll 在根目录额外放一份（与 lib/ 那份同源）。
set(PYCP_DIST_ROOT_FILES "")
if(WIN32 AND BUILD_RUNTIME_SHARED)
    string(APPEND PYCP_DIST_ROOT_FILES "$<TARGET_FILE:PycpRuntime_shared>|")
endif()

# 收集动作由独立脚本执行（建目录 / 覆盖复制 / 路径规范化 / 缺失即报错），
# 多值参数以 '|' 分隔，避免路径中的分号被 CMake 列表吞掉。
add_custom_target(pycp-dist ALL
    COMMAND ${CMAKE_COMMAND}
        -DBASE_DIR=${CMAKE_BINARY_DIR}
        -DDIST_DIR=${PYCP_DIST_OUT_DIR}
        -DLIB_DIR=${PYCP_DIST_OUT_DIR}/lib
        -DINCLUDE_DIR=${PYCP_DIST_OUT_DIR}/include
        -DEXE_FILE=$<TARGET_FILE:pycp>
        -DROOT_FILES=${PYCP_DIST_ROOT_FILES}
        -DRUNTIME_FILES=${PYCP_DIST_RUNTIME_FILES}
        -DSTDLIB_FILES=${PYCP_DIST_STDLIB_FILES}
        -DSTDLIB_STATIC_FILES=${PYCP_DIST_STDLIB_STATIC_FILES}
        -DINCLUDE_DIRS=${PYCP_BACKEND_DIR}/include
        -DWRITE_BUILD_INFO=${PYCP_DIST_WRITE_BUILD_INFO}
        -DBUILD_INFO_SYSTEM=${CMAKE_SYSTEM_NAME}
        -DBUILD_INFO_ARCH=${CMAKE_SYSTEM_PROCESSOR}
        -DBUILD_INFO_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_INFO_COMPILER=${CMAKE_CXX_COMPILER_ID}
        -DBUILD_INFO_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PycpDist.cmake
    COMMENT "Collecting final artifacts into ${PYCP_DIST_OUT_DIR}"
    VERBATIM
)

# 依赖全部产物目标：保证并行构建下收集发生在所有库编译完成之后。
# 缺少该依赖时，首次并行构建会出现“收集时 .so 尚未生成”的 FATAL_ERROR。
add_dependencies(pycp-dist
    pycp
    ${PYCP_STDLIB_TARGETS}
    ${PYCP_STDLIB_STATIC_TARGETS}
    ${PYCP_DIST_RUNTIME_TARGETS}
)

# =====================================================================
# AOT / 解释器一致性对比（pycp-aot-equiv）
# ---------------------------------------------------------------------
# 对每个用例分别跑「解释器」与「--emit-cpp 生成的 AOT 产物」，逐字节比对
# stdout / stderr / 退出码（地址归一化），把两套实现的语义漂移暴露出来。
#
# 不挂 ALL：构建 AOT 项目较慢，不应拖慢常规构建。单独执行：
#   cmake --build build --target pycp-aot-equiv
#
# 用例集在【配置期】GLOB，新增 .pycp 后需重新配置（与 dist 里的头文件
# GLOB 同为「配置期快照」，不追求运行时自增）。
# =====================================================================
file(GLOB_RECURSE _pycp_equiv_cases "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.pycp")
set(_pycp_equiv_case_list "")
foreach(_c IN LISTS _pycp_equiv_cases)
    string(APPEND _pycp_equiv_case_list "${_c}|")
endforeach()
list(LENGTH _pycp_equiv_cases _pycp_equiv_case_count)

add_custom_target(pycp-aot-equiv
    COMMAND ${CMAKE_COMMAND}
        -DPYCP=$<TARGET_FILE:pycp>
        -DCASES=${_pycp_equiv_case_list}
        -DWORK_DIR=${CMAKE_BINARY_DIR}/aot-equiv
        -DMODES=shared$<SEMICOLON>static
        # 复用主构建所用的生成器：保证 AOT 项目用的是同一套可用工具链
        # （不指定时 CMake 会自选，Windows 上可能落到未安装的 nmake）。
        -DGENERATOR=${CMAKE_GENERATOR}
        # 已知缺陷用例（解释器自身行为未定义、预期不一致）降级为 XFAIL，
        # 与 tests/modules/import_tests/run.sh 的 EXPECTED_FAIL_PATTERNS 一致。
        # circular_import 与其两端 mod_circ_x / mod_circ_y 同属「真正循环导入」
        # 已知缺陷：解释器自身会段错误/堆损坏（0xc0000374），AOT 为栈溢出，
        # 退出码必然不一致，属预期。
        # preprocessings 用 # send warning/message 在编译期输出诊断：解释器
        # 运行时复跑预处理器（诊断进 stderr），AOT 预处理器在 --emit-cpp 生成期
        # 已跑、产物 exe 无法复现，属编译期 vs 运行期的结构性差异，预期不一致。
        -DXFAIL=circular_import|mod_circ|preprocessings
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PycpAotEquivalence.cmake
    COMMENT "Comparing AOT vs interpreter behaviour on ${_pycp_equiv_case_count} test case(s)"
    VERBATIM
)
add_dependencies(pycp-aot-equiv pycp pycp-dist)

# =====================================================================
# 构建信息
# =====================================================================
if(BUILD_RUNTIME_SHARED)
    set(_runtime_link_mode "shared (dynamic)")
else()
    set(_runtime_link_mode "static")
endif()

message(STATUS "================================================")
message(STATUS "Project: Pycp v${PROJECT_VERSION}")
message(STATUS "Output executable: ${CMAKE_BINARY_DIR}/pycp")
message(STATUS "Runtime link mode: ${_runtime_link_mode}")
message(STATUS "Dist directory   : ${PYCP_DIST_OUT_DIR}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "================================================")
