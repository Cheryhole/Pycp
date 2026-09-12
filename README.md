# Pycp

Pycp 是一个 **Python-like 语言** 的编译器与解释器，使用 C++17 实现。它采用与 Python 相近的语法，重新实现了词法/语法分析器、AST、代码生成器、栈式字节码虚拟机（VM）以及序列化字节码格式。

Pycp 将源码 `.pycp` 编译为自定义字节码 `.cpycp`（类似 Python 的 `.pyc`），随后由内嵌的 Pycp VM 解释执行。

## 功能特性

| 类别 | 说明 |
|------|------|
| 语言定位 | Python-like，沿用 Python 部分语法 |
| 前端 | Flex 词法分析 + Bison 语法分析，生成 AST |
| 编译器 | AST → 栈式字节码（Codegen），支持常量池 / 符号表 / 代码对象 |
| 运行时 | GC、对象模型（Integer / String / None / Function / Class / Instance / Module / File / List）、ABI 接口 |
| 虚拟机 | 栈式字节码 VM，支持函数调用、闭包、类定义、继承、装饰器、控制流 |
| 字节码 | `.cpycp` 二进制格式（小端 + LEB128 编码），可序列化 / 反序列化 |
| 标准库 | C++ 原生动态库（`io` / `pycp` / `classtools`），`import` 时动态加载 |
| AOT | 将字节码逐指令翻译为依赖 PycpABI 的独立 C++ 源文件（真正的指令翻译，非骨架占位） |

### 对象命名规范

Pycp 对象的类型判定使用**字符串**（而非枚举），与 ABI 保持一致：

- **内置类型名使用大写**：`None`、`Integer`、`String`、`List`、`Function`、`File`。
- **`class` / `instance` / `module` 不采用统一大写名**，而是返回各自的真实名称：
  - 类 `class a{}` 的类型名即 `a`，其实例类型名也为 `a`；
  - 模块的类型名即模块名（如 `io`）。
- **自定义类型的名称与代码定义时的实际名称完全一致（含大小写）**，不做任何改写。
  例如 `class MyClass{}` 的类型名、`<class "MyClass">`、`<MyClass instance at ...>` 均保留 `MyClass`。
- 匿名函数 / 匿名类使用内部名 `@anonymous`（仅作为"名字"，字符串表示仍沿用普通格式）。

### 已支持的语言子集

- 赋值、表达式语句
- 整数 / 字符串 / `None` 字面量
- 算术（`+ - * /`）、幂运算（`**`）、一元取负（`-`）
- 比较（`< <= > >= == !=`）
- `if / elif / else` 条件语句
- 函数定义（`func name(params){...}`）与匿名函数（`func(params){...}`）
- 函数调用、`return`
- **带默认值的形参**（`func f(a, b = 1){...}`）：默认值可为任意表达式，在函数定义执行时求值一次；
  调用时少传**尾部**位置实参即自动填充默认值（`f(10)` → `b=1`）。无关键字实参语法（`f(b=1)` 不支持）。
  非法：默认值形参后不得再接必填普通形参（`func f(a, b = 1, c)` 编译期报错）。共享可变默认对象
  （如 `b = []`）按 Python 语义由所有调用共享。
- 闭包（匿名函数捕获外层局部变量）
- 类定义（`class Name{...}`）与实例化，含 `__initialize__` / `__string__` 等魔术方法
- 单继承（`class Child inherits Parent{...}`）
- 运算符重载（`__addition__` / `__subtraction__` / `__power__` 等魔术方法）
- **列表（List）**：方括号字面量 `[a, b, c]`、下标访问 `obj[key]` 与赋值 `obj[key] = value`、
  负索引、`length()` 方法、`+` 拼接、字符串表示、`pycp.List(obj)` 转换
  （支持 `__get_item__` / `__set_item__` / `__list__` 魔术方法，自定义类可重载）
- 装饰器语法糖（`@decorator`）：把被装饰对象传给装饰器函数，用返回值替换；支持**叠加装饰器**
  （连续多行 `@d1` 换行 `@d2` 修饰同一目标，最靠近目标的装饰器最先应用）；
  访问控制装饰器（`readonly`/`private`/`public`）也可**直接调用**模块顶层名字，效果与 `@` 一致
- 成员可见性（`@private` / `@public` 修饰类内成员，控制类外访问）
- 模块顶层装饰器与文件级导出（`@private` 的顶层符号对其他文件 import 不可见）
- 模块导入（`import foo` / `import foo as bar` / `from foo import a, b`）

## 安装步骤

### 依赖项

| 依赖 | 版本要求 | 用途 | 安装命令（Ubuntu/Debian） |
|------|---------|------|---------------------------|
| CMake | ≥ 3.15 | 构建系统 | `sudo apt install cmake` |
| C++ 编译器（GCC/Clang/MSVC） | 支持 C++17 | 编译源码 | `sudo apt install build-essential` |
| Flex | 任意 | 词法分析器生成 | `sudo apt install flex` |
| Bison | 任意 | 语法分析器生成 | `sudo apt install bison` |
| Python + pybind11 | 可选 | Python 绑定（`BUILD_PYTHON_BINDING`） | `pip install pybind11` |

> 核心功能只需前四项；Python / pybind11 仅在开启 Python 绑定时需要。

### 一键安装命令

```bash
# Ubuntu / Debian
sudo apt update
sudo apt install cmake build-essential flex bison
```

### Windows 构建（MinGW-w64 + win_flex_bison）

