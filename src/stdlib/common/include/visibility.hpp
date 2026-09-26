#ifndef PYCP_STDLIB_COMMON_VISIBILITY_HPP
#define PYCP_STDLIB_COMMON_VISIBILITY_HPP

// =====================================================================
// stdlib/common：private / public / readonly 装饰器的唯一实现
// ---------------------------------------------------------------------
// 背景：这三个装饰器曾在 classtools（src/classtools.cpp）与 pycp
// （src/PycpModule.cpp）两份 TU 中逐字重复。现集中于此，两库各调用一次
// RegisterVisibilityDecorators 即可，保证
// `from classtools import public` 与 `from pycp import public` 完全等价。
//
// 为何是「头文件内联」而不是「common.cpp + 两份 OBJECT」：
//   静态 AOT 会把全部扩展归档（libPycpExt_*.a）链入同一个可执行文件。
//   若 common 以外部链接的普通函数提供，RegisterVisibilityDecorators
//   会在多份归档中各有一份「强符号」，静态链接期即报 multiple definition。
//   故实现全部 inline：每个包含者各持一份「内部链接/vague linkage」副本，
//   既不跨库冲突，也无需 common 产出可加载模块（对应设计文档 R1）。
//
// 行为契约（与历史实现逐字一致，不得漂移）：
//   - 参数规范：CompileArgs("private"|"public"|"readonly", { Required("target") })
//   - 异常消息："visibility decorator expects exactly 1 argument."
//               "readonly decorator expects exactly 1 argument."
//   - 返回语义：原样返回被装饰对象（Incref 后 Owned），供装饰器替换逻辑替换。
// =====================================================================

#include "object/PycpModule.hpp"   // Module 完整定义（set_function）
#include "object/PycpGC.hpp"       // Incref
#include "object/PycpException.hpp"// TypeError
#include "object/PycpExtension.hpp"// Extension::CompileArgs / ArgTable

namespace Pycp {

namespace {

// 内部辅助（非注册函数）：参数个数与类型已由 private/public 的框架 thunk 校验。
// inline + 内部链接：不产生外部符号，多扩展链入同一 exe 不冲突。
inline Object* CommonVisibility(Object* target, bool priv) {
	if (target == nullptr) {
		throw TypeError("visibility decorator expects exactly 1 argument.");
	}
	target->set_private(priv);
	// 原样返回被装饰对象（装饰器替换逻辑用返回值替换原对象）。
	Incref(target);
	return target;
}

// @private / @public：作为普通函数被装饰器语法糖调用，设置目标可见性。
inline Object* CommonPrivate(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"private", { Extension::Arg::Required("target") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return CommonVisibility(r["target"], /*priv=*/true);
}

inline Object* CommonPublic(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"public", { Extension::Arg::Required("target") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	return CommonVisibility(r["target"], /*priv=*/false);
}

// @readonly：对象冻结（拒绝属性写/删）、模块常量绑定（不可再赋值覆盖）、
// 类成员只读字段；设置后原样返回。
inline Object* CommonReadonly(Object*, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"readonly", { Extension::Arg::Required("target") });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* target = r["target"];
	if (target == nullptr) {
		throw TypeError("readonly decorator expects exactly 1 argument.");
	}
	target->set_readonly(true);
	// 原样返回被装饰对象（装饰器替换逻辑用返回值替换原对象）。
	Incref(target);
	return target;
}

} // anonymous namespace

// 把 private / public / readonly 一次性挂到给定模块（注册顺序与历史一致）。
// inline（vague linkage）：跨归档重复定义可由链接器折叠，不产生多重定义。
inline void RegisterVisibilityDecorators(Module* mod) {
	mod->set_function("private",  CommonPrivate);
	mod->set_function("public",   CommonPublic);
	mod->set_function("readonly", CommonReadonly);
}

} // namespace Pycp

#endif // PYCP_STDLIB_COMMON_VISIBILITY_HPP
