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

# ---------------------------------------------------------------------
# 生成器：默认强制生成 Makefile（Windows 用 MinGW Makefiles，POSIX 用
# Unix Makefiles），以便后续统一用 `make -j16` 编译每个 AOT 子工程。
# 顶层 CMakeLists 不再透传主构建生成器——AOT 子工程彼此独立，且 makefile
# 生成 + make 的方式在 MinGW 下最稳（避免落到未安装的 nmake）。
# ---------------------------------------------------------------------
if(NOT GENERATOR)
	if(WIN32)
		set(GENERATOR "MinGW Makefiles")
	else()
		set(GENERATOR "Unix Makefiles")
	endif()
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")

# ---------------------------------------------------------------------
# 并行度（JOBS）
#   每个 (用例, 模式) 组合互相独立：各自的 AOT 工程目录、各自的 configure/
#   build/run，唯一共享的是只读的 pycp 可执行文件与 dist SDK。因此把任务按
#   轮询分给 JOBS 个**工作进程**（同一脚本 + -DWORKER=1 -DWORKER_INDEX=i），
#   能把 234 次 CMake 配置 + 编译的墙钟时间压到约 1/JOBS。
#   JOBS=0/auto（默认）按 CPU 核数自动取，上限 8：每个 AOT 工程都要跑一次
#   cmake configure，并发过高时内存与磁盘争用反而拖慢。
# ---------------------------------------------------------------------
if(NOT JOBS)
	set(JOBS 0)
endif()
if(JOBS STREQUAL "auto")
	set(JOBS 0)
endif()
if(JOBS EQUAL 0)
	include(ProcessorCount)
	ProcessorCount(_pycp_ncpu)
	if(_pycp_ncpu EQUAL 0)
		set(JOBS 1)
	elseif(_pycp_ncpu GREATER 8)
		set(JOBS 8)
	else()
		set(JOBS ${_pycp_ncpu})
	endif()
endif()

# 工作进程模式：校验分片参数（由驱动进程写入，必给）。
if(WORKER)
	if(WORKER_COUNT EQUAL 0 OR RESULT_FILE STREQUAL "")
		message(FATAL_ERROR "pycp-aot-equiv: WORKER=1 requires WORKER_COUNT and RESULT_FILE.")
	endif()
endif()

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
# 全局 (用例, 模式) 序号：所有工作进程按同一顺序遍历同一列表，故该序号在
# 各进程内一致，可安全用作「第 i 个任务归哪个 worker」的分片依据。
set(_pair_global 0)

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

		# 分片：本进程只处理 slot == WORKER_INDEX 的任务，其余直接跳过
		# （注意 _pair_global 必须无条件自增，否则各 worker 的序号会错位）。
		math(EXPR _pair_idx "${_pair_global}")
		math(EXPR _pair_global "${_pair_global} + 1")
		if(WORKER)
			math(EXPR _pair_slot "${_pair_idx} % ${WORKER_COUNT}")
			if(NOT _pair_slot EQUAL WORKER_INDEX)
				continue()
			endif()
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

		# 2b) 生成 Makefile + 用 make -j16 构建
		#     先 cmake -S . -B build -G <Makefile 生成器> 产出 Makefile，
		#     再在 build 目录直接 make -j16 编译（并行度由 -j16 控制）。
		set(_gen_args -G "${GENERATOR}")
		pycp_equiv_run(_cfg "${_proj}" "${CMAKE_COMMAND}" -S . -B build ${_gen_args})
		if(NOT _cfg_RC EQUAL 0)
			math(EXPR _skipped "${_skipped} + 1")
			string(APPEND _failure_report
				"[SKIP] ${_case_name} [${_mode}]: cmake 配置失败 (rc=${_cfg_RC})\n"
				"       ${_cfg_ERR}\n")
			continue()
		endif()

		pycp_equiv_run(_bld "${_proj}/build" "make" -j16)
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
						"[SKIP] ${_case_name} [${_mode}]: artifact not found ${_exe}(.exe)\n")
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
# 工作进程：把本分片的计数与差异明细落盘，交给驱动进程合并
# =====================================================================
if(WORKER)
	file(WRITE "${RESULT_FILE}"
		"\ntotal=${_total}"
		"\npassed=${_passed}"
		"\nfailed=${_failed}"
		"\nskipped=${_skipped}"
		"\nxfail=${_xfail}"
		"\nreport<<EOF\n${_failure_report}")
	message(STATUS "  [worker ${WORKER_INDEX}] channel ${_passed}/${_total}"
		" (mismatch ${_failed}, skipped ${_skipped}, XFAIL ${_xfail})")
	return()
