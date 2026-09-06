#ifndef PYCP_CONFIG_HPP
#define PYCP_CONFIG_HPP

// =============================================================
// Pycp 运行时统一配置
//   - 版本号（消除 CMakeLists 与 pybind11 两处硬编码不同步）
//   - 跨平台 ABI 导出宏 PYCP_API（C++ 风格，保留 namespace/重载）
//   - GC 策略开关与命名常量
// =============================================================

#include <cstdint>

namespace Pycp {

// 版本号：单一事实来源，CMake / pybind11 / 运行时均读取此处
constexpr const char* PYCP_VERSION = "1.0.0";

// =============================================================
// 文件扩展名（源文件 / 字节码 / 生成 C++ 产物）
// =============================================================
constexpr const char* EXT_PYCP  = ".pycp";    // Pycp 源文件
constexpr const char* EXT_CPYCP = ".cpycp";   // 序列化字节码
constexpr const char* EXT_CPP   = ".cpp";     // AOT 生成的 C++ 源码
constexpr const char* EXT_PP_PYCP = ".pp.pycp"; // 预处理输出的源文件

// =============================================================
// AOT 输出命名约定
// =============================================================
// AOT 生成的 C++ 文件统一后缀（入口与依赖模块一致）
constexpr const char* AOT_CPP_SUFFIX = ".gen.cpp";
// 入口 .pycp 生成的 C++ 文件固定名（含 main 函数）
constexpr const char* AOT_ENTRY_CPP_FILENAME = "__pycp_main.gen.cpp";
// 静态链接模式（--static）专用：内置原生扩展的注册/链接拉入桩文件名。
// 该文件显式引用各内置扩展的 PycpModule_<name> 并登记进运行时注册表，
// 既让静态链接下 import 能命中，也强制链接器保留对应静态库成员。
constexpr const char* AOT_BUILTIN_REG_CPP_FILENAME = "__pycp_builtin_reg.gen.cpp";

// =============================================================
// 字节码魔数与格式版本（单一事实来源，序列化/反序列化共享）
// =============================================================
constexpr char BYTECODE_MAGIC[] = "CYCP";        // 4 字节魔数
constexpr uint16_t BYTECODE_VERSION_MAJOR = 3;
// minor 1：CodeObject 新增 default_count 字段（尾部默认值形参个数）。
constexpr uint16_t BYTECODE_VERSION_MINOR = 1;

// =============================================================
// AOT 生成符号前缀（跨模块链接约定）
// =============================================================
// 模块初始化函数前缀（AOT 生成的 .pycp 子模块入口符号为 PycpModule_<name>，
// 无哈希、按模块名唯一）。Pycp::ImportModule 经 dlsym(RTLD_DEFAULT,
// "PycpModule_<name>") 链接，使解释器与 AOT 共用统一导入入口。
constexpr const char* AOT_MODULE_INIT_PREFIX = "PycpModule_"; // 模块初始化函数前缀
constexpr const char* AOT_FN_PREFIX          = "pycp_fn_";      // 代码对象翻译函数前缀
constexpr const char* AOT_ENTRY_FN_NAME      = "pycp_main";     // 入口函数名

// =============================================================
// 模块查找（import）约定
// =============================================================
// 标准库目录名：位于可执行文件同级目录，存放原生扩展（<name>.so）与
// 源码模块（<name>.pycp）。解释器取 pycp 自身所在目录，AOT 编译出的
// 独立程序取该程序自身所在目录。
constexpr const char* STDLIB_DIR_NAME = "stdlib";

// =============================================================
// 特殊模块名
// =============================================================
constexpr const char* MODULE_TOP_NAME   = "<module>"; // 顶层代码对象名
constexpr const char* MODULE_ENTRY_NAME = "<entry>";  // 入口占位模块名

// =============================================================
// REPL 输入源名（报错显示用，等价于 Python 的 <stdin>）
// =============================================================
constexpr const char* REPL_SOURCE_NAME  = "<stdin>";  // REPL 逐条输入的源名称

// =============================================================
// 内建函数名
// =============================================================
constexpr const char* BUILTIN_PRINT = "print";

// =============================================================
// 魔术方法名
// =============================================================
constexpr const char* MAGIC_INITIALIZE = "__initialize__"; // 实例构造时调用
constexpr const char* MAGIC_STRING     = "__string__";     // 字符串转换

// =============================================================
// 匿名对象内部名（匿名函数 / 匿名类）
// =============================================================
constexpr const char* ANONYMOUS_FUNCTION = "@anonymous"; // 匿名函数内部名
constexpr const char* ANONYMOUS_CLASS    = "@anonymous"; // 匿名类内部名

// GC 策略开关
//   PYCP_GC_RC       : 引用计数（默认开启，refcount==0 立即释放）
//   PYCP_GC_CYCLE    : 标记-清除兜底（处理循环引用，由 GC_Collect 触发）
#ifndef PYCP_GC_RC
	#define PYCP_GC_RC 1
#endif
#ifndef PYCP_GC_CYCLE
	#define PYCP_GC_CYCLE 1
#endif

// =============================================================
// 导出符号命名约定（单一、明确规则）
//
// 规则由“声明所在位置”自动决定，与宏无关：
//
//   1) 命名空间内部导出（namespace Pycp { ... }）
//        - 采用 C++ 风格 ABI，保留 namespace 作用域，
//          支持重载、可被 C++ 直接 #include 使用。
//        - 符号【不】添加 PYCP 前缀，仅以 PYCP_API 标记可见性。
//        - 例：Pycp::GC_Track / Pycp::Add / Pycp::Initialize
//
//   2) C 语言方式导出（extern "C" 全局作用域）
//        - 供 C / FFI / 纯 C 链接器使用，无命名空间、无重载。
//        - 符号【保留】PYCP 前缀，以 PYCP_C_API 标记。
//        - 例：PYCP_GC_Track / PYCP_Add / PYCP_Initialize
//
// 两类宏仅控制可见性/链接属性，不改变命名本身；
// 前缀是否添加由上述位置规则决定。
// =============================================================

// C++ 风格导出（命名空间内部，无 PYCP 前缀）
//   Windows 下三分支：
//     PYCP_STATIC     : 静态库（无导入/导出属性，符号自包含）
//     PYCP_BUILD_DLL  : 构建动态库（dllexport）
//     其他            : 消费动态库（dllimport）
#ifdef _WIN32
	#ifdef PYCP_STATIC
		#define PYCP_API
	#elif defined(PYCP_BUILD_DLL)
		#define PYCP_API __declspec(dllexport)
	#else
		#define PYCP_API __declspec(dllimport)
	#endif
#else
	#define PYCP_API __attribute__((visibility("default")))
#endif

// C 语言风格导出（extern "C" 全局，保留 PYCP 前缀）
#ifdef _WIN32
	#ifdef PYCP_STATIC
		#define PYCP_C_API extern "C"
	#elif defined(PYCP_BUILD_DLL)
		#define PYCP_C_API extern "C" __declspec(dllexport)
	#else
		#define PYCP_C_API extern "C" __declspec(dllimport)
	#endif
#else
	#define PYCP_C_API extern "C" __attribute__((visibility("default")))
#endif

} // namespace Pycp

#endif // PYCP_CONFIG_HPP
