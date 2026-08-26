# =====================================================================
# Pycp 标准库构建脚本（动态库）
# ---------------------------------------------------------------------
# 提供数据类型转换等内建功能：
#   Pycp.String(x)：内置类型类，调用时返回字符串表示（语义对齐 Python str(x)）。
#   Pycp.Integer(x)：内置类型类，调用时返回整数（语义对齐 Python int(x)）。
#
# 产物：动态库 Pycp（Pycp.so / Pycp.dll / Pycp.dylib），输出到 build/stdlib/。
# 入口：导出符号 PycpModule_Pycp（按模块名命名，extern "C"），import Pycp 时动态加载。
# 依赖：PycpRuntime。
# =====================================================================

# 本库源文件与头文件
set(PYCP_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpModule.cpp
)

set(PYCP_LIB_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pycp_stdlib.hpp
)

# OBJECT 库（编译期统一，供动态库聚合）
add_library(PycpBuiltin_pycp_obj OBJECT ${PYCP_LIB_SOURCES} ${PYCP_LIB_HEADERS})
target_include_directories(PycpBuiltin_pycp_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../backend/include
)
set_target_properties(PycpBuiltin_pycp_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 动态库
add_library(PycpBuiltin_pycp_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_pycp_obj>)
set_target_properties(PycpBuiltin_pycp_shared PROPERTIES
    OUTPUT_NAME "Pycp"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

# 依赖 PycpRuntime（头文件与链接接口均来自 runtime）。
# Windows 下采用 shared 运行时（PycpRuntime_shared.dll），使扩展 DLL 可解析
# 运行时导出的 C++ 符号；非 Windows 维持静态链接（靠主程序 -rdynamic 导出符号）。
if(WIN32 AND BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_pycp_shared PUBLIC PycpRuntime_shared)
else()
    target_link_libraries(PycpBuiltin_pycp_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_pycp_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_pycp_obj PRIVATE -Wall -Wextra -O2)
endif()