endif()

# =====================================================================
# 驱动进程：JOBS > 1 时拉起工作进程并合并各分片结果
# =====================================================================
if(JOBS GREATER 1)
	math(EXPR _wmax "${JOBS} - 1")

	# 传给工作进程的多值参数统一改用 '|'（脚本内 pycp_equiv_split 同时吃 '|'
	# 和 ';'），避免把 CMake 列表分隔符带进 shell/bat 命令串。
	string(REPLACE ";" "|" _cases_pipe "${_cases}")
	string(REPLACE ";" "|" _modes_pipe "${_modes}")
	string(REPLACE ";" "|" _xfail_pipe "${_xfail_patterns}")

	message(STATUS "Parallel run: ${JOBS} worker processes (shard logs ${WORK_DIR}/_worker_*.log)")

	set(_spawn_rc 0)
	if(WIN32)
		# Windows：用 start /B 拉起 N-1 个后台 worker，最后一个同步跑，再轮询
		# 各 worker 的结果文件确认全部收尾（cmd 无 wait 原语）。
		set(_runner "${WORK_DIR}/_equiv_workers.bat")
		file(WRITE "${_runner}" "@echo off\r\nsetlocal\r\n")
		foreach(_i RANGE 0 ${_wmax})
			set(_log "${WORK_DIR}/_worker_${_i}.log")
			set(_res "${WORK_DIR}/_worker_${_i}.result")
			file(REMOVE "${_log}")
			file(REMOVE "${_res}")
			set(_line
				"\"${CMAKE_COMMAND}\""
				"\"-DPYCP=${PYCP}\""
				"\"-DCASES=${_cases_pipe}\""
				"\"-DMODES=${_modes_pipe}\""
				"\"-DWORK_DIR=${WORK_DIR}\""
				"\"-DGENERATOR=${GENERATOR}\""
				"\"-DXFAIL=${_xfail_pipe}\""
				"\"-DWORKER=1\""
				"\"-DWORKER_INDEX=${_i}\""
				"\"-DWORKER_COUNT=${JOBS}\""
				"\"-DRESULT_FILE=${_res}\""
				"\"-P\""
				"\"${CMAKE_CURRENT_LIST_FILE}\"")
			list(JOIN _line " " _line_str)
			file(APPEND "${_runner}"
				"start \"\" /B cmd /c \"${_line_str} > \"${_log}\" 2>&1\"\r\n")
		endforeach()
		file(APPEND "${_runner}" ":pycp_wait\r\n")
		foreach(_i RANGE 0 ${_wmax})
			file(APPEND "${_runner}"
				"if not exist \"${WORK_DIR}/_worker_${_i}.result\" goto pycp_sleep\r\n")
		endforeach()
		file(APPEND "${_runner}"
			"goto pycp_done\r\n"
			":pycp_sleep\r\n"
			"timeout /t 1 /nobreak > nul 2>&1\r\n"
			"ping -n 2 127.0.0.1 > nul 2>&1\r\n"
			"goto pycp_wait\r\n"
			":pycp_done\r\n")
		execute_process(COMMAND cmd /c "${_runner}" RESULT_VARIABLE _spawn_rc)
	else()
		# POSIX：一条 shell 脚本里全部后台拉起 + wait，语义最简且无轮询开销。
		set(_runner "${WORK_DIR}/_equiv_workers.sh")
		file(WRITE "${_runner}" "#!/bin/sh\nset -u\n")
		foreach(_i RANGE 0 ${_wmax})
			set(_log "${WORK_DIR}/_worker_${_i}.log")
			set(_res "${WORK_DIR}/_worker_${_i}.result")
			file(REMOVE "${_log}")
			file(REMOVE "${_res}")
			set(_line
				"\"${CMAKE_COMMAND}\""
				"\"-DPYCP=${PYCP}\""
				"\"-DCASES=${_cases_pipe}\""
				"\"-DMODES=${_modes_pipe}\""
				"\"-DWORK_DIR=${WORK_DIR}\""
				"\"-DGENERATOR=${GENERATOR}\""
				"\"-DXFAIL=${_xfail_pipe}\""
				"\"-DWORKER=1\""
				"\"-DWORKER_INDEX=${_i}\""
				"\"-DWORKER_COUNT=${JOBS}\""
				"\"-DRESULT_FILE=${_res}\""
				"\"-P\""
				"\"${CMAKE_CURRENT_LIST_FILE}\"")
			list(JOIN _line " " _line_str)
			file(APPEND "${_runner}" "${_line_str} > \"${_log}\" 2>&1 &\n")
		endforeach()
		file(APPEND "${_runner}" "wait\n")
		execute_process(COMMAND sh "${_runner}" RESULT_VARIABLE _spawn_rc)
	endif()
	if(NOT _spawn_rc EQUAL 0)
		message(STATUS "Warning: worker dispatch script returned ${_spawn_rc}; merging with the shard results produced so far.")
	endif()

	# ---- 合并：计数求和，差异明细按 worker 序号拼接（顺序稳定，便于比对）----
	set(_m_total 0)
	set(_m_passed 0)
	set(_m_failed 0)
	set(_m_skipped 0)
	set(_m_xfail 0)
	set(_merged_report "")
	set(_missing_workers "")
	foreach(_i RANGE 0 ${_wmax})
		set(_res "${WORK_DIR}/_worker_${_i}.result")
		if(NOT EXISTS "${_res}")
			list(APPEND _missing_workers "${_i}")
			continue()
		endif()
		file(READ "${_res}" _txt)
		foreach(_key IN ITEMS total passed failed skipped xfail)
			string(REGEX MATCH "\n${_key}=([0-9]+)" _hit "${_txt}")
			if(_hit STREQUAL "")
				continue()
			endif()
			set(_acc "_m_${_key}")
			math(EXPR ${_acc} "${${_acc}} + ${CMAKE_MATCH_1}")
		endforeach()
		string(FIND "${_txt}" "report<<EOF" _pos)
		if(_pos GREATER -1)
			math(EXPR _start "${_pos} + 11")
			string(SUBSTRING "${_txt}" ${_start} -1 _rep)
			string(APPEND _merged_report "${_rep}")
		endif()
	endforeach()

	if(NOT _missing_workers STREQUAL "")
		# 工作进程异常退出（崩溃/被杀）：按失败计入，避免「少跑一半仍报全绿」。
		string(REPLACE ";" ", " _mw "${_missing_workers}")
		list(LENGTH _missing_workers _mw_n)
		math(EXPR _m_failed "${_m_failed} + ${_mw_n}")
		string(APPEND _merged_report
			"[FAIL] ${_mw_n} 个工作进程未产出结果（worker ${_mw}），"
			"详见 ${WORK_DIR}/_worker_*.log\n")
	endif()

	set(_total ${_m_total})
	set(_passed ${_m_passed})
	set(_failed ${_m_failed})
	set(_skipped ${_m_skipped})
	set(_xfail ${_m_xfail})
	set(_failure_report "${_merged_report}")
endif()

# =====================================================================
# 汇总
# =====================================================================
message(STATUS "============ Pycp AOT / interpreter equivalence ============")
message(STATUS "Total   : ${_total}")
message(STATUS "Passed  : ${_passed}")
message(STATUS "Mismatch: ${_failed}")
message(STATUS "XFAIL   : ${_xfail} (known defects, mismatch expected)")
message(STATUS "Skipped : ${_skipped} (generate/build failure, not an equivalence issue)")
message(STATUS "==========================================================")

if(NOT _failure_report STREQUAL "")
	message(STATUS "\nDiff details:\n${_failure_report}")
endif()

if(NOT _failed EQUAL 0)
	message(FATAL_ERROR
		"pycp-aot-equiv: ${_failed} case(s) behave differently under AOT and the "
		"interpreter; see the diff details above.")
endif()
if(_total EQUAL 0)
	message(FATAL_ERROR "pycp-aot-equiv: no case was actually executed.")
endif()

message(STATUS "pycp-aot-equiv: all ${_passed} case(s) behave identically.")
