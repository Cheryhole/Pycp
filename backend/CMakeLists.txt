# =====================================================================
# Pycp Runtime 后端构建脚本
# ---------------------------------------------------------------------
# 构建 PycpRuntime 运行时库（静态/动态/Python 绑定）与测试程序。
#
# 本脚本支持两种使用方式：
#   1. 作为项目顶层 CMakeLists.txt 的子目录被引入（顶层会关闭
#      动态库 / Python 绑定 / 测试选项，仅构建 PycpRuntime_static 静态库）。
#   2. 独立构建（cd backend && cmake -S . -B build），构建完整运行时
#      库与 test_pycp 测试程序，产物输出到编译目录（backend/build/）。
#
# 输出目录使用 CMAKE_CURRENT_BINARY_DIR（编译目录），产物落在 build/
# 树内，不会污染源码目录（backend/）。
# =====================================================================

cmake_minimum_required(VERSION 3.15)

project(PycpRuntime
    VERSION 1.0.0
    LANGUAGES CXX
)

# =====================================================================
# 基础编译配置
# =====================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# 统一输出目录：使用编译目录（顶层构建为 build/backend，独立构建为
# backend/build），避免在任何源码目录下生成二进制文件。
set(OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR})

# =====================================================================
# 编译选项（可同时开启静态/动态库）
# =====================================================================
option(BUILD_RUNTIME_SHARED "Build shared PycpRuntime library" OFF)
option(BUILD_RUNTIME_STATIC "Build static PycpRuntime library" ON)
option(BUILD_PYTHON_BINDING "Build PycpRuntime4Python.pyd" OFF)
option(BUILD_TEST "Build test executable" ON)

