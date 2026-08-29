// =============================================================
// 查找顺序测试用原生扩展（同时验证 PycpExt.h 的扩展编写接口）
// -------------------------------------------------------------
// 编译为 native_probe.so 后，与同目录的 native_probe.pycp 同名。
// 用于验证「同一目录下动态库优先于 .pycp 源码」：动态库版本返回值 1，
// 源码版本取值 2，测试断言结果为 1。
// =============================================================

#include "PycpExt.h"

static Pycp::Object* native_probe_origin([[maybe_unused]] Pycp::Object* self,
                                         [[maybe_unused]] Pycp::Object** argv,
                                         [[maybe_unused]] std::size_t argc) {
	return Pycp::Integer::FromLong(1);
}

PYCP_EXPORT_MODULE(native_probe) {
	Pycp::Module* m = Pycp::Module::New("native_probe");
	auto* ns = m->get_namespace();
	PYCP_SET_FUNC(ns, "origin", native_probe_origin);
	return m;
}
