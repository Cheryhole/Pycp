# Pycp 核心类型方法设计（Python 同名同语义）

> 状态：**设计稿（供评审）**。本轮只产出文档，不改动任何 C++/CMake/pycp 源码。
> 范围：为 9 个核心类型补充「精选高频」方法（String/List/FixedList/Map/File），
> 命名与语义对齐 Python，落地方式遵循仓库既有「方法表 + 魔术方法」机制。
>
> **修订（2026-09-26）**：数值类型（Integer / Float / Decimal / Boolean）**不新增核心方法**；
> 与**数学 / 数值转换**相关的能力统一放入独立的 **`maths` 标准库**（按 Integer / Float / Decimal 三种数字类型区分），见 §13。

---

## 0. 概述与约定

### 0.1 目标
- 让核心类型具备 Python 使用者熟悉的方法（如 `s.split()`、`l.append()`、`m.keys()`、`f.read()`），提升便捷性。
- **与 Python 同名、同语义**；不破坏既有魔术方法 / 运算符 / 行为。
- 方法实现于**运行时类型**（`src/object/…`），因此**解释器与 AOT 产物均天然可用**，无需 `import pycp`。
- 与 AOT 的 `from pycp import String` 改造解耦：本清单只约定「运行时提供哪些方法」；导入书写风格见 `docs/STDLIB_REFACTOR.md`。
- **数值类型的数学能力独立成库**：`Integer/Float/Decimal/Boolean` 不新增核心方法；数学运算与数值转换由 **`maths` 标准库**提供（§13）。转换亦可用既有的 `pycp.String/Integer/Float/Decimal(x)` 类型类构造器。

### 0.2 术语与图例
| 记号 | 含义 |
|---|---|
| ✅ 已有 | 该类型当前已实现（基线盘点确认） |
| 🆕 新增 | 本设计新增 |
| ➖ 不适用 | 该类型不提供该方法（含原因） |
| 魔术方法 | dunder 方法（`__string__`、`__get_item__` …），经统一分派，**不属于本清单的「普通方法」** |

### 0.3 既有机制（实现须遵守，均有出处）
- **方法表**（`src/object/PycpMethodTable.hpp`）：
  ```cpp
  using PycpCFunction = Object* (*)(Object* self, FixedList* args, Map* kwargs);
  struct MethodEntry { const char* name; PycpCFunction native; };  // native==nullptr 表示魔术方法
  using MethodTableFn = const std::vector<MethodEntry>& (*)();
  ```
- **类型注册**（`src/object/PycpModule.cpp:84-94`）：`set_type(name, ctor, initialize, table)` 会遍历 `table()`，
  对 `native != nullptr` 的条目 `Pycp::New<Function>(e.name, e.native)` 并 `cls->add_method`；`nullptr` 条目走
  `GetMagicMethodFunction(e.name)`。
- **实例分派**（既有先例）：`List::__get_attribute__`（`src/object/PycpList.cpp:217-251`）、
  `Map::__get_attribute__`（`src/object/PycpMap.cpp:290-324`）、`File::__get_attribute__`（`src/object/PycpFile.cpp:531-620`）
  均为「`__name__/__class__` → `members_` → 普通方法（懒建并缓存 `Function`）→ 魔术回退 → `AttributeError`」。
- **参数规范**（`src/object/PycpExtension.hpp`）：`static const Extension::ArgTable spec = CompileArgs("name", {…}); spec.Bind(args,kwargs);`
  `Arg::Required/Optional/Rest`；`r.given("x")` 判断可选参数是否显式给出。
- **`__inspect__`**：各类型合并 `members_` + `Xxx_method_table()` 名称 + `CommonInspectNames()`（当前仅 `{"__class__"}`，`src/object/PycpMagic.cpp:184`）。
  → 新增方法**自动**出现在 `__inspect__` 中（以方法表为准）。
- **通用魔术方法全集（29 个）**（`src/object/PycpMagic.cpp:108-138`）是所有类型共享的能力面，本清单的普通方法**不得**与之重名。

### 0.4 全部类型方法计数（本轮目标）

| 类型 | 已有普通方法 | 新增 | 合计 | 备注 |
|---|---|---|---|---|
| String | 0 | 14 | 14 | 现仅有魔术方法 |
| Integer | 0 | 0 | 0 | 数值族：核心**不新增**，数学能力见 §13 `maths` |
| Float | 0 | 0 | 0 | 同上 |
| Decimal | 0 | 0 | 0 | 同上 |
| Boolean | 0 | 0 | 0 | **继承 Integer，复用同一方法表** |
| List | 2（`length`,`append`） | 11 | 13 | |
| FixedList | 1（`length`） | 3 | 4 | 不可变 |
| Map | 2（`length`,`keys`） | 8 | 10 | |
| File | 8 | 7 | 15 | 含 3 个只读属性（非方法） |
| **合计** | **13** | **43** | **56** | 另：`maths` 库（§13）不计入核心类型方法 |

