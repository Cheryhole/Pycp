#include "pycp_stdlib.hpp"
#include "visibility.hpp"          // common：private/public/readonly 的唯一实现
#include "object/PycpModule.hpp"   // runtime 的 Module 完整定义
#include "object/PycpFunction.hpp"
#include "object/PycpString.hpp"    // String_method_table
#include "object/PycpInteger.hpp"   // Integer_method_table（Boolean 复用）
#include "object/PycpBoolean.hpp"
#include "object/PycpFloat.hpp"     // Float_method_table
#include "object/PycpDecimal.hpp"   // Decimal_method_table
#include "object/PycpList.hpp"      // List_method_table
#include "object/PycpFixedList.hpp" // FixedList_method_table / FixedList
#include "object/PycpMap.hpp"       // Map_method_table
#include "object/PycpNone.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpException.hpp"
#include "object/PycpConfig.hpp"
#include "abi/PycpABI.hpp"
#include "object/PycpClass.hpp"     // RegisterObjectClass / 类型对象注册（Module::set_type）
#include "abi/PycpNativeExt.hpp" // SetArgv/GetArgv：构建 pycp.argv 的宿主注入来源
#include "object/PycpExtension.hpp" // 扩展唯一对外头（导出宏 + set_* + 参数规范框架）
#include "bytecode/PycpBytecode.hpp"       // BC::Module（exec 的模块对象入参）
#include "bytecode/PycpBytecodeObject.hpp" // BC::IsModuleRef / UnwrapModule

#include <memory>

