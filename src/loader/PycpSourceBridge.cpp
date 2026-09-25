#include "loader/PycpSourceBridge.hpp"

#include "abi/PycpNativeExt.hpp"
#include "abi/PycpABI.hpp"          // RunBytecodeModule
#include "bytecode/PycpBytecode.hpp"
#include "loader/PycpModuleLoader.hpp"
#include "parser/PycpAstReflect.hpp"

#include <string>

namespace Pycp {

namespace {

// SourceStringCompiler：源码字符串 -> 字节码 Module（普通模块语义）。
// 使用 repl_eval=false：顶层表达式语句不保留返回值（对齐 Python compile
// 的 "exec" 模式），与 REPL 的 compile_string 默认行为区分开。
BC::Module* bridge_compile_string(const std::string& source,
                                  const std::string& filename) {
	return new BC::Module(
		ModuleLoader::compile_string(source, filename, /*repl_eval=*/false));
}

// SourceExecutor：源码字符串 -> 编译并执行，返回顶层结果（Owned）。
// globals 语义（预置 + 写回）由 RunBytecodeModule 统一处理。
Object* bridge_exec_string(const std::string& source,
                           const std::string& filename, Object* globals) {
	BC::Module module =
		ModuleLoader::compile_string(source, filename, /*repl_eval=*/false);
	return RunBytecodeModule(&module, globals);
}

// SourceParser：源码字符串 -> AST 节点树（已映射为 pycp 对象），供 ast.parse。
Object* bridge_parse_string(const std::string& source, const std::string& filename) {
	return Ast::ParseToObjects(source, filename);
}

} // anonymous namespace

void RegisterSourceHooks() {
	SetSourceStringCompiler(&bridge_compile_string);
	SetSourceExecutor(&bridge_exec_string);
	SetSourceParser(&bridge_parse_string);
}

} // namespace Pycp