---

## 1. String

### 1.1 基线（已有）
- 普通方法：**无**。
- 魔术方法（`String_method_table()`，`src/object/PycpString.cpp:135`）：`__integer__ __string__ __raw_string__ __boolean__ __addition__ __multiplication__ __equal__ __get_item__ __list__ __iterator__ __map__ __hash__ __get_attribute__ __set_attribute__ __delete_attribute__ __inspect__`。
- 可访问属性：`__name__`、`__class__`（继承 `Object::__get_attribute__`，`src/object/PycpObject.cpp:80`）。

### 1.2 方法清单

| 状态 | 方法 / 签名 | 语义（对齐 Python） | 异常 / 边界 | 与魔术方法关系 |
|---|---|---|---|---|
| 🆕 | `length() -> Integer` | `len(s)` | 无 | 与 `__get_item__` 边界一致（按字符计） |
| 🆕 | `upper() -> String` | `str.upper()` | 无 | 新串（不可变） |
| 🆕 | `lower() -> String` | `str.lower()` | 无 | 新串 |
| 🆕 | `strip([chars]) -> String` | `str.strip()` | `chars` 非 String/None → `TypeError` | 新串 |
| 🆕 | `lstrip([chars]) -> String` | `str.lstrip()` | 同上 | 新串 |
| 🆕 | `rstrip([chars]) -> String` | `str.rstrip()` | 同上 | 新串 |
| 🆕 | `split([sep[, maxsplit]]) -> List[String]` | `str.split()`；`sep=None` 按任意空白并丢弃空串 | `sep` 非 String/None → `TypeError`；`maxsplit` 非 Integer → `TypeError` | 返回 `List` |
| 🆕 | `join(iterable) -> String` | `str.join()`；`iterable` 为 `List`/`FixedList[String]` | 元素非 String → `TypeError` | — |
| 🆕 | `replace(old, new[, count]) -> String` | `str.replace()`；`count<0` 表示全部 | 参数非 String → `TypeError` | 新串 |
| 🆕 | `find(sub[, start[, end]]) -> Integer` | `str.find()`；未找到返回 `-1` | `sub` 非 String → `TypeError` | 返回下标 |
| 🆕 | `count(sub) -> Integer` | `str.count()`；不重叠计数 | `sub` 非 String → `TypeError` | — |
| 🆕 | `startswith(prefix) -> Boolean` | `str.startswith()` | `prefix` 非 String → `TypeError` | 返回 `Boolean` |
| 🆕 | `endswith(suffix) -> Boolean` | `str.endswith()` | 同上 | 返回 `Boolean` |
| 🆕 | `contains(sub) -> Boolean` | `sub in s`（Python `in` 的显式形态） | `sub` 非 String → `TypeError` | 与 `find>=0` 等价 |

> 说明：Python 的 `len()`/`in` 是内建语法；Pycp 当前无 `len()` 内建，故以 `length()`/`contains()` 显式提供。`__multiplication__`（重复）与 `__addition__`（拼接）已有，不重复提供 `repeat`/`concat`。

### 1.3 实现映射
- 原生实现：`src/object/PycpString.cpp`（anonymous namespace）新增 `_str_length/_str_upper/_str_lower/_str_strip/_str_lstrip/_str_rstrip/_str_split/_str_join/_str_replace/_str_find/_str_count/_str_startswith/_str_endswith/_str_contains`。
- 方法表：`String_method_table()`（`src/object/PycpString.cpp:135`）追加上述条目（`native` 指向对应函数）。
- 分派：**新增 `String::__get_attribute__` 覆写**（当前无），模板同 `List::__get_attribute__`（`PycpList.cpp:217`）：
  `__name__` → `__class__` → `members_` → 普通方法（懒建 `Function` 缓存于实例成员）→ `GetMagicMethodFunction` → `AttributeError`。
- 参数规范：各原生函数首行 `CompileArgs("<name>", {…})`；`strip` 用 `Arg::Optional("chars")` + `r.given("chars")`。
- GC：懒建的 `Function*` 必须加入 `foreach_ref` 并随析构 `Decref`（模板见 `PycpList.cpp:253-258`）。

---

## 2. Integer

### 2.1 基线（已有）
- 普通方法：**无**。魔术方法见 `Integer_method_table()`（`src/object/PycpInteger.cpp:336`），含算术/比较/`__integer__/__float__/__string__/__hash__` 等 22 条。