namespace Pycp {

namespace {

// 参数拆箱辅助（业务侧自行判型：框架只校验个数与名字，不做类型检查）。
std::string require_string(const char* fn, const char* param, Object* v) {
	if (v == nullptr || !IsString(v)) {
		throw TypeError(std::string(fn) + ": argument '" + param +
		                "' expects a string, got '" +
		                (v != nullptr ? v->type_name() : std::string("None")) + "'.");
	}
	return AsString(v);
}

// String([x])：String 类型构造器。语义对齐 Python 的 str(x)。
//   0 参：空白串 ""（对应 str()）。
//   1 参：调用对象的 __string__ 转换为字符串。
// 参数声明为 Optional + given 判断：省略 -> 空串；显式 None -> 走转换（"None"）。
Object* _builtin_string_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"String", { Extension::Arg::Optional("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	if (!r.given("value")) {
		return String::FromCString("");
	}
	Object* src = r["value"];
	if (src == nullptr) throw TypeError("String() argument is null.");
	Object* s = src->__string__();
	if (s == nullptr) return String::FromCString("");
	// __string__ 可能返回 Borrowed（如 String 返回 this），需 Incref 转为
	// Owned（BuiltinTypeClass::instantiate 期望 Owned 返回值）。
	Incref(s);
	return s;
}

// Integer([x])：Integer 类型构造器。语义对齐 Python 的 int(x)。
//   0 参：整数 0（对应 int()）。
//   1 参：复用 Integer(Object*) 构造 —— Integer 传入返回自身；String 传入
//         解析为整数（非法抛 ValueError）；其他对象调 __integer__。
Object* _builtin_integer_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"Integer", { Extension::Arg::Optional("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	if (!r.given("value")) {
		return Integer::FromLong(0);
	}
	Object* src = r["value"];
	if (src == nullptr) throw TypeError("Integer() argument is null.");
	return New<Integer>(src);
}

// Boolean([x])：Boolean 类型构造器。语义对齐 Python 的 bool(x)。
//   0 参：False（对应 bool()）。
//   1 参：复用 Boolean(Object*) 构造 —— Integer 传入（0/非0）转换为
//         False/True；String 按 Python 规则（"", "False", "0" 为 False，
//         其余 True）由 String::__integer__ 还原。
Object* _builtin_boolean_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"Boolean", { Extension::Arg::Optional("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	if (!r.given("value")) {
		// 新建 Owned 的 False，而非返回常驻单例 Boolean::False()：后者是
		// PERMANENT + GC_AddRoot 的共享实例，调用方会按「持有 1 份引用」
		// Decref 返回值，直接返回单例会把 Decref 打到常驻对象上。
		return New<Boolean>(0);
	}
	Object* src = r["value"];
	if (src == nullptr) throw TypeError("Boolean() argument is null.");
	return New<Boolean>(src);
}

// Float([x])：Float 类型构造器。语义对齐 Python 的 float(x)。
//   0 参：浮点 0.0（对应 float()）。
//   1 参：复用 Float(Object*) 构造 —— Float 传入返回自身；Integer/String
//         传入解析为 double（非法抛 ValueError）；其他对象调 __float__。
Object* _builtin_float_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"Float", { Extension::Arg::Optional("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	if (!r.given("value")) {
		return New<Float>(0.0);
	}
	Object* src = r["value"];
	if (src == nullptr) throw TypeError("Float() argument is null.");
	return New<Float>(src);
}

// Decimal([x])：Decimal 类型构造器。语义对齐 Python 的 decimal.Decimal(x)。
//   0 参：整数 0（对应 Decimal()）。
//   1 参：Decimal 传入返回副本；String 传入按十进制文本精确解析（"1.23"、
//         科学计数法等，非法抛 ValueError）；Integer/Float/其他对象经
//         __string__ 取文本形式再解析。
Object* _builtin_decimal_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"Decimal", { Extension::Arg::Optional("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	if (!r.given("value")) {
		return New<Decimal>(INT64_C(0));
	}
	Object* src = r["value"];
	if (src == nullptr) throw TypeError("Decimal() argument is null.");
	return New<Decimal>(src);
}

// List([x])：List 类型构造器。语义对齐 Python 的 list(x)。
//   0 参：空列表 []（对应 list()）。
//   1 参：调用对象的 __list__ 转换；本版仅 list -> list 幂等（返回自身），
//         其他类型抛 TypeError。
Object* _builtin_list_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"List", { Extension::Arg::Optional("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	if (!r.given("value")) {
		return List::New();
	}
	Object* src = r["value"];
	if (src == nullptr) throw TypeError("List() argument is null.");
	Object* out = src->__list__();
	if (out == nullptr) throw TypeError("List() conversion failed.");
	return out;
}

// FixedList([x])：FixedList 类型构造器（对应 Python tuple(x)）。
//   0 参：空 FixedList。
//   1 参：转换 —— FixedList/List 浅拷贝为独立 FixedList；其他可迭代对象经
//          __iterator__ 逐个取出材料化；不可迭代抛 TypeError。
// source 为可选参数，省略时规范表给出默认值 None（表示空构造）。
Object* _builtin_fixedlist_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"FixedList", { Extension::Arg::Optional("source") });   // 省略 -> None
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* src = r["source"];
	if (!r.given("source") || src == nullptr || src == None::instance) {
		return FixedList::New(std::vector<Object*>{});
	}
	if (src == nullptr) throw TypeError("FixedList() argument is null.");

	bool is_fixed = IsType(src, PycpTypeId::FixedList);
	bool is_list  = IsType(src, PycpTypeId::List);
	// FixedList / List：浅拷贝为独立 FixedList（元素引用转移，先 Incref）。
	if (is_fixed || is_list) {
		std::size_t n = is_fixed ? static_cast<FixedList*>(src)->size()
		                         : static_cast<List*>(src)->size();
		std::vector<Object*> items;
		items.reserve(n);
		for (std::size_t i = 0; i < n; ++i) {
			Object* e = is_fixed ? static_cast<FixedList*>(src)->at(i)
			                     : static_cast<List*>(src)->at(i);
			if (e != nullptr) {
				Incref(e);
				items.push_back(e);
			}
		}
		return FixedList::New(items);
	}

