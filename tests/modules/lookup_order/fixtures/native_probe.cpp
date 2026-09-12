// =============================================================
// 查找顺序测试用原生扩展（同时验证 PycpExtension.hpp 的扩展编写接口）
// -------------------------------------------------------------
// 编译为 native_probe.so 后，与同目录的 native_probe.pycp 同名。
// 用于验证「同层 .pycp 源码优先于 .so」「进程内符号层」等查找规则。
//
// 采用新导出 API：容器形态原生函数 + Module::set_function。
// =============================================================

#include "PycpExtension.hpp"

namespace {

// origin()：无参数，返回整数 1（源码/原生探针标记）。
Pycp::Object* native_probe_origin([[maybe_unused]] Pycp::Object* self,
                                  Pycp::FixedList* args,
                                  Pycp::Map* kwargs) {
	static const Pycp::Extension::ArgTable spec =
		Pycp::Extension::CompileArgs("origin", {});
	spec.Bind(args, kwargs);
	return Pycp::Integer::FromLong(1);
}

} // namespace

PYCP_EXPORT_MODULE(native_probe) {
	Pycp::Module* m = Pycp::Module::New("native_probe");
	m->set_function("origin", native_probe_origin);
	return m;
}
