# Pycp

Pycp 是一个 **Python-like 语言** 的编译器与解释器，使用 C++17 实现。它采用与 Python 相近的语法，自研词法/语法分析器、AST、代码生成器、栈式字节码虚拟机（VM）与序列化字节码格式。

`.pycp` 源码被编译为自定义字节码 `.cpycp`（类似 Python 的 `.pyc`），随后由内嵌的 Pycp VM 解释执行；也可以经 AOT 翻译为依赖 Pycp 运行时的 C++ 项目，编译成独立可执行程序。

## 特性

| 类别 | 说明 |
|------|------|
| 前端 | Flex 词法分析 + Bison 语法分析 → AST → 字节码（Codegen） |
| 运行时 | 引用计数 + 标记-清除 GC、对象模型、ABI 接口 |
| 虚拟机 | 栈式字节码 VM：函数调用、闭包、类、继承、装饰器、控制流 |
| 字节码 | `.cpycp` 二进制格式（小端 + LEB128），可序列化 / 反序列化 |
| 标准库 | C++ 原生扩展（`io` / `pycp` / `classtools` / `moduletools` / `ast` / `bytecode` / `filesystem` / `maths` / `json`） |
| 模块系统 | 源码优先的动态 `import`、模块文件夹（package）、原生扩展加载 |
| AOT | `pycp -m aot` 把字节码逐指令翻译为依赖 Pycp 运行时的 C++ 项目（真正指令翻译） |

## 依赖与构建

### 依赖

| 依赖 | 版本要求 | 用途 |
|------|---------|------|
| CMake | ≥ 3.15 | 构建系统 |
| C++ 编译器（GCC/Clang/MSVC） | 支持 C++17 | 编译源码 |
| Flex | 任意 | 词法分析器生成 |
| Bison | 任意 | 语法分析器生成 |

Ubuntu / Debian 一键安装：

```bash
sudo apt update
sudo apt install cmake build-essential flex bison
```