	// 其他可迭代对象：经 __iterator__ / __next__ 逐个取出材料化。
	Object* it = nullptr;
	try {
		it = src->__iterator__();   // Owned（新迭代器）
	} catch (const TypeError&) {
		throw TypeError("cannot convert '" + src->type_name() + "' to FixedList.");
	}
	std::vector<Object*> items;
	try {
		for (;;) {
			Object* item = nullptr;
			try {
				item = it->__next__();   // Owned；耗尽抛 StopIteration
			} catch (const StopIteration&) {
				break;
			}
			items.push_back(item);
		}
	} catch (...) {
		// 非 StopIteration 异常：释放已收集元素与迭代器后原样抛出。
		Decref(it);
		for (Object* o : items) Decref(o);
		throw;
	}
	Decref(it);
	return FixedList::New(items);   // 接管 items（Owned）
}

// Map(x) 的 keys 协议（对齐 Python dict(mapping)）：
// 对象同时提供 keys()（返回键 List）与 __get_item__(k) 时，遍历 keys()
// 的每个键、经下标取值后写入新 Map；自定义类定义这两个方法同样生效。
// 返回 nullptr 表示本协议不适用（无 keys 方法，交由下一协议尝试）；
// 命中协议但用法错误（keys 不可调用 / 返回值非 List / 缺 __get_item__）
// 直接抛 TypeError，不静默降级，避免掩盖用户代码错误。
static Object* _map_from_keys_protocol(Object* src) {
	Object* keys_fn = nullptr;
	try {
		keys_fn = GetAttr(src, "keys");
	} catch (const AttributeError&) {
		return nullptr; // 无 keys：交给二元组协议
	}
	if (keys_fn == nullptr) return nullptr;
	if (!keys_fn->is_type("Function")) {
		Decref(keys_fn);
		throw TypeError("Map() argument 'keys' is not callable.");
	}
	// keys 无参：BoundMethod 自动注入 self，argv 传 nullptr 安全。
	Object* kl = Call(keys_fn, nullptr, 0); // Owned
	Decref(keys_fn);
	// keys() 可返回 List 或 FixedList（均为键快照序列）。
	bool keys_is_list  = IsType(kl, PycpTypeId::List);
	bool keys_is_fixed = IsType(kl, PycpTypeId::FixedList);
	if (kl == nullptr || (!keys_is_list && !keys_is_fixed)) {
		if (kl != nullptr) Decref(kl);
		throw TypeError("Map() argument keys() must return a List or FixedList.");
	}
	// __get_item__ 探测（实际取值走 GetItem，与 x[k] 语义一致）。
	Object* getitem = nullptr;
	try {
		getitem = GetAttr(src, "__get_item__");
	} catch (const AttributeError&) {
		getitem = nullptr;
	}
	bool has_item = (getitem != nullptr && getitem->is_type("Function"));
	if (getitem != nullptr) Decref(getitem); // 仅探测，用完释放
	if (!has_item) {
		Decref(kl);
		throw TypeError("Map() argument must support keys() and __get_item__().");
	}
	// 统一按「长度 + 下标」访问（List / FixedList 接口一致，仅容器类型不同）。
	auto key_count = [&]() -> std::size_t {
		return keys_is_list ? static_cast<List*>(kl)->size()
		                    : static_cast<FixedList*>(kl)->size();
	};
	auto key_at = [&](std::size_t i) -> Object* {
		return keys_is_list ? static_cast<List*>(kl)->at(i)
		                    : static_cast<FixedList*>(kl)->at(i);
	};
	Map* m = Map::New();
	try {
		for (std::size_t i = 0; i < key_count(); ++i) {
			Object* k = key_at(i);
			if (k == nullptr) {
				throw TypeError("Map() got a null key from keys().");
			}
			// GetItem 返回 Borrowed（与 VM 的 GET_ITEM 一致），写入时由
			// __set_item__ 负责 Incref，此处不额外 Decref。
			Object* v = GetItem(src, k);
			if (v == nullptr) {
				throw TypeError("Map() got a null value from __get_item__().");
			}
			Object* r = m->__set_item__(k, v);
			if (r != nullptr) Decref(r);
		}
	} catch (...) {
		// 异常路径：释放已构造的 Map 与键 List 后原样抛出。
		Decref(m);
		Decref(kl);
		throw;
	}
	Decref(kl);
	return m;
}

