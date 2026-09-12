#!/usr/bin/env bash
# =====================================================================
# 原生函数参数解析框架（Pycp::Extension 参数规范表）回归测试
#
# 覆盖内置原生函数迁移到「容器形态原生函数 + 名字规范表」后的行为：
#   1) 内置构造器 / insp / typeof / 实例方法的正常路径
#   2) File 各方法正常路径 + Optional 默认值（mode 省略、read 可省略 size）
#   3) 参数不足 / 过多 / 类型错误时的统一错误消息（含函数名与参数名）
#   4) *rest（可变参数）与 **kw（关键字参数）的规范与收集
#
# 沙箱运行：复制 build/dist 到 build/native_args_work，并把夹具扩展
# fixtures/eg.cpp 编译为 eg.so 放进沙箱，避免污染构建产物与源码目录。
#
# 用法：bash tests/native_args/run.sh
# =====================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PYCP="$REPO_ROOT/build/pycp"
DIST="$REPO_ROOT/build/dist"
FIXTURES="$SCRIPT_DIR/fixtures"
WORK="$REPO_ROOT/build/native_args_work"

if [[ ! -x "$PYCP" ]]; then
	echo "ERROR: 找不到可执行文件 $PYCP（请先执行 cmake --build build）" >&2
	exit 2
fi
if [[ ! -d "$DIST/include" || ! -d "$DIST/lib" || ! -d "$DIST/stdlib" ]]; then
	echo "ERROR: $DIST 不完整（请先执行 cmake --build build）" >&2
	exit 2
fi

# 重建沙箱：一份完整的 dist 副本 + 全部用例脚本
rm -rf "$WORK"
mkdir -p "$WORK"
cp -r "$DIST"/. "$WORK"/
cp "$SCRIPT_DIR"/*.pycp "$WORK"/

# 夹具扩展：容器形态原生函数 + Module::set_function（含 *rest / **kw 用例）。
g++ -std=c++17 -fPIC -shared -I "$DIST/include" \
	"$FIXTURES/eg.cpp" \
	-L "$DIST/lib" -lPycpRuntime -Wl,-rpath,"$DIST/lib" \
	-o "$WORK/eg.so" >/dev/null 2>&1
if [[ ! -f "$WORK/eg.so" ]]; then
	echo "ERROR: 夹具扩展 eg.so 构建失败（检查 g++ 与 $DIST/include）" >&2
	exit 2
fi

pass=0
fail=0

# 正常路径用例：退出码为 0 且输出包含全部期望片段。
#   用法：check_ok <名称> <脚本> <期望片段>...
check_ok() {
	local name="$1" script="$2"
	shift 2
	local out rc ok=1 e
	out="$(cd "$WORK" && ./pycp "$script" 2>&1)"
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

# 错误路径用例：退出码非零且输出包含期望的错误消息。
#   用法：check_err <名称> <脚本> <期望消息>
check_err() {
	local name="$1" script="$2" expect="$3"
	local out rc
	out="$(cd "$WORK" && ./pycp "$script" 2>&1)"
	rc=$?
	if [[ $rc -ne 0 && "$out" == *"$expect"* ]]; then
		echo "[PASS] $name"
		pass=$((pass + 1))
	else
		echo "[FAIL] $name（exit=$rc）"
		echo "----- output -----"; echo "$out"; echo "------------------"
		fail=$((fail + 1))
	fi
}

# --- 1) 内置函数 / 实例方法正常路径 ---
check_ok "内置构造器与实例方法（String/Integer/Boolean/List/FixedList/Map/insp/typeof）" \
	ok_builtin.pycp "NATIVE OK" "<class \"Integer\">" "(1, 2)" "{}" '("k",)'

# --- 2) File 方法与 Optional 默认值 ---
check_ok "File 方法与 Optional 默认值（mode 省略 / read(n)）" \
	ok_file.pycp "FILE OK" '["hello"]'

# --- 3) *rest / **kw（可变参数与关键字参数规范）---
check_ok "*rest 收集与默认值分拣（零个 -> 空元组；**kw 恒空）" \
	ok_rest.pycp "REST OK" "0" "6" "(1, 0, ())" "(1, 2, ())" "(1, 2, (3, 4))" "((), {})"

# --- 4) 参数个数错误（统一消息：函数名 + 参数名）---
check_err "参数不足：append() missing required argument: 'item'." \
	err_arity_missing.pycp "TypeError: append() missing required argument: 'item'."
check_err "参数过多：length() takes no positional arguments (1 given)." \
	err_arity_extra.pycp "TypeError: length() takes no positional arguments (1 given)."
check_err "构造器参数过多（规范表内显式登记函数名）" \
	err_ctor_arity.pycp "TypeError: FixedList() takes at most 1 positional arguments (2 given)."
check_err "*rest 不改变必填参数下界：pick() missing required argument: 'a'." \
	err_rest_missing.pycp "TypeError: pick() missing required argument: 'a'."

# --- 5) 类型错误（框架不做类型检查，业务函数自行判型并报错）---
check_err "类型错误：read() 的 size 须为整数" \
	err_type_int.pycp "TypeError: read(): argument 'size' expects an integer, got 'String'."
check_err "类型错误：open() 的 path 须为字符串" \
	err_type_string.pycp "TypeError: open(): argument 'path' expects a string, got 'Integer'."
check_err "类型错误：*rest 元素由业务函数判型" \
	err_rest_type.pycp "TypeError: sum_rest(): arguments must be integers."

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