### 2.2 方法清单：**不新增核心方法**
- 依 2026-09-26 修订：`abs / bit_length / conjugate / pow / divmod` 等**数学方法**与 `to_float / to_string` 等**转换方法**一律**不**加到 Integer 上。
- 数学能力移至 **`maths` 标准库**（§13）；转换改用既有 `pycp.Integer(x)` / `pycp.Float(x)` / `pycp.String(x)` 类型类构造器。
- 结论：`Integer` 方法表**保持现状**（仅 22 条魔术方法），不新增 `__get_attribute__` 覆写。

---

## 3. Float

### 3.1 基线（已有）
- 普通方法：**无**；魔术方法 `Float_method_table()`（`src/object/PycpFloat.cpp:263`，22 条）。

### 3.2 方法清单：**不新增核心方法**
- `abs / is_integer / round / floor / ceil / as_integer_ratio` 等数学方法 → 移入 **`maths`**（§13）。
- `to_integer / to_string` 等转换方法 → 改用 `pycp.Integer(x)` / `pycp.String(x)`。
- 结论：`Float` 方法表保持现状，不新增 `__get_attribute__` 覆写。

---

## 4. Decimal

### 4.1 基线（已有）
- 普通方法：**无**；魔术方法 `Decimal_method_table()`（`src/object/PycpDecimal.cpp:502`，22 条）。

### 4.2 方法清单：**不新增核心方法**
- `abs / round / floor / ceil / quantize` 等数学方法 → 移入 **`maths`**（§13）。
- `to_integer / to_float / to_string` 等转换方法 → 改用 `pycp.Integer(x)` / `pycp.Float(x)` / `pycp.String(x)`。
- 结论：`Decimal` 方法表保持现状，不新增 `__get_attribute__` 覆写。

---

## 5. FixedList

### 5.1 基线（已有）
- 普通方法：`length`（`_fixedlist_length`，`PycpFixedList.cpp:23`）。
- 不可变：不含 `__set_item__`/`__delete_item__`。

### 5.2 方法清单

| 状态 | 方法 / 签名 | 语义（对齐 Python `tuple`） | 异常 / 边界 | 备注 |
|---|---|---|---|---|
| ✅ | `length() -> Integer` | `len(t)` | 无 | 已有 |
| 🆕 | `index(value) -> Integer` | `tuple.index(v)` | 不存在 → `ValueError` | |
| 🆕 | `count(value) -> Integer` | `tuple.count(v)` | 无 | 相等语义用 `__equal__`/`Compare` |
| 🆕 | `contains(value) -> Boolean` | `v in t` | 无 | |

> **修订（2026-09-26）**：**不新增 `to_list()`**；需要可变副本时改用既有 `pycp.List(fixedlist)` 构造器。

### 5.3 实现映射
- 文件 `src/object/PycpFixedList.cpp`，函数 `_fixedlist_index/_fixedlist_count/_fixedlist_contains`；
- 方法表 `FixedList_method_table()`（`PycpFixedList.cpp:34`）追加；分派在其 `__get_attribute__`（`PycpFixedList.cpp:197`）已存在，按既有模板加分支。

---

## 6. Boolean

- 类声明 `class Boolean : public Integer`（`src/object/PycpBoolean.hpp:12`）；注册时**复用** `Integer_method_table`（`src/stdlib/pycp/src/PycpModule.cpp:520`）。
- **本设计不为 Boolean 新增独立方法**：其方法集 = Integer 方法集（§2，均为魔术方法）。数值/转换能力与 Integer 一致，统一走 **`maths`**（§13）与 `pycp.Integer(x)` / `pycp.String(x)`。
- ➖ 若后续确需 Boolean 专属方法（如 `to_boolean()`），必须**单独建 `Boolean_method_table()`**（否则会污染 Integer），并在 `set_type("Boolean", …)` 处改绑；本设计暂不引入。
- `__inspect__` 现状：Boolean 未覆写 → 继承 `Integer::__inspect__`（`PycpInteger.cpp:364`）。若新增 Integer 方法，Boolean 的 `__inspect__` 会一并体现（符合继承语义）。

---

## 7. List

### 7.1 基线（已有）
- `length`（`_list_length`，`PycpList.cpp:23`）、`append`（`_list_append`，`PycpList.cpp:31`）。
- 魔术方法含 `__get_item__/__set_item__/__delete_item__/__addition__/__iterator__/__map__/__string__/__boolean__` 等。

### 7.2 方法清单

