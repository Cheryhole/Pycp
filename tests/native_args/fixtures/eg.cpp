// =============================================================
// 参数规范表测试夹具扩展（eg）
// -------------------------------------------------------------
// 用新导出 API（PycpExtension.hpp + 容器形态原生函数）覆盖：
//   1) 位置 + 默认值 + *rest 的分拣（pick）
//   2) *rest 的零个 / 多个收集（sum_rest）
//   3) **kw 槽位（echo；当前语言层无关键字实参来源，恒空 Map）
//
// 由 tests/native_args/run.sh 编译为 eg.so 并放入沙箱后被脚本 import。
// =============================================================

#include "PycpExtension.hpp"

#include <vector>

using namespace Pycp;

namespace {

// pick(a[, b][, *rest]) -> FixedList(a, b, rest)
// b 省略时取默认值 0；剩余实参收集为不可变元组（零个 -> 空元组）。
Object* eg_pick(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("pick", {
		Extension::Arg::Required("a"),
		Extension::Arg::Optional("b", Integer::FromLong(0)),
		Extension::Arg::Rest("rest"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);

	// 三个槽位均为 Borrowed，需各自 Incref 后交给新 FixedList 接管。
	std::vector<Object*> items;
	items.reserve(3);
	Object* a = r["a"];
	Incref(a);
	items.push_back(a);
	Object* b = r["b"];
	Incref(b);
	items.push_back(b);
	Object* rest = r["rest"];
	Incref(rest);
	items.push_back(rest);
	return FixedList::New(items);
}

// sum_rest(*rest) -> Integer：全部实参求和（零个 -> 0）。
Object* eg_sum_rest(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("sum_rest", {
		Extension::Arg::Rest("rest"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	FixedList* rest = static_cast<FixedList*>(r["rest"]);
	long long total = 0;
	for (std::size_t i = 0; i < rest->size(); ++i) {
		Object* v = rest->at(i);
		if (!IsIntegerExact(v)) {
			throw TypeError("sum_rest(): arguments must be integers.");
		}
		total += static_cast<Integer*>(v)->get_value();
	}
	return Integer::FromLong(total);
}

// echo([*rest][, **kw]) -> String：打印 *rest 与 **kw 的收集结果。
Object* eg_echo(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("echo", {
		Extension::Arg::Rest("rest"),
		Extension::Arg::Keyword("kw"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::vector<Object*> items;
	Object* rest = r["rest"];
	Incref(rest);
	items.push_back(rest);
	Object* kw = r["kw"];
	Incref(kw);
	items.push_back(kw);
	FixedList* pair = FixedList::New(items);
	Object* s = pair->__string__();
	Decref(pair);
	return s;
}

} // namespace

PYCP_EXPORT_MODULE(eg) {
	Module* mod = Module::New("eg");
	mod->set_function("pick", eg_pick);
	mod->set_function("sum_rest", eg_sum_rest);
	mod->set_function("echo", eg_echo, /*with_keywords=*/true);
	return mod;
}
