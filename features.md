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

- [ ] 支持 `in` 运算符

## Ideas

- 

## Done

- [x] json 模块：`json.load(s)` / `json.dump(obj, indent, sort_keys, separators)`，位于 `src/stdlib/json/`（2026-10-01）
