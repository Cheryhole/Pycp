# =====================================================================
# moduletools 标准库构建脚本（动态库 + 静态库）
# ---------------------------------------------------------------------
# 模块文件夹（package）特性的配套工具库：
#   - this()：当前包模块对象
#   - as_program() / as_library() / role() / is_program() / is_library()
#   - Project()：__codegen__() 返回的项目描述（子模块形态 + 可执行名）
#
# 产物：
#   动态库 moduletools（moduletools.so / .dll / .dylib），输出到 build/stdlib/。
#     入口：导出符号 PycpModule_moduletools（按模块名命名，extern "C"），
#           import moduletools 时动态加载。
#   静态库 PycpExt_moduletools（libPycpExt_moduletools.a），输出到 build/lib/。
#     供 AOT 的 --static 模式链接进产物 exe；同时被 PycpAotSdkLocator 扫描
#     lib/libPycpExt_*.a 时自动收录为「内置扩展」，--emit-cpp 不再把它当
#     未解析依赖告警。
# 依赖：PycpRuntime。
# =====================================================================

set(MODULETOOLS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/moduletools.cpp
)

set(MODULETOOLS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/moduletools.hpp
)

# OBJECT 库（编译期统一，供动态库聚合）
add_library(PycpBuiltin_moduletools_obj OBJECT ${MODULETOOLS_SOURCES} ${MODULETOOLS_HEADERS})
target_include_directories(PycpBuiltin_moduletools_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
set_target_properties(PycpBuiltin_moduletools_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 动态库
add_library(PycpBuiltin_moduletools_shared SHARED $<TARGET_OBJECTS:PycpBuiltin_moduletools_obj>)
set_target_properties(PycpBuiltin_moduletools_shared PROPERTIES
    OUTPUT_NAME "moduletools"
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/stdlib
)

# 与 io/pycp/classtools 一致：构建了 shared 运行时就统一动态链接，
# 保证全进程只有一份运行时状态。
if(BUILD_RUNTIME_SHARED)
    target_link_libraries(PycpBuiltin_moduletools_shared PUBLIC PycpRuntime_shared)
    set_target_properties(PycpBuiltin_moduletools_shared PROPERTIES
        BUILD_RPATH "$ORIGIN/../;$ORIGIN/../backend"
        INSTALL_RPATH "$ORIGIN/../"
    )
else()
    target_link_libraries(PycpBuiltin_moduletools_shared PUBLIC PycpRuntime_static)
endif()

if(MSVC)
    target_compile_options(PycpBuiltin_moduletools_obj PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_moduletools_obj PRIVATE -Wall -Wextra -O2)
endif()

# =====================================================================
# 静态库（AOT --static 模式用）
# ---------------------------------------------------------------------
# 同 io/CMakeLists.txt 的说明：静态库版必须定义 PYCP_STATIC，故不能复用
# 上面的 OBJECT 库，需另建一套。
# =====================================================================
add_library(PycpBuiltin_moduletools_obj_static OBJECT ${MODULETOOLS_SOURCES} ${MODULETOOLS_HEADERS})
target_include_directories(PycpBuiltin_moduletools_obj_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_compile_definitions(PycpBuiltin_moduletools_obj_static PUBLIC PYCP_STATIC)

add_library(PycpBuiltin_moduletools_static STATIC $<TARGET_OBJECTS:PycpBuiltin_moduletools_obj_static>)
set_target_properties(PycpBuiltin_moduletools_static PROPERTIES
    OUTPUT_NAME "PycpExt_moduletools"
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
target_link_libraries(PycpBuiltin_moduletools_static PUBLIC PycpRuntime_static)

if(MSVC)
    target_compile_options(PycpBuiltin_moduletools_obj_static PRIVATE /W4 /EHsc /utf-8)
else()
    target_compile_options(PycpBuiltin_moduletools_obj_static PRIVATE -Wall -Wextra -O2)
endif()

# 注册到全局标准库目标清单（pycp-dist 收集到 <dist>/stdlib/ 与 <dist>/lib/）。
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_TARGETS PycpBuiltin_moduletools_shared)
set_property(GLOBAL APPEND PROPERTY PYCP_STDLIB_STATIC_TARGETS PycpBuiltin_moduletools_static)
