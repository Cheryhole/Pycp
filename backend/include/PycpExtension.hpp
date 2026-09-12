#ifndef PYCP_EXTENSION_HPP
#define PYCP_EXTENSION_HPP

// =============================================================
// Pycp 原生扩展唯一对外头（扩展作者只需 #include "PycpExtension.hpp"）
//
// 本头整合「把 C++ 函数 / 对象导出到 pycp 层」的全部能力：
//   1) 模块入口导出宏：PYCP_EXPORT_MODULE / PYCP_MODULE_EXPORT
//   2) 方法表条目：MethodEntry / MethodTableFn（定义在 PycpMethodTable.hpp）
//   3) Module 绑定 API：set_function / set_variable / set_constant /
//      set_object / set_type（声明在 PycpModule.hpp，本头一并可见）
//   4) 参数规范框架（Pycp::Extension）：以名字规范表声明参数形态，
//      框架负责个数校验、默认值填充、可变参数收集与关键字参数分拣。
//
// 常用辅助函数统一放在 Pycp::Extension 命名空间；其中参数规范工厂同时
// 以 Pycp::Arg 暴露别名（`Arg::Required("a")` 与
// `Extension::Arg::Required("a")` 等价）。
//
// 典型写法：
//
//   #include "PycpExtension.hpp"
//   using namespace Pycp;
//
//   Object* _f1(Object* self, FixedList* args, Map* kwargs) {
//       static const Extension::ArgTable spec = Extension::CompileArgs("f1", {
//           Arg::Required("a"),
//           Arg::Optional("b", Integer::FromLong(0)),
//           Arg::Rest("rest"),          // 收集为 FixedList（空元组表示零个）
//       });
//       auto r = spec.Bind(args, kwargs);
//       Object*    a    = r["a"];
//       FixedList* rest = static_cast<FixedList*>(r["rest"]);
//       ...
//       return None::instance;
//   }
//
//   PYCP_EXPORT_MODULE(eg) {
//       Module* mod = Module::New("eg");
//       mod->set_function("f1", _f1);
//       return mod;
//   }
//
// 参数形态（只校验个数与名字，不做类型检查/转换）：
//   Arg::Required("name")            必填位置参数
//   Arg::Optional("name"[, 默认值])  可选参数（省略默认值即 None）
//   Arg::Rest("name")                可变参数（*rest，收集为 FixedList）
//   Arg::Keyword("name")             关键字参数（**kw，收集为 Map）
// =============================================================

#include "PycpObject.hpp"
#include "PycpNone.hpp"
#include "PycpString.hpp"
#include "PycpInteger.hpp"
#include "PycpFunction.hpp"
#include "PycpFixedList.hpp"
#include "PycpMap.hpp"
#include "PycpClass.hpp"
#include "PycpModule.hpp"
#include "PycpGC.hpp"
#include "PycpException.hpp"
#include "PycpConfig.hpp"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

namespace Pycp {

// =============================================================
// 模块入口导出宏
// =============================================================

// 导出模块入口函数签名（extern "C"，避免 name mangling）。
// 符号名 = "PycpModule_" + name（name 拼入符号名），与运行时按导入名
// dlsym 查找的约定一致。Windows 下需显式 __declspec(dllexport)。
#ifdef _WIN32
  #define PYCP_EXPORT_MODULE(name) \
	extern "C" __declspec(dllexport) Pycp::Module* PycpModule_##name()
#else
  #define PYCP_EXPORT_MODULE(name) \
	extern "C" Pycp::Module* PycpModule_##name()
#endif

// AOT 转译模块（.gen.cpp）统一使用的导出宏：把模块初始化函数
// PycpModule_<name> 声明/定义为可被「编译为 DLL 时导出 / 编译进主程序或
// 静态库时按普通符号链接」的形态。详见 PycpExt 历史注释语义：
//   Windows + PYCP_BUILDING_MODULE -> dllexport；否则裸 extern "C"。
#ifdef _WIN32
  #ifdef PYCP_BUILDING_MODULE
    #define PYCP_MODULE_EXPORT extern "C" __declspec(dllexport)
  #else
    #define PYCP_MODULE_EXPORT extern "C"
  #endif
#else
  #define PYCP_MODULE_EXPORT extern "C"
#endif

// =============================================================
// 参数规范框架
// =============================================================
namespace Extension {

// ---- 参数形态 ----
enum class ArgKind {
	Required,   // 必填位置参数
	Optional,   // 可选位置参数（省略时取登记的默认值）
	Rest,       // *rest：收集为 FixedList（零个 -> 空元组）
	Keyword,    // **kw：收集未匹配的关键字参数为 Map（零个 -> 空 Map）
};

// 单条参数规范。
// 注意：只承载「名字 + 形态 + 可选默认值」，不含类型信息——类型判断由
// 业务函数自行完成（对齐「只校验个数与名字」的设计）。
struct ArgSpec {
	const char* name          = nullptr;   // 参数名（须为合法标识符）
	Object*     default_value = nullptr;   // 仅 Optional 使用
	ArgKind     kind          = ArgKind::Required;
};

// ---- 规范工厂（自文档化，避免裸聚合初始化错位）----
namespace Arg {
inline ArgSpec Required(const char* n) {
	return ArgSpec{n, nullptr, ArgKind::Required};
}
// 省略默认值即 None。
inline ArgSpec Optional(const char* n) {
	return ArgSpec{n, None::instance, ArgKind::Optional};
}
[[maybe_unused]] inline ArgSpec Optional(const char* n, Object* def) {
	return ArgSpec{n, def != nullptr ? def : None::instance, ArgKind::Optional};
}
inline ArgSpec Rest(const char* n) {
	return ArgSpec{n, nullptr, ArgKind::Rest};
}
inline ArgSpec Keyword(const char* n) {
	return ArgSpec{n, nullptr, ArgKind::Keyword};
}
} // namespace Arg

// 规范表常量：无上界（存在 *rest）时 max_args 取该值。
constexpr std::size_t kUnbounded = static_cast<std::size_t>(-1);

class ArgTable;

// =============================================================
// 绑定结果（按名 / 按序取值）
//
// 所有权：
//   - 位置槽位与默认值为 Borrowed（来自调用方容器或规范表内常驻默认值）；
//   - *rest 收集出的 FixedList 与 **kw 的 Map 由本对象持有（Owned），
//     析构时释放。
//   - 因此 ArgResult 不可拷贝、可移动；其有效期不得超出创建它的 ArgTable。
// =============================================================
class PYCP_API ArgResult {
public:
	ArgResult() = default;
	~ArgResult();
	ArgResult(ArgResult&& other) noexcept;
	ArgResult& operator=(ArgResult&& other) noexcept;
	ArgResult(const ArgResult&) = delete;
	ArgResult& operator=(const ArgResult&) = delete;

