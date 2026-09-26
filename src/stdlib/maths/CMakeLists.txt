# =====================================================================
# Pycp maths 标准库构建脚本（动态库 + 静态库）
# ---------------------------------------------------------------------
# 数值类型的数学方法（见 docs/CORE_METHODS.md §13）：
#   maths（maths.so / maths.dll / maths.dylib），输出到 build/stdlib/。
#     入口：导出符号 PycpModule_maths（按模块名命名，extern "C"）。
#   静态库 PycpExt_maths（libPycpExt_maths.a / PycpExt_maths.lib），
#     输出到 build/lib/，供 AOT --static 链接。
# 依赖：PycpRuntime。
# =====================================================================

set(MATHS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/maths.cpp
)
set(MATHS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/maths_stdlib.hpp
)

# OBJECT 库（动态）
add_library(PycpBuiltin_maths_obj OBJECT ${MATHS_SOURCES} ${MATHS_HEADERS})
target_include_directories(PycpBuiltin_maths_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
set_target_properties(PycpBuiltin_maths_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 动态库
add_library(PycpBuiltin_maths_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_maths_obj>)
set_target_properties(PycpBuiltin_maths_shared PROPERTIES
    OUTPUT_NAME "maths"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_maths_shared PUBLIC PycpRuntime_shared)
    set_target_properties(PycpBuiltin_maths_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../"
    )
else()
    target_link_libraries(PycpBuiltin_maths_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_maths_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_maths_obj PRIVATE -Wall -Wextra -O2)
endif()

# =====================================================================
# 静态库（AOT --static 模式用）
# 不能复用动态 OBJECT：PYCP_API 在 Windows 下默认 dllimport，静态版必须
# 定义 PYCP_STATIC（详见 stdlib/io/CMakeLists.txt 的说明）。
# =====================================================================
add_library(PycpBuiltin_maths_obj_static OBJECT ${MATHS_SOURCES} ${MATHS_HEADERS})
target_include_directories(PycpBuiltin_maths_obj_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_compile_definitions(PycpBuiltin_maths_obj_static PUBLIC PYCP_STATIC)

add_library(PycpBuiltin_maths_static STATIC $<TARGET_OBJECTS:PycpBuiltin_maths_obj_static>)
set_target_properties(PycpBuiltin_maths_static PROPERTIES
    OUTPUT_NAME "PycpExt_maths"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
target_link_libraries(PycpBuiltin_maths_static PUBLIC PycpRuntime_static)

if(MSVC)
    target_compile_options(PycpBuiltin_maths_obj_static PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_maths_obj_static PRIVATE -Wall -Wextra -O2)
endif()

# 注册到全局标准库目标清单（pycp-dist 据此收集）。
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_maths_shared)
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_STATIC_TARGETS PycpBuiltin_maths_static)