// Map(x) 的二元组协议（对齐 Python dict(pairs)）：
// 入参为 List 且每个元素为 2 元 List（[k, v]）时逐对写入新 Map；
// 元素形态不符直接抛 TypeError。非 List 入参返回 nullptr 表示协议不适用。
static Object* _map_from_pairs(Object* src) {
	if (!src->is_type("List")) return nullptr;
	List* lst = static_cast<List*>(src);
	Map* m = Map::New();
	for (std::size_t i = 0; i < lst->size(); ++i) {
		Object* pair = lst->at(i);
		if (pair == nullptr || !pair->is_type("List") ||
		    static_cast<List*>(pair)->size() != 2) {
			Decref(m);
			throw TypeError("Map() expects a sequence of 2-element lists.");
		}
		Object* k = static_cast<List*>(pair)->at(0);
		Object* v = static_cast<List*>(pair)->at(1);
		if (k == nullptr || v == nullptr) {
			Decref(m);
			throw TypeError("Map() pair contains a null key or value.");
		}
		Object* r = m->__set_item__(k, v);
		if (r != nullptr) Decref(r);
	}
	return m;
}

// Map() / Map(x)：Map 类型构造器。
//   - 无参数：返回空 Map。
//   - 1 参数：转换（对齐 Python dict(x)），按序尝试
//       * Map（含 __map__ 视图）-> 独立浅拷贝（视图材料化为独立快照）
//       * 提供 keys() + __get_item__(k) 的对象 -> 逐键取值构造
//       * 二元组 List（[[k, v], ...]）-> 逐对构造
//       * 其余类型 -> TypeError
// 可选参数：省略时规范表给出默认值 None（表示空构造）。
Object* _builtin_map_ctor(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"Map", { Extension::Arg::Optional("source") });   // 省略 -> None
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* src = r["source"];
	if (!r.given("source") || src == nullptr || src == None::instance) {
		// 空构造（无实参）。
		return Map::New();
	}
	if (src == nullptr) throw TypeError("Map() argument is null.");
	if (Map* m = dynamic_cast<Map*>(src)) {
		return m->copy_shallow();
	}
	if (Object* r = _map_from_keys_protocol(src)) return r;
	if (Object* r = _map_from_pairs(src)) return r;
	throw TypeError("cannot convert '" + src->type_name() + "' to Map.");
}

