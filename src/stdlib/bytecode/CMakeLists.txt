# =====================================================================
# Pycp bytecode 标准库构建脚本（动态库 + 静态库）
# ---------------------------------------------------------------------
# 字节码结构化内省（类似 dis）：bytecode.compile / bytecode.dump。
# 本模块只链接 PycpRuntime；源码 -> 字节码经宿主 SourceStringCompiler 钩子。
#
# 产物：
#   动态库 bytecode（bytecode.so / .dll / .dylib），输出到 build/stdlib/。
#     入口：导出符号 PycpModule_bytecode（extern "C"）。
#   静态库 PycpExt_bytecode（libPycpExt_bytecode.a），输出到 build/lib/。
# 依赖：PycpRuntime。
# =====================================================================

set(BYTECODE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpBytecodeModule.cpp
)

set(BYTECODE_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/bytecode_stdlib.hpp
)

add_library(PycpBuiltin_bytecode_obj OBJECT ${BYTECODE_SOURCES} ${BYTECODE_HEADERS})
target_include_directories(PycpBuiltin_bytecode_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
set_target_properties(PycpBuiltin_bytecode_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(PycpBuiltin_bytecode_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_bytecode_obj>)
set_target_properties(PycpBuiltin_bytecode_shared PROPERTIES
    OUTPUT_NAME "bytecode"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_bytecode_shared PUBLIC PycpRuntime_shared)
    set_target_properties(PycpBuiltin_bytecode_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../"
    )
else()
    target_link_libraries(PycpBuiltin_bytecode_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_bytecode_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_bytecode_obj PRIVATE -Wall -Wextra -O2)
endif()

# 静态库（AOT --static 模式用；静态版须定义 PYCP_STATIC）
add_library(PycpBuiltin_bytecode_obj_static OBJECT ${BYTECODE_SOURCES} ${BYTECODE_HEADERS})
target_include_directories(PycpBuiltin_bytecode_obj_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_compile_definitions(PycpBuiltin_bytecode_obj_static PUBLIC PYCP_STATIC)

add_library(PycpBuiltin_bytecode_static STATIC $<TARGET_OBJECTS:PycpBuiltin_bytecode_obj_static>)
set_target_properties(PycpBuiltin_bytecode_static PROPERTIES
    OUTPUT_NAME "PycpExt_bytecode"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
target_link_libraries(PycpBuiltin_bytecode_static PUBLIC PycpRuntime_static)

if(MSVC)
    target_compile_options(PycpBuiltin_bytecode_obj_static PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_bytecode_obj_static PRIVATE -Wall -Wextra -O2)
endif()

set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_bytecode_shared)
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_STATIC_TARGETS PycpBuiltin_bytecode_static)
