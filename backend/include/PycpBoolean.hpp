#ifndef PYCP_BOOLEAN_HPP
#define PYCP_BOOLEAN_HPP

#include "PycpObject.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"

namespace Pycp{

// Boolean 类型：继承自 Integer，逻辑真值 True 对应整数 1、False 对应整数 0。
// 与 Python 一致，bool 是 int 的子类，复用 Integer 的全部算术与比较虚方法。
class PYCP_API Boolean : public Integer {
	private:
		static Boolean* g_true;   // 常驻 True 实例（值 1）
		static Boolean* g_false;  // 常驻 False 实例（值 0）

	public:
		static Boolean* True();    // 返回常驻 True 实例
		static Boolean* False();   // 返回常驻 False 实例

		static void Initialize();  // 创建 g_true/g_false 并 GC_AddRoot
		static void Finalize();    // GC_RemoveRoot + Decref + 置空

		Boolean(int64_t v = 0);
		Boolean(Object* obj);

		// 显示："True" / "False"
		Object* __string__() override;

		// 真值判定：按自身值返回 True/False。
		Object* __boolean__() override;

		// __integer__ 继承 Integer（返回自身值 0/1）。
		// __addition__ / __equal__ 等算术与比较继承 Integer（返回 Integer）。
};

} // namespace Pycp

#endif // PYCP_BOOLEAN_HPP