| 状态 | 方法 / 签名 | 语义（对齐 Python `list`） | 异常 / 边界 | 备注 |
|---|---|---|---|---|
| ✅ | `length() -> Integer` | `len(l)` | 无 | 已有 |
| ✅ | `append(item) -> None` | `list.append` | 无 | 已有，原地 |
| 🆕 | `extend(iterable) -> None` | `list.extend` | 不可迭代 → `TypeError` | 原地 |
| 🆕 | `insert(index, item) -> None` | `list.insert` | `index` 非 Integer → `TypeError`；越界按 Python 夹取 | 原地 |
| 🆕 | `remove(value) -> None` | `list.remove` | 不存在 → `ValueError` | 原地 |
| 🆕 | `pop([index]) -> Object` | `list.pop`；默认末元素 | 空/越界 → `IndexError` | 原地 |
| 🆕 | `index(value) -> Integer` | `list.index` | 不存在 → `ValueError` | |
| 🆕 | `count(value) -> Integer` | `list.count` | 无 | |
| 🆕 | `contains(value) -> Boolean` | `v in l` | 无 | |
| 🆕 | `reverse() -> None` | `list.reverse` | 无 | 原地 |
| 🆕 | `sort([reverse]) -> None` | `list.sort`（先支持数值/字符串自然序） | 不可比较 → `TypeError` | 原地；`key` 回调另议 |
| 🆕 | `clear() -> None` | `list.clear` | 无 | 原地 |
| 🆕 | `copy() -> List` | `list.copy` | 无 | 浅拷贝 |

> `sort` 的 `key=` 需函数回调，第一版**仅支持 `reverse` 关键字**（与 `print` 的 `with_keywords` 用法一致）；`key` 作为后续扩展项。

### 7.3 实现映射
- 文件 `src/object/PycpList.cpp`，函数 `_list_extend/_list_insert/_list_remove/_list_pop/_list_index/_list_count/_list_contains/_list_reverse/_list_sort/_list_clear/_list_copy`；
- 方法表 `List_method_table()`（`PycpList.cpp:46`）追加；分派在 `List::__get_attribute__`（`PycpList.cpp:217`）按既有 `length/append` 分支模式补齐（或改走 §11.2 通用派发）；
- 新增缓存成员（如 `Function* extend_fn_` 等）须加入 `foreach_ref`（`PycpList.cpp:253`）。

---

## 8. Map

### 8.1 基线（已有）
- `length`（`_map_length`，`PycpMap.cpp:22`）、`keys`（`_map_keys`，`PycpMap.cpp:30`）。
- 魔术方法含 `__get_item__/__set_item__/__delete_item__/__map__/__string__/__boolean__`（`Map` 不可哈希，继承 `Object::__hash__` 抛错）。

### 8.2 方法清单

| 状态 | 方法 / 签名 | 语义（对齐 Python `dict`） | 异常 / 边界 | 备注 |
|---|---|---|---|---|
| ✅ | `length() -> Integer` | `len(d)` | 无 | 已有 |
| ✅ | `keys() -> List` | `dict.keys()`（Pycp 返回 `List`） | 无 | 已有 |
| 🆕 | `values() -> List` | `dict.values()` | 无 | 返回 `List` |
| 🆕 | `items() -> List[FixedList[key,value]]` | `dict.items()` | 无 | 每项 `FixedList` 二元组 |
| 🆕 | `get(key[, default]) -> Object` | `dict.get`；缺省返回 `None` | 无 | |
| 🆕 | `has(key) -> Boolean` | `key in d` | 无 | |
| 🆕 | `remove(key) -> Object` | 等义 `del d[key]`，返回被删值 | 不存在 → `KeyError` | 原地 |
| 🆕 | `update(other) -> None` | `dict.update` | `other` 非 Map → `TypeError` | 原地 |
| 🆕 | `clear() -> None` | `dict.clear` | 无 | 原地 |
| 🆕 | `copy() -> Map` | `dict.copy` | 无 | 浅拷贝 |

### 8.3 实现映射
- 文件 `src/object/PycpMap.cpp`，函数 `_map_values/_map_items/_map_get/_map_has/_map_remove/_map_update/_map_clear/_map_copy`；
- 方法表 `Map_method_table()`（`PycpMap.cpp:42`）追加；分派在 `Map::__get_attribute__`（`PycpMap.cpp:290`）补齐。

---

## 9. File

### 9.1 基线（已有）
- 普通方法：`write` `read` `read`(size) `readline` `readlines` `close` `flush` `open`
  （`File_method_table()`，`src/object/PycpFile.cpp:478`；native `_file_*`，`PycpFile.cpp:386-465`）。
- 只读属性：`closed`、`name`、`mode`（`File::__get_attribute__`，`PycpFile.cpp:544-563`）。
- `io.File` 与 `filesystem.File` 已别名为同一类型类（`FileTypeClass()`，`PycpFile.cpp:517`）。
- ⚠️ **已知差异（需确认）**：`File::__get_attribute__` 未处理 `__name__`（`PycpFile.cpp:531-620`），与 List/Map 不一致；本设计建议在补齐时一并修正。

### 9.2 方法清单

