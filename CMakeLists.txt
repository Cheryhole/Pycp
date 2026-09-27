# =====================================================================
# Pycp 项目总构建脚本
# ---------------------------------------------------------------------
# 统一构建入口。源码按组件分层组织在 src/ 下：
#   src/object   对象系统 + GC + 类型系统
#   src/bytecode 字节码格式 + 序列化 + dump
#   src/vm       执行引擎
#   src/abi      稳定 ABI + 原生扩展加载
#   src/parser   AST + 词法/语法 + 预处理
#   src/codegen  字节码代码生成
#   src/loader   模块加载器
#   src/aot      原生编译（迁移期临时保留的 C++ 实现）
#   src/main     可执行入口
#   src/stdlib   原生标准库扩展
#
# 两个库：
#   PycpRuntime  = object + bytecode + vm + abi（静态库 + 动态库）
#   PycpFrontend = parser + codegen + loader + aot（静态库，由宿主链接）
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
            "Bison/Flex not found on Windows. Install win_flex_bison and add its "
            "bin directory to PATH, or specify them explicitly:\n"
            "  cmake -S . -B build -DBISON_EXECUTABLE=<path>/win_bison.exe "
            "-DFLEX_EXECUTABLE=<path>/win_flex.exe")
    else()
        message(FATAL_ERROR "Bison/Flex not found. Install them first (e.g. apt install bison flex).")
    endif()
endif()

message(STATUS "Bison : ${BISON_EXECUTABLE}")
message(STATUS "Flex  : ${FLEX_EXECUTABLE}")

# =====================================================================
# 运行时库形态（静态 / 动态，可在构建时选择）
# ---------------------------------------------------------------------
# 默认同时构建静态库与动态库（static ON、shared ON），pycp 与 stdlib
# 扩展统一【动态链接】运行时：全进程只存在一份运行时状态（GC 池、小整数池、
# 扩展句柄缓存），避免每个扩展 .so 内嵌一份副本。静态库仍产出，供 AOT 生成
# 代码选择静态链接。想退回纯静态，配置 -DBUILD_RUNTIME_SHARED=OFF。
# 所有产物最终由 pycp-dist 目标收集到最终输出目录（默认 build/dist）。
# =====================================================================
set(BUILD_RUNTIME_STATIC ON CACHE BOOL "Build static PycpRuntime library")
# 全平台默认构建 shared 运行时。Windows 下更是刚需：内置扩展 DLL
# （io/Pycp/classtools）需解析运行时 C++ 符号，而 Windows 不导出可执行文件
# 符号，必须靠 PycpRuntime_shared.dll 导出。
set(BUILD_RUNTIME_SHARED ON CACHE BOOL "Build shared PycpRuntime library")
set(BUILD_PYTHON_BINDING OFF CACHE BOOL "" FORCE)
# 运行时自测程序（pycp-selftest）：默认关闭。
set(BUILD_TEST OFF CACHE BOOL "Build runtime self-test executable (pycp-selftest)")

# 合法性校验：至少构建一种库，否则无链接目标
if(NOT BUILD_RUNTIME_STATIC AND NOT BUILD_RUNTIME_SHARED)
    message(FATAL_ERROR "At least one of BUILD_RUNTIME_STATIC / BUILD_RUNTIME_SHARED must be ON.")
endif()

# =====================================================================
# 路径与目标选择
# =====================================================================
# 源根：所有组件前缀 include（如 "object/PycpObject.hpp"）均以它为搜索根。
set(PYCP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(PYCP_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PYCP_GENERATED_DIR})

# 运行时库目标：shared 优先（默认），否则静态。PycpFrontend 与 pycp 均链接它。
if(BUILD_RUNTIME_SHARED)
    set(PYCP_RUNTIME_TARGET PycpRuntime_shared)
else()
    set(PYCP_RUNTIME_TARGET PycpRuntime_static)
endif()

# 运行时头文件目录（组件分层）。dist 时【扁平】收集到 dist/include，
# 以匹配 AOT 生成代码的扁平 #include（如 "PycpABI.hpp"）。
set(PYCP_RUNTIME_INCLUDE_DIRS
    ${PYCP_SRC_DIR}/object
    ${PYCP_SRC_DIR}/bytecode
    ${PYCP_SRC_DIR}/vm
    ${PYCP_SRC_DIR}/abi
)

# 行编辑库（提供 REPL 方向键/历史，仅用于无参数 REPL 路径）。
# 统一使用 cpp-linenoise（C++17 头文件库，跨平台，原生支持 Windows 控制台）。
set(LINENOISE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cpp-linenoise)
message(STATUS "Line editing: cpp-linenoise (header-only)")

