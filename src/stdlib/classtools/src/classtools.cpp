#include "classtools.hpp"
#include "visibility.hpp"          // common：private/public/readonly 的唯一实现
#include "object/PycpModule.hpp"   // runtime 的 Module 完整定义
#include "object/PycpFunction.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpException.hpp"
#include "object/PycpConfig.hpp"
#include "abi/PycpABI.hpp"
#include "object/PycpClass.hpp"
#include "object/PycpExtension.hpp" // 扩展唯一对外头（导出宏 + set_* + 参数规范框架）

namespace Pycp {

namespace {

// super()：返回当前类的父类 Class（构造函数/类型本身，而非实例）。
// 通过 thread_local 当前 self 上下文（push_current_self/current_self）取
// 当前方法执行中的接收者实例，再取其所属类 → 父类。语义对齐 Python 的
// super()。返回 Borrowed 引用经 Incref 转为 Owned。
// 无参：个数校验由参数规范表完成。
Object* _builtin_super(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs("super", {});
	spec.Bind(args, kwargs);
	Instance* self = current_self();
	if (self == nullptr) {
		throw TypeError("super() used outside a method.");
	}
	// 基于"当前方法所属类"解析父类，而非最派生实例的类，避免继承链上
	// 重复调用 super 时无限递归到自身（如 o1.__initialize__ 内 super 应
	// 取 o1 的父类而非 o2 的父类）。
	Class* cls = current_class();
	if (cls == nullptr) {
		// 退化：无方法上下文时用最派生实例类（保持旧行为）。
		cls = self->get_class();
	}
	Class* parent = (cls != nullptr) ? cls->get_parent() : nullptr;
	if (parent == nullptr) {
		throw TypeError("super(): class '" +
		                std::string(cls ? cls->get_name() : "?") + "' has no parent.");
	}
	Incref(parent);
	return parent;
}

// =============================================================
// private / public / readonly：可见性 / 只读装饰器
//
// 实现已上提到 stdlib/common（visibility.hpp）：本库与 pycp 库曾逐字
// 重复同一实现，现共用一份；此处仅在 make_classtools_module 中注册，
// 保证 `from classtools import public` 与 `from pycp import public` 等价。
// =============================================================

Module* make_classtools_module() {
	Module* mod = Module::New(MODULE_NAME);

	// 规则 2：classtools 模块命名空间注入 __name__ = 模块名。
	mod->set_variable("__name__", String::FromCString(MODULE_NAME));

	// 可见性/只读装饰器：@private / @public / @readonly（实现见 stdlib/common）。
	RegisterVisibilityDecorators(mod);

	// super：运行时函数，返回父类 Class（thread_local self 上下文）。
	mod->set_function("super", _builtin_super);

	return mod;
}

} // anonymous namespace

// 动态库入口（符号名 PycpModule_classtools，按模块名导出）。
// 由 VM::load_module 经 LoadNativeModule 的 dlsym("PycpModule_classtools") 调用。
// 必须走 PYCP_EXPORT_MODULE：Windows 下没有 __declspec(dllexport) 时，
// 只有"整库无任何显式导出"才会被 MinGW 自动全导出，一旦本 TU 出现任何
// 其它导出符号，GetProcAddress 就找不到入口，import classtools 会静默失效。
PYCP_EXPORT_MODULE(classtools) {
	return make_classtools_module();
}

} // namespace Pycp
