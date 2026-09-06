#!/usr/bin/env bash
# =====================================================================
# 函数参数默认值测试（本期第一阶段）
#   1) 顶层函数 / 匿名函数默认值（含任意表达式、定义期求值一次）
#   2) 非法：默认值形参后不得再接必填普通形参（编译期报错）
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

# --- 3) 非法：默认值后接必填普通形参 → 编译期报错 ---
out="$(cd "$SCRIPT_DIR" && "$PYCP" illegal_default.pycp 2>&1)"
rc=$?
if [[ $rc -ne 0 && "$out" == *"non-default argument follows default argument"* ]]; then
	echo "[PASS] 默认值后接必填形参被拒（编译期报错）"
	pass=$((pass + 1))
else
	echo "[FAIL] 非法顺序未被拒绝（exit=$rc）"
	echo "----- output -----"; echo "$out"; echo "------------------"
	fail=$((fail + 1))
fi

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
