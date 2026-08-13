# =====================================================================
# Pycp Runtime 后端构建脚本
# ---------------------------------------------------------------------
# 构建 PycpRuntime 运行时库（静态/动态/Python 绑定）与测试程序。
#
# 本脚本支持两种使用方式：
#   1. 作为项目顶层 CMakeLists.txt 的子目录被引入（顶层会关闭
#      动态库 / Python 绑定 / 测试选项，仅构建 PycpRuntime_static 静态库）。
#   2. 独立构建（cd backend && cmake -S . -B build），构建完整运行时
#      库与 test_pycp 测试程序，产物输出到 backend/bin/。
#
# 输出目录使用绝对路径（CMAKE_CURRENT_SOURCE_DIR），天然隔离，
# 不会污染顶层 build/ 目录。
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

# 统一输出目录
set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)
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
    ${PYCP_INCLUDE_DIR}/PycpException.hpp
    ${PYCP_INCLUDE_DIR}/PycpFunction.hpp
    ${PYCP_INCLUDE_DIR}/PycpManager.hpp
    ${PYCP_INCLUDE_DIR}/PycpBytecode.hpp
    ${PYCP_INCLUDE_DIR}/PycpBytecodeVM.hpp
)

set(PYCP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpNone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpInteger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpString.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpFunction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpGC.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpABI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpBytecode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpBytecodeVM.cpp
)

# =====================================================================
# 核心：编译为 OBJECT 目标文件（不链接成库）
# =====================================================================
add_library(PycpRuntime_obj OBJECT ${PYCP_SOURCES} ${PYCP_HEADERS})
target_include_directories(PycpRuntime_obj PUBLIC ${PYCP_INCLUDE_DIR})

if(MSVC)
    target_compile_options(PycpRuntime_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpRuntime_obj PRIVATE -Wall -Wextra -O2)
endif()

# =====================================================================
# 静态库 / 动态库（目标名分离，避免冲突）
# =====================================================================
# 静态库：内部目标名 PycpRuntime_static，输出文件名 PycpRuntime
if(BUILD_RUNTIME_STATIC)
    add_library(PycpRuntime_static STATIC $<TARGET_OBJECTS:PycpRuntime_obj>)
    target_include_directories(PycpRuntime_static PUBLIC ${PYCP_INCLUDE_DIR})
    set_target_properties(PycpRuntime_static PROPERTIES
        OUTPUT_NAME "PycpRuntime"
        ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )
endif()

# 动态库：内部目标名 PycpRuntime_shared，输出文件名 PycpRuntime
if(BUILD_RUNTIME_SHARED)
    add_library(PycpRuntime_shared SHARED $<TARGET_OBJECTS:PycpRuntime_obj>)
    target_include_directories(PycpRuntime_shared PUBLIC ${PYCP_INCLUDE_DIR})
    if(MSVC)
        target_compile_definitions(PycpRuntime_shared PRIVATE PYCP_BUILD_DLL)
    endif()
    set_target_properties(PycpRuntime_shared PROPERTIES
        OUTPUT_NAME "PycpRuntime"
        LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )
endif()

# =====================================================================
# Python 绑定：直接嵌入目标文件（无外部依赖）
# =====================================================================
if(BUILD_PYTHON_BINDING)
    find_package(Python REQUIRED COMPONENTS Interpreter Development)
    find_package(pybind11 CONFIG REQUIRED)

    pybind11_add_module(PycpRuntime4Python
        ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpRuntime4Python.cpp
        $<TARGET_OBJECTS:PycpRuntime_obj>
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

# =====================================================================
# 测试程序
# =====================================================================
if(BUILD_TEST)
    add_executable(test_pycp ${CMAKE_CURRENT_SOURCE_DIR}/src/test.cpp)
    target_link_libraries(test_pycp PRIVATE PycpRuntime_obj)
    set_target_properties(test_pycp PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )
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
