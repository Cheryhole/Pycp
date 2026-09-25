// =====================================================================
// Pycp 源码桥接自测（可选目标 pycp-hooks-test，-DBUILD_TEST=ON 构建）
// ---------------------------------------------------------------------
// 校验 RegisterSourceHooks 注册的源码字符串编译 / 执行钩子生效：
//   1) CompileSourceString 能编译一段源码为字节码 Module；
//   2) ExecSourceString 能在全新 globals 下执行；
//   3) ExecSourceString 传入 Map 时能把新变量写回该 Map。
// =====================================================================
#include <iostream>

#include "abi/Pycp.hpp"
#include "abi/PycpNativeExt.hpp"
#include "bytecode/PycpBytecode.hpp"
#include "loader/PycpSourceBridge.hpp"
#include "object/PycpMap.hpp"
#include "object/PycpString.hpp"
#include "object/PycpInteger.hpp"

int main() {
	Pycp::Initialize();
	Pycp::RegisterSourceHooks();

	// 1) 编译钩子：源码字符串 -> 字节码 Module。
	Pycp::BC::Module* mod = nullptr;
	try {
		mod = Pycp::CompileSourceString("x = 1\n", "<hooks-test>");
	} catch (const Pycp::Exception& e) {
		std::cout << "FAIL compile: " << e.what() << std::endl;
		Pycp::Finalize();
		return 1;
	}
	delete mod;

	// 2) 执行钩子（全新 globals）。
	Pycp::Object* r = Pycp::ExecSourceString("a = 41\n", "<hooks-test>", nullptr);
	if (r != nullptr) Pycp::Decref(r);

	// 3) 执行钩子 + globals 写回。
	Pycp::Map* g = Pycp::Map::New();
	Pycp::Object* r2 = Pycp::ExecSourceString("b = 7\n", "<hooks-test>", g);
	if (r2 != nullptr) Pycp::Decref(r2);

	Pycp::Object* key = Pycp::String::FromCString("b"); // Owned
	Pycp::Object* val = g->__get_item__(key);           // Borrowed
	const bool ok = (val != nullptr && val->is_type("Integer") &&
	                 static_cast<Pycp::Integer*>(val)->get_value() == 7);
	Pycp::Decref(key);
	Pycp::Decref(g);

	std::cout << (ok ? "HOOKS OK" : "FAIL globals writeback") << std::endl;
	Pycp::Finalize();
	return ok ? 0 : 1;
}
