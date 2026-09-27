# =====================================================================
# Pycp json 标准库构建脚本（动态库 + 静态库）
# ---------------------------------------------------------------------
# 提供 JSON 编解码（json.load / json.dump），语义对齐 Python 的 json。
#
# 产物：
#   动态库 json（json.so / json.dll / json.dylib），输出到 build/stdlib/。
#     入口：导出符号 PycpModule_json（按模块名命名，extern "C"），
#           import json 时动态加载。
#   静态库 PycpExt_json（libPycpExt_json.a / PycpExt_json.lib），输出到 build/lib/。
#     供 AOT 的 --static 模式链接进产物 exe。
# 依赖：PycpRuntime。
# =====================================================================

# 本库源文件与头文件
set(JSON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/json.cpp
)

set(JSON_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/json.hpp
)

# OBJECT 库（编译期统一，供动态库聚合）
add_library(PycpBuiltin_json_obj OBJECT ${JSON_SOURCES} ${JSON_HEADERS})
target_include_directories(PycpBuiltin_json_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
set_target_properties(PycpBuiltin_json_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 动态库
add_library(PycpBuiltin_json_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_json_obj>)
set_target_properties(PycpBuiltin_json_shared PROPERTIES
    OUTPUT_NAME "json"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

# 依赖 PycpRuntime（头文件与链接接口均来自 runtime）。
if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_json_shared PUBLIC PycpRuntime_shared)
    set_target_properties(PycpBuiltin_json_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../"
    )
else()
    target_link_libraries(PycpBuiltin_json_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_json_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_json_obj PRIVATE -Wall -Wextra -O2)
endif()

# =====================================================================
# 静态库（AOT --static 模式用）
# =====================================================================
add_library(PycpBuiltin_json_obj_static OBJECT ${JSON_SOURCES} ${JSON_HEADERS})
target_include_directories(PycpBuiltin_json_obj_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_compile_definitions(PycpBuiltin_json_obj_static PUBLIC PYCP_STATIC)
target_link_libraries(PycpBuiltin_json_obj_static PUBLIC PycpCommon)

add_library(PycpBuiltin_json_static STATIC $<TARGET_OBJECTS:PycpBuiltin_json_obj_static>)
set_target_properties(PycpBuiltin_json_static PROPERTIES
    OUTPUT_NAME "PycpExt_json"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
target_link_libraries(PycpBuiltin_json_static PUBLIC PycpRuntime_static)

if(MSVC)
    target_compile_options(PycpBuiltin_json_obj_static PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_json_obj_static PRIVATE -Wall -Wextra -O2)
endif()

# 注册到全局标准库目标清单：顶层 CMakeLists 的 pycp-dist 目标据此把
# 本动态库收集到 <dist>/stdlib/。
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_json_shared)
# 静态库清单：pycp-dist 据此收集到 <dist>/lib/，供 AOT --static 链接。
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_STATIC_TARGETS PycpBuiltin_json_static)
