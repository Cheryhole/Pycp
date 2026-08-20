# =====================================================================
# Pycp classtools 标准库构建脚本（动态库）
# ---------------------------------------------------------------------
# super / private / public 为语言关键字（词法/语法层实现），
# 本模块仅提供空命名空间，使 `from classtools import ...` 加载不报错。
#
# 产物：动态库 classtools（classtools.so / .dll / .dylib），输出到 build/stdlib/。
# 入口：导出统一符号 PycpModuleInit（extern "C"），import classtools 时动态加载。
# 依赖：PycpRuntime。
# =====================================================================

# 本库源文件与头文件
set(CLASSTOOLS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/classtools.cpp
)

set(CLASSTOOLS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/classtools.hpp
)

# OBJECT 库（编译期统一，供动态库聚合）
add_library(PycpBuiltin_classtools_obj OBJECT ${CLASSTOOLS_SOURCES} ${CLASSTOOLS_HEADERS})
target_include_directories(PycpBuiltin_classtools_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../backend/include
)
set_target_properties(PycpBuiltin_classtools_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 动态库
add_library(PycpBuiltin_classtools_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_classtools_obj>)
set_target_properties(PycpBuiltin_classtools_shared PROPERTIES
    OUTPUT_NAME "classtools"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

# 依赖 PycpRuntime（头文件与链接接口均来自 runtime）。
# Windows 下采用 shared 运行时（PycpRuntime_shared.dll），使扩展 DLL 可解析
# 运行时导出的 C++ 符号；非 Windows 维持静态链接（靠主程序 -rdynamic 导出符号）。
if(WIN32 AND BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_classtools_shared PUBLIC PycpRuntime_shared)
else()
    target_link_libraries(PycpBuiltin_classtools_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_classtools_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_classtools_obj PRIVATE -Wall -Wextra -O2)
endif()
