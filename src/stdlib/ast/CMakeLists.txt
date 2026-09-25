# =====================================================================
# Pycp ast 标准库构建脚本（动态库 + 静态库）
# ---------------------------------------------------------------------
# 提供源码 AST 内省：ast.parse / ast.dump / ast.type_of。
# 本模块只链接 PycpRuntime；解析能力经宿主注册的 SourceParser 钩子获取。
#
# 产物：
#   动态库 ast（ast.so / ast.dll / ast.dylib），输出到 build/stdlib/。
#     入口：导出符号 PycpModule_ast（按模块名命名，extern "C"）。
#   静态库 PycpExt_ast（libPycpExt_ast.a / PycpExt_ast.lib），输出到 build/lib/。
# 依赖：PycpRuntime。
# =====================================================================

set(AST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PycpAstModule.cpp
)

set(AST_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/ast_stdlib.hpp
)

# OBJECT 库（编译期统一，供动态库聚合）
add_library(PycpBuiltin_ast_obj OBJECT ${AST_SOURCES} ${AST_HEADERS})
target_include_directories(PycpBuiltin_ast_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
set_target_properties(PycpBuiltin_ast_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 动态库
add_library(PycpBuiltin_ast_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_ast_obj>)
set_target_properties(PycpBuiltin_ast_shared PROPERTIES
    OUTPUT_NAME "ast"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_ast_shared PUBLIC PycpRuntime_shared)
    set_target_properties(PycpBuiltin_ast_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../"
    )
else()
    target_link_libraries(PycpBuiltin_ast_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_ast_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_ast_obj PRIVATE -Wall -Wextra -O2)
endif()

# =====================================================================
# 静态库（AOT --static 模式用）
# ---------------------------------------------------------------------
# 静态库版必须定义 PYCP_STATIC（详见 io/CMakeLists.txt 同名段落说明）。
# =====================================================================
add_library(PycpBuiltin_ast_obj_static OBJECT ${AST_SOURCES} ${AST_HEADERS})
target_include_directories(PycpBuiltin_ast_obj_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_compile_definitions(PycpBuiltin_ast_obj_static PUBLIC PYCP_STATIC)

add_library(PycpBuiltin_ast_static STATIC $<TARGET_OBJECTS:PycpBuiltin_ast_obj_static>)
set_target_properties(PycpBuiltin_ast_static PROPERTIES
    OUTPUT_NAME "PycpExt_ast"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
target_link_libraries(PycpBuiltin_ast_static PUBLIC PycpRuntime_static)

if(MSVC)
    target_compile_options(PycpBuiltin_ast_obj_static PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_ast_obj_static PRIVATE -Wall -Wextra -O2)
endif()

# 注册到全局标准库目标清单（供 pycp-dist 收集）。
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_ast_shared)
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_STATIC_TARGETS PycpBuiltin_ast_static)
