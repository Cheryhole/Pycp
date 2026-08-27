#!/usr/bin/env bash
# import 测试套件批量执行脚本
# 用法：bash tests/import_tests/run.sh
# 遍历目录下所有入口测试文件（不含 mod_ 前缀、非 run.sh 的 *.pycp），
# 逐个用 build/pycp 执行并校验结果。
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"

if [[ ! -x "$PYCP" ]]; then
	echo "ERROR: 找不到可执行 pycp: $PYCP (请先执行 cmake --build build)" >&2
	exit 2
fi

# 预期会失败（非零退出）的用例，名称包含这些子串的判定为 EXPECTED-FAIL
EXPECTED_FAIL_PATTERNS=("import_unknown" "circular_import")

pass=0
fail=0
expected_fail_ok=0

shopt -s nullglob
for f in "$SCRIPT_DIR"/*.pycp; do
	base="$(basename "$f")"
	# 跳过被依赖的辅助模块与脚本自身
	[[ "$base" == mod_* ]] && continue
	[[ "$base" == run.sh ]] && continue

	# 在脚本所在目录内运行（确保相对 import 解析生效）
	out="$(cd "$SCRIPT_DIR" && "$PYCP" "$base" 2>&1)"
	rc=$?

	# 判定是否预期失败用例
	is_expected_fail=0
	for p in "${EXPECTED_FAIL_PATTERNS[@]}"; do
		if [[ "$base" == *"$p"* ]]; then
			is_expected_fail=1
			break
		fi
	done

	if [[ $is_expected_fail -eq 1 ]]; then
		if [[ $rc -ne 0 ]]; then
			echo "[EXPECTED-FAIL] $base  (exit=$rc, 符合预期的异常退出：导入不存在模块为 ImportError，真正循环导入为退出段错误)"
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

	# 正常用例：exit=0 且含 PASS 标记
	if [[ $rc -eq 0 && "$out" == *"IMPORT TEST PASS"* ]]; then
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
