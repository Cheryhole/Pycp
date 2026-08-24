#include "PycpManager.hpp"
#include "PycpNone.hpp"
#include "PycpInteger.hpp"
#include "PycpBoolean.hpp"
#include "PycpString.hpp"
#include "PycpFunction.hpp"
#include "PycpGC.hpp"
#include "PycpNativeExt.hpp"

#include <atomic>

namespace Pycp {

namespace {
	std::atomic<bool> g_initialized{false};
}

void Initialize(){
	// 幂等保护：重复调用直接返回
	if (g_initialized.exchange(true)) return;

	// 分层初始化顺序（修正原 None 早于 Integer 的 bug）
	None::Initialize();      // 常驻 None 实例（内部 lazy 触发 Integer 池亦可）
	Integer::Initialize();   // 小整数池（PERMANENT + root）
	Boolean::Initialize();   // 常驻 True/False 实例（依赖 Integer 池）
	String::Initialize();    // 预留：字符串驻留表等
	Function::Initialize();  // 内建函数（print 等）

	// 将 None 实例登记为根（已在 None::Initialize 内完成 AddRoot）
}

void Finalize(){
	if (!g_initialized.exchange(false)) return;

	// 注意顺序：必须先回收对象再关闭原生扩展。原生扩展（io/Pycp.so）里的
	// 对象（File 等）其方法实现位于动态库，若先 dlclose 再 GC_Collect，
	// mark 阶段调用这些对象的虚函数（foreach_ref 等）会跳转到已卸载地址
	// 导致段错误。故先 GC_Collect（回收常驻 root 之外的对象），后关闭扩展。
	GC_Collect();

	NativeExt_Finalize();    // 关闭所有已加载的原生扩展句柄
	Function::Finalize();
	String::Finalize();
	Boolean::Finalize();     // 先于 Integer 释放（Boolean 依赖 Integer 池）
	Integer::Finalize();
	None::Finalize();
}

} // namespace Pycp
