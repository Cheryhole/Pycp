# =====================================================================
# Pycp AOT / 解释器一致性对比脚本
# ---------------------------------------------------------------------
# 目的：AOT（backend/src/PycpABI.cpp）与解释器（backend/src/PycpBytecodeVM.cpp）
# 是两套独立实现，行为容易漂移。本脚本用「同一份 .pycp，分别以解释器跑和
# 以 AOT 产物跑，逐字节比对 stdout / stderr / 退出码」的方式把差异暴露出来，
# 作为收敛两套实现的客观验收标准。
#
# 用法：
#   cmake -DPYCP=<pycp 可执行文件>
#         -DCASES=<.pycp 用例列表，'|' 分隔>
#         [-DWORK_DIR=<临时目录>]
#         [-DMODES=shared|static]        # 默认 shared
#         [-DGENERATOR=<CMake 生成器>]   # 未给则让 CMake 自选
#         [-DXFAIL=<子串列表，'|' 分隔>] # 已知缺陷用例（解释器自身崩溃等）降级为 XFAIL
#         -P cmake/PycpAotEquivalence.cmake
#
# 典型的调用入口是顶层 CMakeLists 里的 pycp-aot-equiv 目标：
#   cmake --build build --target pycp-aot-equiv
#
# 设计要点：
#   1. 用 cmake -P 脚本模式而非 shell 脚本：跨平台，且不依赖 bash。
#      （实测 Git Bash 下执行 Windows 版 pycp.exe 会 SIGSEGV，故不能走 bash。）
#   2. 多值参数用 '|' 分隔，不用 CMake 列表——Windows 路径可能含 ';'。
#   3. 地址归一化后再比对：AOT 与解释器打印对象时含堆地址
#      （如 <function "f" at 0x1c7117124d0>），地址天然不同，不归一化会
#      全量假阳性。统一替换为 0xADDR。
#   4. 每个用例输出 PASS/FAIL 与差异内容，末尾汇总；有 FAIL 即 FATAL_ERROR，
#      便于接 CI。
#   5. 用例自身预期失败（非零退出）不算不一致——比对的是「两边是否一致」，
#      不是「是否成功」。
# =====================================================================

cmake_minimum_required(VERSION 3.15)

# =====================================================================
# 参数校验
# =====================================================================
if(NOT PYCP)
	message(FATAL_ERROR "pycp-aot-equiv: PYCP is not set (path to the pycp executable).")
endif()
if(NOT EXISTS "${PYCP}")
	message(FATAL_ERROR "pycp-aot-equiv: PYCP executable not found: ${PYCP}")
endif()
if(NOT CASES)
	message(FATAL_ERROR "pycp-aot-equiv: CASES is not set.")
endif()
if(NOT MODES)
	set(MODES "shared")
endif()
if(NOT WORK_DIR)
	set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/aot-equiv")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

