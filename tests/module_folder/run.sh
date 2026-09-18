#!/usr/bin/env bash
# =====================================================================
# 模块文件夹（package）测试套件
# ---------------------------------------------------------------------
# 覆盖：
#   1) 程序角色（解释）：`pycp -m <pkg>` 调用清单的 main(argv)，
#      返回值经 __integer__ 作为退出码；子模块用点号全名。
#   2) 库角色（解释）：import <pkg>，__name__ 为模块名（可自定义），
#      包属性钩子（__get_attribute__ / __string__）生效且不递归。
#   3) 用途错误：program 被 import → ImportError；
#      入口 __name__ 只读（清单赋值被拒）。
#   4) AOT：--emit-cpp -m <pkg> 生成可执行项目（exe 名取自
#      set_executable_name），构建运行与解释态输出/退出码一致。
#   5) AOT 用途与形态错误：库角色写 set_executable_name → 中止；
#      __codegen__ 指定的 static 被 ≥2 链接目标引用 → 报错中止；
#      未声明角色 → 按库生成并打印 note。
#
# 用法：bash tests/module_folder/run.sh
# =====================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"
DIST="$REPO_ROOT/build/dist"
WORK="$REPO_ROOT/build/module_folder_work"

if [[ ! -x "$PYCP" ]]; then
	echo "ERROR: 找不到可执行文件 $PYCP（请先执行 cmake --build build）" >&2
	exit 2
fi

pass=0
fail=0

# 解释态用例：在用例目录内运行，校验退出码与输出片段（全部命中才算通过）。
#   用法：check <名称> <期望退出码> <期望片段，'|' 分隔> -- <命令...>
check() {
	local name="$1" expect_rc="$2" expect_msgs="$3"
	shift 3
	[[ "${1:-}" == "--" ]] && shift
	local out rc ok=1 m
	out="$(cd "$SCRIPT_DIR" && "$@" 2>&1)"
	rc=$?
	[[ "$rc" == "$expect_rc" ]] || ok=0
	if [[ -n "$expect_msgs" ]]; then
		while IFS= read -r m; do
			[[ -z "$m" ]] && continue
			[[ "$out" == *"$m"* ]] || ok=0
		done <<< "${expect_msgs//|/$'\n'}"
	fi
	if [[ $ok -eq 1 ]]; then
		echo "[PASS] $name"
		pass=$((pass + 1))
	else
		echo "[FAIL] $name（exit=$rc，期望 $expect_rc）"
		echo "----- output -----"; echo "$out"; echo "------------------"
		fail=$((fail + 1))
	fi
}

# AOT 转译用例：只跑 --emit-cpp（不构建），校验退出码与输出片段。
# 直接转发给 check（调用方已带 '--' 分隔符）。
check_emit() {
	check "$@"
}

echo "== 1) 程序角色（解释执行） =="
check "pycp -m prog_pkg 调用 main(argv) 并以返回值作退出码" 3 \
	"PROG MAIN|Fixed[\"prog_pkg\", \"a\", \"b\"]|objA.method1: from-main|role=program" \
	-- "$PYCP" -m prog_pkg a b

echo "== 2) 库角色（解释执行） =="
check "import <pkg>：__name__ 为模块名、属性钩子生效、子模块可调用" 0 \
	"name=CustomLibName|hook get: ping|util.ping|helper: x|str=LIBPKG<CustomLibName>" \
	-- "$PYCP" use_lib.pycp

echo "== 3) 用途错误（解释执行） =="
check "program 被 import → ImportError" 1 \
	"ImportError|declared as a program" \
	-- "$PYCP" use_program.pycp
check "-m 一个未声明程序的包 → RuntimeError（无 main）" 1 \
	"RuntimeError|does not define func main(argv)" \
	-- "$PYCP" -m plain_lib_pkg
check "程序角色入口 __name__ 只读" 1 \
	"read-only binding '__name__'" \
	-- "$PYCP" -m name_guard_pkg

echo "== 4) AOT：程序角色的包 → 可执行项目 =="
if [[ -d "$DIST/include" && -d "$DIST/lib" ]]; then
	rm -rf "$WORK"
	mkdir -p "$WORK"
	check_emit "AOT 转译：角色为 program、子模块按点号全名" 0 \
		"role=program|translated  prog_pkg.obj_a" \
		-- "$PYCP" --emit-cpp -m prog_pkg -o "$WORK/prog_aot" --show-imports
	if [[ -f "$WORK/prog_aot/CMakeLists.txt" ]]; then
		if grep -q "add_executable(prog_app" "$WORK/prog_aot/CMakeLists.txt"; then
			echo "[PASS] 生成的 CMakeLists 使用脚本指定的可执行名 prog_app"
			pass=$((pass + 1))
		else
			echo "[FAIL] 生成的 CMakeLists 未使用 prog_app"
			fail=$((fail + 1))
		fi
		if cmake -S "$WORK/prog_aot" -B "$WORK/prog_aot/build" -DPYCP_DIST="$DIST" >/dev/null 2>&1 &&
		   cmake --build "$WORK/prog_aot/build" -j"$(nproc)" >/dev/null 2>&1; then
			out="$("$WORK/prog_aot/build/prog_app" x y 2>&1)"
			rc=$?
			if [[ $rc -eq 3 && "$out" == *"PROG MAIN"* && "$out" == *"role=program"* &&
			      "$out" == *"objA.method1: from-main"* ]]; then
				echo "[PASS] AOT 产物运行：main(argv) 被调用且退出码为 3"
				pass=$((pass + 1))
			else
				echo "[FAIL] AOT 产物运行（exit=$rc）"
				echo "----- output -----"; echo "$out"; echo "------------------"
				fail=$((fail + 1))
			fi
		else
			echo "[FAIL] AOT 产物构建失败"
			fail=$((fail + 1))
		fi
	else
		echo "[FAIL] 未生成 $WORK/prog_aot/CMakeLists.txt"
		fail=$((fail + 1))
	fi

	echo "== 5) AOT：用途与形态错误 =="
	check_emit "库角色写 set_executable_name → 转译中止" 1 \
		"按【库】角色转译" \
		-- "$PYCP" --emit-cpp -m aot_badgen_pkg -o "$WORK/badgen_aot"
	check_emit "__codegen__ 指定的 static 被 ≥2 链接目标引用 → 转译中止" 1 \
		"由 __codegen__ 指定为 static" \
		-- "$PYCP" --emit-cpp -m aot_conf_pkg -o "$WORK/conf_aot"
	check_emit "未声明角色 → 按库生成并打印 note" 0 \
		"未声明角色，已按【库】生成" \
		-- "$PYCP" --emit-cpp -m lib_pkg -o "$WORK/lib_aot"
else
	echo "[SKIP] 缺少 $DIST（先执行 cmake --build build --target pycp-dist）"
fi

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