| 状态 | 方法 / 签名 | 语义（对齐 Python `io`） | 异常 / 边界 | 备注 |
|---|---|---|---|---|
| ✅ | `read([size]) -> String` | `file.read` | 未打开/不可读 → `ValueError` | 已有 |
| ✅ | `readline() -> String` | `file.readline` | 同上 | 已有 |
| ✅ | `readlines() -> List[String]` | `file.readlines` | 同上 | 已有 |
| ✅ | `write(data) -> Object` | `file.write` | 不可写 → `ValueError` | 已有（返回值以现有实现为准） |
| ✅ | `close() -> None` | `file.close` | 已关闭为幂等 | 已有 |
| ✅ | `flush() -> None` | `file.flush` | 未打开 → `ValueError` | 已有 |
| ✅ | `open(path[, mode]) -> None` | `file.open` | 打开失败 → `ValueError` | 已有 |
| 🆕 | `writelines(lines) -> None` | `file.writelines` | 元素非 String → `TypeError` | |
| 🆕 | `seek(offset[, whence]) -> Integer` | `file.seek` | 不可 seek → `ValueError` | `whence∈{0,1,2}` |
| 🆕 | `tell() -> Integer` | `file.tell` | 未打开 → `ValueError` | |
| 🆕 | `readable() -> Boolean` | `file.readable()` | 无 | |
| 🆕 | `writable() -> Boolean` | `file.writable()` | 无 | |
| 🆕 | `seekable() -> Boolean` | `file.seekable()` | 无 | |
| 🆕 | `is_closed() -> Boolean` | 等价属性 `closed` | 无 | 与属性并存 |

> 属性 `closed/name/mode` 保持既有的属性访问形式（非方法），与 Python 一致（`f.closed`）。`is_closed()` 仅为便捷别名。

### 9.3 实现映射
- 文件 `src/object/PycpFile.cpp`，函数 `_file_writelines/_file_seek/_file_tell/_file_readable/_file_writable/_file_seekable/_file_is_closed`；
- 方法表 `File_method_table()`（`PycpFile.cpp:478`）追加；分派在 `File::__get_attribute__`（`PycpFile.cpp:567` 一带）补齐；
- 提升 `std::fstream file_` 的 seek/tell 支持；对 `owns_stream_==false`（stdin/stdout/stderr）返回「不可 seek」。

---

## 10. 实现通用规范（所有类型一致）

### 10.1 原生函数模板
```cpp
// src/object/Pycp<Type>.cpp（anonymous namespace）
Object* _<type>_<method>(Object* self, FixedList* args, Map* kwargs) {
    static const Extension::ArgTable spec = Extension::CompileArgs(
        "<method>", { /* Arg::Required/Optional/Rest */ });
    Extension::ArgResult r = spec.Bind(args, kwargs);
    <Type>* obj = static_cast<<Type>*>(self);
    // … 实现 …
    return <result>;           // Owned（见注）
}
```
- 返回值：既有 `_list_length` 返回新 `Integer`（Owned）；`_list_append` 返回 `None::instance`（常量，不走 Decref）。
  统一遵循既有约定，并在文档/实现中标注所有权。

### 10.2 分派（推荐：通用派发，减少样板）
现状 `String/Integer/Float/Decimal` 无 `__get_attribute__` 覆写，`List/Map/FixedList/File` 各写一段。
**推荐方案**：在基类引入虚方法表访问器 + 通用分派，一次实现、全类型复用：
```cpp
// src/object/PycpObject.hpp（新增，默认 nullptr）
virtual MethodTableFn method_table() const { return nullptr; }
```
各类型覆写 `method_table()` 返回自身 `Xxx_method_table()`；随后在 `Object::__get_attribute__`（`PycpObject.cpp:80`）
与各覆写处共用一段逻辑：`members_` → 遍历 `method_table()` 命中 `native != nullptr` 的条目 → 懒建并缓存
`Function(name, native)` → `GetMagicMethodFunction` 回退 → `AttributeError`。
- 收益：新增方法**只需改方法表 + 写原生函数**，无需为每个类型手写分支；`String/Integer/Float/Decimal` 无需新增覆写。
- 兼容：`List/Map/FixedList/File` 的既有覆写可保留（自定义分支先行，再回退到通用逻辑），迁移期可逐步收敛。
- 缓存：懒建 `Function` 建议以「每实例 `std::unordered_map<std::string, Function*>`」或类型类方法表（`set_type` 已把公开方法注册进类）承载；
  **无论采用哪种，必须保证 `foreach_ref` 访问、析构 `Decref`**，否则 GC 漏引用。

### 10.3 参数与异常约定
- 名称/个数/类型校验统一由 `Extension::ArgTable` 完成（与 `print`/`FileConstructor` 一致）。
- 异常类型对齐 Python：`TypeError`（类型/参数错）、`ValueError`（值非法/找不到）、`IndexError`（越界）、`KeyError`（键缺失）、`ZeroDivisionError`。
- 越界/默认值语义严格对齐 Python（如 `pop` 空表 `IndexError`、`insert` 夹取、`split` 空白规则、`round` 银行家舍入）。

