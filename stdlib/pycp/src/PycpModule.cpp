#include "pycp_stdlib.hpp"
#include "PycpModule.hpp"   // runtime 的 Module 完整定义
#include "PycpFunction.hpp"
#include "PycpString.hpp"
#include "PycpInteger.hpp"
#include "PycpBoolean.hpp"
#include "PycpList.hpp"      // List_length_fn / List_append_fn
#include "PycpMap.hpp"       // Map_length_fn
#include "PycpNone.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"
#include "PycpClass.hpp"
#include "PycpMagic.hpp"     // GetMagicMethodFunction
#include "PycpExt.h"         // PYCP_EXPORT_MODULE（Windows 下带 dllexport）

namespace Pycp {

namespace {

// String(x)：String 类型构造器。调用对象的 __string__ 转换为字符串，
// 返回内置 String 对象。语义对齐 Python 的 str(x)。
Object* _builtin_string_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("String() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("String() argument is null.");
	Object* s = argv[0]->__string__();
	if (s == nullptr) return String::FromCString("");
	// __string__ 可能返回 Borrowed（如 String 返回 this），需 Incref 转为
	// Owned（BuiltinTypeClass::instantiate 期望 Owned 返回值）。
	Incref(s);
	return s;
}

// Integer(x)：Integer 类型构造器。复用 Integer(Object*) 构造：
// Integer 传入返回自身；String 传入解析为整数（String::__integer__，
// 非法抛 ValueError）；其他对象调 __integer__。语义对齐 Python 的 int(x)。
Object* _builtin_integer_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("Integer() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("Integer() argument is null.");
	return New<Integer>(argv[0]);
}

// Boolean(x)：Boolean 类型构造器。复用 Boolean(Object*) 构造。
// Integer 传入（0/非0）转换为 False/True；String 按 Python 规则
// ("", "False", "0" 为 False，其余 True) 由 String::__integer__ 还原。
Object* _builtin_boolean_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("Boolean() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("Boolean() argument is null.");
	return New<Boolean>(argv[0]);
}

// List(x)：List 类型构造器。调用对象的 __list__ 转换，返回内置 List。
// 本版仅 list -> list 幂等（返回自身）；其他类型抛 TypeError。
Object* _builtin_list_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("List() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("List() argument is null.");
	Object* r = argv[0]->__list__();
	if (r == nullptr) throw TypeError("List() conversion failed.");
	return r;
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
	if (kl == nullptr || !kl->is_type("List")) {
		if (kl != nullptr) Decref(kl);
		throw TypeError("Map() argument keys() must return a List.");
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
	List* lst = static_cast<List*>(kl);
	Map* m = Map::New();
	try {
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* k = lst->at(i);
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
Object* _builtin_map_ctor(Object*, Object** argv, std::size_t argc) {
	if (argc == 0) {
		// 空构造：调用方应按约定提供非空 argv 数组（元素为 null）。
		return Map::New();
	}
	if (argc != 1) {
		throw TypeError("Map() expects 0 or 1 argument.");
	}
	Object* src = argv[0];
	if (src == nullptr) throw TypeError("Map() argument is null.");
	if (Map* m = dynamic_cast<Map*>(src)) {
		return m->copy_shallow();
	}
	if (Object* r = _map_from_keys_protocol(src)) return r;
	if (Object* r = _map_from_pairs(src)) return r;
	throw TypeError("cannot convert '" + src->type_name() + "' to Map.");
}

// introspect(obj)：返回包含 obj 所有成员名称（含方法）的 List。
Object* _builtin_introspect(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("introspect() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("introspect() argument is null.");
	return argv[0]->__introspect__();
}

// 将类对象以指定名字放入模块命名空间（构造 BuiltinTypeClass ->
// Incref 进 map -> 释放 Owned）。返回 cls 以便调用方 add_method 注册
// 类型方法，使 Pycp.X.__introspect__() 能枚举到（而非空列表）。
BuiltinTypeClass* set_type_class(Module* mod, const char* name, PycpNativeFunction ctor) {
	auto* ns = mod->get_namespace();
	BuiltinTypeClass* cls = New<BuiltinTypeClass>(name, ctor);
	(*ns)[name] = cls;
	Incref(cls);
	Decref(cls); // namespace 持有
	return cls;
}

// 把一组魔术方法名注册进类型类 methods_（复用 PycpMagic 维护的缓存
// Function，与实例 __get_attribute__ 回退同源）。未识别的魔方法名跳过。
void add_magic_methods(BuiltinTypeClass* cls, const std::vector<std::string>& names) {
	for (const auto& n : names) {
		Object* m = Pycp::GetMagicMethodFunction(n);
		if (m != nullptr) {
			cls->add_method(n, static_cast<Function*>(m));
		}
	}
}

// =============================================================
// private / public：可见性装饰器函数
//
// 作为通用装饰器语法糖：@private / @public 把被装饰对象（函数或任意
// 对象）作为参数传给本函数，本函数设置该对象的可见性后原样返回，由
// 装饰器替换逻辑用返回值替换原对象。可见性经对象通用的
// is_private()/set_private() 属性（C++ ABI 底层）设置。
//
//   - 类内成员：控制该成员在类外的访问可见性（公开/私有）。
//   - 模块顶层符号：控制其他文件 import 时是否可访问。
//
// 注意：本实现与 classtools 库中的 private/public 功能完全一致，
// 两库均导出同名装饰器函数以保证 `from classtools import public`
// 与 `from Pycp import public` 行为一致。
// =============================================================
Object* _builtin_visibility(Object*, Object** argv, std::size_t argc, bool priv) {
	if (argc != 1 || argv == nullptr || argv[0] == nullptr) {
		throw TypeError("visibility decorator expects exactly 1 argument.");
	}
	argv[0]->set_private(priv);
	// 原样返回被装饰对象（装饰器替换逻辑用返回值替换原对象）。
	Incref(argv[0]);
	return argv[0];
}

Object* _builtin_private(Object* self, Object** argv, std::size_t argc) {
	return _builtin_visibility(self, argv, argc, /*priv=*/true);
}

Object* _builtin_public(Object* self, Object** argv, std::size_t argc) {
	return _builtin_visibility(self, argv, argc, /*priv=*/false);
}

// 将原生函数以指定名字放入模块命名空间。
void set_func(Module* mod, const char* name, PycpNativeFunction fn) {
	auto* ns = mod->get_namespace();
	Function* f = New<Function>(name, fn);
	(*ns)[name] = f;
	Incref(f);
	Decref(f); // namespace 持有
}

// Object 的默认 __initialize__（空实现，接受 self，供子类 super() 调用）。
Object* _object_init(Object*, Object** argv, std::size_t argc) {
	(void)argv; (void)argc;
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
Object* _object_get_attribute(Object* /*fn*/, Object** argv, std::size_t argc) {
	if (argc != 2)
		throw TypeError("__get_attribute__() expects 2 arguments (self, name).");
	if (argv[1] == nullptr || !argv[1]->is_type("String"))
		throw TypeError("__get_attribute__() name must be a String.");
	const std::string& nm = static_cast<String*>(argv[1])->get_value();
	return argv[0]->__get_attribute__(nm); // 转发到 C++ 虚方法（Owned 语义）
}

Object* _object_set_attribute(Object* /*fn*/, Object** argv, std::size_t argc) {
	if (argc != 3)
		throw TypeError("__set_attribute__() expects 3 arguments (self, name, value).");
	if (argv[1] == nullptr || !argv[1]->is_type("String"))
		throw TypeError("__set_attribute__() name must be a String.");
	const std::string& nm = static_cast<String*>(argv[1])->get_value();
	argv[0]->__set_attribute__(nm, argv[2]);
	return None::instance;
}

Object* _object_string(Object* /*fn*/, Object** argv, std::size_t argc) {
	if (argc != 1)
		throw TypeError("__string__() expects 1 argument (self).");
	return argv[0]->__string__();
}

// 将普通类对象放入命名空间，并附带默认 __initialize__（用于 Pycp.Object）。
void set_object_class(Module* mod, const char* name) {
	auto* ns = mod->get_namespace();
	Class* cls = New<Class>(name);
	// 默认 __initialize__：空实现，供子类 super().__initialize__(self) 调用。
	Function* init = New<Function>("__initialize__", _object_init);
	cls->add_method("__initialize__", init);
	Decref(init); // add_method 已 Incref
	// Object 协议方法：__get_attribute__ / __set_attribute__ / __string__。
	// 作为可被子类 override 的属性访问/赋值钩子与字符串化默认实现。
	{
		Function* m1 = New<Function>("__get_attribute__", _object_get_attribute);
		cls->add_method("__get_attribute__", m1);
		m1->set_owner_class(cls);
		Decref(m1);
		Function* m2 = New<Function>("__set_attribute__", _object_set_attribute);
		cls->add_method("__set_attribute__", m2);
		m2->set_owner_class(cls);
		Decref(m2);
		Function* m3 = New<Function>("__string__", _object_string);
		cls->add_method("__string__", m3);
		m3->set_owner_class(cls);
		Decref(m3);
	}
	(*ns)[name] = cls;
	Incref(cls);
	Decref(cls); // namespace 持有
}

Module* make_pycp_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 内置类型类：Pycp.String(x) / Pycp.Integer(x)。
	// 调用时走类实例化路径（BuiltinTypeClass::instantiate），把参数
	// 传给构造回调，返回内置 String / Integer 对象。
	// 注册类型方法，使 Pycp.X.__introspect__() 枚举到（与实例 __introspect__ 一致）。
	BuiltinTypeClass* string_cls = set_type_class(mod, "String", _builtin_string_ctor);
	BuiltinTypeClass* integer_cls = set_type_class(mod, "Integer", _builtin_integer_ctor);
	BuiltinTypeClass* list_cls    = set_type_class(mod, "List",   _builtin_list_ctor);
	BuiltinTypeClass* boolean_cls = set_type_class(mod, "Boolean", _builtin_boolean_ctor);
	BuiltinTypeClass* map_cls     = set_type_class(mod, "Map",    _builtin_map_ctor);

	// List 的公开方法（真实实例方法）：length / append。
	list_cls->add_method("length", New<Function>("length", List_length_fn()));
	list_cls->add_method("append", New<Function>("append", List_append_fn()));
	// List 魔术方法。
	add_magic_methods(list_cls, {
		"__iterator__", "__list__", "__addition__", "__string__",
		"__get_item__", "__set_item__",
	});

	// Map 的公开方法（真实实例方法）：length / keys。
	map_cls->add_method("length", New<Function>("length", Map_length_fn()));
	map_cls->add_method("keys",   New<Function>("keys",   Map_keys_fn()));
	// Map 魔术方法（Map 本身不可哈希，故不注册 __hash__）。
	add_magic_methods(map_cls, {
		"__map__", "__boolean__", "__string__",
		"__get_item__", "__set_item__", "__delete_item__",
	});

	// String 魔术方法（无真实公开非魔术方法）。
	add_magic_methods(string_cls, {
		"__integer__", "__string__", "__addition__", "__multiplication__",
		"__get_item__", "__list__", "__iterator__",
	});

	// Integer 魔术方法（算术 / 比较 / 一元）。
	add_magic_methods(integer_cls, {
		"__integer__", "__string__", "__negation__", "__addition__",
		"__subtraction__", "__multiplication__", "__division__", "__power__",
		"__less_than__", "__less_equal__", "__equal__", "__not_equal__",
		"__greater_than__", "__greater_equal__",
	});

	// Boolean 魔术方法（继承 Integer 算术/比较，复用同一集合）。
	add_magic_methods(boolean_cls, {
		"__integer__", "__string__", "__negation__", "__addition__",
		"__subtraction__", "__multiplication__", "__division__", "__power__",
		"__less_than__", "__less_equal__", "__equal__", "__not_equal__",
		"__greater_than__", "__greater_equal__",
	});

	// Pycp.Object 基类：类似 Python 的 object，含默认空 __initialize__
	// （供子类 super().__initialize__(self) 调用）。不自动继承；
	// 实例化走默认 instantiate（返回 Instance）。
	set_object_class(mod, "Object");

	// 可见性装饰器函数：@private / @public（与 classtools 库功能一致）。
	set_func(mod, "private", _builtin_private);
	set_func(mod, "public",  _builtin_public);

	// introspect(obj)：返回对象所有成员名称（含方法）的 List。
	set_func(mod, "introspect", _builtin_introspect);

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