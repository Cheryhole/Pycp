# =====================================================================
# Pycp Flex/Bison 生成物自检脚本
# ---------------------------------------------------------------------
# 由顶层 / frontend 的 CMakeLists.txt 在【构建期】通过
#   ${CMAKE_COMMAND} -DFILE=<generated> [-DEXPECT=<symbol>] -P cmake/PycpCheckGenerated.cmake
# 调用，紧跟在每条 bison / flex 生成命令之后（同一条 add_custom_command 的
# 最后一个 COMMAND），用于在编译器介入前拦下损坏的生成物。
#
# 背景：Windows 常用工具链 win_flex / win_bison（winflexbison）用
# _tempnam + fopen 申请临时文件（TOCTOU），并行运行多个实例时会互相覆盖
# 同一个临时文件，产出“未展开的 m4 骨架”——文件内容里残留
#   M4_YY_PREFIX[[...]]、m4_ifdef([[...]])、[[#ifndef ...]]、#line 0 ...
# 这类模板文本。拿去编译会得到几百行毫无意义的错误，例如：
#   error: '#endif' without '#if'
#   error: stray '#' in program
#   error: 'out_ALREADY_DEFINED' does not name a type
#   error: expected unqualified-id before ']' token
# 且由于产物比 .l/.y 新，不手动删除就不会重新生成，问题会“卡死”。
# 上游已知缺陷：
#   https://github.com/lexxmark/winflexbison/issues/86
#   https://github.com/westes/flex/issues/580
# 因此除在 CMake 层串行化生成外，再加一道自检，把“几百行莫名报错”变成
# 一条可操作的错误。
#
# 参数：
#   FILE    必填，被校验的生成文件（绝对路径）。
#   EXPECT  可选，该文件中必须出现的特征子串（用于识别“前缀错位”——
#           例如 PycpPreprocessorLexer.cpp 里出现的是 Pycp_create_buffer
#           而非 Pycpp_create_buffer，说明两份产物互相串了内容）。
#   LABEL   可选，错误信息中显示的人类可读名字，缺省为 FILE。
#
# 行为：
#   1. FILE 不存在                    -> FATAL_ERROR（生成命令未产出）
#   2. 含 m4 模板残留                 -> 删除该文件后 FATAL_ERROR
#      （删除是必需的：否则下次构建认为产物已最新，不会重跑生成规则）
#   3. 给了 EXPECT 但文件中找不到     -> 删除该文件后 FATAL_ERROR
#
# 说明：本脚本用 file(READ) 一次性读入（生成物约 80KB），开销可忽略。
# =====================================================================

cmake_minimum_required(VERSION 3.15)

# =====================================================================
# 参数校验
# =====================================================================
if(NOT FILE)
	message(FATAL_ERROR "pycp-check-generated: FILE is not set.")
endif()

if(NOT LABEL)
	set(LABEL "${FILE}")
endif()

# =====================================================================
# 1) 文件必须存在
# =====================================================================
if(NOT EXISTS "${FILE}")
	message(FATAL_ERROR
		"pycp-check-generated: ${LABEL}\n"
		"  生成物不存在：${FILE}\n"
		"  bison / flex 未产出该文件，请检查其上方的生成命令输出。")
endif()

# =====================================================================
# 2) 读取内容
# =====================================================================
file(READ "${FILE}" _gen_content)

# =====================================================================
# 3) m4 模板残留检测
# ---------------------------------------------------------------------
# 正常产物中这些标记一定已被 m4 展开，出现即代表生成过程被并发打断。
# =====================================================================
# 注意：不能把裸 "]]" 当标记——bison 生成的注释里就有 YYTABLE[YYPACT[N]]，
# 会误报。只取 "带内容" 的模板片段，正常产物中绝不会出现。
set(_gen_m4_markers
	"M4_YY_PREFIX"      # 未展开的前缀占位符：#define yylex M4_YY_PREFIX[[lex]]
	"m4_ifdef"          # 未展开的条件模板
	"m4_dnl"            # 未展开的注释模板
	"m4_define"         # 未展开的定义
	"[[#ifndef"         # 模板里的预处理片段（正常产物不会出现 [[# 形式）
	"]]M4_YY_PREFIX"    # 模板引用：#ifndef ]]M4_YY_PREFIX[[xxx_ALREADY_DEFINED
)

set(_gen_bad_marker "")
foreach(_marker IN LISTS _gen_m4_markers)
	string(FIND "${_gen_content}" "${_marker}" _gen_pos)
	if(NOT _gen_pos EQUAL -1)
		set(_gen_bad_marker "${_marker}")
		break()
	endif()
endforeach()

if(NOT _gen_bad_marker STREQUAL "")
	file(REMOVE "${FILE}")
	message(FATAL_ERROR
		"pycp-check-generated: ${LABEL}\n"
		"  生成物损坏：检出未展开的 m4 模板残留 \"${_gen_bad_marker}\"。\n"
		"  原因：win_flex / win_bison 用共享临时文件，并行运行多个实例时\n"
		"        会互相覆盖（上游缺陷：lexxmark/winflexbison#86、\n"
		"        westes/flex#580）。\n"
		"  已删除损坏文件：${FILE}\n"
		"  处理：直接重新构建即可（本项目已在 CMake 层把生成步骤串行化）；\n"
		"        若仍有残留，请删除 build/generated 目录后重建，或以 -j1 构建。")
endif()

# =====================================================================
# 4) 特征子串检测（前缀错位 / 产物互串）
# =====================================================================
if(EXPECT)
	string(FIND "${_gen_content}" "${EXPECT}" _gen_expect_pos)
	if(_gen_expect_pos EQUAL -1)
		file(REMOVE "${FILE}")
		message(FATAL_ERROR
			"pycp-check-generated: ${LABEL}\n"
			"  生成物内容异常：未找到应有特征 \"${EXPECT}\"。\n"
			"  通常是两份生成物互相串了内容（win_flex 并发写同一临时文件）。\n"
			"  已删除损坏文件：${FILE}\n"
			"  处理：直接重新构建即可；若仍有残留，请删除 build/generated 后重建。")
	endif()
endif()

# 校验通过：静默返回（构建日志不产生噪音）。