### 10.4 与魔术方法/运算符的关系
- `__addition__/__multiplication__/__get_item__/__set_item__/__iter__` 等**保持不变**；普通方法不改变任何运算符行为。
- `length()`/`contains()` 为 Python 内建 `len()`/`in` 的显式替代，语义一致但不影响既有语法。
- 本项目**不为数值类型提供 `to_string/to_integer/to_float` 等方法**：字符串化/转换统一走既有魔术方法（`__string__`/`__integer__`/`__float__`）与其暴露的 `pycp.String/Integer/Float(x)` 构造器；数学运算走 `maths`（§13）。

### 10.5 AOT 与运行时可
- 方法在运行时类型上实现 → AOT 产物（仅链接 PycpRuntime）**无需任何改动**即可调用。
- AOT 生成的指令走 `Pycp::GetAttr`/`Call`（见 `src/stdlib/aot/emit.pycp` 的 `LOAD_ATTR`/`CALL`），方法分派与解释器同路径。
- 新增方法**不引入** `src/object` 对 `stdlib` 的反向依赖。

---

## 11. 交叉核对（设计闸门）

### 11.1 用户举例方法覆盖
| 用户举例 | 落位 |
|---|---|
| `String.length` | §1.2 🆕 `String.length()` |
| `String.split` | §1.2 🆕 `String.split()` |
| `List.append` | §7.2 ✅ 已有 |
| `Map.keys` | §8.2 ✅ 已有 |
| `File.read` | §9.2 ✅ 已有 |
| `List.extend/insert/remove/pop/index/sort/reverse/clear` | §7.2 🆕 全部覆盖 |
| `Map.values/items/get/has/remove/update` | §8.2 🆕 全部覆盖 |
| `File.readline/readlines/write/close` | §9.2 ✅ 已有 |
| `String.join/strip/replace/find/upper/lower` | §1.2 🆕 全部覆盖 |
> 结论：用户点名方法 **100% 覆盖**。

### 11.2 零重名核对（对照基线盘点）
- 与各类型**已有普通方法**比对：List（`length/append`）、FixedList（`length`）、Map（`length/keys`）、File（`write/read/readline/readlines/close/flush/open`）——本设计新增名与均不冲突。
- 与 **29 个通用魔术方法**比对：新增名均为非 dunder，无冲突。
- 与 `Object` 属性 `__name__/__class__`（含 File 的 `closed/name/mode` 属性）比对：无冲突。
- 与 `pycp`/`classtools` 模块导出（`private/public/readonly/insp/typeof/exec/super`）比对：无冲突（不同命名空间）。

### 11.3 一致性要求（供实现校对）
- 方法表条目、`__get_attribute__` 分支、`__inspect__` 输出三者必须一致（`__inspect__` 由方法表驱动，天然满足）。
- 每条「实现映射」的文件/符号须真实存在（本轮已按 `src/object/*.cpp` 实际行号标注）。

---

## 12. 后续实现拆分（下一轮，非本轮范围）
1. 通用派发（§10.2）：`Object::method_table()` + 统一分派逻辑。
2. 数值族：**不改核心类型**；按 §13 新增 `maths` 标准库（C++ 原生扩展，四目标构建模式）。
3. 字符串：String 原生方法 + 覆写（§1）。
4. 容器：List/FixedList/Map 原生方法 + 分派补齐（§5/7/8）。
5. 文件：File 原生方法 + 分派补齐 + `__name__` 一致性修正（§9）。
6. 测试：为每个方法补 `tests/**` 用例（解释器 + AOT 双向验证）。
7. `docs/STDLIB_REFACTOR.md` 中的 `from pycp import X` 改造与本清单并行推进。

---

## 13. 数值类型的数学方法：`maths` 标准库

> **修订（2026-09-26）**：数值类型的**数学 / 转换**方法**不落在核心类型**上，改为独立的 `maths` 标准库；
> 并按 **Integer / Float / Decimal** 三种数字类型区分各自适用的数学方法。

### 13.1 形态与归属
- 新增 stdlib 原生扩展 `src/stdlib/maths/`（`include/maths_stdlib.hpp` + `src/maths.cpp` + `CMakeLists.txt`），
  遵循既有「OBJECT(动态) + OBJECT(静态, `-DPYCP_STATIC`) + SHARED + STATIC」四目标模式（模板 `src/stdlib/io/CMakeLists.txt`）；
  模块名 `maths`，入口 `PYCP_EXPORT_MODULE(maths)`。
