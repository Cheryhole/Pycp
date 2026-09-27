# =====================================================================
# Pycp 标准库动态库统一构建入口
# ---------------------------------------------------------------------
# 逐个引入子库（io / Pycp）。每个子库编译为独立动态库
# （io.so / Pycp.so 等），输出到 ${CMAKE_BINARY_DIR}/stdlib/，
# 运行时通过 LoadNativeModule 从可执行文件旁的 stdlib/ 目录动态加载。
#
# 由顶层 CMakeLists.txt 在 add_subdirectory(src) 之后引入。
# =====================================================================

# common 先于各模块引入：它提供被 ≥2 个模块复用的共性实现（仅头文件的
# INTERFACE 目标 PycpCommon），各模块 target_link_libraries 取用其头搜索路径。
add_subdirectory(common)
add_subdirectory(io)
add_subdirectory(pycp)
add_subdirectory(classtools)
add_subdirectory(moduletools)
add_subdirectory(ast)
add_subdirectory(bytecode)
add_subdirectory(filesystem)
add_subdirectory(maths)
add_subdirectory(json)