Windows 需安装 [win_flex_bison](https://github.com/lexxmark/winflexbison/releases) 并将其所在目录加入 `PATH`，或显式指定 `-DBISON_EXECUTABLE=... -DFLEX_EXECUTABLE=...`。

### 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

构建完成后可执行文件位于 `build/pycp`，最终自包含产物由 `pycp-dist` 目标收集到 `build/dist/`。

## 快速开始

创建 `hello.pycp`：

```
import io
import pycp

a = 23 - 5 * 7
io.print(a)                 # -12
io.print(2 ** 3)            # 8
```

运行：

```bash
./build/pycp hello.pycp
```

无参数启动进入交互式 REPL：

```bash
./build/pycp
```

## 命令行接口

```
pycp [options] <input_file>
```

| 选项 | 说明 |
|------|------|
| `-h, --help` | 显示帮助信息 |
| `-i, --interpret` | 解释执行（默认；接受 `.pycp` 或 `.cpycp`） |
| `-c, --compile` | 将 `.pycp` 编译为 `.cpycp` 字节码（不执行） |
| `-b, --bytecode` | 生成 `.cpycp` 字节码（`-c` 的别名） |
| `-o, --output <f>` | 指定输出路径（与 `-c/-b` 为 `.cpycp`；与 `-p` 为 `.pp.pycp`） |
| `-d, --dump` | 查看字节码内容（常量池 / 符号表 / 代码对象 / 指令与行号） |
| `-p, --preprocess` | 预处理 `.pycp`（不执行），输出 `<basename>.pp.pycp` |
| `-m, --module <name> ...` | 把位置参数当作**模块文件夹**运行；其后的参数原样交给包的 `main(argv)` |

AOT 经 `-m aot` 进入：`pycp -m aot <entry.pycp | 模块文件夹> [out_dir] [--compile-* ...]`（用法见 `pycp -m aot --help`）。

## 语言概述

块定界符为花括号 `{}`，与 Python 类似但不用缩进区分作用域。

### 字面量与语法

- 注释：`//` 与 `/* */`；`#` 是**预处理指令**而非注释。
- 字符串：仅双引号 `"..."`；转义 `\n \t \r \0 \\ \" \xHH \uXXXX`。
- 数值：整数 `[0-9]+`、十六进制 `0x..`、浮点 `[0-9]+\.[0-9]+`；后缀 `f/F`→Float、`d/D`→Decimal。
- 运算符：算术 `+ - * /`、幂 `**`、一元负 `-`；比较 `< <= > >= == !=`。
- 无逻辑运算符 `and`/`or`/`not`（布尔条件靠嵌套 `if` 组合）；无 `//` 整除（用 `maths.floor(a/b)`）。

### 控制流与函数

- `if / elif / else`
- 循环：`repeat if cond` / `repeat N` / `repeat from a to b` / `repeat ... by s`（含端点），`break` 跳出（无 `continue`、无 `while`）
- `func name(params) { ... }` 与匿名函数 `func(params) { ... }`
- 默认值形参、`*args`（收集为不可变 `FixedList`）、`**kwargs`（收集为 `Map`）、关键字-only 形参（裸 `*` 之后）
- 闭包（匿名函数捕获外层局部变量）

### 类与对象

- `class Name { ... }` 与实例化，`class Child inherits Parent { ... }` 单继承
- 魔术方法：`__initialize__` / `__string__` / `__raw_string__` 及运算符重载（`__addition__` / `__equal__` 等）
- 装饰器 `@decorator`（支持叠加，最靠近目标者最先应用）；访问控制 `@private` / `@public` / `@readonly`（也可直接调用）

### 容器

- `List`：`[a, b, c]`、下标读写、负索引、`length()`、`+` 拼接
- `FixedList`：不可变定长序列（对应 Python tuple）
- `Map`：`{k: v, ...}` 键值映射（对应 Python dict）

### 模块

```pycp
import foo
import foo as bar
from foo import a, b
```

运行期查找顺序（命中即止）：进程内符号 / AOT 注册表 → cwd → 脚本目录 → 可执行文件同级 `stdlib/`；每层 `.pycp` 源码优先于同名动态库。

**模块文件夹（package）**：一个目录即一个模块，`package.mpycp` 为清单，其余 `.pycp` 为包内子模块（点号全名 `pkg.sub`）。

## 对象系统

内置类型：`Integer`、`String`、`Float`、`Decimal`、`Boolean`（`Integer` 的子类）、`None`、`List`、`FixedList`、`Map`、`Function`、`Class`、`Instance`、`Module`、`File`、`Iterator`。

数值转换用构造器：`pycp.String(x)` / `pycp.Integer(x)` / `pycp.Float(x)` / `pycp.Decimal(x)`。

## 标准库

| 模块 | 说明 |
|------|------|
| `io` | `print` / `input`、`stdin` / `stdout` / `stderr`、`File`（write / read / readline / flush 等） |
| `pycp` | 类型转换构造器、`public` / `private` / `readonly` 装饰器、`argv`、类型内省 |
| `classtools` | `super()`、可见性装饰器 |
| `moduletools` | 包角色声明（`as_program` / `as_library`）、`this()`、AOT `Project` 配置 |
| `maths` | 数值数学（`sqrt` / `gcd` 等） |
| `json` | `load(s)`（字符串 → 对象）与 `dump(obj, indent, sort_keys, separators)`（对象 → 字符串） |
| `filesystem` | 文件系统操作 |
| `ast` | 源码字符串编译（AST 级） |
| `bytecode` | 源码字符串编译（字节码级） |
| `aot` | AOT 编译器（纯 Pycp 实现的包，经 `pycp -m aot` 使用） |

## AOT 编译

```bash
# 将 .pycp 及其全部 import 依赖翻译为一个 CMake 项目目录（默认 ./hello/）
./build/pycp -m aot hello.pycp
# 或指定输出目录
./build/pycp -m aot hello.pycp ./my_project

cd hello && cmake -S . -B build && cmake --build build
./build/hello
```

链接形态由 `--compile-runtime` / `--compile-modules` / `--compile-module:<name>=` 控制（`shared` / `static`）：

- 默认：运行时动态链接，每个依赖模块编译为独立 DLL，运行期按名加载。
- 全静态自包含：`--compile-runtime=static --compile-modules=static`。
- 被 ≥2 个链接目标引用的 `static` 模块会自动提升为 `shared`（`--compile-runtime=static` 与任何动态模块组合直接报错）。

## 项目结构

```
src/
├── object/    对象系统 + GC + 类型系统
├── bytecode/  字节码格式 + 序列化 + dump
├── vm/        执行引擎
├── abi/       稳定 ABI + 原生扩展加载
├── parser/    AST + 词法/语法（Flex/Bison）+ 预处理
├── codegen/   字节码代码生成
├── loader/    模块加载器
├── main/      可执行入口（PycpMain.cpp）
└── stdlib/    原生标准库扩展（io/pycp/classtools/... ）
```

两个库：**PycpRuntime**（object + bytecode + vm + abi）与 **PycpFrontend**（parser + codegen + loader）。

## 测试

```bash
# 解释器回归
bash tests/modules/import_tests/run.sh
# AOT / 解释器一致性对比（shared 与 static 各跑一遍，逐字节比对）
cmake --build build --target pycp-aot-equiv
```
