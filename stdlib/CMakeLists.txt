# =====================================================================
# Pycp 标准库动态库统一构建入口
# ---------------------------------------------------------------------
# 逐个引入子库（io / Pycp）。每个子库编译为独立动态库
# （io.so / Pycp.so 等），输出到 ${CMAKE_BINARY_DIR}/stdlib/，
# 运行时通过 LoadNativeModule 从可执行文件旁的 stdlib/ 目录动态加载。
#
# 由顶层 CMakeLists.txt 在 add_subdirectory(backend) 之后引入。
# =====================================================================

add_subdirectory(io)
add_subdirectory(Pycp)
add_subdirectory(classtools)
