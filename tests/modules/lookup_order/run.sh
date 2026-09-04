#!/usr/bin/env bash
# =====================================================================
# 模块查找顺序测试
# ---------------------------------------------------------------------
# 在沙箱（build/lookup_order_work，dist 的一份副本）中验证新的「本地优先、
# 源码优先」顺序：
#   静态链接/进程内符号 -> cwd -> 脚本目录 -> exe 目录 stdlib/
# 每层内部：.pycp 源码优先于同名 .so。
# 验证点：
#   1) stdlib/ 下的 .pycp 源码模块可被导入（最后一层）
#   2) 同名模块同时在 stdlib/ 与 cwd 时，cwd（本地）优先
#   3) 同层同名 .so 与 .pycp 时，源码优先
#   4) stdlib/ 下可放与内置扩展同名的 .pycp 源码覆盖内置扩展
#   5) 全部未命中抛 ImportError（附各层诊断）
#
# 沙箱复制自 build/dist，不污染构建产物。
#
# 注意：fixtures/ 下是「沙箱夹具」，须由本脚本复制到沙箱的合适位置后才能
# 运行——例如 shadow_*.pycp 在沙箱中统一改名为 shadow.pycp，native_probe.cpp
# 需先编译为 .so。直接在 fixtures/ 目录内运行其中的入口脚本会因依赖缺失
# 而失败，属预期现象，批量回归时无需关注。
#
# 用法：bash tests/modules/lookup_order/run.sh
# =====================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"
DIST="$REPO_ROOT/build/dist"
FIXTURES="$SCRIPT_DIR/fixtures"
SANDBOX="$REPO_ROOT/build/lookup_order_work"

if [[ ! -x "$PYCP" ]]; then
	echo "ERROR: 找不到可执行文件 $PYCP（请先执行 cmake --build build）" >&2
	exit 2
fi
if [[ ! -d "$DIST/stdlib" || ! -d "$DIST/include" || ! -d "$DIST/lib" ]]; then
	echo "ERROR: $DIST 不完整（请先执行 cmake --build build）" >&2
	exit 2
fi

# 重建沙箱：一份完整的 dist 副本
rm -rf "$SANDBOX"
mkdir -p "$SANDBOX"
cp -r "$DIST"/. "$SANDBOX"/

pass=0
fail=0

# 在沙箱中运行入口脚本，校验退出码与输出标记（期望成功）
run_case() {
	local label="$1" expect="$2" script="$3"
	local out rc
	out="$(cd "$SANDBOX" && ./pycp "$script" 2>&1)"
	rc=$?
	if [[ $rc -eq 0 && "$out" == *"$expect"* ]]; then
		echo "[PASS] $label"
		pass=$((pass + 1))
	else
		echo "[FAIL] $label (exit=$rc)"
		echo "----- output -----"
		echo "$out"
		echo "------------------"
		fail=$((fail + 1))
	fi
}

# 在沙箱中运行入口脚本，校验「非零退出且输出含指定片段」
run_case_fail() {
	local label="$1" expect="$2" script="$3"
	local out rc
	out="$(cd "$SANDBOX" && ./pycp "$script" 2>&1)"
	rc=$?
	if [[ $rc -ne 0 && "$out" == *"$expect"* ]]; then
		echo "[PASS] $label"
		pass=$((pass + 1))
	else
		echo "[FAIL] $label (exit=$rc)"
		echo "----- output -----"
		echo "$out"
		echo "------------------"
		fail=$((fail + 1))
	fi
}

# --- 用例 1：exe 目录 stdlib/ 下的 .pycp 源码模块（第 4 层）---
cp "$FIXTURES/stdlib_src.pycp"     "$SANDBOX/stdlib/stdlib_src.pycp"
cp "$FIXTURES/use_stdlib_src.pycp" "$SANDBOX/"
run_case "第 4 层：exe 目录 stdlib/ 下的 .pycp 源码模块" \
	"LOOKUP PASS: stdlib pycp source" "use_stdlib_src.pycp"

# --- 用例 2：同名模块同时在 stdlib/ 与 cwd，验证 cwd（本地）优先 ---
cp "$FIXTURES/shadow_stdlib.pycp" "$SANDBOX/stdlib/shadow.pycp"
cp "$FIXTURES/shadow_cwd.pycp"    "$SANDBOX/shadow.pycp"
cp "$FIXTURES/shadow_main.pycp"   "$SANDBOX/"
run_case "cwd（本地）优先于 exe 目录 stdlib/" \
	"LOOKUP PASS: cwd over stdlib" "shadow_main.pycp"

# --- 用例 3：同层同名 .pycp 与 .so，验证源码优先 ---
g++ -std=c++17 -fPIC -shared -I "$DIST/include" \
	"$FIXTURES/native_probe.cpp" \
	-L "$DIST/lib" -lPycpRuntime -Wl,-rpath,"$DIST/lib" \
	-o "$SANDBOX/native_probe.so" 2>&1 | head -10
cp "$FIXTURES/native_probe.pycp" "$SANDBOX/native_probe.pycp"
cp "$FIXTURES/probe_main.pycp"   "$SANDBOX/"
run_case "同层 .pycp 源码优先于同名动态库" \
	"LOOKUP PASS: pycp over so" "probe_main.pycp"

# --- 用例 4：stdlib/ 下同名源码覆盖内置扩展（io）---
# 沙箱 stdlib/ 已含内置 io.so；再放一份 io.pycp 源码，应被优先加载。
cp "$FIXTURES/io_override.pycp"   "$SANDBOX/stdlib/io.pycp"
cp "$FIXTURES/override_main.pycp" "$SANDBOX/"
if ( cd "$SANDBOX" && ./pycp override_main.pycp ) >/dev/null 2>&1; then
	echo "[PASS] stdlib/ 下同名 .pycp 源码覆盖内置扩展（io）"
	pass=$((pass + 1))
else
	echo "[FAIL] stdlib/ 下同名 .pycp 源码覆盖内置扩展（io）：未能命中源码版"
	fail=$((fail + 1))
fi

# --- 用例 5：全部未命中 -> ImportError（附各层诊断）---
cp "$FIXTURES/miss_main.pycp" "$SANDBOX/"
run_case_fail "全未命中抛 ImportError 并附诊断" \
	"No module named 'totally_missing'" "miss_main.pycp"

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
