#!/usr/bin/env bash
# =====================================================================
# AOT 导入测试
# ---------------------------------------------------------------------
# 验证 --emit-cpp 生成的多个 .gen.cpp 在两种链接方式下都能正常 import：
#   1) 所有 .gen.cpp 一起直接链接
#   2) 依赖模块先编为静态库（.a），再与入口链接（不加 --whole-archive）
#
# 场景 2 是最容易静默失败的一种：入口翻译单元若不显式引用
# PycpModule_<dep>，链接器会把 dep.gen.o 从静态库中整体丢弃，其静态
# 初始化器无从执行，表现为「编译链接全部成功，运行时却报 ImportError」。
# 生成代码中的「链接拉入桩」正是为解决此问题。
#
# 用法：bash tests/aot_import/run.sh
# =====================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"
DIST="$REPO_ROOT/build/dist"
WORK="$REPO_ROOT/build/aot_import_work"
ENTRY="$SCRIPT_DIR/a_entry.pycp"

if [[ ! -x "$PYCP" ]]; then
	echo "ERROR: 找不到可执行文件 $PYCP（请先执行 cmake --build build）" >&2
	exit 2
fi
if [[ ! -d "$DIST/include" || ! -d "$DIST/lib" ]]; then
	echo "ERROR: 找不到 $DIST 下的 include/ 或 lib/（请先执行 cmake --build build）" >&2
	exit 2
fi

rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK" || exit 2

# 生成 C++ 源码（输出目录 a_entry/ 建立在当前工作目录）
"$PYCP" --emit-cpp "$ENTRY" >/dev/null || {
	echo "ERROR: --emit-cpp 失败" >&2
	exit 2
}

# 运行时需要可执行文件同级的 stdlib/（io.so 等原生扩展）
cp -r "$DIST/stdlib" ./stdlib

CXXFLAGS="-std=c++17 -I $DIST/include"
LDFLAGS="-L $DIST/lib -lPycpRuntime -Wl,-rpath,$DIST/lib"

pass=0
fail=0

# 运行指定可执行文件，校验退出码与输出标记
check() {
	local label="$1" exe="$2"
	if [[ ! -x "./$exe" ]]; then
		echo "[FAIL] $label（可执行文件未生成）"
		fail=$((fail + 1))
		return
	fi
	local out rc
	out="$(./"$exe" 2>&1)"
	rc=$?
	if [[ $rc -eq 0 && "$out" == *"AOT IMPORT PASS"* ]]; then
		echo "[PASS] $label"
		pass=$((pass + 1))
		return
	fi
	echo "[FAIL] $label (exit=$rc)"
	echo "----- output -----"
	echo "$out"
	echo "------------------"
	fail=$((fail + 1))
}

# --- 场景 1：两个 .gen.cpp 一起直接链接 ---
g++ $CXXFLAGS a_entry/__pycp_main.gen.cpp a_entry/b_dep.gen.cpp \
	$LDFLAGS -o aot_direct 2>&1 | head -5
check "多个 .gen.cpp 直接链接后 import" "aot_direct"

# --- 场景 2：依赖模块编为静态库再链接（不加 --whole-archive）---
g++ $CXXFLAGS -c a_entry/b_dep.gen.cpp -o b_dep.o 2>&1 | head -5 &&
	ar rcs libb_dep.a b_dep.o
g++ $CXXFLAGS a_entry/__pycp_main.gen.cpp -L. -lb_dep \
	$LDFLAGS -o aot_static 2>&1 | head -5
check "依赖编为静态库后 import（无 --whole-archive）" "aot_static"

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
