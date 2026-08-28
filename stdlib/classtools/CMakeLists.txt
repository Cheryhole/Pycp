# =====================================================================
# Pycp classtools 标准库构建脚本（动态库）
# ---------------------------------------------------------------------
# super / private / public 为语言关键字（词法/语法层实现），
# 本模块仅提供空命名空间，使 `from classtools import ...` 加载不报错。
#
# 产物：动态库 classtools（classtools.so / .dll / .dylib），输出到 build/stdlib/。
# 入口：导出符号 PycpModule_classtools（按模块名命名，extern "C"），import classtools 时动态加载。
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
# 构建了 shared 运行时就统一动态链接：全进程只存在一份运行时状态（GC 池、
# 小整数池、扩展句柄缓存），避免每个扩展 .so 内嵌一份副本。
if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_classtools_shared PUBLIC PycpRuntime_shared)
    # rpath：dist/stdlib/*.so -> dist/lib；build/stdlib/*.so -> build/backend。
    set_target_properties(PycpBuiltin_classtools_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../lib;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
else()
    target_link_libraries(PycpBuiltin_classtools_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_classtools_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_classtools_obj PRIVATE -Wall -Wextra -O2)
endif()

# 注册到全局标准库目标清单（供 pycp-dist 收集到 <dist>/stdlib/）。
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_classtools_shared)
