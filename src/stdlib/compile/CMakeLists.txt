# =====================================================================
# Pycp compile 标准库构建脚本（动态库 + 静态库）
# ---------------------------------------------------------------------
# 语言内编译与执行：compile.compile / compile.exec。
# 本模块只链接 PycpRuntime；源码->字节码经宿主 SourceStringCompiler 钩子，
# 字节码->执行走运行时 VM（RunBytecodeModule）。
#
# 产物：
#   动态库 compile（compile.so / .dll / .dylib），输出到 build/stdlib/。
#     入口：导出符号 PycpModule_compile（extern "C"）。
#   静态库 PycpExt_compile（libPycpExt_compile.a），输出到 build/lib/。
# 依赖：PycpRuntime。
# =====================================================================

set(COMPILE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpCompileModule.cpp
)

set(COMPILE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/compile_stdlib.hpp
)

add_library(PycpBuiltin_compile_obj OBJECT ${COMPILE_SOURCES} ${COMPILE_HEADERS})
target_include_directories(PycpBuiltin_compile_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
set_target_properties(PycpBuiltin_compile_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(PycpBuiltin_compile_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_compile_obj>)
set_target_properties(PycpBuiltin_compile_shared PROPERTIES
    OUTPUT_NAME "compile"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_compile_shared PUBLIC PycpRuntime_shared)
    set_target_properties(PycpBuiltin_compile_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../"
    )
else()
    target_link_libraries(PycpBuiltin_compile_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_compile_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_compile_obj PRIVATE -Wall -Wextra -O2)
endif()

# 静态库（AOT --static 模式用；静态版须定义 PYCP_STATIC）
add_library(PycpBuiltin_compile_obj_static OBJECT ${COMPILE_SOURCES} ${COMPILE_HEADERS})
target_include_directories(PycpBuiltin_compile_obj_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_compile_definitions(PycpBuiltin_compile_obj_static PUBLIC PYCP_STATIC)

add_library(PycpBuiltin_compile_static STATIC $<TARGET_OBJECTS:PycpBuiltin_compile_obj_static>)
set_target_properties(PycpBuiltin_compile_static PROPERTIES
    OUTPUT_NAME "PycpExt_compile"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
target_link_libraries(PycpBuiltin_compile_static PUBLIC PycpRuntime_static)

if(MSVC)
    target_compile_options(PycpBuiltin_compile_obj_static PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_compile_obj_static PRIVATE -Wall -Wextra -O2)
endif()

set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_compile_shared)
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_STATIC_TARGETS PycpBuiltin_compile_static)
