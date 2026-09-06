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
		Object* __inspect__() override;

		void foreach_ref(const std::function<void(Object*)>& visit) override;
};

} // namespace Pycp

#endif // PYCP_NONE_HPP