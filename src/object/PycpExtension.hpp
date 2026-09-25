#ifndef PYCP_EXTENSION_HPP
#define PYCP_EXTENSION_HPP

// =============================================================
// Pycp 原生扩展唯一对外头（扩展作者只需 #include "object/PycpExtension.hpp"）
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
//   #include "object/PycpExtension.hpp"
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
//   Arg::Required("name")            位置必填参数
//   Arg::Optional("name"[, 默认值])  位置可选参数（省略默认值即 None）
//   Arg::Rest("name")                可变位置参数（*args，收集为 FixedList）
//   Arg::RestKeywords("name")        可变关键字参数（**kwargs，收集为 Map）
//
// 关键字-only 参数（对应语言层 `*args` / 裸 `*` 之后的形参，只能按关键字
// 传参）由**位置隐含**，无需显式写法：写在 Arg::Rest(...) 之后的
// Required 即必填关键字-only，其后的 Optional 即可选关键字-only（省略时
// 取默认值）。ArgKind::Keyword 仅为语言层映射用的内部形态，不设公开工厂。
// 顺序规则：
//   Required* → Optional* → Rest? → Required/Optional(关键字-only)* → RestKeywords?
// 其中 Rest / RestKeywords 各至多一个，RestKeywords 必须是最后一个；
// 一旦越过 Arg::Rest，其后不再允许位置形态（Required / Optional 按
// 关键字-only 解释）。
//
// 必填判定：Required 恒必填；Optional 恒取默认值；关键字-only 段内的
// Required 必填、Optional 可选（default_value 归一化为 None）。
// =============================================================

#include "object/PycpObject.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpString.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpFunction.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpMap.hpp"
#include "object/PycpClass.hpp"
#include "object/PycpModule.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpException.hpp"
#include "object/PycpConfig.hpp"

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
// 关键字-only 为内部形态 ArgKind::Keyword：由语言层（裸 `*` / `*args` 之后的
// 形参映射，见 PycpBytecodeVM.cpp）产生；公开规范表通过「Rest 之后的
// Required / Optional」位置隐含表达，不提供显式工厂。
enum class ArgKind {
	Required,       // 位置必填；位于 Rest 之后 => 必填关键字-only
	Optional,       // 位置可选（省略时取登记的默认值）；位于 Rest 之后 => 可选关键字-only
	Rest,           // *args：收集为 FixedList（零个 -> 空元组）
	RestKeywords,   // **kwargs：收集未匹配的关键字参数为 Map（零个 -> 空 Map）
	Keyword,        // [内部] 关键字-only：default_value == nullptr 为必填，否则取默认值
};

// 单条参数规范。
// 注意：只承载「名字 + 形态 + 可选默认值」，不含类型信息——类型判断由
// 业务函数自行完成（对齐「只校验个数与名字」的设计）。
struct ArgSpec {
	const char* name          = nullptr;   // 参数名（须为合法标识符）
	Object*     default_value = nullptr;   // Optional 使用（nullptr 归一化为 None）；
	                                       // Keyword 中 nullptr 表示「必填」
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
inline ArgSpec RestKeywords(const char* n) {
	return ArgSpec{n, nullptr, ArgKind::RestKeywords};
}
} // namespace Arg

// =============================================================
// 统一绑定内核（语言层字节码函数 与 C++ 规范表 共用）
//
// 语义对齐 Python：
//   1) 位置实参依序填位置段（Rest / Keyword 之前的 Required / Optional）；
//   2) 多余的实参进 *args（FixedList）；
//   3) 关键字按名字匹配位置段与关键字-only 段（Keyword 槽位 + Rest 之后的
//      Required / Optional；同一形参重复赋值报错）；
//   4) 未匹配的关键字进 **kwargs（Map）；无 **kwargs 则报未知关键字；
//   5) 缺失的可选形参取默认值，缺失的必填形参报错（位置 / 关键字-only 分开）。
//
// 内核【不抛异常】：失败时填 BindError 由调用方各自格式化文案
// （语言层用 CPython 文案，原生规范表沿用既有文案）。
// =============================================================

