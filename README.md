# Pycp

Pycp 是一个 **Python-like 语言** 的编译器与解释器，使用 C++17 实现。它采用与 Python 相近的语法，拥有自有的词法/语法分析器、AST、代码生成器、栈式字节码虚拟机（VM）以及序列化字节码格式。

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
| 标准库 | C++ 原生动态库（`io` / `Pycp` / `classtools`），`import` 时动态加载 |
| AOT | 将字节码逐指令翻译为依赖 PycpABI 的独立 C++ 源文件（真正的指令翻译，非骨架占位） |

### 对象命名规范

Pycp 对象的类型判定使用**字符串**（而非枚举），与 ABI 保持一致：

- **内置类型名使用大写**：`None`、`Integer`、`String`、`List`、`Function`、`File`。
- **`class` / `instance` / `module` 不采用统一大写名**，而是返回各自的真实名称：
  - 类 `class a{}` 的类型名即 `a`，其实例类型名也为 `a`；
  - 模块的类型名即模块名（如 `io`）。
- **自定义类型的名称与代码定义时的实际名称完全一致（含大小写）**，不做任何改写。
  例如 `class MyClass{}` 的类型名、`<class "MyClass">`、`<MyClass instance at ...>` 均保留 `MyClass`。
- 匿名函数 / 匿名类使用内部名 `@anonymous`。

### 已支持的语言子集

- 赋值、表达式语句
- 整数 / 字符串 / `None` 字面量
- 算术（`+ - * /`）、幂运算（`**`）、一元取负（`-`）
- 比较（`< <= > >= == !=`）
- `if / elif / else` 条件语句
- 函数定义（`func name(params){...}`）与匿名函数（`func(params){...}`）
- 函数调用、`return`
- 闭包（匿名函数捕获外层局部变量）
- 类定义（`class Name{...}`）与实例化，含 `__initialize__` / `__string__` 等魔术方法
- 单继承（`class Child inherits Parent{...}`）
- 运算符重载（`__addition__` / `__subtraction__` / `__power__` 等魔术方法）
- **列表（List）**：方括号字面量 `[a, b, c]`、下标访问 `obj[key]` 与赋值 `obj[key] = value`、
  负索引、`length()` 方法、`+` 拼接、字符串表示、`Pycp.List(obj)` 转换
  （支持 `__get_item__` / `__set_item__` / `__list__` 魔术方法，自定义类可重载）
- 装饰器语法糖（`@decorator`）：把被装饰对象传给装饰器函数，用返回值替换
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
| `-o, --output <f>` | 指定输出文件路径（配合 `-c/-b/--emit-cpp`） |
| `--emit-cpp` | 将 `.pycp` 翻译为独立 C++ 源文件（AOT 指令翻译，输出 `.cpp`） |
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

**4. 翻译为独立 C++ 源文件（AOT）**

```bash
# 将 .pycp 逐指令翻译为依赖 PycpABI 的 C++ 源文件
./build/pycp --emit-cpp hello.pycp -o hello.gen.cpp

# 编译：头文件在 build/dist/include，运行时库在 build/dist/lib
g++ -std=c++17 -I build/dist/include hello.gen.cpp \
    -L build/dist/lib -lPycpRuntime -Wl,-rpath,'$ORIGIN/lib' -o hello
```

生成的可执行文件运行时仍按「可执行文件同级 `stdlib/`」加载原生模块，
因此请把 `build/dist/lib/` 与 `build/dist/stdlib/` 一并放到 `hello` 旁边
（或改用静态链接 `-L build/dist/lib -l:libPycpRuntime.a`，此时无需 `lib/`）。

生成的 `.cpp` 包含 `main()`，内部将字节码翻译为 `Pycp::Add`/`Sub`/`Call` 等
ABI 调用序列（常量内联为 `g_c[]`，控制流翻译为 `goto`，函数翻译为
`pycp_fn_N`）。它不依赖解释器循环，但编译时仍需链接 `PycpRuntime` 库
（静态 `libPycpRuntime.a`，或动态 `libPycpRuntime.dll` + 导入库 `libPycpRuntime.dll.a`；Linux/macOS 下为 `libPycpRuntime.so`）。

