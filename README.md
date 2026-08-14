# Pycp

Pycp 是一个 **Python-like 语言** 的编译器与解释器，使用 C++17 实现。它采用与 Python 相近的语法，拥有自有的词法/语法分析器、AST、代码生成器、栈式字节码虚拟机（VM）以及序列化字节码格式。

Pycp 将源码 `.pycp` 编译为自定义字节码 `.cpycp`（类似 Python 的 `.pyc`），随后由内嵌的 Pycp VM 解释执行。

## 功能特性

| 类别 | 说明 |
|------|------|
| 语言定位 | Python-like，沿用 Python 部分语法 |
| 前端 | Flex 词法分析 + Bison 语法分析，生成 AST |
| 编译器 | AST → 栈式字节码（Codegen），支持常量池 / 符号表 / 代码对象 |
| 运行时 | GC、对象模型（Integer / String / None / Function）、ABI 接口 |
| 虚拟机 | 栈式字节码 VM，支持函数调用、闭包、控制流 |
| 字节码 | `.cpycp` 二进制格式（小端 + LEB128 编码），可序列化 / 反序列化 |
| AOT（预留） | 将字节码翻译为独立 C++ 源文件的接口骨架 |

### 已支持的语言子集

- 赋值、表达式语句
- 整数 / 字符串 / `None` 字面量
- 算术（`+ - * /`）、一元取负（`-`）
- 比较（`< <= > >= == !=`）
- `if / elif / else` 条件语句
- 函数定义（`func name(params){...}`）与匿名函数（`func(params){...}`）
- 函数调用、`return`
- 闭包（匿名函数捕获外层局部变量）

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
| `--emit-cpp` | 将 `.pycp` 翻译为独立 C++ 源文件（AOT 预留接口） |
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

**4. 一个最小示例**

创建 `hello.pycp`：

```
a = 23 - 5 * 7
print(a)
print(2 ** 3)
```

运行：

```bash
./build/pycp hello.pycp
# 输出：
# -12
# 8
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

顶层 `CMakeLists.txt` 在引入 backend 时强制设定以下选项以加速全量编译：

| 变量 | 值 | 说明 |
|------|-----|------|
| `BUILD_RUNTIME_SHARED` | `OFF` | 不构建动态库 |
| `BUILD_PYTHON_BINDING` | `OFF` | 不构建 Python 绑定 |
| `BUILD_TEST` | `OFF` | 不构建测试程序 |

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
```

### 目录职责说明

| 目录 | 职责 |
|------|------|
| `PycpMain.cpp` | 真正的 `main()` 入口，统一前端解析 + 后端编译/执行 |
| `frontend/` | 前端：词法/语法分析、AST、代码生成、AOT |
| `backend/` | 后端：运行时库（对象模型、GC、VM、字节码），**不依赖前端** |
| 顶层 `CMakeLists.txt` | 全项目统一构建入口，产出 `pycp` 可执行文件 |
| `frontend/CMakeLists.txt` | 独立构建 `parser_test`（打印 AST 的解析器测试） |
| `backend/CMakeLists.txt` | 独立构建运行时库与 `test_pycp` 测试 |

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

本项目仍处于开发阶段，`--emit-cpp`（AOT）等接口为预留骨架，尚未完整实现；`example.pycp` 中包含的类型注解、`class`、`map`、`list`、`Pointer` 等高级语法可能尚未被当前解析器完全支持。