// 绑定失败原因。
enum class BindErrorCode {
	None,                 // 成功
	TooManyPositional,    // 位置实参过多（无 *args 接收）
	MissingPositional,    // 缺必填位置形参
	MissingKeywordOnly,   // 缺必填关键字-only 形参
	MultipleValues,       // 同一形参被位置与关键字重复赋值
	UnexpectedKeyword,    // 未知关键字（无 **kwargs 接收）
};

struct BindError {
	BindErrorCode      code     = BindErrorCode::None;
	std::vector<std::string> names;   // 涉及的形参名 / 关键字名（缺参时按声明顺序）
	std::size_t        expected = 0;  // TooManyPositional：位置形参上界
	std::size_t        got      = 0;  // TooManyPositional：实到的位置实参个数
};

// 绑定结果：slots 为「声明顺序」槽位（长度 == nspec，Borrowed 与 Owned 混合）；
// owned 为内核新建的容器（*args 的 FixedList / **kwargs 的 Map），由本对象
// 托管并在析构时释放。不可拷贝、可移动；有效期内不得超出调用方的实参容器寿命。
class PYCP_API BoundArgs {
public:
	BoundArgs() = default;
	~BoundArgs();
	BoundArgs(BoundArgs&& other) noexcept;
	BoundArgs& operator=(BoundArgs&& other) noexcept;
	BoundArgs(const BoundArgs&) = delete;
	BoundArgs& operator=(const BoundArgs&) = delete;

	// 主动释放 owned（幂等）；slots 随即失效。
	void ReleaseOwned();

	std::vector<Object*>       slots;   // 声明顺序槽位
	std::vector<unsigned char> given;   // 该槽位是否由调用方显式给出
	std::vector<Object*>       owned;   // 内核新建的容器（Owned）
};

// 绑定：specs 为声明顺序的形参规范；pos/npos 为位置实参视图（Borrowed）；
// kwargs 为关键字实参（Borrowed，可为 nullptr）。成功返回 true 并填 out。
bool PYCP_API BindParams(const ArgSpec* specs, std::size_t nspec,
                         Object* const* pos, std::size_t npos,
                         Map* kwargs, BoundArgs& out, BindError& err);

// 规范表常量：无上界（存在 *args）时 max_args 取该值。
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

	// 绑定实参（走统一内核 BindParams，本表只负责把结果转成按名取值形态，
	// 并把结构化错误格式化为原生扩展的既有文案）。
	//   args   : 位置参数容器（可为 nullptr，等价空）
	//   kwargs : 关键字参数字典（可为 nullptr，等价空）
	ArgResult Bind(FixedList* args, Map* kwargs = nullptr) const;

	const std::string& name() const { return name_; }
	// 位置形参的必填个数 / 上界（关键字-only 形参不计入，与 Python 一致）。
	std::size_t min_args() const { return min_args_; }
	std::size_t max_args() const { return max_args_; }
	bool has_kwargs() const { return kw_index_ != kNone; }
	// 是否存在关键字-only 形参（显式 Keyword，或 Rest 之后的 Required / Optional）。
	bool has_kwonly() const { return kwonly_start_ < specs_.size(); }

private:
	friend class ArgResult;   // ArgResult 的按名查找需读取 index_ / name_
	friend PYCP_API ArgTable CompileArgs(const char* fn_name,
	                                     std::initializer_list<ArgSpec> specs);
	static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

	std::string                                  name_;      // 函数名（错误消息用）
	std::vector<ArgSpec>                         specs_;     // 按声明顺序
	std::vector<std::string>                     names_;     // 参数名（拥有）
	std::unordered_map<std::string, std::size_t> index_;     // 名字 -> 槽位
	std::size_t min_args_   = 0;                            // 位置必填个数
	std::size_t max_args_   = 0;                            // 位置参数上界（kUnbounded 表示无上界）
	std::size_t rest_index_ = kNone;                        // *args 槽位
	std::size_t kw_index_   = kNone;                        // **kwargs 槽位
	// 位置段长度 == min(rest_index_, 首个 Keyword 槽位)（两者皆无时即 specs_.size()）；
	// 关键字-only 段自 kwonly_start_ 起（显式 Keyword 槽位，或 Rest 的下一槽位）。
	std::size_t kwonly_start_ = 0;
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
