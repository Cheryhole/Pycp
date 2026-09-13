// =============================================================
// 参数规范表测试夹具扩展（eg）
// -------------------------------------------------------------
// 用新导出 API（PycpExtension.hpp + 容器形态原生函数）覆盖：
//   1) 位置 + 默认值 + *rest 的分拣（pick）
//   2) *rest 的零个 / 多个收集（sum_rest）
//   3) **kw 槽位（echo；调用方经关键字传参时有值，否则空 Map）
//   4) 位置 + *rest + 关键字-only 参数（kwonly / kwonly_opt）：写在
//      Rest 之后的 Required / Optional 位置隐含为必填 / 可选关键字-only，
//      必须按关键字传（无显式 Arg::Keyword 工厂）
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
		Extension::Arg::RestKeywords("kw"),
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

// kwonly(a, *rest, b) -> FixedList(a, rest, b)：
// b 写在 *rest 之后 => 关键字-only（必填，必须按关键字传）。
Object* eg_kwonly(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("kwonly", {
		Extension::Arg::Required("a"),
		Extension::Arg::Rest("rest"),
		Extension::Arg::Required("b"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);

	std::vector<Object*> items;
	Object* a = r["a"];
	Incref(a);
	items.push_back(a);
	Object* rest = r["rest"];
	Incref(rest);
	items.push_back(rest);
	Object* b = r["b"];
	Incref(b);
	items.push_back(b);
	return FixedList::New(items);
}

// kwonly_opt(a, *rest, mode = 0) -> FixedList(a, rest, mode)：
// mode 写在 *rest 之后 => 可选关键字-only（必须按关键字传；省略取默认值 0）。
Object* eg_kwonly_opt(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("kwonly_opt", {
		Extension::Arg::Required("a"),
		Extension::Arg::Rest("rest"),
		Extension::Arg::Optional("mode", Integer::FromLong(0)),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);

	std::vector<Object*> items;
	Object* a = r["a"];
	Incref(a);
	items.push_back(a);
	Object* rest = r["rest"];
	Incref(rest);
	items.push_back(rest);
	Object* mode = r["mode"];
	Incref(mode);
	items.push_back(mode);
	return FixedList::New(items);
}

} // namespace

PYCP_EXPORT_MODULE(eg) {
	Module* mod = Module::New("eg");
	mod->set_function("pick", eg_pick);
	mod->set_function("sum_rest", eg_sum_rest);
	mod->set_function("echo", eg_echo, /*with_keywords=*/true);
	mod->set_function("kwonly", eg_kwonly, /*with_keywords=*/true);
	mod->set_function("kwonly_opt", eg_kwonly_opt, /*with_keywords=*/true);
	return mod;
}
