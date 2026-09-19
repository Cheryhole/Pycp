#!/usr/bin/env bash
# =====================================================================
# 模块文件夹（package）测试套件
# ---------------------------------------------------------------------
# 覆盖：
#   1) 程序角色（解释）：`pycp -m <pkg>` 调用清单的 main(argv)，
#      返回值经 __integer__ 作为退出码；子模块用点号全名。
#   2) 库角色（解释）：import <pkg>，__name__ 为模块名（可自定义），
#      包属性钩子（__get_attribute__ / __string__）生效且不递归。
#   3) 用途错误：program 被 import → ImportError；-m 一个库包 → RuntimeError；
#      入口 __name__ 可写（清单可用 __name__ = "..." 覆盖为模块名）。
#   4) AOT：--emit-cpp -m <pkg> 生成可执行项目（exe 名取自
#      set_executable_name），构建运行与解释态输出/退出码一致。
#   5) AOT 用途与形态错误：库角色写 set_executable_name → 中止；
#      __codegen__ 指定的 static 被 ≥2 链接目标引用 → 报错中止；
#      未声明角色 → 按库生成并打印 note。
#   6) AOT 目录布局：被 import 的包生成到 <pkg>/（pycp.gen.cpp +
#      子模块 .gen.cpp），并一起 cmake 编译运行（包钩子与解释态一致）。
#   7) .cpycp 与运行期解析：脚本编译为 .cpycp 后删源码，import 全靠运行期
#      （目录即包 + 点号子模块）；仅存在 .cpycp 的模块也能被 import。
#   8) `pycp -m <name>` 按名查找包：cwd → exe 同级 stdlib/（沙箱 dist 内放
#      夹具包）；程序角色可直接运行并传递退出码，库角色给出「是库、不可
#      运行」的明确报错，未命中列出候选路径，cwd 同名包优先于 stdlib。
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

