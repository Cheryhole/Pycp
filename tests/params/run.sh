#!/usr/bin/env bash
# =====================================================================
# 函数参数体系测试
#   第一阶段：默认值
#     1) 顶层函数 / 匿名函数默认值（含任意表达式、定义期求值一次）
#     2) 非法：默认值形参后不得再接必填普通形参（编译期报错）
#   第二阶段：Python 对齐的完整形参/实参形态
#     3) 可变位置参数 *args（收集为不可变元组）、关键字-only、裸 *、**kwargs
#     4) 调用侧关键字实参 f(x=1)；运行期/解析期报错文案与 CPython 一致
#
# 用法：bash tests/params/run.sh
# =====================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"
# Windows/MinGW 下可执行文件带 .exe 后缀；Linux 下为无后缀的 build/pycp。
[[ -x "$PYCP" ]] || PYCP="$REPO_ROOT/build/pycp.exe"

if [[ ! -x "$PYCP" ]]; then
	echo "ERROR: 找不到可执行文件 $PYCP（请先执行 cmake --build build）" >&2
	exit 2
fi

pass=0
fail=0

# --- 1) 顶层/匿名函数默认值 ---
out="$(cd "$SCRIPT_DIR" && "$PYCP" default_args.pycp 2>&1)"
rc=$?
if [[ $rc -eq 0 && "$out" == *"DEFAULT ARGS PASS"* && \
      "$out" == *"g y=3"* ]]; then
	echo "[PASS] 顶层/匿名函数默认值（位置省略触发）"
	pass=$((pass + 1))
else
	echo "[FAIL] 默认值正向用例（exit=$rc）"
	echo "----- output -----"; echo "$out"; echo "------------------"
	fail=$((fail + 1))
fi

# --- 2) 类方法默认值（self 之后的形参带默认值）---
out="$(cd "$SCRIPT_DIR" && "$PYCP" method_default_args.pycp 2>&1)"
rc=$?
if [[ $rc -eq 0 && "$out" == *"METHOD DEFAULT PASS"* ]]; then
	echo "[PASS] 类方法默认值（含临时实例/变量持有/部分省略）"
	pass=$((pass + 1))
else
	echo "[FAIL] 类方法默认值用例（exit=$rc）"
	echo "----- output -----"; echo "$out"; echo "------------------"
	fail=$((fail + 1))
fi

# --- 3) 非法：默认值后接必填普通形参 → 解析期报 SyntaxError ---
out="$(cd "$SCRIPT_DIR" && "$PYCP" illegal_default.pycp 2>&1)"
rc=$?
if [[ $rc -ne 0 && "$out" == *"SyntaxError: parameter without a default follows parameter with a default"* && \
      "$out" == *"File "*", line "* ]]; then
	echo "[PASS] 默认值后接必填形参被拒（解析期 SyntaxError，带 File/line）"
	pass=$((pass + 1))
else
	echo "[FAIL] 非法顺序未被正确拒绝（exit=$rc）"
	echo "----- output -----"; echo "$out"; echo "------------------"
	fail=$((fail + 1))
fi

# --- 4) *args / **kwargs / 关键字-only / 裸 *（第二阶段新增）---
# 正常路径：退出码为 0 且输出包含全部期望片段。
check_ok() {
	local name="$1" script="$2"
	shift 2
	local out rc ok=1 e
	out="$(cd "$SCRIPT_DIR" && "$PYCP" "$script" 2>&1)"
	rc=$?
	[[ $rc -eq 0 ]] || ok=0
	for e in "$@"; do
		[[ "$out" == *"$e"* ]] || ok=0
	done
	if [[ $ok -eq 1 ]]; then
		echo "[PASS] $name"
		pass=$((pass + 1))
	else
		echo "[FAIL] $name（exit=$rc）"
		echo "----- output -----"; echo "$out"; echo "------------------"
		fail=$((fail + 1))
	fi
}

# 失败路径：退出码非零，且输出包含全部期望片段（运行期 TypeError / 解析期 SyntaxError 共用）。
check_reject() {
	local name="$1" script="$2"
	shift 2
	local out rc ok=1 e
	out="$(cd "$SCRIPT_DIR" && "$PYCP" "$script" 2>&1)"
	rc=$?
	[[ $rc -ne 0 ]] || ok=0
	# shellcheck disable=SC2068
	for e in "$@"; do
		[[ "$out" == *"$e"* ]] || ok=0
	done
	if [[ $ok -eq 1 ]]; then
		echo "[PASS] $name"
		pass=$((pass + 1))
	else
		echo "[FAIL] $name（exit=$rc，期望被拒）"
		echo "----- output -----"; echo "$out"; echo "------------------"
		fail=$((fail + 1))
	fi
}

check_ok "*args 收集（零个 -> Fixed[]；与默认值形参共存）" varargs.pycp \
	"VARARGS PASS" "f 1 2 Fixed[3, 4]" "g Fixed[1, 2]"
check_ok "关键字-only 形参（*args 之后必须按关键字传）" keyword_only.pycp \
	"KEYWORD ONLY PASS" "f 1 Fixed[] 2 7" "f 1 Fixed[2, 3] 4 5"
check_ok "裸 * 分隔符（其后均为关键字-only）" bare_star.pycp \
	"BARE STAR PASS" "f 1 2 9" "f 1 2 3"
check_ok "**kwargs 收集（按名匹配形参后剩余入 Map）" kwargs.pycp \
	"KWARGS PASS" 'f 1 Fixed[] 1 {}' 'f 1 Fixed[2,] 3 {"x": 4}'

check_reject "缺必填关键字-only -> TypeError" err_missing_kwonly.pycp \
	"TypeError: f() missing 1 required keyword-only argument: 'kw'" \
	'File "err_missing_kwonly.pycp"' "line 5"
check_reject "位置实参过多 -> TypeError" err_too_many_positional.pycp \
	"TypeError: f() takes 2 positional arguments but 3 were given"
check_reject "未知关键字实参 -> TypeError" err_unexpected_keyword.pycp \
	"TypeError: f() got an unexpected keyword argument 'x'"
check_reject "同一形参重复赋值 -> TypeError" err_multiple_values.pycp \
	"TypeError: f() got multiple values for argument 'a'"
check_reject "裸 * 后无关键字-only -> SyntaxError" illegal_params.pycp \
	"SyntaxError: named arguments must follow bare *"
check_reject "**kwargs 非最后形参 -> SyntaxError" illegal_kwargs.pycp \
	"SyntaxError: arguments cannot follow var-keyword argument"
check_reject "调用侧位置实参在关键字之后 -> SyntaxError" illegal_call.pycp \
	"SyntaxError: positional argument follows keyword argument"

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