> 说明：AOT 已实现完整的指令翻译（含类定义 `MAKE_CLASS`、属性
> `LOAD_ATTR`/`STORE_ATTR`、列表字面量与下标 `BUILD_LIST`/`GET_ITEM`/`SET_ITEM`、
> 函数调用等）。闭包通过 `BytecodeFunction` 的 native 模式落地，匿名函数捕获
> 外层局部变量后翻译出的 C++ 与原生函数一致。生成的代码需链接 `PycpRuntime`
> 库（静态 `libPycpRuntime.a`，或动态 `libPycpRuntime.dll` + 导入库 `libPycpRuntime.dll.a`；Linux/macOS 下为 `libPycpRuntime.so`）。

**5. 一个最小示例**

创建 `hello.pycp`：

```
import io
import Pycp

a = 23 - 5 * 7
io.stdout.write(Pycp.String(a))
io.stdout.write("\n")
io.stdout.write(Pycp.String(2 ** 3))
io.stdout.write("\n")
```

运行：

```bash
./build/pycp hello.pycp
# 输出：
# -12
# 8
```

**6. 标准输入输出与可见性装饰器示例**

`io.print` / `io.input`（对齐 Python3 单参数语义）：

```
import io

io.print("hello")           # 输出 "hello" 并自动换行
name = io.input("Enter: ")  # 打印提示（不换行）后读取一行
io.print("Hello " + name)
```

装饰器与成员可见性（`public` / `private` 可从 `Pycp` 或 `classtools` 导入）：

```
import Pycp
import io
from Pycp import public, private

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

### 文件扩展名

| 扩展名 | 类型 | 说明 |
|--------|------|------|
| `.pycp` | 源代码 | 文本格式，Pycp 语言源码 |
| `.cpycp` | 字节码 | 二进制格式，编译产物，需由 Pycp VM 反序列化后执行（类似 `.pyc`） |

> 注意：`.cpycp` 是编译后的字节码（类似 Python 的 `.pyc`），而非可执行文件。它必须由内嵌 Pycp VM 的 `pycp` 程序反序列化后执行。

### 模块导入查找顺序

`import xxx` 按下表顺序查找，**命中即终止**，不再继续下探：

| 优先级 | 查找位置 | 候选形式 |
|--------|----------|----------|
| 1 | 当前程序已链接的符号 | `PycpModule_xxx` |
| 2 | 可执行文件所在目录的 `stdlib/` | `xxx.so` → `xxx.pycp` |
| 3 | 当前工作目录 → 脚本所在目录 | `xxx.so` → `xxx.pycp` |
| 4 | 解释器编译期收集的依赖表（`registry`） | `.pycp` |
| — | 全部未命中 | 抛 `ImportError: No module named 'xxx'` |

说明：

- **同层动态库优先于 `.pycp` 源码**。原生扩展（`xxx.so`，入口符号 `PycpModule_xxx`）性能更好，也与 `stdlib/` 现状（全部为动态库）一致。
- **第 1 层主要针对编译产物**。AOT（`--emit-cpp`）生成的每个模块都导出 `PycpModule_<模块名>`，因此「多个 `.gen.cpp` 一起链接」或「把 `b.gen.cpp` 编成静态库再链接」都能正常 `import b`。该层由两级机制互为兜底：全局符号查找（`dlsym`）与静态初始化注册表——主程序符号默认不进动态符号表，未加 `-rdynamic` 时前者会失效，此时后者生效。
- **第 2 层的 `stdlib/` 随可执行文件定位**：解释器取 `pycp` 自身所在目录，AOT 编译出的独立程序取该程序自身所在目录。
- **`.pycp` 源码需要宿主支持**。解析器位于 frontend，解释器（`pycp`）已注册编译器钩子，故第 2/3 层可直接加载 `.pycp`；AOT 生成的独立程序未注册该钩子，其 `stdlib/` 仅识别动态库。
- **第 1 层命中符号但初始化返回空**时直接抛 `ImportError`，不继续下探——符号存在即表明明确的链接意图，静默回退会掩盖「静态库成员被链接器丢弃」这类问题。

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
│   ├── Pycp.so
│   └── classtools.so
├── lib/                     运行时库
│   ├── libPycpRuntime.so    动态库（默认构建，pycp 动态链接它）
│   └── libPycpRuntime.a     静态库（AOT 生成代码静态链接用）
│                            Windows 下另有 PycpRuntime.dll 与导入库
├── include/                 后端头文件（AOT / 原生扩展编译用）
│   └── Pycp*.hpp / PycpExt.h
└── BUILD_INFO.txt           产物溯源信息（平台 / 构建类型 / 编译器 / 版本）
```