Windows 通常没有系统自带的 Flex/Bison，需安装
[win_flex_bison](https://github.com/lexxmark/winflexbison/releases) 并把其
所在目录加入 `PATH`，或显式指定：

```powershell
cmake -S . -B build -DBISON_EXECUTABLE=D:/winflex/bison.exe -DFLEX_EXECUTABLE=D:/winflex/flex.exe
cmake --build build -j
```

> **已知工具链缺陷（非本项目问题）**：`win_flex.exe` / `win_bison.exe` 用
> `_tempnam` + `fopen` 申请临时文件，**并行运行多个实例会共用同一个临时文件并互相覆盖**
> （[winflexbison#86](https://github.com/lexxmark/winflexbison/issues/86)、
> [westes/flex#580](https://github.com/westes/flex/issues/580)）。此时产出的
> `.cpp` 是"未展开的 m4 骨架"，编译时报出几百行看似无关的错误，例如：
>
> ```
> error: '#endif' without '#if'
> error: stray '#' in program
> error: 'out_ALREADY_DEFINED' does not name a type
> error: expected unqualified-id before ']' token
> ```
>
> 由于损坏产物比 `.l` / `.y` 新，不手动删除就不会重新生成，问题会一直卡住。
> Linux/WSL 的 GNU flex/bison 用 `mkstemp`，并发安全，因此该问题只在 Windows 出现。
>
> 本项目已在 CMake 层处理：把 4 步生成串成依赖链（仅 `WIN32` 串行化，Linux/WSL 仍并行），
> 并在每条生成命令后执行 `cmake/PycpCheckGenerated.cmake` 自检——一旦检出 m4 模板残留
> 或前缀错位，会**删除损坏产物**并给出可操作的错误，直接重新构建即可恢复。
> 若仍遇到异常，删除 `build/generated` 目录后重建，或以 `-j1` 构建。

### 构建

```bash
# 在项目根目录执行
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

构建完成后，可执行文件位于 `build/pycp`。如需安装到系统：

```bash
sudo cmake --install build
```

## 使用方法

### 命令行接口

```
pycp [options] <input_file>
```

| 选项 | 说明 |
|------|------|
| `-h, --help` | 显示帮助信息 |
| `-c, --compile` | 将 `.pycp` 编译为 `.cpycp` 字节码（不执行） |
| `-b, --bytecode` | 生成 `.cpycp` 字节码（`-c` 的别名） |
| `-i, --interpret` | 解释执行（默认行为；接受 `.pycp` 或 `.cpycp`） |
| `-o, --output <f>` | 指定输出路径：配合 `-c/-b` 为 `.cpycp` 文件路径；配合 `--emit-cpp` 为**项目目录**（默认 `./<入口名>/`）；配合 `-p` 为 `.pp.pycp` 文件路径 |
| `--emit-cpp` | 将 `.pycp` 翻译为可直接编译的 **CMake 项目目录**（AOT 指令翻译，输出 `.gen.cpp` + `CMakeLists.txt`） |
| `--compile-runtime=shared\|static` | 配合 `--emit-cpp`：运行时库 `libPycpRuntime` 的链接方式（默认 `shared`）。`static` 需 SDK 静态产物，且禁止存在任何动态模块（会生成两份运行时） |
| `--compile-modules=shared\|static` | 配合 `--emit-cpp`：转译 `.pycp` 模块的**全局形态**（默认 `shared`：每个模块编成一个动态库运行期加载；`static`：编进主程序） |
| `--compile-module:<name>=shared\|static` | 配合 `--emit-cpp`：**按模块覆盖**（内置扩展 io/pycp/classtools，或任一转译依赖模块）；覆盖与全局默认相同时为空操作（会给出提示） |
| `--show-imports` | 配合 `--emit-cpp`：打印编译期 import 解析清单（translated / unresolved）**与逐模块形态决策表**（最终形态 + 决策来源） |
| `--shared` / `--static` | `--compile-runtime=shared` / `static` 的**旧别名**（deprecated；不能与 `--compile-*` 参数混用） |
| `-d, --dump` | 查看字节码内容（常量池 / 符号表 / 代码对象 / 指令与行号），接受 `.pycp` 或 `.cpycp` |

### 使用示例

**1. 解释执行 `.pycp` 源文件（默认）**

```bash
./build/pycp hello.pycp
```

**2. 编译为字节码，再执行字节码**

```bash
# 编译：.pycp -> .cpycp
./build/pycp -c hello.pycp -o hello.cpycp

# 执行编译后的字节码
./build/pycp hello.cpycp
```

**3. 查看字节码内容（调试 / 开发用）**

```bash
# 查看源文件编译后的字节码
./build/pycp -d hello.pycp

# 查看已编译字节码文件的内容
./build/pycp --dump hello.cpycp
```

输出包含文件头（格式版本、源文件路径）、全局常量池、符号表，以及每个代码对象的方法签名（`nparams` / `nlocals`）、局部变量字段（`locals`）和逐条指令（含行号与操作数注释）。示例：

```
[Header]
  format version : 2.0
  source path    : hello.pycp

[Constant Pool] (2 entries)
  0: 1
  1: 2

[Symbol Table] (5 entries)
  0: <module>
  1: a
  2: b
  3: add
  4: result

[Code Objects] (2)

--- CodeObject[0] ---
  name    : <module>
  nparams : 0
  nlocals : 0
  code (8 instrs):
  2  MAKE_FUNCTION 1  # add
  2  STORE_VAR 3  # add
  5  LOAD_VAR 3  # add
  5  LOAD_CONST 0  # 1
  5  LOAD_CONST 1  # 2
  5  CALL 2
  5  STORE_VAR 4  # result
  5  HALT

--- CodeObject[1] ---
  name    : add
  nparams : 2
  nlocals : 2
  locals  : a, b
  code (4 instrs):
  2  LOAD_VAR 1  # a
  2  LOAD_VAR 2  # b
  2  BINARY_ADD
  2  RETURN
```

**4. 翻译为可直接编译的 C++ 项目（AOT）**

```bash
# 将 .pycp 及其全部 import 依赖翻译为一个 CMake 项目目录（默认 ./hello/）
./build/pycp --emit-cpp hello.pycp
# 或指定输出目录：-o <dir> 必须是目录
./build/pycp --emit-cpp hello.pycp -o ./my_project

# 生成目录内含：
#   __pycp_main.gen.cpp  （入口，含 main）
#   <dep>.gen.cpp        （每个被 import 的模块各一份）
#   CMakeLists.txt       （自动定位 Pycp 运行时 SDK、动态链接、部署 stdlib/ 与 lib/）
cd hello && cmake -S . -B build && cmake --build build
./build/hello   # 输出与解释执行一致
```

默认生成的 CMake 项目里，每个被 import 的模块各有一个 `add_library(pycp_mod_<name> SHARED ...)`
目标（DLL/SO），运行期按模块名加载；加 `--compile-modules=static` 则改为各一个
`STATIC` 目标（`lib<name>.a`）并链进主程序。

生成的 CMake 项目会自动：
- **动态链接** `PycpRuntime`（stdio 三个扩展 `.so` 同样动态链接运行时，避免进程内两份运行时状态）；
- 在 Linux 上加 `-rdynamic`，使 import 的「进程内符号」查找（`dlsym(RTLD_DEFAULT, "PycpModule_xxx")`）能命中链接进本程序的模块；
- 构建期（`POST_BUILD`）把 SDK 的 `stdlib/` 与 `lib/` 复制到 exe 同级，**产物自包含**，可单独拷到任意目录运行（Windows 额外把运行时 DLL 复制到 exe 同级；其名随编译器而异：MinGW 为 `libPycpRuntime.dll`，MSVC 为 `PycpRuntime.dll`）；
- SDK 根目录以 `CACHE PATH` 形式写入 `PYCP_DIST`，换机器时用 `-DPYCP_DIST=<path>` 覆盖即可。

> 若你只想手工编译而不用 CMake，也可直接 `g++`（与旧版一致）：
> ```bash
> g++ -std=c++17 -I build/dist/include __pycp_main.gen.cpp <dep>.gen.cpp \
>     -L build/dist/lib -lPycpRuntime -Wl,-rpath,'$ORIGIN/lib' -o hello
> # 运行前把 build/dist/lib/ 与 build/dist/stdlib/ 放到 hello 旁边
> ```

生成的 `.cpp` 包含 `main()`，内部将字节码翻译为 `Pycp::Add`/`Sub`/`Call` 等
ABI 调用序列（常量内联为 `g_c[]`，控制流翻译为 `goto`，函数翻译为
`pycp_fn_N`）。它不依赖解释器循环，但编译时仍需链接 `PycpRuntime` 库
（静态 `libPycpRuntime.a`，或动态 `libPycpRuntime.dll` + 导入库 `libPycpRuntime.dll.a`；Linux/macOS 下为 `libPycpRuntime.so`）。

**三档编译形态：`--compile-runtime` / `--compile-modules` / `--compile-module:<name>`**

`--emit-cpp` 把「运行时库」「内置扩展（io/pycp/classtools）」「转译的 `.pycp` 依赖模块」
三类对象的链接方式拆开控制：

```bash
# 默认：依赖模块各编成一个模块 DLL（kShared，运行期加载），运行时与内置扩展动态
./build/pycp --emit-cpp hello.pycp

# 全静态自包含：运行时 + 内置扩展 + 依赖模块全部静态链入，产物为单个 exe
# （runtime 为 static 时不允许任何动态形态，故必须同时给出 --compile-modules=static）
./build/pycp --emit-cpp --compile-runtime=static --compile-modules=static hello.pycp

# 依赖模块编进主程序（与历史行为一致）
./build/pycp --emit-cpp --compile-modules=static hello.pycp

# 按模块覆盖（内置扩展或某个依赖模块）：只有 b_dep 编进主程序，其余为 DLL
./build/pycp --emit-cpp --compile-module:b_dep=static hello.pycp

# 反过来的「只让某一个模块动态」：把全局默认设为 static 再覆盖该模块
./build/pycp --emit-cpp --compile-modules=static --compile-module:b_dep=shared hello.pycp

# 查看逐模块的最终形态与决策来源（含被强制提升的模块）
./build/pycp --emit-cpp --show-imports --compile-module:b_dep=static hello.pycp
```

形态决策规则：

- **默认**：依赖模块 `shared`（每个模块一个 DLL，运行期从 exe 同级 `stdlib/` 按名加载）；
  内置扩展跟随运行时（`runtime=shared` → 从 `stdlib/` 加载；`runtime=static` → 链接
  `libPycpExt_*.a`）。
- **优先级**：按模块覆盖（`--compile-module:<name>=`）> 全局默认（`--compile-modules=`）。
  注意覆盖与全局默认形态相同时是**空操作**（例如默认 `shared` 时再写
  `--compile-module:x=shared`），此时 `pycp` 会在 stderr 给出提示。
- **逐模块独立 target**：生成的 `CMakeLists.txt` 里每个依赖模块各有一个
  `add_library(pycp_mod_<name> ...)`——`static` 为独立归档（`lib<name>.a`），
  `shared` 为独立 DLL/SO（`<name>.dll/.so`）。静态模块之间用
  `target_link_libraries(... PUBLIC ...)` 传播静态依赖闭包，故主程序与每个
  DLL 只会链入自己真正需要的静态模块（CMake 允许静态库之间成环，模块间循环
  import 依然成立）。
- **冲突自动裁决**：任何被 ≥2 个链接目标（主程序 / 多个动态库）引用的模块会**强制
  提升为 `shared`**（否则同一模块被复制进多个目标，产生两份模块对象与注册表覆盖）。
  提升若否定了用户显式指定的 `=static`，决策原因会带上
  `[overrides --compile-module:<name>=static]` 标记，可用 `--show-imports` 查看。
- **`--compile-runtime=static` 与任何动态模块组合都直接报错**——运行时若静态链接而
  又有动态库存在，进程内会出现两份运行时状态（GC 池 / 小整数池 / 句柄缓存）。
- 旧 `--shared` / `--static` 保留为 `--compile-runtime=` 的兼容别名（deprecated）；
  二者不能与 `--compile-*` 参数混用。

| 维度 | `--compile-runtime=shared`（默认） | `--compile-runtime=static` |
|------|------------------------------------|----------------------------|
| 运行时 | 动态链接 `PycpRuntime`（DLL/so） | 静态链接 `libPycpRuntime.a` |
| 内置扩展（默认） | 运行时从 `stdlib/` 加载 DLL/so | 静态链接 `libPycpExt_*.a` |
| 静态 `PYCP_STATIC` 宏 | 不定义 | 参与编译的 TU 均定义（Windows 必需） |
| 动态模块 DLL | `PYCP_BUILDING_MODULE`（Windows 导出入口符号） | 不允许存在 |
| `-rdynamic` / rpath / POST_BUILD | 需要（复制 `stdlib/`、`lib/` 与 DLL 到 exe 同级） | 不需要 |
| 产物形态 | exe + 同级 `stdlib/`、`lib/`（+ 模块 DLL） | 全静态时单个自包含 exe |

- static 模式通过 `dist/lib/` 下的 `libPycpExt_io.a` / `libPycpExt_pycp.a` /
  `libPycpExt_classtools.a` 静态扩展库 + 生成器发射的注册/拉入桩
  （`RegisterAotModule`）实现"扩展静态链接并强制被链接器拉入"。
- 转译生成的 `.gen.cpp` 若被编译为 DLL，其 `PycpModule_<name>` 入口由
  `PYCP_MODULE_EXPORT`（`PycpExtension.hpp`）在 Windows 下显式导出，DLL 以 `<name>.so/.dll`
  命名并复制到 exe 同级的 `stdlib/`，运行期按模块名加载。
- 行为上应与解释器逐字节一致（见下文一致性回归）。

> 说明：AOT 已实现完整的指令翻译（含类定义 `MAKE_CLASS`、属性
> `LOAD_ATTR`/`STORE_ATTR`、列表字面量与下标 `BUILD_LIST`/`GET_ITEM`/`SET_ITEM`、
> 函数调用等）。闭包通过 `BytecodeFunction` 的 native 模式落地，匿名函数捕获
> 外层局部变量后翻译出的 C++ 与原生函数一致。生成的代码需链接 `PycpRuntime`
> 库（静态 `libPycpRuntime.a`，或动态 `libPycpRuntime.dll` + 导入库 `libPycpRuntime.dll.a`；Linux/macOS 下为 `libPycpRuntime.so`）。

**5. 一个最小示例**

创建 `hello.pycp`：

```
import io
import pycp

a = 23 - 5 * 7
io.stdout.write(pycp.String(a))
io.stdout.write("\n")
io.stdout.write(pycp.String(2 ** 3))
io.stdout.write("\n")
```

运行：

```bash
./build/pycp hello.pycp
# 输出：
# -12
# 8
```

**5.5 命令行参数 `pycp.argv`**

`pycp.argv` 返回启动参数列表（`List[String]`），语义对齐 Python 的 `sys.argv`：

```
import io
import pycp

# 运行：pycp args.pycp alice bob
# 输出：["args.pycp", "alice", "bob"]
io.print(pycp.argv)
io.print(pycp.String(pycp.argv.length()))   # 3
io.print(pycp.argv[0])                       # "args.pycp"（不含 pycp 可执行文件）
```

解释运行下 `argv[0]` 为脚本名；AOT 编译产物运行下 `argv[0]` 为程序完整路径；REPL 下为空列表 `[]`。

**6. 标准输入输出与可见性装饰器示例**

`io.print` / `io.input`（对齐 Python3 单参数语义）：

```
import io

io.print("hello")           # 输出 "hello" 并自动换行
name = io.input("Enter: ")  # 打印提示（不换行）后读取一行
io.print("Hello " + name)
```

装饰器与成员可见性（`public` / `private` 可从 `pycp` 或 `classtools` 导入）：

```
import pycp
import io
from pycp import public, private

@public
func greet(name) {
    return "Hello " + name
}

class Animal {
    @private
    _age = 0
    @public
    func __initialize__(self, age) {
        self._age = age
    }
    @public
    func age(self) {
        return self._age
    }
}

io.print(greet("Pycp"))
a = Animal(3)
io.print(a.age())   # 类外访问 public 方法正常
# a._age 在类外访问会抛 AttributeError（private 成员）
```

叠加装饰器（同一目标可连续书写多个装饰器，装饰器之间与目标之间均允许换行）：

```
import io
from classtools import readonly

@private
@readonly
func secret() {
    return 5
}

@private
@readonly
class Box {
    @private
    @readonly
    value = 3
}

@readonly
@private
CONST = 7
```

叠加顺序与 Python 一致：**最靠近被装饰对象的装饰器最先应用**，
即 `@d1` 换行 `@d2` 换行 `target` 等价于 `d1(d2(target))`。
函数、变量（`@d1` 换行 `@d2` 换行 `NAME = value`）、类与类成员（成员变量 / 方法）
均支持叠加；类成员上 `@private` 与 `@readonly` 可同时生效。

访问控制装饰器（`readonly` / `private` / `public`，来自 `pycp` 或 `classtools`）
**也可以直接调用**，效果与 `@` 语法糖一致（等价形式）：

```
import pycp
from classtools import private, public, readonly

secret = 1
shown = 2
CONST = 3

secret2 = 4
func helper() { return 5 }

private(secret)        # 等价于 @private secret = 1（跨模块不可见）
public(shown)          # 等价于 @public  shown = 2
private(helper)        # 等价于 @private func helper() {...}
readonly(CONST)        # 等价于 @readonly CONST = 3（不可重赋值）
```

直接调用时实参必须是**模块顶层名字**（裸标识符，非当前函数局部名）：
编译器在调用后为该名字登记绑定级属性；若实参是属性/下标/表达式结果，或该名字是
函数局部变量，则只保留值级效果（对象冻结等），不产生绑定级声明。

### 文件扩展名

| 扩展名 | 类型 | 说明 |
|--------|------|------|
| `.pycp` | 源代码 | 文本格式，Pycp 语言源码 |
| `.cpycp` | 字节码 | 二进制格式，编译产物，需由 Pycp VM 反序列化后执行（类似 `.pyc`） |

> 注意：`.cpycp` 是编译后的字节码（类似 Python 的 `.pyc`），而非可执行文件。它必须由内嵌 Pycp VM 的 `pycp` 程序反序列化后执行。

### 模块导入查找顺序

`import xxx` 按下表顺序查找，**命中即终止**，不再继续下探：

| 优先级 | 查找位置 | 候选形式（每层内部 `.pycp` 源码优先于同名动态库） |
|--------|----------|----------|
| 1 | 进程级缓存 / 当前程序已链接的符号（`PycpModule_xxx` + AOT 静态注册表） | 编译产物 |
| 2 | 当前工作目录（cwd） | `xxx.pycp` → `xxx.so` |
| 3 | 脚本所在目录（为 `.` 时与 cwd 重合而跳过） | `xxx.pycp` → `xxx.so` |
| 4 | 可执行文件所在目录的 `stdlib/` | `xxx.pycp` → `xxx.so` |
| — | 全部未命中 | 抛 `ImportError`（附各层候选目录诊断） |

说明：

- **本地优先 + 源码优先**：文件系统层（2/3/4）由近及远，且每层内 **`.pycp` 源码优先于
  同名动态库**——"所见即所得"，改源码即生效；也因此允许在 `stdlib/` 下放置与内置
  扩展同名的 `.pycp` 源码来覆盖内置扩展（与 Python 的"扩展优先"相反，是 Pycp 的
  刻意选择）。
- **第 1 层主要针对编译产物**。AOT（`--emit-cpp`）生成的每个模块都导出
  `PycpModule_<模块名>`（或经静态注册表登记），因此「编成静态库链接进主程序」、
  「编成动态库运行期加载」都能正常 `import`。该层由两级机制互为兜底：全局符号查找
  （`dlsym`）与静态初始化注册表。
- **第 4 层的 `stdlib/` 随可执行文件定位**：解释器取 `pycp` 自身所在目录，AOT 编译出的
  独立程序取该程序自身所在目录。
- **`.pycp` 源码需要宿主支持**。解析器位于 frontend，解释器（`pycp`）已注册编译器钩子，
  故第 2/3/4 层可直接加载 `.pycp`；AOT 生成的独立程序未注册该钩子，只认动态库——这正是
  "转译产物不能调用未转译源码"的机制：转译期 `--show-imports` 会列出全部 import 的解析
  结果，未解析到源码的依赖将按运行期动态库处理。
- **第 1 层命中符号但初始化返回空**时直接抛 `ImportError`，不继续下探——符号存在即表明
  明确的链接意图，静默回退会掩盖「静态库成员被链接器丢弃」这类问题。

> **关于静态库**：把 `b.gen.cpp` 编成 `.a` 再链接时，生成代码会自动为依赖模块插入**链接拉入桩**（在入口翻译单元显式引用 `PycpModule_b`），强制链接器拉入对应目标文件。否则未被引用的成员会被整体丢弃，表现为「编译链接全部成功，运行时却报 `ImportError`」。因此**无需** `-Wl,--whole-archive`；若把多个依赖分别编成静态库，仍须遵守静态库的标准链接顺序（依赖方在前、被依赖方在后）。

## 配置说明

本项目通过 CMake 变量（option）控制构建行为，无需环境变量或配置文件。

### 顶层构建选项

顶层 `CMakeLists.txt` 在引入 backend 时设定以下选项（默认同时构建静态库与
动态库；产物输出到 `build/backend/`）：

| 变量 | 值 | 说明 |
|------|-----|------|
| `BUILD_RUNTIME_STATIC` | `ON` | 构建 `libPycpRuntime.a` 静态库 |
| `BUILD_RUNTIME_SHARED` | `ON` | 构建 `libPycpRuntime.so` 动态库 |
| `BUILD_PYTHON_BINDING` | `OFF` | 不构建 Python 绑定 |
| `BUILD_TEST` | `OFF` | 不构建测试程序 |

`pycp` 与三个标准库扩展默认【动态链接】运行时——全进程只存在一份运行时
状态（GC 池、小整数池、扩展句柄缓存），避免每个扩展 `.so` 内嵌一份副本。
静态库仍一并产出，供 AOT 生成代码选择静态链接。想退回纯静态链接：

```bash
cmake -S . -B build -DBUILD_RUNTIME_SHARED=OFF
```

### 最终产物目录（dist）

构建完成后，所有产物会被统一收集到最终输出目录（默认 `build/dist/`），
该目录自包含、可直接运行或部署，无需再去其它路径查找依赖：

```
build/dist/
├── pycp                     主程序（解释器 / 编译器 / REPL）
├── stdlib/                  标准库原生扩展，运行时固定在此目录查找
│   ├── io.so
│   ├── pycp.so
│   └── classtools.so
├── lib/                     运行时库
│   ├── libPycpRuntime.so    动态库（默认构建，pycp 动态链接它）
│   ├── libPycpRuntime.a     静态库（AOT 生成代码静态链接用）
│   ├── libPycpExt_io.a      原生扩展静态库（`--emit-cpp --static` 用）
│   ├── libPycpExt_pycp.a
│   ├── libPycpExt_classtools.a
│                            Windows 下另有运行时 DLL 与导入库（MinGW：
│                            libPycpRuntime.dll + libPycpRuntime.dll.a；
│                            MSVC：PycpRuntime.dll + PycpRuntime.lib）
├── include/                 后端头文件（AOT / 原生扩展编译用）
│   └── Pycp*.hpp（扩展作者入口：PycpExtension.hpp）
└── BUILD_INFO.txt           产物溯源信息（平台 / 构建类型 / 编译器 / 版本）
```

> Windows 例外：加载 DLL 只搜索可执行文件所在目录、不搜索 `lib/`，因此
> 运行时 DLL 会在 dist 根目录再放一份（与 `lib/` 那份同源）。其文件名随
> 编译器而异——MinGW 为 `libPycpRuntime.dll`，MSVC 为 `PycpRuntime.dll`。

收集步骤默认随 `ALL` 自动执行，也可单独触发（幂等，只覆盖同名文件、不清空目录）：

```bash
cmake --build build -j              # 构建并自动收集
cmake --build build --target pycp-dist   # 仅重新收集
```

可通过 CMake 变量调整输出位置与布局：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `PYCP_DIST_DIR` | `build/dist` | 最终输出目录，可指向任意路径 |
| `PYCP_DIST_PLATFORM_SUBDIR` | `OFF` | `ON` 时输出到 `dist/<system>-<arch>/`，多平台 / 多架构产物共存不冲突 |
| `PYCP_DIST_WRITE_BUILD_INFO` | `ON` | 是否生成 `BUILD_INFO.txt` |

```bash
# 输出到自定义位置，并按平台分层
cmake -S . -B build -DPYCP_DIST_DIR=/opt/pycp -DPYCP_DIST_PLATFORM_SUBDIR=ON
```

> 注意：`stdlib/` 目录名不可更改——运行时按“可执行文件所在目录 + `/stdlib/`”
> 查找 `io.so` / `pycp.so` / `classtools.so`，因此分发时请整目录带走。

各产物靠 rpath 相互定位，脱离构建树仍可运行：

| 产物 | rpath | 解析到 |
|------|-------|--------|
| `dist/pycp` | `$ORIGIN`（优先）/ `$ORIGIN/lib` / `$ORIGIN/backend` | `dist/libPycpRuntime.so`（或回退 `dist/lib/libPycpRuntime.so`） |
| `dist/stdlib/*.so` | `$ORIGIN/../`（优先）/ `$ORIGIN/../backend` | `dist/libPycpRuntime.so`（与 `pycp` 共用同一份） |

因此把整个 `dist/` 拷到任意路径（或拷到别的机器同架构上）都能直接运行。
`cmake --install` 沿用同一套布局：`bin/pycp`、`bin/stdlib/`、`lib/`、`include/`。

### backend 独立构建选项

进入 `backend/` 单独构建时，可自由控制以下选项：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `BUILD_RUNTIME_STATIC` | `ON` | 构建 `PycpRuntime` 静态库 |
| `BUILD_RUNTIME_SHARED` | `OFF` | 构建 `PycpRuntime` 动态库 |
| `BUILD_PYTHON_BINDING` | `OFF` | 构建 `PycpRuntime4Python` Python 绑定 |
| `BUILD_TEST` | `ON` | 构建 `test_pycp` 测试程序 |

示例：

```bash
# 构建完整运行时（含动态库 + 测试）
cd backend
cmake -S . -B build -DBUILD_RUNTIME_SHARED=ON
cmake --build build -j

# 构建 Python 绑定
cmake -S . -B build -DBUILD_PYTHON_BINDING=ON
```

### 字节码格式

`.cpycp` 采用稳定的二进制格式（小端）：

```
[Magic 4B]["CYCP"] [Major 2B] [Minor 2B] [flags 4B]
[常量池] [符号表] [代码对象表]
```

- 每个段以 `uint32` 长度前缀自描述，旧加载器可跳过未知段。
- 栈式字节码指令为 `1 字节 opcode + LEB128 操作数`。
- 格式版本中 Major 变更 = 不兼容，Minor 变更 = 向后兼容。

## 项目结构

```
pycp/
├── CMakeLists.txt          # 顶层总构建脚本（一键编译整个项目）
├── PycpMain.cpp            # 主程序入口（编译 / 解释执行 CLI）
├── cmake/                  # 构建辅助脚本
│   └── PycpDist.cmake      # 最终产物收集（由 pycp-dist 目标构建期调用）
├── example.pycp            # 示例源码（含尚未支持的高级语法）
├── frontend/               # 前端：词法/语法分析、AST、代码生成、AOT
│   ├── CMakeLists.txt      # 独立构建入口（产出 pycp，不含 stdlib 原生扩展）
│   ├── include/
│   │   ├── PycpAstNode.hpp     # AST 节点定义
│   │   ├── PycpCodegen.hpp     # 代码生成器接口（AST -> 字节码）
│   │   └── aot/                 # AOT 模块化子目录（单一职责 + 纯数据接口）
│   │       ├── PycpAot.hpp            # 字节码 -> C++ 源码翻译（逻辑不变）
│   │       ├── PycpAotSdkLocator.hpp  # SDK 定位与校验
│   │       ├── PycpProjectSpec.hpp    # 项目描述数据模型 + 校验
│   │       ├── PycpBuildScriptGenerator.hpp # 生成器抽象接口 + 注册表
│   │       ├── PycpCMakeGenerator.hpp  # CMakeLists 渲染器
│   │       └── PycpAotProject.hpp     # 编排入口
│   └── src/
│       ├── PycpLexer.l         # Flex 词法规则
│       ├── PycpParser.y        # Bison 语法规则
│       ├── PycpAstNode.cpp     # AST 节点实现
│       ├── PycpCodegen.cpp     # 代码生成器实现
│       └── aot/                 # 与 include/aot 一一对应的实现
│           ├── PycpAot.cpp
│           ├── PycpAotSdkLocator.cpp
│           ├── PycpProjectSpec.cpp
│           ├── PycpBuildScriptGenerator.cpp
│           ├── PycpCMakeGenerator.cpp
│           └── PycpAotProject.cpp
└── backend/                # 后端：运行时（对象模型 / GC / VM / 字节码）
    ├── CMakeLists.txt      # 独立构建入口（静态/动态库 + 测试）
    ├── include/            # 运行时头文件（13 个 .hpp）
    │   ├── Pycp.hpp            # 运行时总入口头文件
    │   ├── PycpObject.hpp      # 对象基类
    │   ├── PycpInteger.hpp     # 整数类型
    │   ├── PycpString.hpp      # 字符串类型
    │   ├── PycpNone.hpp        # None 类型
    │   ├── PycpFunction.hpp    # 函数类型
    │   ├── PycpGC.hpp          # 垃圾回收
    │   ├── PycpABI.hpp         # ABI 接口
    │   ├── PycpManager.hpp     # 运行时管理器
    │   ├── PycpBytecode.hpp    # 字节码格式 / 序列化
    │   └── PycpBytecodeVM.hpp  # 虚拟机
    └── src/                # 运行时源文件（11 个 .cpp）
├── stdlib/                # 标准库（C++ 原生动态库，import 时动态加载）
│   ├── CMakeLists.txt     # 标准库统一构建入口（逐个 add_subdirectory）
│   ├── io/                # io 标准库（io.so）：标准流对象与 print/input
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── io.hpp             # 模块名与入口声明
│   │   │   └── PycpFile.hpp       # File（stdin/stdout/stderr）
│   │   └── src/
│   │       ├── io.cpp             # 模块装配（stdin/stdout/stderr、print/input）
│   │       ├── PycpFile.cpp       # File 实现（write/readline）
│   │       └── FileFromStream.cpp # File::FromStream 工厂
│   ├── pycp/              # pycp 标准库（pycp.so）：类型转换与可见性装饰器
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   └── pycp_stdlib.hpp    # 模块名与入口声明
│   │   └── src/
│   │       └── PycpModule.cpp     # String/Integer/Object 与 public/private
│   └── classtools/        # classtools 标准库（classtools.so）：类工具
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── classtools.hpp     # 模块名与入口声明
│       └── src/
│           └── classtools.cpp     # super 与 public/private
├── examples/              # 示例代码
├── tests/                 # 测试源码（.pycp）
└── LICENSE
```

### 目录职责说明

| 目录 | 职责 |
|------|------|
| `PycpMain.cpp` | 真正的 `main()` 入口，统一前端解析 + 后端编译/执行 |
| `frontend/` | 前端：词法/语法分析、AST、代码生成、AOT |
| `backend/` | 后端：运行时库（对象模型、GC、VM、字节码），**不依赖前端** |
| `stdlib/` | 标准库：C++ 原生动态库（`io` / `pycp` / `classtools`），`import` 时由 VM 动态加载 |
| 顶层 `CMakeLists.txt` | 全项目统一构建入口，产出 `pycp` 可执行文件 |
| `frontend/CMakeLists.txt` | 独立构建 `pycp`（不构建 stdlib 原生扩展，`import io` 等不可用） |
| `backend/CMakeLists.txt` | 独立构建运行时库与 `test_pycp` 测试 |

> 各标准库子库统一采用 `<name>/include`（头文件）+ `<name>/src`（源文件）+ `<name>/CMakeLists.txt` 的目录结构，编译为独立动态库（`io.so` / `pycp.so` / `classtools.so`），输出到 `build/stdlib/`。

### 模块独立构建

除了顶层一键编译，各模块也可独立构建：

```bash
# 仅构建前端（会连带构建 backend 运行时，但不构建 stdlib 原生扩展）
cd frontend
cmake -S . -B build
cmake --build build -j
./build/pycp ../example.pycp

# 仅构建后端运行时库 + 测试
cd backend
cmake -S . -B build
cmake --build build -j
```

## 最近更新

- **访问控制装饰器可直接调用（`@` 语法糖的等价形式）**：`pycp.readonly(a)` /
  `pycp.private(f)` / `pycp.public(x)`（含 `classtools` 同名函数、`from` 导入后的裸名、
  模块别名 `p.readonly`）与 `@readonly a = ...` 效果一致。实参为模块顶层裸标识符时，
  编译器在 `CALL` 后补发 `MARK_BINDING`，按当前值标记登记（或清除）该名字的
  绑定级属性：`readonly` 后重赋值被拒、`private` 后跨模块不可见、`public` 可重新公开。
  实参为局部变量/表达式时仅保留值级效果（解释器与 AOT 一致）。
- **修复「被导入模块的函数读不到本模块全局名」**：`VM::call` 此前一律把入口命名空间的
  globals 作为调用环境，导致 `mod.pycp` 中 `G = 42; func get() { return G }` 被 import 后
  调用报 `NameError`（AOT 生成代码本就使用本模块 `g_mod_ns`，两者不一致）。现按
  「字节码模块 → 运行时模块对象」映射取函数所属模块的命名空间（REPL 语句模块回退到
  入口命名空间）。

- **三处解析/运行缺陷修复**：
  1. **`.cpycp` 加载后类方法名/成员名丢失**：序列化时类名、父类名、成员名、方法名、
     代码对象名与函数局部名都以「符号表索引」写入，而编译器只 intern 了指令操作数
     用到的名字，缺失者被写成索引 0（`<module>`），于是 `__initialize__`、
     `__init_defaults__` 等隐式方法在反序列化后消失（实例属性永不初始化、
     `self.x` 报 `AttributeError`）。现改为序列化前把待写名字统一追加到符号表副本
     末尾（只追加，既有索引不变）。
  2. **闭包捕获的局部变量在定义帧退出时被释放**：此前 VM/AOT 在函数返回时无条件
     释放并清空局部槽，即使该环境已被闭包捕获——闭包随后读到已释放对象或越界读取
     （典型表现：装饰器返回「捕获被装饰函数」的闭包并回写同名全局时段错误）。
     现 `Environment` 新增 `keep_names`（闭包自由变量名集合）与析构释放，帧退出改用
     `Environment_ReleaseFrame`（保留被捕获槽位，其余立即释放）；新增
     `CodeObject::free_names`（编译/反序列化后由 `ComputeFreeNames` 传递性计算，
     不参与序列化）与 ABI `Environment_KeepNames` / `Environment_KeepNamesOfCodeObject`，
     解释器与 AOT 两条路径行为一致且不引入引用环泄漏。
  3. **匿名 `repeat N` / `repeat from a to b` 嵌套时计数器互相覆盖**：匿名循环此前
     共用固定变量名 `$repeat`，内层循环会把外层计数器一起重置（外层 N 大于内层 N 时
     外层永不退出 = 挂死）。现每个匿名循环使用唯一临时名。
  4. **REPL 中 `@readonly` / `@private` 绑定不生效**：REPL 使用无入口模块的 VM
     （`VM(nullptr)`），全局命名空间是独立 map 且未登记归属模块，而 `MARK_BINDING`
     与只读重赋值检查都依赖「globals → Module」映射，故 `@readonly a = 1` 后
     `a = 2` 不会被拒绝。现 REPL 同样创建入口模块承载全局命名空间并登记该映射
     （键为 `<entry>`），`@readonly` / `@private` 在交互模式下与文件模式行为一致。

- **叠加装饰器（stacked decorators）**：同一目标现可连续书写多个装饰器
  （`@d1` 换行 `@d2` 换行 目标，装饰器之间与目标之间均允许换行），应用顺序与
  Python 一致——最靠近目标的装饰器最先应用（`@d1` 换行 `@d2` 换行 `f` 等价于
  `f = d1(d2(f))`）。支持函数定义、变量声明（`NAME = value`）、类定义与类成员
  （成员变量 / 方法），单一装饰器写法与行为完全不变。实现：语法层新增
  `decorator_list` 非终结符，AST 五个节点的 `decorator` 字段改为 `decorators`
  列表，codegen 按「源码顺序压栈 + 连续 N 次 `CALL 1`」实现；`ClassDef` 的成员 /
  方法装饰器槽位由「每成员单槽位」改为「连续槽位分组」，新增 ABI
  `ApplyDecoratorChain` / `ApplyDecoratorMemberFlagsChain` 供 VM 与 AOT 共用
  （由内向外串联，`private` / `readonly` 等标志自然累加）。字节码 Minor 版本
  1 → 2（Minor 向后兼容：`minor < 2` 仍按旧单槽位格式读取）。

- **AOT 支持 `--static` / `--shared` 两种链接模式 + 一致性回归测试**：
  `--emit-cpp` 默认 `--shared`（动态链接，保持原有行为零改动）；`--static`
  改为静态链接运行时与三个原生扩展（io / Pycp / classtools），产物为单个
  自包含可执行文件。static 模式下生成器对 exe 发射 `target_compile_definitions
  PRIVATE PYCP_STATIC`（Windows 避免 `__imp__...` undefined reference），用
  全路径链接 `libPycpRuntime.a` 与 `libPycpExt_*.a`，并追加一份
  `__pycp_builtin_modules.gen.cpp` 注册桩（复用既有 `RegisterAotModule`）让
  静态扩展符号被链接器强制拉入；shared 分支逐行保留，二者互不干扰。
  dist 在 `lib/` 下新增三份静态扩展库 `libPycpExt_io.a` /
  `libPycpExt_pycp.a` / `libPycpExt_classtools.a`，SDK 定位器扫描 `lib/` 下
  所有 `PycpExt_*` 推导内置扩展清单（新增子库零改动）。新增
  `cmake/PycpAotEquivalence.cmake` + `pycp-aot-equiv` 目标，对全部 `tests/`
  用例分别跑解释器与 AOT 产物（shared 与 static 各一次），地址归一化后逐
  字节比对 stdout / stderr / 退出码，有差异即 `FATAL_ERROR`，用于收敛两套
  实现的行为漂移。
- **AOT 引用计数失衡修复（`GET_ITEM` 漏 `Incref`）**：AOT 生成的
  `GET_ITEM` 从容器取元素（`GetItem` 返回 **Borrowed**）压栈时漏了
  `Incref`，导致后续消费路径对容器共享元素多减一次引用——全局常量
  （尤其 Integer 小整数常量）被提前释放，随后 use-after-free，最终在嵌套
  下标 / map 字面量场景抛 `TypeError: unhashable type: Integer`（解释器因
  模块常量池持有常量而免疫）。现 AOT 与解释器一致地 `st.push_back(res);
  Incref(res)`，嵌套下标与容器销毁不再级联释放共享常量。一致性回归已覆盖
  该场景（`tests/containers/map.pycp` 等在 shared / static 下全部通过）。
- **dist 布局调整与运行时默认动态链接**：dist 目录改为标准分层布局——头文件
  归入 `dist/include/`、运行时库（动态库 + 静态库 + Windows 导入库）归入
  `dist/lib/`、主程序留在 `dist/` 根、标准库原生扩展仍留在 `dist/stdlib/`
  （运行时硬约束）。`BUILD_RUNTIME_SHARED` 改为全平台默认 `ON`，`pycp` 与
  三个 stdlib 扩展统一动态链接运行时，全进程只有一份运行时状态；静态库仍
  一并产出供 AOT 静态链接，`-DBUILD_RUNTIME_SHARED=OFF` 可退回纯静态。
  配套改动：①三个 stdlib 子库加 `BUILD_RPATH $ORIGIN/../lib`（构建树为
  `$ORIGIN/../backend`），`pycp` 改 `$ORIGIN/lib`，使 dist 脱离构建树自包含；
  ②Windows 下 `PycpRuntime.dll` 在 dist 根额外放一份（Windows 不搜索 `lib/`）；
  ③`backend` 的 `PycpRuntime_shared` 补 `${CMAKE_DL_LIBS}`；
  ④`install` 规则镜像新布局（`bin/`+`bin/stdlib/`+`lib/`+`include/`）。
  修复两处此前被静态链接掩盖的问题：`-Wl,--export-all-symbols` 是 MinGW
  专有选项却应用于所有非 MSVC 编译器（Linux 下报 `unrecognized option`），
  现限定 `MINGW`；`BUILD_RPATH` 曾指向源码目录 `backend/bin`。AOT 编译命令
  相应改为 `-I build/dist/include -L build/dist/lib`。
- **最终产物目录（`build/dist`）**：新增 `pycp-dist` 目标，构建结束自动把
  `pycp` 可执行文件、`stdlib/*.so`（io / Pycp / classtools）、运行时库与全部
  后端头文件收集到同一个自包含目录，开箱即可运行与部署。收集逻辑集中在
  `cmake/PycpDist.cmake`（建目录 / 覆盖更新 / 路径规范化 / 源文件缺失即报错），
  标准库子库通过 `PYCP_STDLIB_TARGETS` 全局属性注册，新增子库无需改动顶层
  脚本。可用 `PYCP_DIST_DIR` 改输出位置、`PYCP_DIST_PLATFORM_SUBDIR=ON` 按
  `<system>-<arch>` 分层放置多平台产物。原 `build/bin` SDK 目录已移除。
- **VM 跳转越界检查修复「跳回指令 0」边界**：当循环恰好是代码对象的第一条指令时（REPL 中先 `import io` 再单独粘贴 `repeat if True{...}`、或文件/函数体以循环开头），循环末尾向后跳转的目标为指令 0。此前 VM 五处跳转 opcode（`FOR_ITER`/`JUMP`/`JUMP_IF_FALSE`/`JUMP_IF_TRUE`/`BREAK`）把 int64 中间结果强转 `size_t` 后再判越界，向后跳回指令 0 时中间值 `-1` 回绕成 `0xFFFF...F`，恒 `>= pc_end` 而误报 `jump out of range`。现改为在 int64 域计算并校验最终目标 `target ∈ [0, pc_end)`，再赋 `pc = target - 1`（`target=0` 时 `size_t` 回绕，主循环 `++pc` 后合法到达指令 0）；真正越界的跳转仍正确报错，字节码格式与 codegen 不变。
- **REPL 会话级连续行号**：REPL 错误行号不再每次输入从 1 重新计数，而是按整个会话的物理输入行连续递增（含空行、续行、编译失败的行），对齐 Python 交互模式。实现上为 `parse_statement`/`compile_statement` 增加 `initial_line` 参数，由 REPL 主循环维护会话行号计数并传入；词法/语法/语义/运行时错误均自动获得正确的会话行号。同时修复文件模式 `parse()` 不重置行号导致的 import 子模块报错行号累积问题（此前 2 行的子模块错误会报 `line 5`，现正确报 `line 2`）。
- **运行时类型命名去 `Object` 后缀**：`ListObject`→`List`、`ClassObject`→`Class`、
  `InstanceObject`→`Instance`、`ModuleObject`→`Module`、`FileObject`→`File`，ABI 工厂/
  类型操作函数统一改为对应类型的静态方法（`Integer::FromLong`、`String::FromCString`、
  `List::New` 等），旧名与旧自由函数已完全移除。
- **AOT（`--emit-cpp`）指令翻译补齐**：新增 `LOAD_ATTR`/`STORE_ATTR`/`BUILD_LIST`/
  `GET_ITEM`/`SET_ITEM`/`MAKE_CLASS` 翻译，闭包通过 `BytecodeFunction` native 模式落地，
  内建库导入经 `LoadNativeModule` 缓存，生成的 C++ 可经 g++ 真正编译运行。
- **`--emit-cpp` 升级为「生成可直接编译的 CMake 项目」+ AOT 模块化重构**：
  `--emit-cpp hello.pycp` 现输出一个项目目录（默认 `./hello/`，`-o <dir>` 指定），
  内含入口 `__pycp_main.gen.cpp`、各依赖 `<name>.gen.cpp` 与一份开箱即用的
  `CMakeLists.txt`。该 CMake 项目动态链接 `PycpRuntime`、Linux 加 `-rdynamic`、
  设置 `BUILD_RPATH`/`INSTALL_RPATH`、构建期（`POST_BUILD`）复制 `stdlib/` 与 `lib/`
  使产物自包含，SDK 路径以 `CACHE PATH PYCP_DIST` 写入（换机器 `-DPYCP_DIST=<path>` 覆盖）。
  AOT 代码收敛到 `frontend/{include,src}/aot/`，按单一职责拆分为 SDK 定位、项目描述、
  生成器接口+注册表、CMake 生成器、编排五个模块，模块间仅经纯数据 `ProjectSpec` 通信；
  生成器经注册表自注册，后续加 Makefile/Ninja 生成器零改动既有模块。对应的顶层与
  `frontend/` 两套 `CMakeLists` 的 `AOT_SOURCES` 同步更新（并补齐 frontend 独立入口
  此前缺失的 `escape_handler.cpp` 与 preprocessor 源）。
- **解释执行 bug 修复**：修复 `STORE_ATTR` 未释放 `pop` 传入值的引用计数导致的退出时
  double free；`Module` 新增 `foreach_ref` 遍历命名空间使模块级对象在 GC 标记阶段可达，
  避免误回收。
- **向后跳转修复**：修复 VM 中 `FOR_ITER`/`JUMP`/`JUMP_IF_FALSE`/`JUMP_IF_TRUE`/`BREAK`
  将负偏移 operand（循环回跳）强转为无符号导致越界抛 `jump out of range` 的问题；现改用
  有符号算术计算目标 pc，使 `repeat if True` 等恒真无限循环及 while/range/foreach/break
  控制流正常执行。

## 贡献指南

我们欢迎任何形式的贡献，包括但不限于 Bug 修复、功能实现、文档改进与测试补充。

### 参与流程

1. **Fork 本仓库**，并克隆到本地。
2. **创建分支**：为你的改动创建一个描述性分支。

   ```bash
   git checkout -b feature/my-feature
   ```

3. **提交改动**：保持提交信息清晰、原子化（一个小改动一个提交）。
4. **构建并测试**：确保改动通过编译与测试。

   ```bash
   cmake -S . -B build
   cmake --build build -j
   # 运行测试验证
   ./build/pycp hello.pycp
   ```

5. **发起 Pull Request**：详细描述改动的动机、内容与影响范围。

### 代码规范

- 使用 C++17 标准，遵循项目现有的命名空间约定（`Pycp::Ast`、`Pycp::BC`、`Pycp`）。
- 新功能请优先补充或更新对应的测试用例。
- 修改字节码格式时，需同步更新 `backend/include/PycpBytecode.hpp` 中的版本号（不兼容变更递增 Major，兼容变更递增 Minor）。

### 提交信息约定

```
<type>: <简短描述>

<详细说明（可选）>
```

`type` 可选值：`feat`（新功能）、`fix`（修复）、`docs`（文档）、`refactor`（重构）、`test`（测试）、`chore`（杂项）。

## 许可证

本项目当前**未附带 LICENSE 文件**，默认保留所有权利（All Rights Reserved）。如需使用、修改或分发，请先联系项目维护者获取授权。

若你计划开源本项目，建议在根目录添加合适的开源许可证文件（如 MIT、Apache-2.0、GPL-3.0 等），并在此处更新对应说明。

## 免责声明

本项目仍处于开发阶段，`--emit-cpp`（AOT）已实现真正的字节码指令翻译（输出依赖 PycpABI 的 `.cpp` 源文件），覆盖函数调用、类定义、属性访问、列表与下标、闭包（native 模式）等完整指令集。

已支持的面向对象特性（解释执行）：

- **类定义**：`class Name{ ... }`，含成员变量声明、方法定义（`func name(self, ...){}`）。
- **单继承**：`class Child inherits Parent{ ... }`，子类复制父类成员与方法（含可见性），支持 `super()` 调用父类方法。
- **构造与字符串转换**：`__initialize__`（创建实例自动调用）、`__string__`（转字符串时自动调用）。
- **默认字符串表示**：未定义 `__string__` 的对象输出 `<name at 0xADDR>`；函数输出 `<function "name" at 0xADDR>`；类输出 `<class "name">`；类实例输出 `<Name instance at 0xADDR>`；模块输出 `<module "name">`。匿名函数 / 匿名类沿用各自格式，仅名字位置为内部名 `@anonymous`，即 `<function "@anonymous" at 0xADDR>` / `<class "@anonymous">`（其类实例为 `<@anonymous instance at 0xADDR>`）。
- **运算符重载**（魔术方法）：`__addition__` / `__subtraction__` / `__multiplication__` / `__division__` / `__power__` / `__negation__`，及比较 `__less_than__` / `__less_equal__` / `__equal__` / `__not_equal__` / `__greater_than__` / `__greater_equal__`。
- **装饰器语法糖**：`@decorator` 把被装饰对象（函数或任意对象）作为参数传给装饰器函数，用返回值替换原对象。
- **叠加装饰器**：同一目标可连续书写多个装饰器（`@d1` 换行 `@d2` 换行 目标），
  应用顺序与 Python 一致——最靠近目标的装饰器最先应用（`d1(d2(target))`）；
  支持函数、变量（`@d1` 换行 `@d2` 换行 `NAME = value`）、类定义与类成员（成员变量 / 方法）。
- **成员可见性**：`@private` / `@public` 修饰类内成员，控制该成员在类外的访问可见性（方法内部经 `self` 访问不受限）；叠加使用时可与 `@readonly` 等标志同时生效。
- **访问控制装饰器可直接调用**：`pycp.readonly(a)` / `pycp.private(f)` / `pycp.public(x)`（或 `classtools` 同名函数 / `from` 导入后的裸名）与 `@` 语法糖等价，实参须为模块顶层名字。
- **文件级导出**：模块顶层符号默认 public；`@private func foo(){}` 的顶层符号对其他文件 `import` 时不可见。
- **内置库**（`stdlib/` 目录，C++ 原生实现）：
  - `io`：`io.stdin` / `io.stdout` / `io.stderr` 文件对象（`write` / `readline` 方法），以及 `io.print(value)`（输出内容后自动换行）与 `io.input(prompt)`（打印提示后读取一行）。
  - `pycp`：`pycp.String(x)` / `pycp.Integer(x)` 类型转换类、`pycp.Object` 基类，以及 `pycp.public` / `pycp.private` 可见性装饰器函数。
    - **`pycp.argv`**：命令行参数列表（`List[String]`），语义对齐 Python 的 `sys.argv`。
      - 解释运行（如 `pycp test.pycp arg1 arg2`）：`pycp.argv == ["test.pycp", "arg1", "arg2"]`——
        列表**不含 `pycp` 可执行文件本身**，仅含脚本名与传入参数。
      - AOT 编译产物（如 `./test arg1 arg2`）：`pycp.argv == ["<程序完整路径>", "arg1", "arg2"]`，
        `argv[0]` 为程序路径（对齐 `sys.argv[0]`）。
      - REPL（无参数 `pycp`）：空列表 `[]`。
  - `classtools`：`classtools.super()`（返回父类）、`classtools.public` / `classtools.private`（可见性装饰器函数，与 pycp 库功能一致）。
- 本版起不注入任何内建函数（不导入库时命名空间仅含用户定义内容）。

暂不支持：类型注解（`x: int`）、`map` 字面量、`Pointer`、多继承。AOT 后端已支持类定义、属性、列表与下标翻译。