- 归属理由：保持核心类型精简（数值类型不膨胀普通方法）；数学能力集中一处、可独立演进；对 AOT 产物仍可用
  （AOT 链接 `libPycpExt_maths.a` 或运行期加载 `maths`）。
- **转换不进 `maths`**：`to_string/to_integer/to_float` 等改用既有 `pycp.String/Integer/Float/Decimal(x)` 类型类构造器（`src/stdlib/pycp/src/PycpModule.cpp:515-533`）。

### 13.2 API 设计（按数字类型区分）

统一约定：函数对入参做**运行时类型分派**（`IsExact<Integer>/IsExact<Float>/IsExact<Decimal>`）；不适用于该类型的操作抛 `TypeError`；`maths` 模块不新增类型，只导出函数。

**A. 通用（接受 Integer / Float / Decimal）**
| 函数 / 签名 | 语义（对齐 Python） | 返回 | 适用类型 |
|---|---|---|---|
| `abs(x)` | 绝对值 | 同 `x` 类型 | 三种 |
| `floor(x) -> Integer` | 向左取整（`math.floor`） | Integer | Float / Decimal（Integer 返回自身） |
| `ceil(x) -> Integer` | 向右取整（`math.ceil`） | Integer | Float / Decimal |
| `round(x[, ndigits])` | 四舍五入（默认 `ROUND_HALF_EVEN`） | 同 `x` 类型 | 三种 |
| `pow(x, y)` | 幂（复用 `__power__`） | 同 `x` 类型 | 三种 |
| `divmod(x, y) -> FixedList[Integer, Integer]` | 商与余（Python `divmod`） | 二元 `FixedList` | 三种（按类型规则） |

**B. Integer 专属**
| 函数 / 签名 | 语义 | 备注 |
|---|---|---|
| `bit_length(x) -> Integer` | `int.bit_length()` | 0 → 0 |
| `conjugate(x) -> Integer` | `int.conjugate()` | 返回自身 |
| `gcd(a, b) -> Integer` | `math.gcd` | 非 Integer → `TypeError` |
| `lcm(a, b) -> Integer` | `math.lcm` | 同上 |

**C. Float 专属**
| 函数 / 签名 | 语义 | 备注 |
|---|---|---|
| `is_integer(x) -> Boolean` | `float.is_integer()` | |
| `as_integer_ratio(x) -> FixedList[Integer, Integer]` | `float.as_integer_ratio()` | `NaN/Inf` → `ValueError` |

**D. Decimal 专属**
| 函数 / 签名 | 语义 | 备注 |
|---|---|---|
| `quantize(x, exp) -> Decimal` | 对齐 `Decimal.quantize` | 指数/位数由 `exp` 指定 |
| `is_finite(x) -> Boolean` | `Decimal.is_finite()` | |

> **命名说明**：CPython 中这些多为**内建函数**（`abs/round/pow/divmod`）或**类型方法**（`int.bit_length`、`float.is_integer`）。
> `maths` 统一以**函数**形态提供，语义对齐 CPython；跨类型不适用时显式 `TypeError`（不做静默提升）。

### 13.3 实现要点
- 每个函数：`static const Extension::ArgTable spec = CompileArgs("<name>", {…}); Extension::ArgResult r = spec.Bind(args, kwargs);`（`src/object/PycpExtension.hpp`）。
- 类型分派用既有判断（`TypeOf/IsType/IsExact<T>`，`src/object/PycpObject.hpp`）。
- 尽量**复用既有魔术方法实现**：`abs`→`__negation__` 语义、`pow`→`__power__`、`divmod`→整除/取余；避免与核心重复实现。
- `floor/ceil`：`Float` 用 `std::floor/ceil` 后转 `Integer`；`Decimal` 用 mpdecimal 取整；`Integer` 直接返回自身。
- 参数/异常风格与核心一致（`TypeError`/`ValueError`/`ZeroDivisionError`，见 §10.3）。

### 13.4 与 AOT / stdlib 重构的关系
- `maths` 是普通原生扩展，遵循 `docs/STDLIB_REFACTOR.md` 的四目标模式；**不改** `PYCP_STDLIB_TARGETS` 契约
  （新增子库照常 `set_property(GLOBAL APPEND PROPERTY …)` 注册），`pycp-dist` 自动收集到 `dist/stdlib/` 与 `dist/lib/`。
- 若采用 `from` 导入风格：`from maths import abs, floor, ceil, round, pow, divmod, bit_length, ...`。

