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

# 生成 C++ 源码（默认输出目录 <entry>/ = a_entry/，含 CMakeLists.txt）
"$PYCP" --emit-cpp "$ENTRY" >/dev/null || {
	echo "ERROR: --emit-cpp 失败" >&2
	exit 2
}

pass=0
fail=0

# --- 新增场景 0：生成目录应同时包含 CMakeLists.txt 与全部 .gen.cpp ---
if [[ ! -f a_entry/CMakeLists.txt || ! -f a_entry/__pycp_main.gen.cpp || ! -f a_entry/b_dep.gen.cpp ]]; then
	echo "[FAIL] 生成目录缺少 CMakeLists.txt 或 .gen.cpp 源文件"
	fail=$((fail + 1))
else
	echo "[PASS] 生成目录包含 CMakeLists.txt 与全部 .gen.cpp"
	pass=$((pass + 1))
fi

# 运行时需要可执行文件同级的 stdlib/（io.so 等原生扩展）
cp -r "$DIST/stdlib" ./stdlib

CXXFLAGS="-std=c++17 -I $DIST/include"
LDFLAGS="-L $DIST/lib -lPycpRuntime -Wl,-rpath,$DIST/lib"

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

# --- 新增场景 3：用生成的 CMakeLists.txt 构建并运行 ---
CMAKE_PROJ="$WORK/a_entry"
rm -rf "$CMAKE_PROJ/build"
if cmake -S "$CMAKE_PROJ" -B "$CMAKE_PROJ/build" >/dev/null 2>&1 &&
   cmake --build "$CMAKE_PROJ/build" >/dev/null 2>&1; then
	check "生成的 CMake 项目构建并运行" "a_entry/build/a_entry"
else
	echo "[FAIL] 生成的 CMake 项目构建失败"
	echo "----- cmake configure/build log (tail) -----"
	tail -20 "$CMAKE_PROJ/build/CMakeFiles/CMakeError.log" 2>/dev/null
	fail=$((fail + 1))
fi

# --- 新增场景 4：自包含部署（exe 拷到空目录，携带同级 lib/ 与 stdlib/）---
SELF_DIR="$WORK/self_contained"
rm -rf "$SELF_DIR"
mkdir -p "$SELF_DIR"
cp "$CMAKE_PROJ/build/a_entry" "$SELF_DIR/"
cp -r "$CMAKE_PROJ/build/stdlib" "$SELF_DIR/"
cp -r "$CMAKE_PROJ/build/lib" "$SELF_DIR/"
if ( cd "$SELF_DIR" && ./a_entry ) 2>&1 | grep -q "AOT IMPORT PASS"; then
	echo "[PASS] 自包含部署（空目录 + 同级 lib/stdlib）运行正常"
	pass=$((pass + 1))
else
	echo "[FAIL] 自包含部署运行异常"
	fail=$((fail + 1))
fi

# --- 新增场景 5：-o 自定义目录应把全部文件（含 CMakeLists.txt）落到该目录 ---
CUSTOM="$WORK/custom_out"
rm -rf "$CUSTOM"
"$PYCP" --emit-cpp "$ENTRY" -o "$CUSTOM" >/dev/null 2>&1
if [[ -f "$CUSTOM/CMakeLists.txt" && -f "$CUSTOM/__pycp_main.gen.cpp" && -f "$CUSTOM/b_dep.gen.cpp" ]]; then
	echo "[PASS] -o 自定义目录包含 CMakeLists.txt 与全部 .gen.cpp"
	pass=$((pass + 1))
else
	echo "[FAIL] -o 自定义目录缺少文件（可能散落到默认目录）"
	fail=$((fail + 1))
fi

# 形态组合：从同一份 fixture 生成不同形态的 CMake 项目并构建运行
emit_and_build() {
	local label="$1" exe_rel="$2" opts="$3"
	local out rc
	rm -rf "$WORK/form"
	# shellcheck disable=SC2086
	out="$("$PYCP" --emit-cpp "$ENTRY" $opts -o "$WORK/form" 2>&1)"
	rc=$?
	if [[ $rc -ne 0 ]]; then
		echo "[FAIL] $label（--emit-cpp 失败）"
		echo "$out"
		fail=$((fail + 1))
		return 1
	fi
	if ( cd "$WORK/form" && cmake -S . -B build >/dev/null 2>&1 &&
	     cmake --build build >/dev/null 2>&1 ); then
		:
	else
		echo "[FAIL] $label（cmake 构建失败）"
		fail=$((fail + 1))
		return 1
	fi
	if ( cd "$WORK/form/build" && ./"$exe_rel" ) 2>&1 | grep -q "AOT IMPORT PASS"; then
		echo "[PASS] $label"
		pass=$((pass + 1))
	else
		echo "[FAIL] $label（运行输出未含 AOT IMPORT PASS）"
		fail=$((fail + 1))
	fi
}

# --- 场景 6：--compile-runtime=static 全静态自包含 ---
emit_and_build "全静态自包含（--compile-runtime=static）" "a_entry" \
	"--compile-runtime=static"

# --- 场景 7：--compile-modules=shared 把依赖模块编成动态库 ---
emit_and_build "依赖模块编为动态库（--compile-modules=shared）" "a_entry" \
	"--compile-modules=shared"

# --- 场景 8：runtime=static 与动态模块冲突必须报错（禁止两份运行时）---
rm -rf "$WORK/form_bad"
out="$("$PYCP" --emit-cpp "$ENTRY" --compile-runtime=static \
	--compile-modules=shared -o "$WORK/form_bad" 2>&1)"
rc=$?
if [[ $rc -ne 0 && "$out" == *"静态"* ]]; then
	echo "[PASS] runtime=static + 动态模块组合被拒（两份运行时防护）"
	pass=$((pass + 1))
else
	echo "[FAIL] runtime=static + 动态模块组合未被拒绝（rc=$rc）"
	echo "$out"
	fail=$((fail + 1))
fi

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
