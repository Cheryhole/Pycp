#ifndef PYCP_MANAGER_HPP
#define PYCP_MANAGER_HPP

// 运行时初始化 / 销毁管理
//   实现下沉至 PycpManager.cpp（见路线图第 4 步）
//   初始化顺序：Core -> GC -> None -> Integer -> String -> Function -> Builtin
//   具备幂等保护：重复 Initialize 不会重复初始化

#include "PycpConfig.hpp"

namespace Pycp {

// 标记 PYCP_API：Windows 下构建 shared 运行时时导出，供 pycp.exe 跨 DLL 调用。
PYCP_API void Initialize();
PYCP_API void Finalize();

} // namespace Pycp

#endif // PYCP_MANAGER_HPP
