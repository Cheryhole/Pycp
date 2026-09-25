# =====================================================================
# Pycp filesystem 标准库构建脚本（动态库 + 静态库）
# ---------------------------------------------------------------------
# 文件系统与进程环境原语：mkdir / listdir / join / basename / dirname /
# exists / is_file / is_dir / getenv / cwd / exe_dir / stdlib_dir + File。
# File 与 io.File 为同一类型（实现位于运行时 object/PycpFile.cpp）。
#
# 产物：
#   动态库 filesystem（filesystem.so / .dll / .dylib），输出到 build/stdlib/。
#     入口：导出符号 PycpModule_filesystem（extern "C"）。
#   静态库 PycpExt_filesystem（libPycpExt_filesystem.a），输出到 build/lib/。
# 依赖：PycpRuntime。
# =====================================================================

set(FILESYSTEM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpFilesystemModule.cpp
)

set(FILESYSTEM_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/filesystem_stdlib.hpp
)

add_library(PycpBuiltin_filesystem_obj OBJECT ${FILESYSTEM_SOURCES} ${FILESYSTEM_HEADERS})
target_include_directories(PycpBuiltin_filesystem_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
set_target_properties(PycpBuiltin_filesystem_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(PycpBuiltin_filesystem_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_filesystem_obj>)
set_target_properties(PycpBuiltin_filesystem_shared PROPERTIES
    OUTPUT_NAME "filesystem"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_filesystem_shared PUBLIC PycpRuntime_shared)
    set_target_properties(PycpBuiltin_filesystem_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../"
    )
else()
    target_link_libraries(PycpBuiltin_filesystem_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_filesystem_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_filesystem_obj PRIVATE -Wall -Wextra -O2)
endif()

# 静态库（AOT --static 模式用；静态版须定义 PYCP_STATIC）
add_library(PycpBuiltin_filesystem_obj_static OBJECT ${FILESYSTEM_SOURCES} ${FILESYSTEM_HEADERS})
target_include_directories(PycpBuiltin_filesystem_obj_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_compile_definitions(PycpBuiltin_filesystem_obj_static PUBLIC PYCP_STATIC)

add_library(PycpBuiltin_filesystem_static STATIC $<TARGET_OBJECTS:PycpBuiltin_filesystem_obj_static>)
set_target_properties(PycpBuiltin_filesystem_static PROPERTIES
    OUTPUT_NAME "PycpExt_filesystem"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
target_link_libraries(PycpBuiltin_filesystem_static PUBLIC PycpRuntime_static)

if(MSVC)
    target_compile_options(PycpBuiltin_filesystem_obj_static PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_filesystem_obj_static PRIVATE -Wall -Wextra -O2)
endif()

set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_filesystem_shared)
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_STATIC_TARGETS PycpBuiltin_filesystem_static)