// insp(obj)：返回包含 obj 所有成员名称（含方法）的 List。
// 模块公开接口名与 Python 惯例一致用短名 insp（inspect 的缩写）。
Object* _builtin_insp(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"insp", { Extension::Arg::Required("obj") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* obj = r["obj"];
	if (obj == nullptr) throw TypeError("insp() argument is null.");
	return obj->__inspect__();
}

// typeof(obj)：返回 obj 所属的类对象（类对象返回 pycp.Object）。
// 语义对齐 Python 的 type(x) / x.__class__。
Object* _builtin_typeof(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"typeof", { Extension::Arg::Required("obj") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* obj = r["obj"];
	if (obj == nullptr) throw TypeError("typeof() argument is null.");
	Class* c = obj->get_type_class();
	if (c == nullptr) {
		throw TypeError("typeof(): cannot determine type class of '" +
		                 obj->type_name() + "'.");
	}
	Incref(c); // 返回 Owned（类对象由注册表/实例持有，此处增持引用）
	return c;
}

// =============================================================
// private / public / readonly：可见性 / 只读装饰器
//
// 实现已上提到 stdlib/common（visibility.hpp）：本库与 classtools 库曾
// 逐字重复同一实现，现共用一份；此处仅在 make_pycp_module 中注册，
// 保证 `from pycp import public` 与 `from classtools import public` 等价。
// =============================================================

// Object 的默认 __initialize__（空实现，接受 self，供子类 super() 调用）。
Object* _object_init(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec =
		Extension::CompileArgs("__initialize__", {});
	spec.Bind(args, kwargs);
	return None::instance; // 无操作
}

// ---- Object 默认魔术方法（native 包装 C++ 虚方法行为）----
// 这些方法注册到 Pycp.Object，作为可被子类 override 的协议方法：
//   __get_attribute__(self, name) : 属性取值钩子
//   __set_attribute__(self, name, value) : 属性赋值钩子
//   __string__() : 字符串化
// 注意：Instance 的属性访问/赋值钩子分派对 Object 默认实现回退 C++
// 内部路径（见 PycpClass.cpp），故这些 native 主要供方法存在性/枚举/
// super() 调用，且用户 override 后优先走用户实现。
// name 须为 String（业务侧自行判型，框架只校验个数与名字）。
Object* _object_get_attribute(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"__get_attribute__", { Extension::Arg::Required("name") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::string name = require_string("__get_attribute__", "name", r["name"]);
	return self->__get_attribute__(name); // 转发到 C++ 虚方法（Owned 语义）
}

Object* _object_set_attribute(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"__set_attribute__", { Extension::Arg::Required("name"),
		                       Extension::Arg::Required("value") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	std::string name = require_string("__set_attribute__", "name", r["name"]);
	self->__set_attribute__(name, r["value"]);
	return None::instance;
}

Object* _object_string(Object* self, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec =
		Extension::CompileArgs("__string__", {});
	spec.Bind(args, kwargs);
	return self->__string__();
}

// Pycp.Object 的方法表（协议方法：属性访问/赋值钩子、字符串化）。
// 对象自身的 __initialize__ 经 Module::set_type 的 initialize 参数注册，
// 与其它方法一并随对象一次性完成。
const std::vector<MethodEntry>& Object_method_table() {
	static const std::vector<MethodEntry> table = {
		{"__get_attribute__", _object_get_attribute},
		{"__set_attribute__", _object_set_attribute},
		{"__string__",        _object_string},
		{"__raw_string__",    nullptr},   // 魔术方法：经 GetMagicMethodFunction 分派
	};
	return table;
}

// exec(code [, globals])：执行源码字符串或字节码模块对象（对齐 Python exec）。
//   code    : 源码字符串，或 bytecode.compile 返回的字节码模块对象；
//   globals : 可选 Map。给定则以其为全局命名空间执行，并把执行后新增 /
//             改动的名字写回（对齐 exec(code, globals)）；省略 / None 则用
//             全新全局命名空间。
// 返回顶层结果（Owned）。字节码模块对象经运行时 VM 执行；源码字符串经宿主
// 注册的 SourceExecutor 钩子编译并执行。
Object* _builtin_exec(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("exec", {
		Extension::Arg::Required("code"),
		Extension::Arg::Optional("globals"),
	});
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* code = r["code"];
	Object* globals = r.given("globals") ? r["globals"] : nullptr;
	if (code == nullptr) throw TypeError("exec: argument 'code' is null.");

	if (BC::IsModuleRef(code)) {
		std::shared_ptr<BC::Module> mod = BC::UnwrapModule(code);
		if (!mod) throw TypeError("exec: invalid bytecode module object.");
		return RunBytecodeModule(mod.get(), globals);
	}
	if (IsString(code)) {
		return ExecSourceString(AsString(code), "<exec>", globals);
	}
	throw TypeError("exec: argument must be a bytecode module or a source string.");
}

Module* make_pycp_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 内置类型类：一次性完整注册（方法表驱动，含类型类登记）。
	// 调用时走类实例化路径（BuiltinTypeClass::instantiate），把参数传给
	// 构造回调并返回内置对象（语义对齐 Python 的 str(x)/int(x)/list(x) 等）。
	// 方法表为各类型「全部方法」的唯一权威来源，注册与 __inspect__ 同源。
	// 函数名与可选参数默认值均由各构造器内部的参数规范表登记（构造调用时
	// self 为 nullptr，故规范表内显式写明函数名以生成可读报错）。
	mod->set_type("String", _builtin_string_ctor, /*initialize=*/nullptr,
	              String_method_table);
	mod->set_type("Integer", _builtin_integer_ctor, /*initialize=*/nullptr,
	              Integer_method_table);
	// Boolean 继承 Integer，复用同一方法表。
	mod->set_type("Boolean", _builtin_boolean_ctor, /*initialize=*/nullptr,
	              Integer_method_table);
	// 浮点类型（IEEE 754 double，8 字节）。
	mod->set_type("Float", _builtin_float_ctor, /*initialize=*/nullptr,
	              Float_method_table);
	// 精确小数类型（mpdecimal 实现，默认精度 28 位有效数字）。
	mod->set_type("Decimal", _builtin_decimal_ctor, /*initialize=*/nullptr,
	              Decimal_method_table);
	mod->set_type("List", _builtin_list_ctor, /*initialize=*/nullptr,
	              List_method_table);
	// 可选参数 source 省略 -> None（空 FixedList / 空 Map）。
	mod->set_type("FixedList", _builtin_fixedlist_ctor, /*initialize=*/nullptr,
	              FixedList_method_table);
	mod->set_type("Map", _builtin_map_ctor, /*initialize=*/nullptr,
	              Map_method_table);

	// Pycp.Object 基类：类似 Python 的 object。对象自身的 __initialize__
	// （空实现）随对象一次性注册，供子类 super().__initialize__(self) 调用；
	// 协议方法（__get_attribute__ 等）由 Object_method_table 提供。
	// 不自动继承，实例化走默认 instantiate（返回 Instance）。
	Class* object_cls = mod->set_type("Object", /*ctor=*/nullptr, _object_init,
	                                  Object_method_table);
	RegisterObjectClass(object_cls); // 类对象的 typeof/__class__ 返回它

	// 可见性/只读装饰器：@private / @public / @readonly
	// （与 classtools 库功能一致；实现见 stdlib/common/visibility.hpp）。
	RegisterVisibilityDecorators(mod);

	// insp(obj)：返回对象所有成员名称（含方法）的 List。
	mod->set_function("insp", _builtin_insp);

	// typeof(obj)：返回对象所属的类对象（类对象返回 pycp.Object）。
	mod->set_function("typeof", _builtin_typeof);

	// exec(code [, globals])：执行源码字符串 / 字节码模块对象。
	mod->set_function("exec", _builtin_exec, /*with_keywords=*/true);

	// argv：命令行参数列表（对齐 Python 的 sys.argv）。构造时从宿主注入的
	// 全局 argv（Pycp::GetArgv）构建为 List[String]，读一次快照加入命名空间。
	// 未注入（如 REPL）时为空列表 []。各元素为 String（FromCString 返回 Owned，
	// append 内部 Incref），故列表与元素均交命名空间持有引用。
	{
		List* argv_list = List::New();
		for (const auto& a : Pycp::GetArgv()) {
			argv_list->append(String::FromCString(a.c_str()));
		}
		mod->set_variable("argv", argv_list);
	}

	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_pycp，按模块名导出）。
// 由 VM::load_module 经 LoadNativeModule 的 dlsym("PycpModule_pycp") 调用。
// 必须走 PYCP_EXPORT_MODULE：Windows 下没有 __declspec(dllexport) 时，
// 只有"整库无任何显式导出"才会被 MinGW 自动全导出，一旦本 TU 出现任何
// 其它导出符号，GetProcAddress 就找不到入口，import pycp 会静默失效。
PYCP_EXPORT_MODULE(pycp) {
	return make_pycp_module();
}

} // namespace Pycp