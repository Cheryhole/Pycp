# =====================================================================
# Pycp 最终产物收集脚本
# ---------------------------------------------------------------------
# 由顶层 CMakeLists.txt 中的 pycp-dist 目标通过
#   ${CMAKE_COMMAND} -D<param>=<value> ... -P cmake/PycpDist.cmake
# 在【构建期】调用（非配置期），将散落在构建树各处的产物统一收集到
# 一个自包含的最终输出目录：
#
#   <DIST_DIR>/pycp                 可执行文件
#   <DIST_DIR>/stdlib/<name>.so     标准库原生扩展（运行时硬约束：
#                                   PycpNativeExt::GetStdlibDir() 固定查找
#                                   可执行文件旁的 stdlib/ 目录）
#   <LIB_DIR>/libPycpExt_<name>.a   标准库扩展的静态库（AOT --static 链接用）
#   <LIB_DIR>/libPycpRuntime.a      运行时静态库（AOT 生成代码链接用）
#   <LIB_DIR>/libPycpRuntime.so     运行时动态库（+ Windows 导入库）
#   <INCLUDE_DIR>/*.h / *.hpp       AOT 与原生扩展所需的全部后端头文件
#   <DIST_DIR>/BUILD_INFO.txt       产物溯源信息（可选）
#
# 子目录缺省回退到 DIST_DIR（保持“平铺”用法仍可用）：
#   LIB_DIR 未给 -> DIST_DIR；INCLUDE_DIR 未给 -> DIST_DIR。
# stdlib/ 始终与可执行文件同级，不参与配置（运行时硬约束）。
#
# 设计要点：
#   1. 多值参数统一用 '|' 分隔，不用 CMake 列表——Windows 路径可能含 ';'，
#      拼成列表会被静默截断。脚本内 string(REPLACE "|" ";") 还原。
#   2. 所有路径经 file(TO_CMAKE_PATH) + get_filename_component(ABSOLUTE)
#      规范化（Windows '\' -> '/'、去掉 ./ 与 ..、统一绝对路径）。
#   3. 复制用 ${CMAKE_COMMAND} -E copy_if_different：同名文件覆盖更新，
#      内容相同则跳过时间戳变更；源文件缺失立即 FATAL_ERROR，避免产出
#      “看起来构建成功但缺文件”的半成品目录。
#   4. 只覆盖同名文件、不清空目标目录，重复执行幂等。
# =====================================================================

cmake_minimum_required(VERSION 3.15)

# =====================================================================
# 参数校验
# =====================================================================
if(NOT DIST_DIR)
	message(FATAL_ERROR "pycp-dist: DIST_DIR is not set.")
endif()

if(NOT BASE_DIR)
	message(FATAL_ERROR "pycp-dist: BASE_DIR is not set.")
endif()

if(NOT CMAKE_COMMAND)
	message(FATAL_ERROR "pycp-dist: CMAKE_COMMAND is not available.")
endif()

# =====================================================================
# 工具函数
# =====================================================================

# '|' 分隔字符串 -> CMake 列表
function(pycp_dist_split out_var value)
	set(_items "")
	if(NOT value STREQUAL "")
		string(REPLACE "|" ";" _items "${value}")
	endif()
	set(${out_var} "${_items}" PARENT_SCOPE)
endfunction()

# 路径规范化：统一分隔符并绝对路径化（相对路径以 base_dir 为基准）
function(pycp_dist_abs out_var path base_dir)
	file(TO_CMAKE_PATH "${path}" _p)
	if(IS_ABSOLUTE "${_p}")
		get_filename_component(_a "${_p}" ABSOLUTE)
	else()
		get_filename_component(_a "${base_dir}/${_p}" ABSOLUTE)
	endif()
	set(${out_var} "${_a}" PARENT_SCOPE)
endfunction()

