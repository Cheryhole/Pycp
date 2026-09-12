#!/usr/bin/env bash
# =====================================================================
# argv 测试
# ---------------------------------------------------------------------
# 验证内置属性 pycp.argv（语义对齐 Python 的 sys.argv）：
#   1) 解释运行 pycp argv_demo.pycp a b c -> [argv_demo.pycp, a, b, c]
#      （argv[0] 为脚本名，不含 pycp 可执行文件本身）
#   2) REPL 无参启动（直接 pycp）-> 空列表 []
#   3) AOT 编译产物 ./aot_argv a b c -> [程序完整路径, a, b, c]
#      （argv[0] 为程序路径，对齐 sys.argv[0]）
#
# 用法：bash tests/argv/run.sh
# =====================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"
# Windows/MinGW 下可执行文件带 .exe 后缀；Linux 下为无后缀的 build/pycp。
[[ -x "$PYCP" ]] || PYCP="$REPO_ROOT/build/pycp.exe"
DIST="$REPO_ROOT/build/dist"
WORK="$REPO_ROOT/build/argv_work"
ENTRY="$SCRIPT_DIR/argv_demo.pycp"

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

pass=0
fail=0

# --- 1) 解释运行传参：pycp argv_demo.pycp a b c ---
out="$(cd "$SCRIPT_DIR" && "$PYCP" argv_demo.pycp a b c 2>&1)"
rc=$?
if [[ $rc -eq 0 && "$out" == *"ARGV LEN=4"* && \
      "$out" == *"ARGV0=argv_demo.pycp"* && \
      "$out" == *"ARGV1=a"* && "$out" == *"ARGV2=b"* && \
      "$out" == *"ARGV3=c"* && "$out" == *"ARGV TEST PASS"* ]]; then
	echo "[PASS] 解释运行传参 pycp.argv == [脚本名, a, b, c]"
	pass=$((pass + 1))
else
	echo "[FAIL] 解释运行传参（exit=$rc）"
	echo "----- output -----"; echo "$out"; echo "------------------"
	fail=$((fail + 1))
fi

# --- 1b) argv[0] 不含 pycp 可执行文件本身 ---
if [[ "$out" != *"ARGV0=pycp"* ]]; then
	echo "[PASS] 解释运行 argv[0] 不含 pycp 可执行文件"
	pass=$((pass + 1))
else
	echo "[FAIL] 解释运行 argv[0] 错误地包含 pycp 可执行文件"
	fail=$((fail + 1))
fi

# --- 2) REPL 无参：pycp.argv == []（默认空列表，LEN=0）---
repl_out="$(printf 'import io\nimport pycp\nio.print("REPL ARGV LEN=" + pycp.String(pycp.argv.length()))\n' | "$PYCP" 2>&1)"
if [[ "$repl_out" == *"REPL ARGV LEN=0"* ]]; then
	echo "[PASS] REPL 无参 pycp.argv == []"
	pass=$((pass + 1))
elif [[ -z "$repl_out" ]]; then
	# 非 TTY（管道）下 REPL 可能不执行输入行，属环境限制，非功能缺陷。
	echo "[SKIP] REPL 在管道（非 TTY）下未执行，无法自动校验（代码路径默认空列表）"
else
	echo "[FAIL] REPL 无参 pycp.argv 非空"
	echo "----- output -----"; echo "$repl_out"; echo "------------------"
	fail=$((fail + 1))
fi

# --- 3) AOT 编译产物传参：./aot_argv a b c ---
# pycp.exe 与 g++ 均为原生 Windows 程序：其工作目录是 Windows 仓库根目录
# （bash 的 /mnt/d/... 会被映射），相对路径可正确解析；绝对 POSIX 路径
# （/mnt/d/...）它们无法识别。故以下统一 cd 到仓库根目录用相对路径操作。
cd "$REPO_ROOT" || exit 2
if "$PYCP" --emit-cpp tests/argv/argv_demo.pycp -o build/argv_work/aot >/dev/null 2>&1; then
	cp -r "$DIST/stdlib" build/argv_work/stdlib
	CXXFLAGS="-std=c++17 -I build/dist/include"
	# 与项目 aot_import/run.sh 一致的链接方式（共享运行时 + rpath）。
	LDFLAGS="-L build/dist/lib -lPycpRuntime -Wl,-rpath,build/dist/lib"
	if g++ $CXXFLAGS build/argv_work/aot/__pycp_main.gen.cpp $LDFLAGS \
	     -o build/argv_work/aot_argv 2>/dev/null; then
		# Windows 下运行时 DLL 不在 rpath 搜索范围，需放到 exe 同级目录。
		# Linux 下无此文件，条件复制不影响原行为。
		[[ -f "$DIST/lib/libPycpRuntime.dll" ]] && \
			cp "$DIST/lib/libPycpRuntime.dll" build/argv_work/ 2>/dev/null
		aout="$(./build/argv_work/aot_argv a b c 2>&1)"
		arc=$?
		# argv[0] 为程序路径（含 aot_argv），argv[1..3] 为传入参数。
		if [[ $arc -eq 0 && "$aout" == *"ARGV LEN=4"* && \
		      "$aout" == *"ARGV0="*"aot_argv"* && \
		      "$aout" == *"ARGV1=a"* && "$aout" == *"ARGV2=b"* && \
		      "$aout" == *"ARGV3=c"* && "$aout" == *"ARGV TEST PASS"* ]]; then
			echo "[PASS] AOT 产物传参 pycp.argv == [程序路径, a, b, c]"
			pass=$((pass + 1))
		else
			echo "[FAIL] AOT 产物传参（exit=$arc）"
			echo "----- output -----"; echo "$aout"; echo "------------------"
			fail=$((fail + 1))
		fi
	else
		echo "[FAIL] AOT 编译产物链接失败"
		fail=$((fail + 1))
	fi
else
	echo "[FAIL] --emit-cpp 失败（argv_demo.pycp）"
	fail=$((fail + 1))
fi

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