> Windows 例外：加载 DLL 只搜索可执行文件所在目录、不搜索 `lib/`，因此
> `PycpRuntime.dll` 会在 dist 根目录再放一份（与 `lib/` 那份同源）。

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
> 查找 `io.so` / `Pycp.so` / `classtools.so`，因此分发时请整目录带走。

各产物靠 rpath 相互定位，脱离构建树仍可运行：

| 产物 | rpath | 解析到 |
|------|-------|--------|
| `dist/pycp` | `$ORIGIN/lib` | `dist/lib/libPycpRuntime.so` |
| `dist/stdlib/*.so` | `$ORIGIN/../lib` | `dist/lib/libPycpRuntime.so` |

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
Pycp/
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
│   │   └── PycpAot.hpp         # AOT 原生编译接口（预留骨架）
│   └── src/
│       ├── PycpLexer.l         # Flex 词法规则
│       ├── PycpParser.y        # Bison 语法规则
│       ├── PycpAstNode.cpp     # AST 节点实现
│       ├── PycpCodegen.cpp     # 代码生成器实现
│       └── PycpAot.cpp         # AOT 实现（预留骨架）
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
│   ├── Pycp/              # Pycp 标准库（Pycp.so）：类型转换与可见性装饰器
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
| `stdlib/` | 标准库：C++ 原生动态库（`io` / `Pycp` / `classtools`），`import` 时由 VM 动态加载 |
| 顶层 `CMakeLists.txt` | 全项目统一构建入口，产出 `pycp` 可执行文件 |
| `frontend/CMakeLists.txt` | 独立构建 `pycp`（不构建 stdlib 原生扩展，`import io` 等不可用） |
| `backend/CMakeLists.txt` | 独立构建运行时库与 `test_pycp` 测试 |

> 各标准库子库统一采用 `<name>/include`（头文件）+ `<name>/src`（源文件）+ `<name>/CMakeLists.txt` 的目录结构，编译为独立动态库（`io.so` / `Pycp.so` / `classtools.so`），输出到 `build/stdlib/`。

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
- **默认字符串表示**：未定义 `__string__` 的对象输出 `<name at 0xADDR>`；普通函数输出 `<function "name" at 0xADDR>`；类输出 `<class "name">`；匿名函数 / 匿名类统一输出 `@anonymous`。
- **运算符重载**（魔术方法）：`__addition__` / `__subtraction__` / `__multiplication__` / `__division__` / `__power__` / `__negation__`，及比较 `__less_than__` / `__less_equal__` / `__equal__` / `__not_equal__` / `__greater_than__` / `__greater_equal__`。
- **装饰器语法糖**：`@decorator` 把被装饰对象（函数或任意对象）作为参数传给装饰器函数，用返回值替换原对象。
- **成员可见性**：`@private` / `@public` 修饰类内成员，控制该成员在类外的访问可见性（方法内部经 `self` 访问不受限）。
- **文件级导出**：模块顶层符号默认 public；`@private func foo(){}` 的顶层符号对其他文件 `import` 时不可见。
- **内置库**（`stdlib/` 目录，C++ 原生实现）：
  - `io`：`io.stdin` / `io.stdout` / `io.stderr` 文件对象（`write` / `readline` 方法），以及 `io.print(value)`（输出内容后自动换行）与 `io.input(prompt)`（打印提示后读取一行）。
  - `Pycp`：`Pycp.String(x)` / `Pycp.Integer(x)` 类型转换类、`Pycp.Object` 基类，以及 `Pycp.public` / `Pycp.private` 可见性装饰器函数。
  - `classtools`：`classtools.super()`（返回父类）、`classtools.public` / `classtools.private`（可见性装饰器函数，与 Pycp 库功能一致）。
- 本版起不注入任何内建函数（不导入库时命名空间仅含用户定义内容）。

暂不支持：类型注解（`x: int`）、`map` 字面量、`Pointer`、多继承。AOT 后端已支持类定义、属性、列表与下标翻译。