# 复制单个文件（自动建目录 / 覆盖更新 / 失败即报错）
#   pycp_dist_copy(<src> <dst> [EXECUTABLE])
function(pycp_dist_copy src dst)
	cmake_parse_arguments(PYCP_DIST "EXECUTABLE" "" "" ${ARGN})

	if(NOT EXISTS "${src}")
		message(FATAL_ERROR "pycp-dist: source file is missing: ${src}")
	endif()

	get_filename_component(_dst_dir "${dst}" DIRECTORY)
	file(MAKE_DIRECTORY "${_dst_dir}")

	execute_process(
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${dst}"
		RESULT_VARIABLE _res
		ERROR_VARIABLE _err
	)
	if(NOT _res EQUAL 0)
		message(FATAL_ERROR
			"pycp-dist: failed to copy\n"
			"  from: ${src}\n"
			"  to  : ${dst}\n"
			"  err : ${_err}")
	endif()

	# 可执行文件与动态库在非 Windows 下显式补可执行位，
	# 防止复制环节丢失权限导致 dist 中 ./pycp 无法启动。
	get_filename_component(_ext "${dst}" EXT)
	set(_need_exec FALSE)
	if(PYCP_DIST_EXECUTABLE)
		set(_need_exec TRUE)
	elseif(_ext STREQUAL ".so" OR _ext STREQUAL ".dylib")
		set(_need_exec TRUE)
	endif()
	if(_need_exec AND NOT CMAKE_HOST_WIN32)
		file(CHMOD "${dst}"
			PERMISSIONS
				OWNER_READ OWNER_WRITE OWNER_EXECUTE
				GROUP_READ GROUP_EXECUTE
				WORLD_READ WORLD_EXECUTE
		)
	endif()

	math(EXPR _count "${PYCP_DIST_COUNT} + 1")
	set(PYCP_DIST_COUNT ${_count} PARENT_SCOPE)
endfunction()

# =====================================================================
# 规范化目标目录
# =====================================================================
pycp_dist_abs(DIST_DIR "${DIST_DIR}" "${BASE_DIR}")

# 子目录缺省回退到 dist 根目录；stdlib 始终与可执行文件同级（运行时硬约束）
if(NOT LIB_DIR)
	set(LIB_DIR "${DIST_DIR}")
else()
	pycp_dist_abs(LIB_DIR "${LIB_DIR}" "${BASE_DIR}")
endif()
if(NOT INCLUDE_DIR)
	set(INCLUDE_DIR "${DIST_DIR}")
else()
	pycp_dist_abs(INCLUDE_DIR "${INCLUDE_DIR}" "${BASE_DIR}")
endif()
set(PYCP_DIST_STDLIB_DIR "${DIST_DIR}/stdlib")

file(MAKE_DIRECTORY "${DIST_DIR}")
file(MAKE_DIRECTORY "${LIB_DIR}")
file(MAKE_DIRECTORY "${INCLUDE_DIR}")
file(MAKE_DIRECTORY "${PYCP_DIST_STDLIB_DIR}")

set(PYCP_DIST_COUNT 0)

# =====================================================================
# 1. 主可执行文件
# =====================================================================
if(EXE_FILE)
	pycp_dist_abs(_exe "${EXE_FILE}" "${BASE_DIR}")
	get_filename_component(_exe_name "${_exe}" NAME)
	pycp_dist_copy("${_exe}" "${DIST_DIR}/${_exe_name}" EXECUTABLE)
endif()

# =====================================================================
# 2. 标准库原生扩展（io / Pycp / classtools ...）
# =====================================================================
pycp_dist_split(_stdlib_files "${STDLIB_FILES}")
foreach(_f IN LISTS _stdlib_files)
	if(_f STREQUAL "")
		continue()
	endif()
	pycp_dist_abs(_src "${_f}" "${BASE_DIR}")
	get_filename_component(_name "${_src}" NAME)
	pycp_dist_copy("${_src}" "${PYCP_DIST_STDLIB_DIR}/${_name}")
endforeach()

# =====================================================================
# 2b. 标准库扩展的静态库（libPycpExt_<name>.a）-> LIB_DIR
# ---------------------------------------------------------------------
# 供 AOT 的 --static 模式链接进产物 exe：扩展与主程序共享同一份运行时
# 状态，不需要部署 stdlib/ 目录。
# =====================================================================
pycp_dist_split(_stdlib_static_files "${STDLIB_STATIC_FILES}")
foreach(_f IN LISTS _stdlib_static_files)
	if(_f STREQUAL "")
		continue()
	endif()
	pycp_dist_abs(_src "${_f}" "${BASE_DIR}")
	get_filename_component(_name "${_src}" NAME)
	pycp_dist_copy("${_src}" "${LIB_DIR}/${_name}")