### 13.5 被移除的核心方法对照（2026-09-26 修订）
| 原拟核心方法 | 去向 |
|---|---|
| `Integer.abs/bit_length/conjugate/pow/divmod` | → `maths.{abs, bit_length, conjugate, pow, divmod}` |
| `Integer.to_float/to_string` | → `pycp.Float(x)` / `pycp.String(x)` |
| `Float.abs/is_integer/round/floor/ceil/as_integer_ratio` | → `maths.{abs, is_integer, round, floor, ceil, as_integer_ratio}` |
| `Float.to_integer/to_string` | → `pycp.Integer(x)` / `pycp.String(x)` |
| `Decimal.abs/round/floor/ceil` | → `maths.{abs, round, floor, ceil}` |
| `Decimal.to_integer/to_float/to_string` | → `pycp.Integer(x)` / `pycp.Float(x)` / `pycp.String(x)` |
| `FixedList.to_list` | → `pycp.List(fixedlist)` |

### 13.6 与 CPython `math` 模块同名的函数（按适用类型区分）

`maths` 在 §13.2 的基础分组之外，再提供一组与 CPython **`math` 模块同名同语义**的函数。
**适用类型**列用于区分三种数字类型各自可用/不可用的函数（不适用 → `TypeError`）。

| 函数 / 签名 | 适用类型 | 语义（对齐 CPython `math`） |
|---|---|---|
| `trunc(x) -> Integer` | Integer / Float / Decimal | `math.trunc`（向零取整；Integer 返回自身） |
| `fabs(x) -> Float` | 三种 | `math.fabs`（**恒返回 Float**，与 §13.2 的类型保持 `abs` 区分） |
| `copysign(x, y) -> Float` | Float / Decimal | `math.copysign` |
| `fmod(x, y) -> Float` | Float | `math.fmod`（浮点取余，区别于 `divmod`） |
| `modf(x) -> FixedList[Float, Float]` | Float | `math.modf` →（小数部分, 整数部分） |
| `frexp(x) -> FixedList[Float, Integer]` | Float | `math.frexp` →（尾数, 指数） |
| `ldexp(x, i) -> Float` | Float | `math.ldexp`（`x * 2**i`） |
| `isnan(x) -> Boolean` | Float / Decimal | `math.isnan` |
| `isinf(x) -> Boolean` | Float / Decimal | `math.isinf` |
| `isfinite(x) -> Boolean` | 三种 | `math.isfinite`（Integer 恒 `True`） |
| `isclose(a, b[, rel_tol[, abs_tol]]) -> Boolean` | Float / Decimal | `math.isclose`（默认 `rel_tol=1e-09, abs_tol=0.0`） |
| `sqrt(x) -> Float` | Float / Decimal | `math.sqrt`（Integer 先转 Float） |
| `exp(x) / log(x[, base]) / log2(x) / log10(x) -> Float` | Float / Decimal | `math.exp/log/log2/log10` |
| `sin/cos/tan/asin/acos/atan/atan2/sinh/cosh/tanh -> Float` | Float / Decimal | 三角函数与双曲函数 |
| `hypot(*coords) -> Float` | Float | `math.hypot` |
| `dist(p, q) -> Float` | Float | `math.dist`（`p`/`q` 为等长数值序列） |
| `degrees(x) -> Float` / `radians(x) -> Float` | Float | 角度/弧度转换 |
| `fsum(iterable) -> Float` | Float / Decimal | `math.fsum`（数值序列精确求和） |
| `prod(iterable[, start]) -> 同元素类型` | 三种 | `math.prod`（数值序列连乘） |
| `factorial(n) -> Integer` | Integer | `math.factorial`（负数 → `ValueError`） |
| `isqrt(n) -> Integer` | Integer | `math.isqrt`（非负整数平方根，向下取整） |
| `comb(n, k) -> Integer` / `perm(n, k) -> Integer` | Integer | `math.comb` / `math.perm` |
| `gcd(a, b, ...) -> Integer` / `lcm(a, b, ...) -> Integer` | Integer | `math.gcd` / `math.lcm`（变参） |

**模块常量（经 `Module::set_variable` 暴露）**：
| 名称 | 类型 | 语义 |
|---|---|---|
| `pi` / `e` / `tau` | Float | `math.pi` / `math.e` / `math.tau` |
| `inf` / `nan` | Float | `math.inf` / `math.nan` |

> **重名说明**：`floor / ceil / pow` 在 CPython `math` 与本库 §13.2 同名。本库以 **§13.2 的「类型保持」语义为准**
> （`maths.pow` 按 `x` 的类型返回，而非 CPython `math.pow` 的「恒 Float」）；若确需 CPython `math.pow` 的浮点语义，
> 追加别名 `powf(x, y) -> Float` 即可（本设计先不引入，避免同名双语义）。
>
> **实现提示**：`sin/cos/…/sqrt/exp/log` 等超越函数走 C 标准库 `std::`；`Decimal` 侧优先用 mpdecimal 原生 API
> （精度受上下文控制）；`Integer` 侧数论函数用整数算法（`isqrt` 用牛顿迭代/i64 边界保护）。
