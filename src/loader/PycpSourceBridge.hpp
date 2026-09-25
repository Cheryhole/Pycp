#ifndef PYCP_SOURCE_BRIDGE_HPP
#define PYCP_SOURCE_BRIDGE_HPP

// =============================================================
// 宿主源码桥接（PycpFrontend）
// -------------------------------------------------------------
// 把前端的「源码字符串 -> 字节码 / -> 执行 / -> AST」能力，经运行时钩子
// （SetSourceStringCompiler / SetSourceExecutor / SetSourceParser）注册给
// PycpRuntime，使只链接运行时的 stdlib 模块（compile / codebyte / ast）
// 也能获得编译能力，而无需反向依赖前端。
//
// 由链接了 PycpFrontend 的宿主（pycp 可执行文件）在启动时调用一次。
// =============================================================

namespace Pycp {

// 注册源码字符串编译 / 执行钩子（AST 解析钩子随 ast 反射模块一并注册）。
// 幂等：重复调用覆盖为同一组实现。
void RegisterSourceHooks();

} // namespace Pycp

#endif // PYCP_SOURCE_BRIDGE_HPP
