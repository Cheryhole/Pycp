# =====================================================================
# Pycp stdlib/common：被 ≥2 个模块复用的共性实现（仅头文件）
# ---------------------------------------------------------------------
# 内容：
#   include/visibility.hpp   private / public / readonly 装饰器（classtools、pycp）
#   include/file_common.hpp  File 类型类模块绑定（io、filesystem）
#
# 形态：INTERFACE 库（只提供头文件搜索路径），不产出任何可加载模块，
#       也不注册 PYCP_STDLIB_TARGETS —— 故 dist 收集契约完全不变。
#
# 为何不是 OBJECT/SHARED/STATIC 库：静态 AOT 会把全部扩展归档
# （libPycpExt_*.a）链入同一 exe，任何以「外部链接普通函数」提供的共性
# 实现都会在多份归档中重复定义。故实现全部 inline（内部链接/vague
# linkage），每个包含者各持一份副本 —— 既不跨库冲突，也不必新增产物。
# 详见 include/visibility.hpp 顶部说明与 docs/STDLIB_REFACTOR.md R1。
# =====================================================================

add_library(PycpCommon INTERFACE)

target_include_directories(PycpCommon INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 归入 runtime 头搜索根（各模块已有的 ${CMAKE_CURRENT_SOURCE_DIR}/../..），
# 使 common 头内的 "object/Pycp*.hpp" 引用可解析。
target_include_directories(PycpCommon INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
