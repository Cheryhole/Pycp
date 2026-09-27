#!/usr/bin/env bash
# json 模块测试套件批量执行脚本
# 用法：bash tests/json/run.sh
# 正常用例：exit=0 且输出含 "JSON TEST PASS"。
# 名称含 "err_" 的用例为预期失败（抛异常、exit!=0）。
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"

if [[ ! -x "$PYCP" ]]; then
	echo "ERROR: 找不到可执行 pycp: $PYCP (请先执行 cmake --build build)" >&2
	exit 2
fi

pass=0
fail=0
expected_fail_ok=0

shopt -s nullglob
for f in "$SCRIPT_DIR"/*.pycp; do
	base="$(basename "$f")"
	[[ "$base" == run.sh ]] && continue

	out="$(cd "$SCRIPT_DIR" && "$PYCP" "$base" 2>&1)"
	rc=$?

	is_expected_fail=0
	if [[ "$base" == *"err_"* ]]; then
		is_expected_fail=1
	fi

	if [[ $is_expected_fail -eq 1 ]]; then
		if [[ $rc -ne 0 ]]; then
			echo "[EXPECTED-FAIL] $base  (exit=$rc, 符合预期的异常退出)"
			expected_fail_ok=$((expected_fail_ok+1))
		else
			echo "[FAIL] $base  预期非零退出但 exit=0"
			echo "----- output -----"
			echo "$out"
			echo "------------------"
			fail=$((fail+1))
		fi
		continue
	fi

	if [[ $rc -eq 0 && "$out" == *"JSON TEST PASS"* ]]; then
		echo "[PASS] $base"
		pass=$((pass+1))
	else
		echo "[FAIL] $base  (exit=$rc)"
		echo "----- output -----"
		echo "$out"
		echo "------------------"
		fail=$((fail+1))
	fi
done

echo "==================================="
echo "PASS=$pass  EXPECTED_FAIL_OK=$expected_fail_ok  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
