#include "pycp_stdlib.hpp"
#include "PycpModule.hpp"   // runtime 的 Module 完整定义
#include "PycpFunction.hpp"
#include "PycpString.hpp"    // String_method_table
#include "PycpInteger.hpp"   // Integer_method_table（Boolean 复用）
#include "PycpBoolean.hpp"
#include "PycpList.hpp"      // List_method_table
#include "PycpMap.hpp"       // Map_method_table
#include "PycpMethodTable.hpp" // MethodEntry / MethodTableFn（RegisterTypeObject 用）
#include "PycpNone.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"
#include "PycpABI.hpp"
#include "PycpClass.hpp"     // RegisterTypeObject / RegisterObjectClass
#include "PycpNativeExt.hpp" // SetArgv/GetArgv：构建 pycp.argv 的宿主注入来源
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

// insp(obj)：返回包含 obj 所有成员名称（含方法）的 List。
// 模块公开接口名与 Python 惯例一致用短名 insp（inspect 的缩写）。
Object* _builtin_insp(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("insp() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("insp() argument is null.");
	return argv[0]->__inspect__();
}

// typeof(obj)：返回 obj 所属的类对象（类对象返回 pycp.Object）。
// 语义对齐 Python 的 type(x) / x.__class__。
Object* _builtin_typeof(Object*, Object** argv, std::size_t argc) {
	if (argc != 1) throw TypeError("typeof() expects exactly 1 argument.");
	if (argv[0] == nullptr) throw TypeError("typeof() argument is null.");
	Class* c = argv[0]->get_type_class();
	if (c == nullptr) {
		throw TypeError("typeof(): cannot determine type class of '" +
		                 argv[0]->type_name() + "'.");
	}
	Incref(c); // 返回 Owned（类对象由注册表/实例持有，此处增持引用）
	return c;
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

// @readonly 装饰器：把被装饰对象（变量/函数/类/实例/任意对象）设为只读
// 后原样返回（与 classtools.readonly 行为一致）。只读语义由底层
// readonly_ 标志承载：任意对象冻结（属性写/删被拒）、模块常量绑定
// （不可再赋值覆盖）、类成员只读字段。
Object* _builtin_readonly(Object*, Object** argv, std::size_t argc) {
	if (argc != 1 || argv == nullptr || argv[0] == nullptr) {
		throw TypeError("readonly decorator expects exactly 1 argument.");
	}
	argv[0]->set_readonly(true);
	// 原样返回被装饰对象（装饰器替换逻辑用返回值替换原对象）。
	Incref(argv[0]);
	return argv[0];
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

// Pycp.Object 的方法表（协议方法：属性访问/赋值钩子、字符串化）。
// 对象自身的 __initialize__ 经 RegisterTypeObject 的 initialize 参数注册，
// 与其它方法一并随对象一次性完成。
const std::vector<MethodEntry>& Object_method_table() {
	static const std::vector<MethodEntry> table = {
		{"__get_attribute__", _object_get_attribute},
		{"__set_attribute__", _object_set_attribute},
		{"__string__",        _object_string},
	};
	return table;
}

Module* make_pycp_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 内置类型类：一次性完整注册（方法表驱动，含类型类登记）。
	// 调用时走类实例化路径（BuiltinTypeClass::instantiate），把参数传给
	// 构造回调并返回内置对象（语义对齐 Python 的 str(x)/int(x)/list(x) 等）。
	// 方法表为各类型「全部方法」的唯一权威来源，注册与 __inspect__ 同源。
	RegisterTypeObject(mod, "String",  _builtin_string_ctor,  /*initialize=*/nullptr,
	                String_method_table);
	RegisterTypeObject(mod, "Integer", _builtin_integer_ctor, /*initialize=*/nullptr,
	                Integer_method_table);
	// Boolean 继承 Integer，复用同一方法表。
	RegisterTypeObject(mod, "Boolean", _builtin_boolean_ctor, /*initialize=*/nullptr,
	                Integer_method_table);
	RegisterTypeObject(mod, "List",    _builtin_list_ctor,    /*initialize=*/nullptr,
	                List_method_table);
	RegisterTypeObject(mod, "Map",     _builtin_map_ctor,     /*initialize=*/nullptr,
	                Map_method_table);

	// Pycp.Object 基类：类似 Python 的 object。对象自身的 __initialize__
	// （空实现）随对象一次性注册，供子类 super().__initialize__(self) 调用；
	// 协议方法（__get_attribute__ 等）由 Object_method_table 提供。
	// 不自动继承，实例化走默认 instantiate（返回 Instance）。
	Class* object_cls = RegisterTypeObject(mod, "Object", /*ctor=*/nullptr,
	                                    _object_init, Object_method_table);
	RegisterObjectClass(object_cls); // 类对象的 typeof/__class__ 返回它

	// 可见性装饰器函数：@private / @public（与 classtools 库功能一致）。
	set_func(mod, "private", _builtin_private);
	set_func(mod, "public",  _builtin_public);

	// 只读装饰器：@readonly（对象冻结 / 模块常量绑定 / 只读成员）。
	set_func(mod, "readonly", _builtin_readonly);

	// insp(obj)：返回对象所有成员名称（含方法）的 List。
	set_func(mod, "insp", _builtin_insp);

	// typeof(obj)：返回对象所属的类对象（类对象返回 pycp.Object）。
	set_func(mod, "typeof", _builtin_typeof);

	// argv：命令行参数列表（对齐 Python 的 sys.argv）。构造时从宿主注入的
	// 全局 argv（Pycp::GetArgv）构建为 List[String]，读一次快照加入命名空间。
	// 未注入（如 REPL）时为空列表 []。各元素为 String（FromCString 返回 Owned，
	// append 内部 Incref），故列表与元素均交命名空间持有引用。
	{
		auto* ns = mod->get_namespace();
		List* argv_list = List::New();
		for (const auto& a : Pycp::GetArgv()) {
			argv_list->append(String::FromCString(a.c_str()));
		}
		(*ns)["argv"] = argv_list;
		Incref(argv_list);
		Decref(argv_list); // 命名空间持有
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