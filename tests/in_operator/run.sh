#!/usr/bin/env bash
# =====================================================================
# `in` 运算符 / __contains__ 测试套件
# ---------------------------------------------------------------------
# 覆盖：
#   1) List：存在 / 不存在 / 空 / 重复 / 嵌套(身份) / 类型不匹配(不报错)
#   2) FixedList：存在 / 不存在 / 空
#   3) Map.keys() / Map.values()：命中 / 未命中（裸 Map 不支持，见错误路径）
#   4) String：子串命中 / 未命中 / 空子串
#   5) 自定义 class 定义 __contains__：`in` 通用分派到类方法
#   6) 迭代回退：类仅实现 __iterator__（无 __contains__）时遍历比较
#   7) 错误路径：x in 5 / x in <裸 Map> → TypeError
#   8) AOT / 解释器一致性（正例，shared + static）
#
# 用法：bash tests/in_operator/run.sh
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

# 正向：退出码为 0 且输出包含全部期望片段。
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

# 失败：退出码非零，且输出包含全部期望片段（TypeError 等）。
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

echo "== 1) List / FixedList / Map 视图 / String 成员测试 =="
check_ok "List/FixedList/Map.keys/Map.values/String 成员测试" contains.pycp \
	"list_hit True" "list_miss False" "list_empty False" "list_dup True" \
	"list_nested False" "list_typemismatch False" "list_last True" \
	"fixed_hit True" "fixed_miss False" "fixed_empty False" \
	"keys_hit True" "keys_miss False" "values_hit True" "values_miss False" \
	"str_hit True" "str_miss False" "str_empty_sub True" \
	"CONTAINS PASS"

echo "== 2) 自定义类 __contains__ 通用分派 =="
check_ok "自定义 class 定义 __contains__ 被 in 调用" user_contains.pycp \
	"user_hit True" "user_miss False" "USER PASS"

echo "== 3) 迭代回退（仅 __iterator__，无 __contains__） =="
check_ok "无 __contains__ 但有 __iterator__ → 遍历比较" iter_fallback.pycp \
	"fallback_hit True" "fallback_miss False" "FALLBACK PASS"

echo "== 4) 错误路径 =="
check_reject "x in 5（不可迭代且无 __contains__）→ TypeError" err_not_iterable.pycp \
	"TypeError" "not iterable"
check_reject "x in <裸 Map> 不支持 → TypeError" err_bare_map.pycp \
	"TypeError" "not iterable"

echo "== 5) AOT / 解释器一致性 =="
if [[ -d "$DIST/include" && -d "$DIST/lib" ]]; then
	cases="$SCRIPT_DIR/contains.pycp|$SCRIPT_DIR/user_contains.pycp|$SCRIPT_DIR/iter_fallback.pycp"
	if cmake -DPYCP="$PYCP" -DCASES="$cases" -DMODES="shared;static" \
		-DWORK_DIR="$REPO_ROOT/build/in_operator_equiv" \
		-P "$REPO_ROOT/cmake/PycpAotEquivalence.cmake" >/dev/null 2>&1; then
		echo "[PASS] AOT 一致性：in 正例（shared + static）"
		pass=$((pass + 1))
	else
		echo "[FAIL] AOT 一致性：in 正例（重跑并显示差异）"
		cmake -DPYCP="$PYCP" -DCASES="$cases" -DMODES="shared;static" \
			-DWORK_DIR="$REPO_ROOT/build/in_operator_equiv" \
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
