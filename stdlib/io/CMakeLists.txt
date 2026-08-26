# =====================================================================
# Pycp io 标准库构建脚本（动态库）
# ---------------------------------------------------------------------
# 提供标准输入输出对象（stdin/stdout/stderr 为 File，
# 支持 .write / .readline 方法）。
#
# 产物：动态库 io（io.so / io.dll / io.dylib），输出到 build/stdlib/。
# 入口：导出符号 PycpModule_io（按模块名命名，extern "C"），import io 时动态加载。
# 依赖：PycpRuntime。
# =====================================================================

# 本库源文件与头文件
set(IO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io.cpp
)

set(IO_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/PycpFile.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/io.hpp
)

# OBJECT 库（编译期统一，供动态库聚合）
add_library(PycpBuiltin_io_obj OBJECT ${IO_SOURCES} ${IO_HEADERS})
target_include_directories(PycpBuiltin_io_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../backend/include
)
set_target_properties(PycpBuiltin_io_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 动态库
add_library(PycpBuiltin_io_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_io_obj>)
set_target_properties(PycpBuiltin_io_shared PROPERTIES
    OUTPUT_NAME "io"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

# 依赖 PycpRuntime（头文件与链接接口均来自 runtime）。
# Windows 下采用 shared 运行时（PycpRuntime_shared.dll），使扩展 DLL 可解析
# 运行时导出的 C++ 符号；非 Windows 维持静态链接（靠主程序 -rdynamic 导出符号）。
if(WIN32 AND BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_io_shared PUBLIC PycpRuntime_shared)
else()
    target_link_libraries(PycpBuiltin_io_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_io_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_io_obj PRIVATE -Wall -Wextra -O2)
endif()