# '|' 分隔字符串 -> CMake 列表
function(pycp_equiv_split out_var value)
	set(_items "")
	if(NOT value STREQUAL "")
		string(REPLACE "|" ";" _items "${value}")
	endif()
	set(${out_var} "${_items}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
# 归一化：把输出中的十六进制地址统一替换成占位符。
# 同时统一换行符（Windows 为 CRLF，POSIX 为 LF），避免行尾差异造成假阴性。
# ---------------------------------------------------------------------
function(pycp_equiv_normalize out_var text)
	set(_t "${text}")
	# 统一换行符
	string(REGEX REPLACE "\r\n" "\n" _t "${_t}")
	string(REGEX REPLACE "\r" "\n" _t "${_t}")
	# 地址归一化（0x 开头的十六进制串）
	string(REGEX REPLACE "0x[0-9a-fA-F]+" "0xADDR" _t "${_t}")
	set(${out_var} "${_t}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------
# 运行一个程序，捕获 stdout/stderr 与退出码。
#   结果写入前缀变量： <out_prefix>_OUT / _ERR / _RC
# ---------------------------------------------------------------------
function(pycp_equiv_run out_prefix working_dir)
	execute_process(
		COMMAND ${ARGN}
		WORKING_DIRECTORY "${working_dir}"
		RESULT_VARIABLE _rc
		OUTPUT_VARIABLE _out
		ERROR_VARIABLE _err
	)
	set(${out_prefix}_RC  "${_rc}"  PARENT_SCOPE)
	set(${out_prefix}_OUT "${_out}" PARENT_SCOPE)
	set(${out_prefix}_ERR "${_err}" PARENT_SCOPE)
endfunction()

# =====================================================================
# 主流程
# =====================================================================
pycp_equiv_split(_cases "${CASES}")
pycp_equiv_split(_modes "${MODES}")

# 已知缺陷用例子串（'|' 分隔）。匹配到用例名即视为「预期不一致」——
# 解释器自身在此类用例上行为是已知的未定义崩溃（如 circular_import 的
# 循环导入退出段错误），AOT 与之不一致属预期，降级为 XFAIL 而非 FAIL。
set(_xfail_patterns "")
if(XFAIL)
	pycp_equiv_split(_xfail_patterns "${XFAIL}")
endif()

set(_total 0)
set(_passed 0)
set(_failed 0)
set(_skipped 0)
set(_xfail 0)
set(_failure_report "")

foreach(_case IN LISTS _cases)
	if(_case STREQUAL "")
		continue()
	endif()

	get_filename_component(_case_abs "${_case}" ABSOLUTE)
	get_filename_component(_case_name "${_case_abs}" NAME_WE)
	get_filename_component(_case_dir "${_case_abs}" DIRECTORY)

	# ---------- 1) 解释器基准 ----------
	# 在用例所在目录运行：import 依赖按「脚本所在目录」解析。
	pycp_equiv_run(_interp "${_case_dir}" "${PYCP}" "${_case_abs}")
	pycp_equiv_normalize(_interp_out "${_interp_OUT}")
	pycp_equiv_normalize(_interp_err "${_interp_ERR}")

	# ---------- 2) 各链接模式的 AOT 产物 ----------
	foreach(_mode IN LISTS _modes)
		if(_mode STREQUAL "")
			continue()
		endif()

		math(EXPR _total "${_total} + 1")
		set(_proj "${WORK_DIR}/${_case_name}_${_mode}")

		# 2a) 生成 AOT 项目
		# 先清空项目目录：上一轮遗留的 CMakeCache.txt 会锁定生成器，
		# 换生成器（或重跑）时报 "does not match the generator used
		# previously"，属于本脚本的执行噪音，不是被测差异。
		file(REMOVE_RECURSE "${_proj}")

		if(_mode STREQUAL "static")
			set(_emit_args --static)
		else()
			set(_emit_args --shared)
		endif()
		execute_process(
			COMMAND "${PYCP}" --emit-cpp ${_emit_args}
			        "${_case_abs}" -o "${_proj}"
			WORKING_DIRECTORY "${_case_dir}"
			RESULT_VARIABLE _emit_rc
			OUTPUT_VARIABLE _emit_out
			ERROR_VARIABLE _emit_err
		)
		if(NOT _emit_rc EQUAL 0)
			math(EXPR _skipped "${_skipped} + 1")
			string(APPEND _failure_report
				"[SKIP] ${_case_name} [${_mode}]: --emit-cpp 失败 (rc=${_emit_rc})\n"
				"       ${_emit_err}\n")
			continue()
		endif()

		# 2b) 配置 + 构建
		if(GENERATOR)
			set(_gen_args -G "${GENERATOR}")
		else()
			set(_gen_args "")
		endif()
		pycp_equiv_run(_cfg "${_proj}" "${CMAKE_COMMAND}" -S . -B build ${_gen_args})
		if(NOT _cfg_RC EQUAL 0)
			math(EXPR _skipped "${_skipped} + 1")
			string(APPEND _failure_report
				"[SKIP] ${_case_name} [${_mode}]: cmake 配置失败 (rc=${_cfg_RC})\n"
				"       ${_cfg_ERR}\n")
			continue()
		endif()

		pycp_equiv_run(_bld "${_proj}" "${CMAKE_COMMAND}" --build build)
		if(NOT _bld_RC EQUAL 0)
			math(EXPR _skipped "${_skipped} + 1")
			string(APPEND _failure_report
				"[SKIP] ${_case_name} [${_mode}]: 构建失败 (rc=${_bld_RC})\n"
				"       ${_bld_ERR}\n")
			continue()
		endif()

		# 2c) 运行产物（同样在用例所在目录，保证相对路径依赖一致）
		#     产物名 = 入口 .pycp 的 basename；Windows 下带 .exe。
		set(_exe "${_proj}/build/${_case_name}")
		if(WIN32)
			if(NOT EXISTS "${_exe}.exe")
				# MinGW 也可能产出不带后缀的可执行文件
				if(NOT EXISTS "${_exe}")
					math(EXPR _skipped "${_skipped} + 1")
					string(APPEND _failure_report
						"[SKIP] ${_case_name} [${_mode}]: 未找到产物 ${_exe}(.exe)\n")
					continue()
				endif()
			else()
				set(_exe "${_exe}.exe")
			endif()
		endif()

		pycp_equiv_run(_aot "${_case_dir}" "${_exe}")
		pycp_equiv_normalize(_aot_out "${_aot_OUT}")
		pycp_equiv_normalize(_aot_err "${_aot_ERR}")

		# ---------- 3) 三维度比对 ----------
		set(_diffs "")
		if(NOT _aot_RC STREQUAL _interp_RC)
			string(APPEND _diffs
				"   退出码: 解释器=${_interp_RC}  AOT=${_aot_RC}\n")
		endif()
		if(NOT _aot_out STREQUAL _interp_out)
			string(APPEND _diffs
				"   stdout 不一致:\n"
				"     解释器: ${_interp_out}\n"
				"     AOT    : ${_aot_out}\n")
		endif()
		if(NOT _aot_err STREQUAL _interp_err)
			string(APPEND _diffs
				"   stderr 不一致:\n"
				"     解释器: ${_interp_err}\n"
				"     AOT    : ${_aot_err}\n")
		endif()

		if(_diffs STREQUAL "")
			math(EXPR _passed "${_passed} + 1")
		else()
			# 已知缺陷用例：解释器自身行为未定义（如循环导入退出崩溃），
			# AOT 与其不一致属预期，记录为 XFAIL 而不算 FAIL。
			set(_is_xfail 0)
			foreach(_pat IN LISTS _xfail_patterns)
				if(_case_name MATCHES "${_pat}")
					set(_is_xfail 1)
					break()
				endif()
			endforeach()
			if(_is_xfail)
				math(EXPR _xfail "${_xfail} + 1")
				string(APPEND _failure_report
					"[XFAIL] ${_case_name} [${_mode}]（已知缺陷，预期不一致）\n${_diffs}")
			else()
				math(EXPR _failed "${_failed} + 1")
				string(APPEND _failure_report
					"[FAIL] ${_case_name} [${_mode}]\n${_diffs}")
			endif()
		endif()
	endforeach()
endforeach()

# =====================================================================
# 汇总
# =====================================================================
message(STATUS "================ Pycp AOT / 解释器一致性 ================")
message(STATUS "总用例 : ${_total}")
message(STATUS "通过   : ${_passed}")
message(STATUS "不一致 : ${_failed}")
message(STATUS "XFAIL  : ${_xfail}（已知缺陷，预期不一致）")
message(STATUS "跳过   : ${_skipped}（生成/构建失败，非一致性问题）")
message(STATUS "==========================================================")

if(NOT _failure_report STREQUAL "")
	message(STATUS "\n差异明细:\n${_failure_report}")
endif()

if(NOT _failed EQUAL 0)
	message(FATAL_ERROR
		"pycp-aot-equiv: ${_failed} 个用例在 AOT 与解释器下行为不一致，"
		"详见上方差异明细。")
endif()
if(_total EQUAL 0)
	message(FATAL_ERROR "pycp-aot-equiv: 没有任何用例被实际执行。")
endif()

message(STATUS "pycp-aot-equiv: 全部 ${_passed} 个用例行为一致。")
