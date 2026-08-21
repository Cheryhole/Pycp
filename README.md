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
```

生成的 `.cpp` 包含 `main()`，内部将字节码翻译为 `Pycp::Add`/`Sub`/`Call` 等
ABI 调用序列（常量内联为 `g_c[]`，控制流翻译为 `goto`，函数翻译为
`pycp_fn_N`）。它不依赖解释器循环，但编译时仍需链接 `PycpRuntime` 库
（静态 `libPycpRuntime.a` 或动态 `libPycpRuntime.so`）。

> 说明：AOT 已实现完整的指令翻译（含类定义 `MAKE_CLASS`、属性
> `LOAD_ATTR`/`STORE_ATTR`、列表字面量与下标 `BUILD_LIST`/`GET_ITEM`/`SET_ITEM`、
> 函数调用等）。闭包通过 `BytecodeFunction` 的 native 模式落地，匿名函数捕获
> 外层局部变量后翻译出的 C++ 与原生函数一致。生成的代码需链接 `PycpRuntime`
> 库（静态 `libPycpRuntime.a` 或动态 `libPycpRuntime.so`）。

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

## 配置说明

本项目通过 CMake 变量（option）控制构建行为，无需环境变量或配置文件。

### 顶层构建选项

顶层 `CMakeLists.txt` 在引入 backend 时强制设定以下选项（同时构建静态库
与动态库，产物统一输出到 `backend/bin/`）：

| 变量 | 值 | 说明 |
|------|-----|------|
| `BUILD_RUNTIME_STATIC` | `ON` | 构建 `libPycpRuntime.a` 静态库 |
| `BUILD_RUNTIME_SHARED` | `ON` | 构建 `libPycpRuntime.so` 动态库 |
| `BUILD_PYTHON_BINDING` | `OFF` | 不构建 Python 绑定 |
| `BUILD_TEST` | `OFF` | 不构建测试程序 |

构建完成后，`backend/bin/` 下会同时产出：

- `libPycpRuntime.a`（静态库）
- `libPycpRuntime.so`（动态库，Linux；macOS 为 `.dylib`）

顶层 `pycp` 可执行文件当前静态链接 `libPycpRuntime.a`。

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
├── example.pycp            # 示例源码（含尚未支持的高级语法）
├── frontend/               # 前端：词法/语法分析、AST、代码生成、AOT
│   ├── CMakeLists.txt      # 独立构建入口（parser_test 测试程序）
│   ├── include/
│   │   ├── PycpAstNode.hpp     # AST 节点定义
│   │   ├── PycpCodegen.hpp     # 代码生成器接口（AST -> 字节码）
│   │   └── PycpAot.hpp         # AOT 原生编译接口（预留骨架）
│   └── src/
│       ├── PycpLexer.l         # Flex 词法规则
│       ├── PycpParser.y        # Bison 语法规则
│       ├── PycpParser.cpp      # parser_test 入口（打印 AST）
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
| `frontend/CMakeLists.txt` | 独立构建 `parser_test`（打印 AST 的解析器测试） |
| `backend/CMakeLists.txt` | 独立构建运行时库与 `test_pycp` 测试 |

> 各标准库子库统一采用 `<name>/include`（头文件）+ `<name>/src`（源文件）+ `<name>/CMakeLists.txt` 的目录结构，编译为独立动态库（`io.so` / `Pycp.so` / `classtools.so`），输出到 `build/stdlib/`。

### 模块独立构建

除了顶层一键编译，各模块也可独立构建：

```bash
# 仅构建前端 parser_test（无需 backend）
cd frontend
cmake -S . -B build
cmake --build build -j
./build/parser_test ../example.pycp   # 打印 AST

# 仅构建后端运行时库 + 测试
cd backend
cmake -S . -B build
cmake --build build -j
```

## 最近更新

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
