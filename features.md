# Features / Ideas

个人想法与待办列表。自由记录，无需格式。

## Todo

- [ ] 实现 Python-like 的 f-string 功能
	`f"a = {a}, b = {obj1.b1()}"`
- [ ] 实现 `[ i for i in ...]`
	语法糖，实际为 `
	l = []
	for i in ...{
		l.append(i)
	}
	`
- [ ] 实现 `[ i for i in ... if ...]`
	语法糖，实际为 `
	l = []
	for i in ...{
		if ... {
			l.append(i)
		}
	}
	`
- [ ] 实现 `[ from a to b ]` （类似 range，但左右包含）
	语法糖，实际为 `
	l = []
	repeat from a to b as i{
		l.append(i)
	}
	`
- [ ] 实现 `a of b` 的写法 （即 b.a）

- [ ] 支持 `not` 运算符，等于 `!`。
- 支持 Type Hints

## Ideas

- 支持更语义化的运算符表达，例如 `equals` `is greater than` `is not ...` 等
- 支持网络库
- 支持指定直接生成原生	C++，如 `Integer` -> `int64_t`， `func f(i, s) -> NoReturn` -> `void f(int64_t i, const std::string& s)`， `class obj{...}` -> `class obj{...};`
- 对 `to` `by` `of` 关键字添加更多应用（更加语义化）

## Done

- [x] json 模块：`json.load(s)` / `json.dump(obj, indent, sort_keys, separators)`，位于 `src/stdlib/json/`（2026-10-01）
- [x] `in` 运算符与 `__contains__` 魔术方法：List/FixedList 遍历元素、String 子串、`map.keys()`/`map.values()`；自定义 class 通用分派 + 迭代回退；未实现且不可迭代抛 TypeError；VM 与 AOT 双路径（2026-10-01）

关键字预算:新增可能的关键字有 of(第5)、not(第7),Ideas 还可能加 is/greater/than/equals。每加一个都压缩标识符空间,且要同步改语法的非终结符。建议严格限量,优先符号形式(!)而非新词。
魔术方法表:__contains__ 是唯一需要动 src/object/PycpMagic.cpp(magic_thunks,29→30)的条目,还要更新 __inspect__ 与文档。
AOT 双通路:第 1/2/3/4 条纯语法糖务必确认 --emit-cpp 发射器同样支持,否则「解释能跑、AOT 报错」。
作用域语义:列表推导的变量作用域、f-string 内嵌表达式的作用域,是需要先拍板的设计点。
建议的推进顺序(仅讨论,非承诺):

低风险高价值:in/not in + __contains__;not/!;列表推导(含 if);[from a to b]。
中等:f-string(词法复杂度较高);a of b(需接受新关键字)。
较大:Type Hints(先「解析+忽略」)。
独立/研究线:网络库(标准库工程)、原生 C++ 生成、语义化运算符。