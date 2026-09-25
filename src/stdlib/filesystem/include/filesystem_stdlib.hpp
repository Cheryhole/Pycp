#ifndef PYCP_FILESYSTEM_STDLIB_HPP
#define PYCP_FILESYSTEM_STDLIB_HPP

// =============================================================
// pycp filesystem 标准库公开头（动态库 filesystem.so / .dll / .dylib）
//
// filesystem 模块提供文件系统与进程环境原语（对齐 Python os / os.path 常用面）：
//   - mkdir(path [, parents])：创建目录（parents 为真时递归创建）。
//   - listdir(path)：列出目录下的条目名（List[String]）。
//   - join(*parts)：路径拼接（绝对分量会重置，语义同 os.path.join）。
//   - basename(path) / dirname(path)：路径分解。
//   - exists(path) / is_file(path) / is_dir(path)：路径判定。
//   - getenv(name [, default])：读取环境变量（未设置时返回 default，默认 None）。
//   - cwd()：当前工作目录。
//   - exe_dir()：pycp 可执行文件所在目录。
//   - stdlib_dir()：可执行文件旁的 stdlib/ 目录。
//   - File(path [, mode])：与 io.File 为【同一类型】（实现已上提至运行时
//       object/PycpFile.hpp，二者共享同一 PycpTypeId::File）。
//
// 动态库导出入口符号 PycpModule_filesystem（extern "C"，按模块名导出）。
// =============================================================

namespace Pycp {

class Module; // 前置声明（完整定义见 runtime 的 object/PycpModule.hpp）

// 本库的模块名（import filesystem 时匹配）。
constexpr const char* MODULE_NAME = "filesystem";

// 动态库入口（extern "C" 定义于 PycpFilesystemModule.cpp）。
extern "C" Module* PycpModule_filesystem();

} // namespace Pycp

#endif // PYCP_FILESYSTEM_STDLIB_HPP