# =====================================================================
# 组件分层（PycpRuntime + PycpFrontend + Flex/Bison 生成）
# =====================================================================
# 在 src/CMakeLists.txt 中定义两个库目标，并完成解析器/词法器的生成。
add_subdirectory(src)

# =====================================================================
# 标准库动态库（io / Pycp / classtools / moduletools）
# ---------------------------------------------------------------------
# 在 src 之后引入，使动态库可链接 PycpRuntime。产物输出到
# ${CMAKE_BINARY_DIR}/stdlib/，运行时可执行文件旁的 stdlib/ 目录加载。
# =====================================================================
add_subdirectory(src/stdlib)

# 取出各 stdlib 子库注册的全局目标清单（安装与 dist 收集共用）。
# 新增 stdlib 子库只需在其 CMakeLists 中 set_property 注册，此处无需改动。
get_property(PYCP_STDLIB_TARGETS GLOBAL PROPERTY PYCP_STDLIB_TARGETS)
# 静态版 stdlib 扩展（AOT --static 模式用，收集到 dist/lib/）。
get_property(PYCP_STDLIB_STATIC_TARGETS GLOBAL PROPERTY PYCP_STDLIB_STATIC_TARGETS)

# =====================================================================
# 可执行文件
# ---------------------------------------------------------------------
# 主程序入口（src/main/PycpMain.cpp）链接运行时 + 前端两个库。
# =====================================================================
add_executable(pycp
    ${PYCP_SRC_DIR}/main/PycpMain.cpp
)

# 前端 OBJECT 库的产物直接并入可执行文件（OBJECT 库保证静态自注册对象，
# 如 CMake/构建脚本生成器，不被静态库链接期裁撤）。
target_sources(pycp PRIVATE $<TARGET_OBJECTS:PycpFrontend_obj>)

target_include_directories(pycp
    PRIVATE
    ${PYCP_SRC_DIR}
    ${PYCP_GENERATED_DIR}
    ${LINENOISE_INCLUDE_DIR}
)

if(MSVC)
    target_compile_options(pycp PRIVATE /W4 /MP)
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
# 链接 Flex 库（仅 Linux）与运行时 / 前端库
# =====================================================================
if(UNIX AND NOT APPLE)
    # flex 库（libfl）提供 yywrap/main，但本工程 lexer 已 noyywrap 且自带
    # main，故通常无需链接；仅当 find_package 显式给出且非空时才链接，
    # 且允许通过 -DPYCP_SKIP_FLEX_LINK=ON 跳过（ASan 下 libfl 符号异常）。
    if(FLEX_LIBRARIES AND NOT PYCP_SKIP_FLEX_LINK)
        target_link_libraries(pycp PRIVATE ${FLEX_LIBRARIES})
    endif()
endif()

# io/Pycp 等库已改为动态库（src/stdlib/），import 时通过 LoadNativeModule
# 动态加载，不再静态链接进 pycp。pycp 链接运行时；前端以 OBJECT 并入。
target_link_libraries(pycp PRIVATE ${PYCP_RUNTIME_TARGET})

# 原生扩展（dlopen/LoadLibrary 加载的 .so/.dll）需解析运行时 C++ 符号
# （Module::New/New 等）。POSIX 下以 -rdynamic 导出全局符号；Windows 下
# 采用 shared 运行时（PycpRuntime_shared.dll），由 DLL 自身导出符号，
# 主程序无需导出，因此不添加 -rdynamic（MSVC 亦无此选项）。
if(UNIX AND NOT APPLE)
    target_link_options(pycp PRIVATE -rdynamic)
endif()

# 动态链接运行时时，将 libPycpRuntime.so（libPycpRuntime.dll）复制到 pycp 可执行
# 文件所在目录，使其启动即可在自身同级目录定位运行时（POSIX 免去对 lib/ 的 rpath
# 依赖；Windows 加载 DLL 本就不搜索 lib/）。lib/ 仍保留一份供 AOT 链接。
if(BUILD_RUNTIME_SHARED)
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
        # $ORIGIN：运行时 .so 已复制到 pycp 同级，优先在此定位（可直接携带
        # dist/ 整体运行）；其后两项为兼容回退（构建树 backend、安装布局 lib/）。
        BUILD_RPATH "$ORIGIN;$ORIGIN/lib;$ORIGIN/backend"
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
# lib/libPycpRuntime.*、lib/libPycpFrontend.a、include/*.h*。rpath 均为 $ORIGIN/../lib。
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

# AOT / 原生扩展编译所需的运行时头文件：保留组件子目录（object/ bytecode/
# vm/ abi/），与源码分层及 dist/include 同构。
foreach(_include_dir IN LISTS PYCP_RUNTIME_INCLUDE_DIRS)
    get_filename_component(_include_component ${_include_dir} NAME)
    install(DIRECTORY ${_include_dir}/
        DESTINATION include/${_include_component}
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
    )
