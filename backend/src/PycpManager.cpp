#include "PycpManager.hpp"
#include "PycpNone.hpp"
#include "PycpInteger.hpp"
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
	String::Initialize();    // 预留：字符串驻留表等
	Function::Initialize();  // 内建函数（print 等）

	// 将 None 实例登记为根（已在 None::Initialize 内完成 AddRoot）
}

void Finalize(){
	if (!g_initialized.exchange(false)) return;

	NativeExt_Finalize();    // 关闭所有已加载的原生扩展句柄
	Function::Finalize();
	String::Finalize();
	Integer::Finalize();
	None::Finalize();

	// 兜底：回收任何遗留的不可达对象
	GC_Collect();
}

} // namespace Pycp