# =====================================================================
# 源文件 / 头文件（严格分离）
# =====================================================================
set(PYCP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(PYCP_HEADERS
    ${PYCP_INCLUDE_DIR}/Pycp.hpp
    ${PYCP_INCLUDE_DIR}/PycpConfig.hpp
    ${PYCP_INCLUDE_DIR}/PycpGC.hpp
    ${PYCP_INCLUDE_DIR}/PycpABI.hpp
    ${PYCP_INCLUDE_DIR}/PycpObject.hpp
    ${PYCP_INCLUDE_DIR}/PycpNone.hpp
    ${PYCP_INCLUDE_DIR}/PycpInteger.hpp
    ${PYCP_INCLUDE_DIR}/PycpString.hpp
    ${PYCP_INCLUDE_DIR}/PycpList.hpp
    ${PYCP_INCLUDE_DIR}/PycpIterator.hpp
    ${PYCP_INCLUDE_DIR}/PycpMagic.hpp
    ${PYCP_INCLUDE_DIR}/PycpException.hpp
    ${PYCP_INCLUDE_DIR}/PycpFunction.hpp
    ${PYCP_INCLUDE_DIR}/PycpManager.hpp
    ${PYCP_INCLUDE_DIR}/PycpModule.hpp
    ${PYCP_INCLUDE_DIR}/PycpClass.hpp
    ${PYCP_INCLUDE_DIR}/PycpBytecode.hpp
    ${PYCP_INCLUDE_DIR}/PycpBytecodeDump.hpp
    ${PYCP_INCLUDE_DIR}/PycpBytecodeVM.hpp
    ${PYCP_INCLUDE_DIR}/PycpEnvironment.hpp
    ${PYCP_INCLUDE_DIR}/PycpNativeExt.hpp
    ${PYCP_INCLUDE_DIR}/PycpExt.h
)

set(PYCP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpNone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpInteger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpString.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpFunction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpGC.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpClass.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpIterator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpMagic.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpABI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpBytecode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpBytecodeVM.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpBytecodeDump.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpNativeExt.cpp
)

# =====================================================================
# 核心：按选项编译 OBJECT 目标文件并生成对应库
# ---------------------------------------------------------------------
# 静态库与动态库各自独立编译目标文件，因为 Windows 上二者的符号
# 导出属性不同（静态库无属性、动态库需 dllexport），无法共用同一组
# .o；Linux/macOS 上 visibility("default") 二者一致，但统一拆分以
# 保证跨平台行为一致。静态 OBJECT 库以 PUBLIC 传播 PYCP_STATIC，
# 使静态链接消费方（如 pycp）自动获得该宏。
# OBJECT 库按需编译：未启用的库不编译其 OBJECT 目标，避免浪费。
# =====================================================================

# 静态库：内部目标名 PycpRuntime_static，输出文件名 PycpRuntime
if(BUILD_RUNTIME_STATIC)
    add_library(PycpRuntime_obj_static OBJECT ${PYCP_SOURCES} ${PYCP_HEADERS})
    target_include_directories(PycpRuntime_obj_static PUBLIC ${PYCP_INCLUDE_DIR})
    target_compile_definitions(PycpRuntime_obj_static PUBLIC PYCP_STATIC)

    add_library(PycpRuntime_static STATIC $<TARGET_OBJECTS:PycpRuntime_obj_static>)
    target_include_directories(PycpRuntime_static PUBLIC ${PYCP_INCLUDE_DIR})
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

# 动态库：内部目标名 PycpRuntime_shared，输出文件名 PycpRuntime
if(BUILD_RUNTIME_SHARED)
    add_library(PycpRuntime_obj_shared OBJECT ${PYCP_SOURCES} ${PYCP_HEADERS})
    target_include_directories(PycpRuntime_obj_shared PUBLIC ${PYCP_INCLUDE_DIR})
    target_compile_definitions(PycpRuntime_obj_shared PRIVATE PYCP_BUILD_DLL)

    add_library(PycpRuntime_shared SHARED $<TARGET_OBJECTS:PycpRuntime_obj_shared>)
    target_include_directories(PycpRuntime_shared PUBLIC ${PYCP_INCLUDE_DIR})
    set_target_properties(PycpRuntime_shared PROPERTIES
        OUTPUT_NAME "PycpRuntime"
        LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )

    if(MSVC)
        target_compile_options(PycpRuntime_obj_shared PRIVATE /W4 /EHsc /utf-8)
    else()
        target_compile_options(PycpRuntime_obj_shared PRIVATE -Wall -Wextra -O2)
        # MinGW 下导出 DLL 的全部符号（__declspec 未逐符号标注的也一并导出），
        # 保证主程序与内置扩展 DLL 跨 DLL 解析所有运行时 C++ 符号
        # （如 Pycp::BC::VM、Pycp::BC::Module 等字节码符号）。
        target_link_options(PycpRuntime_shared PRIVATE -Wl,--export-all-symbols)
    endif()
endif()

# =====================================================================
# Python 绑定：直接嵌入目标文件（无外部依赖）
# ---------------------------------------------------------------------
# 绑定是动态模块，依赖 shared OBJECT；仅当动态库启用时才可构建。
# =====================================================================
if(BUILD_PYTHON_BINDING)
    if(NOT BUILD_RUNTIME_SHARED)
        message(WARNING "BUILD_PYTHON_BINDING requires BUILD_RUNTIME_SHARED=ON; skipping.")
    else()
        find_package(Python REQUIRED COMPONENTS Interpreter Development)
        find_package(pybind11 CONFIG REQUIRED)

        pybind11_add_module(PycpRuntime4Python
            ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpRuntime4Python.cpp
            $<TARGET_OBJECTS:PycpRuntime_obj_shared>
        )
        target_include_directories(PycpRuntime4Python PRIVATE ${PYCP_INCLUDE_DIR})

        set_target_properties(PycpRuntime4Python PROPERTIES
            PREFIX ""
            SUFFIX ".pyd"
            OUTPUT_NAME "PycpRuntime4Python"
            LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}
            RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
        )
    endif()
endif()

# =====================================================================
# 测试程序
# ---------------------------------------------------------------------
# 依赖静态 OBJECT；仅当静态库启用时才可构建。
# =====================================================================
if(BUILD_TEST)
    if(NOT BUILD_RUNTIME_STATIC)
        message(WARNING "BUILD_TEST requires BUILD_RUNTIME_STATIC=ON; skipping.")
    else()
        add_executable(test_pycp ${CMAKE_CURRENT_SOURCE_DIR}/src/test.cpp)
        target_link_libraries(test_pycp PRIVATE PycpRuntime_obj_static)
        set_target_properties(test_pycp PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
        )
    endif()
endif()

# =====================================================================
# 构建信息
# =====================================================================
message(STATUS "================================================")
message(STATUS "Project: PycpRuntime v${PROJECT_VERSION}")
message(STATUS "Build shared runtime: ${BUILD_RUNTIME_SHARED}")
message(STATUS "Build static runtime: ${BUILD_RUNTIME_STATIC}")
message(STATUS "Build Python binding: ${BUILD_PYTHON_BINDING}")
message(STATUS "Build test program: ${BUILD_TEST}")
message(STATUS "Output directory: ${OUTPUT_DIR}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "================================================")
