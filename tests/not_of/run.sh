#!/usr/bin/env bash
# =====================================================================
# `not` / `!` / `of` 测试套件
# ---------------------------------------------------------------------
# 覆盖：
#   1) `not` / `!` 真值（True/False/0/1/None/空串/非空串/空列表/0.0）
#   2) `!` 为 not 别名；`!!x` / `not not x` 链式
#   3) Python 风格优先级：`not 2 == 1` → True（not 低于比较）
#   4) `of` 反向成员访问：`a of b` ≡ `b.a`；右结合 `a of b of c` ≡ (c.b).a；
#      右侧后缀链（下标 / 调用）；与 not 组合
#   5) `a not in b` → 语法错误（未生成 not in）
#   6) AOT / 解释器一致性（正例，shared + static）
#
# 用法：bash tests/not_of/run.sh
# =====================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"
DIST="$REPO_ROOT/build/dist"
[[ -x "$PYCP" ]] || PYCP="$REPO_ROOT/build/pycp.exe"

if [[ ! -x "$PYCP" ]]; then
	echo "ERROR: 找不到可执行文件 $PYCP（请先执行 cmake --build build）" >&2
	exit 2
fi

pass=0
fail=0

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

check_reject() {
	local name="$1" script="$2"
	shift 2
	local out rc ok=1 e
	out="$(cd "$SCRIPT_DIR" && "$PYCP" "$script" 2>&1)"
	rc=$?
	[[ $rc -ne 0 ]] || ok=0
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

echo "== 1) not / ! 真值、别名、链式、优先级 =="
check_ok "not/! 真值与 Python 风格优先级" not_bang.pycp \
	"not_true False" "not_false True" "not_zero True" "not_one False" \
	"not_none True" "not_emptystr True" "not_str False" "not_emptylist True" \
	"not_float0 True" \
	"bang_true False" "bang_zero True" "double_bang True" "not_not True" \
	"prec_python True" "prec_bang True" "prec_eq False" \
	"NOT BANG PASS"

echo "== 2) of 反向成员访问 =="
check_ok "of 等价 b.a / 右结合 / 后缀链 / 与 not 组合" of_op.pycp \
	"of_attr 7" "of_eq_dot True" "of_chain 7" "of_index 7" "of_call 99" \
	"not_of True" "OF PASS"

echo "== 3) not in 未实现（语法错误） =="
check_reject "a not in b → 语法错误" err_not_in.pycp \
	"SyntaxError"

echo "== 4) AOT / 解释器一致性 =="
if [[ -d "$DIST/include" && -d "$DIST/lib" ]]; then
	cases="$SCRIPT_DIR/not_bang.pycp|$SCRIPT_DIR/of_op.pycp"
	if cmake -DPYCP="$PYCP" -DCASES="$cases" -DMODES="shared;static" \
		-DWORK_DIR="$REPO_ROOT/build/not_of_equiv" \
		-P "$REPO_ROOT/cmake/PycpAotEquivalence.cmake" >/dev/null 2>&1; then
		echo "[PASS] AOT 一致性：not/!/of 正例（shared + static）"
		pass=$((pass + 1))
	else
		echo "[FAIL] AOT 一致性：not/!/of 正例（重跑并显示差异）"
		cmake -DPYCP="$PYCP" -DCASES="$cases" -DMODES="shared;static" \
			-DWORK_DIR="$REPO_ROOT/build/not_of_equiv" \
			-P "$REPO_ROOT/cmake/PycpAotEquivalence.cmake" 2>&1 | tail -50
		fail=$((fail + 1))
	fi
else
	echo "[SKIP] 缺少 $DIST（先执行 cmake --build build --target pycp-dist）"
fi

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