	// 按名取值；名字未在规范中声明时抛 KeyError。
	Object* operator[](const char* name) const;
	// 按规范顺序取值；越界抛 IndexError。
	Object* operator[](std::size_t index) const;

	std::size_t size() const { return values_.size(); }
	// 该名字对应槽位是否由调用方显式给出（Optional 未给出时为 false）。
	bool given(const char* name) const;

private:
	friend class ArgTable;
	const ArgTable*                owner_ = nullptr;   // Borrowed（规范表须存活）
	std::vector<Object*>           values_;            // Borrowed
	std::vector<unsigned char>     given_;
	std::vector<Object*>           owned_;             // 本结果持有的 Owned 容器
};

// =============================================================
// 参数规范表（一次编译，多次绑定）
// =============================================================
class PYCP_API ArgTable {
public:
	ArgTable() = default;

	// 绑定实参：位置填充 -> *rest 收集 -> 关键字分拣 -> 默认值 / 缺参检查。
	//   args   : 位置参数容器（可为 nullptr，等价空）
	//   kwargs : 关键字参数字典（可为 nullptr，等价空）
	ArgResult Bind(FixedList* args, Map* kwargs = nullptr) const;

	const std::string& name() const { return name_; }
	std::size_t min_args() const { return min_args_; }
	std::size_t max_args() const { return max_args_; }
	bool has_kwargs() const { return kw_index_ != kNone; }

private:
	friend class ArgResult;   // ArgResult 的按名查找需读取 index_ / name_
	friend PYCP_API ArgTable CompileArgs(const char* fn_name,
	                                     std::initializer_list<ArgSpec> specs);
	static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

	std::string                                  name_;      // 函数名（错误消息用）
	std::vector<ArgSpec>                         specs_;     // 按声明顺序
	std::vector<std::string>                     names_;     // 参数名（拥有）
	std::unordered_map<std::string, std::size_t> index_;     // 名字 -> 槽位
	std::size_t min_args_   = 0;                            // 必填个数
	std::size_t max_args_   = 0;                            // 位置参数上界（kUnbounded 表示无上界）
	std::size_t rest_index_ = kNone;                        // *rest 槽位
	std::size_t kw_index_   = kNone;                        // **kw 槽位
};

// 编译参数规范：一次性完成名字合法性 / 重名 / 顺序 / 唯一性校验并预计算，
// 校验失败抛 ValueError（属扩展作者书写错误）。调用点建议写成函数内
// `static const ArgTable spec = CompileArgs(...);`，此后每次调用零校验开销。
PYCP_API ArgTable CompileArgs(const char* fn_name,
                              std::initializer_list<ArgSpec> specs);

// =============================================================
// 容器辅助（调用侧打包实参 / 常驻空容器）
// =============================================================

// 由数组构造位置参数容器（返回 Owned；元素 Incref 后交给 FixedList 接管）。
PYCP_API FixedList* MakeArgs(Object* const* items, std::size_t n);
inline FixedList* MakeArgs(std::initializer_list<Object*> items) {
	return MakeArgs(items.begin(), items.size());
}

// 常驻空位置参数容器 / 常驻空关键字字典（Borrowed，永不释放）。
// 二者均为只读输入：调用方与业务函数都不得修改（空 FixedList 本就不可变；
// 空 Map 由框架常驻，仅在完全无关键字参数时作为输入传递）。
PYCP_API FixedList* EmptyArgs();
PYCP_API Map*       EmptyKwargs();

// 便捷调用：把位置实参打包为容器后转调 Function::invoke（自动释放临时容器）。
// 供内部转发点（类钩子、魔术方法分派等）使用，返回 Owned/Borrowed 与
// 被调用函数一致。
PYCP_API Object* Invoke(Function* fn, Object* self,
                        std::initializer_list<Object*> args,
                        Map* kwargs = nullptr);

} // namespace Extension

// 参数规范工厂的顶层别名：`Arg::Required("a")` 等价于
// `Extension::Arg::Required("a")`。
namespace Arg = Extension::Arg;

} // namespace Pycp

#endif // PYCP_EXTENSION_HPP