# 通用用例：在指定工作目录下运行，校验退出码与输出片段（全部命中才算通过）。
#   用法：check_in <名称> <期望退出码> <期望片段，'|' 分隔> <工作目录> -- <命令...>
check_in() {
	local name="$1" expect_rc="$2" expect_msgs="$3" wd="$4"
	shift 4
	[[ "${1:-}" == "--" ]] && shift
	local out rc ok=1 m
	out="$(cd "$wd" && "$@" 2>&1)"
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

# 解释态用例：在用例目录内运行。
check() {
	local name="$1" expect_rc="$2" expect_msgs="$3"
	shift 3
	check_in "$name" "$expect_rc" "$expect_msgs" "$SCRIPT_DIR" "$@"
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
	"RuntimeError|没有程序入口|不可作为程序运行|import plain_lib_pkg" \
	-- "$PYCP" -m plain_lib_pkg
check "入口 __name__ 可写（清单可覆盖为自定义模块名）" 0 \
	"name=RenamedByManifest" \
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

	echo "== 6) AOT：包输出到 <pkg>/ 子目录并一起编译 =="
	check_emit "AOT 转译导入了包的脚本" 0 "" \
		-- "$PYCP" --emit-cpp use_lib.pycp -o "$WORK/pkg_aot"
	if [[ -f "$WORK/pkg_aot/lib_pkg/pycp.gen.cpp" &&
	      -f "$WORK/pkg_aot/lib_pkg/util.gen.cpp" ]]; then
		echo "[PASS] 包文件集中生成到 lib_pkg/（pycp.gen.cpp + util.gen.cpp）"
		pass=$((pass + 1))
	else
		echo "[FAIL] 未按包分目录生成（缺 lib_pkg/pycp.gen.cpp 或 lib_pkg/util.gen.cpp）"
		find "$WORK/pkg_aot" -name '*.gen.cpp' 2>/dev/null
		fail=$((fail + 1))
	fi
	if cmake -S "$WORK/pkg_aot" -B "$WORK/pkg_aot/build" -DPYCP_DIST="$DIST" >/dev/null 2>&1 &&
	   cmake --build "$WORK/pkg_aot/build" -j"$(nproc)" >/dev/null 2>&1; then
		out="$("$WORK/pkg_aot/build/use_lib" x 2>&1)"
		rc=$?
		if [[ $rc -eq 0 && "$out" == *"name=CustomLibName"* &&
		      "$out" == *"str=LIBPKG<CustomLibName>"* &&
		      "$out" == *"hook get: ping"* && "$out" == *"util.ping"* ]]; then
			echo "[PASS] 包目录布局的 AOT 产物可编译运行，且包钩子与解释态一致"
			pass=$((pass + 1))
		else
			echo "[FAIL] 包目录布局的 AOT 产物运行（exit=$rc）"
			echo "----- output -----"; echo "$out"; echo "------------------"
			fail=$((fail + 1))
		fi
	else
		echo "[FAIL] 包目录布局的 AOT 产物构建失败"
		fail=$((fail + 1))
	fi

	echo "== 7) .cpycp 与运行期解析 =="
	# 7a) 纯运行期解析：脚本编译为 .cpycp 后删掉源码，import 全部由运行期完成
	rm -rf "$WORK/lib_pkg"
	cp -r "$SCRIPT_DIR/lib_pkg" "$WORK/lib_pkg"
	cp "$SCRIPT_DIR/use_lib.pycp" "$WORK/use_lib.pycp"
	(cd "$WORK" && "$PYCP" -c use_lib.pycp -o use_lib.cpycp >/dev/null 2>&1 &&
		rm -f use_lib.pycp)
	check_in ".cpycp 运行：运行期探测「目录即包」+ 点号子模块" 0 \
		"name=CustomLibName|str=LIBPKG<CustomLibName>|hook get: ping|util.ping|helper: x" \
		"$WORK" -- "$PYCP" use_lib.cpycp

	# 7b) 仅存在 .cpycp 的模块可被 import（.cpycp 与 .pycp 等价）
	cp "$SCRIPT_DIR/mod_only_src.pycp" "$WORK/mod_only.pycp"
	cp "$SCRIPT_DIR/use_mod_only.pycp" "$WORK/use_mod_only.pycp"
	(cd "$WORK" && "$PYCP" -c mod_only.pycp -o mod_only.cpycp >/dev/null 2>&1 &&
		rm -f mod_only.pycp)
	check_in "仅 .cpycp 存在的模块可被 import" 0 \
		"mod_only.ping" \
		"$WORK" -- "$PYCP" use_mod_only.pycp

	echo "== 8) pycp -m 按名查找 stdlib 包 =="
	if [[ -d "$DIST/include" && -d "$DIST/lib" ]]; then
		# 沙箱 dist：可搬迁（pycp + libPycpRuntime.so + stdlib/），
		# 这样 GetStdlibDir() 指向沙箱内的 stdlib，不污染 build/stdlib。
		rm -rf "$WORK/dist"
		mkdir -p "$WORK/dist"
		cp -r "$DIST"/. "$WORK/dist"/
		mkdir -p "$WORK/dist/stdlib/stdpkg" "$WORK/dist/stdlib/stdlib_lib"

		cat > "$WORK/dist/stdlib/stdpkg/pycp.mpycp" <<'EOF'
import io
import moduletools

moduletools.as_program()

func main(argv) {
	io.print("STDPKG MAIN")
	io.print(argv)
	return 5
}
EOF

		cat > "$WORK/dist/stdlib/stdlib_lib/pycp.mpycp" <<'EOF'
import io
import moduletools

func helper() {
	io.print("stdlib_lib.helper")
}
EOF

		check_in "stdlib 包可直接运行（-m 按名查找 + main 退出码）" 5 \
			'STDPKG MAIN|Fixed["stdpkg", "a", "b"]' \
			"$WORK" -- "$WORK/dist/pycp" -m stdpkg a b

		check_in "stdlib 里的库 → 明确报错「是库、不可运行」" 1 \
			"没有程序入口|不可作为程序运行|import stdlib_lib|main(argv)" \
			"$WORK" -- "$WORK/dist/pycp" -m stdlib_lib

		check_in "-m 未命中 → 列出候选路径与用法提示" 2 \
			"未找到|./no_such_pkg/pycp.mpycp|只接受模块文件夹" \
			"$WORK" -- "$WORK/dist/pycp" -m no_such_pkg

		# cwd 同名包优先于 stdlib（本地优先）
		mkdir -p "$WORK/stdpkg"
		cat > "$WORK/stdpkg/pycp.mpycp" <<'EOF'
import io
import moduletools

moduletools.as_program()

func main(argv) {
	io.print("LOCAL STDPKG")
	return 0
}
EOF
		check_in "cwd 同名包覆盖 stdlib（本地优先）" 0 \
			"LOCAL STDPKG" \
			"$WORK" -- "$WORK/dist/pycp" -m stdpkg

		check_in "--emit-cpp 也可从 stdlib 找到包并转译" 0 \
			"Package: stdpkg|role=program" \
			"$WORK" -- "$WORK/dist/pycp" --emit-cpp -m stdpkg \
			-o "$WORK/stdpkg_aot" --show-imports
	else
		echo "[SKIP] 缺少 $DIST（先执行 cmake --build build --target pycp-dist）"
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