endforeach()

# =====================================================================
# 最终产物收集（dist）
# ---------------------------------------------------------------------
# 把散落在构建树各处的产物（build/pycp、build/backend/libPycpRuntime.*、
# build/stdlib/*.so、运行时头文件）统一收集到一个自包含目录，
# 开箱即可运行与部署，无需再拼接其它路径：
#
#   <PYCP_DIST_DIR>/pycp                主程序
#   <PYCP_DIST_DIR>/stdlib/             标准库原生扩展（运行时按可执行文件
#                                       同级目录的 stdlib/ 查找，不可改名）
#   <PYCP_DIST_DIR>/lib/                运行时库（动态库 + 静态库 + 导入库）
#   <PYCP_DIST_DIR>/include/            AOT / 原生扩展所需的头文件
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

# 精确小数依赖（mpdecimal）静态库：AOT --static 链接时需要，随静态运行时
# 一并收进 dist/lib。
if(TARGET mpdec)
    string(APPEND PYCP_DIST_RUNTIME_FILES "$<TARGET_FILE:mpdec>|")
endif()

# 需与可执行文件同级的运行时文件：libPycpRuntime.so（libPycpRuntime.dll）复制到
# pycp 同级目录，使其启动即可定位（POSIX 免去对 lib/ 的 rpath 依赖；Windows 加载
# DLL 不搜索 lib/）。lib/ 仍保留一份供 AOT 链接，二者同源。
set(PYCP_DIST_ROOT_FILES "")
if(BUILD_RUNTIME_SHARED)
    string(APPEND PYCP_DIST_ROOT_FILES "$<TARGET_FILE:PycpRuntime_shared>|")
endif()

# dist 收集的 include 目录：组件分层目录以 '|' 连接（脚本按 '|' 拆分）。
set(PYCP_DIST_INCLUDE_DIRS "")
foreach(_include_dir IN LISTS PYCP_RUNTIME_INCLUDE_DIRS)
    string(APPEND PYCP_DIST_INCLUDE_DIRS "${_include_dir}|")
endforeach()

# 纯 pycp 实现的标准库包（模块文件夹）-> dist/stdlib/<包名>/
# 运行期由 GetStdlibDir() 按名查找，故必须保持包目录层级。
set(PYCP_DIST_STDLIB_PACKAGE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/aot")

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
        -DSTDLIB_PACKAGE_DIRS=${PYCP_DIST_STDLIB_PACKAGE_DIRS}
        -DINCLUDE_DIRS=${PYCP_DIST_INCLUDE_DIRS}
        -DWRITE_BUILD_INFO=${PYCP_DIST_WRITE_BUILD_INFO}
        -DBUILD_INFO_SYSTEM=${CMAKE_SYSTEM_NAME}
        -DBUILD_INFO_ARCH=${CMAKE_SYSTEM_PROCESSOR}
        -DBUILD_INFO_TYPE=${CMAKE_BUILD_TYPE}
        -DBUILD_INFO_COMPILER=${CMAKE_CXX_COMPILER_ID}
        -DBUILD_INFO_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PycpDist.cmake
    # 构建树同构：把纯 pycp 标准库包也放进 <build>/stdlib/，使 build/pycp
    # 与 dist/pycp 行为一致（运行期按 GetStdlibDir() = <exe 同级>/stdlib 查找
    # 包，如 `pycp -m aot`）。否则只有 dist 树可用，构建树会报「未找到」。
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/src/stdlib/aot
        ${CMAKE_BINARY_DIR}/stdlib/aot
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
# 对每个用例分别跑「解释器」与「pycp -m aot 生成的 AOT 产物」，逐字节比对
# stdout / stderr / 退出码（地址归一化），把两套实现的语义漂移暴露出来。
#
# 不挂 ALL：构建 AOT 项目较慢，不应拖慢常规构建。单独执行：
#   cmake --build build --target pycp-aot-equiv
# =====================================================================
set(PYCP_EQUIV_JOBS "" CACHE STRING
    "pycp-aot-equiv 并行工作进程数（空 = 按 CPU 核数自动，上限 8）")
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
        # 并行分片：空 -> 脚本按 CPU 核数自动决定
        -DJOBS=${PYCP_EQUIV_JOBS}
        # 已知缺陷用例降级为 XFAIL（与 tests/modules/import_tests/run.sh 一致）。
        -DXFAIL=circular_import|mod_circ|preprocessings|import_unknown|miss_main|shadow_main|argv_demo
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
