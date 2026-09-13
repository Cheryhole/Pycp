#ifndef PYCP_NONE_HPP
#define PYCP_NONE_HPP

#include "PycpObject.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"

namespace Pycp{

class PYCP_API None : public Object{
	private:
		String* none_str_;

	public:
		static None* instance;

		static void Initialize();
		static void Finalize();

		None();
		~None();

		Object* __integer__() override;
		Object* __string__() override;
		// repr：固定 "None"（不加引号），与 Python 的 repr(None) 一致。
		Object* __raw_string__() override;
		Object* __inspect__() override;

		void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// 类型萃取特化：None（可哈希）。
template <> struct TypeTraits<None> {
	static constexpr PycpTypeId   id            = PycpTypeId::None;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::Hashable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif // PYCP_NONE_HPP