endforeach()

# =====================================================================
# 3. 运行时库（静态库 / 动态库 / Windows 导入库）-> LIB_DIR
# =====================================================================
pycp_dist_split(_runtime_files "${RUNTIME_FILES}")
foreach(_f IN LISTS _runtime_files)
	if(_f STREQUAL "")
		continue()
	endif()
	pycp_dist_abs(_src "${_f}" "${BASE_DIR}")
	get_filename_component(_name "${_src}" NAME)
	pycp_dist_copy("${_src}" "${LIB_DIR}/${_name}")
endforeach()

# =====================================================================
# 3b. 需与可执行文件同级的运行时文件（Windows DLL 例外）
# ---------------------------------------------------------------------
# Windows 加载 DLL 只搜索可执行文件所在目录，不会搜索 lib/，因此
# PycpRuntime.dll 必须在 exe 同级再放一份；POSIX 靠 rpath 定位 lib/，
# 无需此例外。
# =====================================================================
pycp_dist_split(_root_files "${ROOT_FILES}")
foreach(_f IN LISTS _root_files)
	if(_f STREQUAL "")
		continue()
	endif()
	pycp_dist_abs(_src "${_f}" "${BASE_DIR}")
	get_filename_component(_name "${_src}" NAME)
	pycp_dist_copy("${_src}" "${DIST_DIR}/${_name}")
endforeach()

# =====================================================================
# 4. 后端头文件（构建期 GLOB，新增头文件零改动自动纳入）
# =====================================================================
pycp_dist_split(_include_dirs "${INCLUDE_DIRS}")
foreach(_d IN LISTS _include_dirs)
	if(_d STREQUAL "")
		continue()
	endif()
	pycp_dist_abs(_dir "${_d}" "${BASE_DIR}")
	if(NOT IS_DIRECTORY "${_dir}")
		message(WARNING "pycp-dist: include directory does not exist: ${_dir}")
		continue()
	endif()
	file(GLOB _headers "${_dir}/*.h" "${_dir}/*.hpp")
	foreach(_h IN LISTS _headers)
		get_filename_component(_name "${_h}" NAME)
		pycp_dist_copy("${_h}" "${INCLUDE_DIR}/${_name}")
	endforeach()
endforeach()

# =====================================================================
# 5. 产物溯源信息
# =====================================================================
if(WRITE_BUILD_INFO)
	string(TIMESTAMP _now "%Y-%m-%d %H:%M:%S")
	file(WRITE "${DIST_DIR}/BUILD_INFO.txt"
		"Pycp ${BUILD_INFO_VERSION} distribution\n"
		"  system     : ${BUILD_INFO_SYSTEM}\n"
		"  processor  : ${BUILD_INFO_ARCH}\n"
		"  build type : ${BUILD_INFO_TYPE}\n"
		"  compiler   : ${BUILD_INFO_COMPILER}\n"
		"  generated  : ${_now}\n"
		"\n"
		"Layout:\n"
		"  pycp                Pycp executable (looks up <dir>/stdlib for native modules)\n"
		"  stdlib/             Standard library native extensions (io / Pycp / classtools)\n"
		"  lib/libPycpExt_*.a  Static native extensions (for AOT --static builds)\n"
		"  lib/libPycpRuntime* Runtime library (shared + static) for AOT-generated C++\n"
		"  include/*.h *.hpp   Backend headers required to build AOT / native extensions\n"
		"\n"
		"AOT build (shared, default): -I <dir>/include -L <dir>/lib -lPycpRuntime\n"
		"AOT build (static)         : pycp --emit-cpp --static app.pycp -o <out>\n"
	)
	math(EXPR _count "${PYCP_DIST_COUNT} + 1")
	set(PYCP_DIST_COUNT ${_count})
endif()

# =====================================================================
# 摘要（单行，避免刷屏）
# =====================================================================
message(STATUS "pycp-dist: ${PYCP_DIST_COUNT} file(s) -> ${DIST_DIR}")
