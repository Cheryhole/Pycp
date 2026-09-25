#ifndef PYCP_MODULETOOLS_HPP
#define PYCP_MODULETOOLS_HPP

// =============================================================
// Pycp moduletools 标准库公开头（moduletools.so / libPycpExt_moduletools.a）
//
// moduletools 是「模块文件夹（package）」特性的配套工具库，只在包清单
// （pycp.mpycp）及其子文件中使用：
//
//   - this()：返回当前包模块对象（等价于 Python 模块内的模块自身），
//     供清单里覆写 __get_attribute__ / __string__ 等钩子时访问自身命名空间。
//
//   - as_program() / as_library()：声明本包的角色。
//       * program：本包是程序，入口为清单顶层 func main(argv)。
//       * library：本包是库，只能被 import。
//     未声明时由宿主按用途推断（`-m` 直接执行为 program；被 import 为
//     library；AOT 默认为 library），声明与用途冲突即报错。
//
//   - role() / is_program() / is_library()：查询当前角色。
//
//   - Project()：__codegen__() 返回的项目描述对象，用于向 AOT 声明
//     子模块链接形态与可执行文件名：
//         proj = moduletools.Project()
//         proj.set_executable_name("my_app")
//         proj["obj_a"].static()
//         proj["method_b"].shared()
//         return proj
//
// 动态库导出入口符号 PycpModule_moduletools（extern "C"，按模块名导出）。
// =============================================================

namespace Pycp {

class Module; // 前置声明（完整定义见 backend/include/PycpModule.hpp）

// 本库的模块名（import moduletools 时匹配）。
constexpr const char* MODULE_NAME = "moduletools";

// 动态库入口（extern "C" 定义于 moduletools.cpp）。
extern "C" Module* PycpModule_moduletools();

} // namespace Pycp

#endif // PYCP_MODULETOOLS_HPP
