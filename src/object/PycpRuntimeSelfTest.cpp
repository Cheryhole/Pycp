// =====================================================================
// PycpRuntime 自测程序（可选目标 pycp-selftest，-DBUILD_TEST=ON 构建）
// ---------------------------------------------------------------------
// 最小样板：经统一 ABI 构造对象、读取其值、统计 GC 存活对象数。
// 用于快速确认运行时库可独立链接并正常工作。
// =====================================================================
#include <iostream>

#include "abi/Pycp.hpp"

int main() {
	Pycp::Initialize();

	// 经静态工厂构造（Owned，refcount = 1）。
	Pycp::Object* s = Pycp::String::FromCString("Hello World");
	std::cout << static_cast<Pycp::String*>(s)->get_value() << std::endl;
	Pycp::Decref(s);

	// 诊断信息：当前存活对象数。
	std::cout << "GC live objects: " << Pycp::GC_LiveCount() << std::endl;

	Pycp::Finalize();
	return 0;
}
