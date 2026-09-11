#!/usr/bin/env bash
# REPL 测试套件：以管道输入驱动交互模式，校验输出、错误与退出码。
# 用法：bash tests/repl/run.sh
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

check() {
	local label="$1" out="$2" rc="$3"
	if [[ $rc -eq 0 && "$out" == *"REPL TEST PASS"* ]]; then
		echo "[PASS] $label"
		pass=$((pass+1))
	else
		echo "[FAIL] $label (exit=$rc)"
		echo "----- output -----"
		echo "$out"
		echo "------------------"
		fail=$((fail+1))
	fi
}

# --- 用例 1：@readonly 绑定在 REPL 生效（重赋值被拒、值保持）、普通变量可重赋值 ---
out1="$("$PYCP" 2>&1 <<'EOF'
import pycp
import io
@pycp.readonly A = 1
A = 2
io.print(A)
c = 1
c = 2
io.print(c)
io.print(__name__)
io.print(nope)
if (A == 1) {
} else {
io.print("BAD A")
}
if (c == 2) {
} else {
io.print("BAD c")
}
io.print("REPL TEST PASS")
EOF
)"
check "readonly 绑定 + 普通变量 + 错误不中断" "$out1" $?

# --- 用例 2：REPL 中函数 / 闭包 / 类定义可用 ---
out2="$("$PYCP" 2>&1 <<'EOF'
import io
func f(x) {
return x + 1
}
io.print(f(41))
func mk(k) {
func g(v) {
return v + k
}
return g
}
h = mk(2)
io.print(h(40))
class C {
func get(self) {
return 5
}
}
io.print(C().get())
@pycp.readonly PI = 3
io.print(PI)
if (f(41) == 42) {
} else {
io.print("BAD f")
}
if (h(40) == 42) {
} else {
io.print("BAD h")
}
io.print("REPL TEST PASS")
EOF
)"
check "函数 / 闭包 / 类定义可用" "$out2" $?

# --- 用例 3：只读绑定经 pycp.readonly 显式导入写法同样生效 ---
out3="$("$PYCP" 2>&1 <<'EOF'
import pycp
from pycp import readonly
import io
@readonly B = 7
B = 8
io.print(B)
if (B == 7) {
} else {
io.print("BAD B")
}
io.print("REPL TEST PASS")
EOF
)"
check "from 导入的 readonly 装饰器" "$out3" $?

# --- 用例 4：装饰器函数的「直接调用」形式与 @ 语法糖等价 ---
out4="$("$PYCP" 2>&1 <<'EOF'
import pycp
import io
a = 1
pycp.readonly(a)
a = 2
io.print(a)
func f() {
return 7
}
pycp.private(f)
pycp.public(f)
io.print(f())
if (a == 1) {
} else {
io.print("BAD a")
}
io.print("REPL TEST PASS")
EOF
)"
check "readonly/private/public 直接调用形式" "$out4" $?

echo "==================================="
echo "PASS=$pass  FAIL=$fail"
if [[ $fail -gt 0 ]]; then
	exit 1
fi
exit 0
