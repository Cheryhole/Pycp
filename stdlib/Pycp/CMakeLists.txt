# =====================================================================
# Pycp 标准库构建脚本（动态库）
# ---------------------------------------------------------------------
# 提供数据类型转换等内建功能：
#   Pycp.String(x)：内置类型类，调用时返回字符串表示（语义对齐 Python str(x)）。
#   Pycp.Integer(x)：内置类型类，调用时返回整数（语义对齐 Python int(x)）。
#
# 产物：
#   动态库 Pycp（Pycp.so / Pycp.dll / Pycp.dylib），输出到 build/stdlib/。
#     入口：导出符号 PycpModule_Pycp（按模块名命名，extern "C"），
#           import Pycp 时动态加载。
#   静态库 PycpExt_Pycp（libPycpExt_Pycp.a / PycpExt_Pycp.lib），输出到 build/lib/。
#     供 AOT 的 --static 模式链接进产物 exe。
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
# 构建了 shared 运行时就统一动态链接：全进程只存在一份运行时状态（GC 池、
# 小整数池、扩展句柄缓存），避免每个扩展 .so 内嵌一份副本。
if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_pycp_shared PUBLIC PycpRuntime_shared)
    # rpath：dist/stdlib/*.so -> dist/lib；build/stdlib/*.so -> build/backend。
    set_target_properties(PycpBuiltin_pycp_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../lib;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
else()
    target_link_libraries(PycpBuiltin_pycp_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_pycp_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_pycp_obj PRIVATE -Wall -Wextra -O2)
endif()

# =====================================================================
# 静态库（AOT --static 模式用）
# ---------------------------------------------------------------------
# 不能复用上面的 OBJECT 库：PYCP_API 在 Windows 下默认 dllimport，
# 动态库版必须保持 dllimport，静态库版必须定义 PYCP_STATIC（详见
# stdlib/io/CMakeLists.txt 中的同名段落说明）。
# =====================================================================
add_library(PycpBuiltin_pycp_obj_static OBJECT ${PYCP_LIB_SOURCES} ${PYCP_LIB_HEADERS})
target_include_directories(PycpBuiltin_pycp_obj_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../backend/include
)
target_compile_definitions(PycpBuiltin_pycp_obj_static PUBLIC PYCP_STATIC)

add_library(PycpBuiltin_pycp_static STATIC $<TARGET_OBJECTS:PycpBuiltin_pycp_obj_static>)
set_target_properties(PycpBuiltin_pycp_static PROPERTIES
    OUTPUT_NAME "PycpExt_Pycp"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
target_link_libraries(PycpBuiltin_pycp_static PUBLIC PycpRuntime_static)

if(MSVC)
    target_compile_options(PycpBuiltin_pycp_obj_static PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_pycp_obj_static PRIVATE -Wall -Wextra -O2)
endif()

# 注册到全局标准库目标清单（供 pycp-dist 收集到 <dist>/stdlib/）。
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_pycp_shared)
# 静态库清单（供 pycp-dist 收集到 <dist>/lib/）。
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_STATIC_TARGETS PycpBuiltin_pycp_static)
