#ifndef PYCP_NONE_HPP
#define PYCP_NONE_HPP

#include "PycpObject.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"

namespace Pycp{

class None : public Object{
	private:
		String* none_str;

	public:
		static None* instance;

		static void Initialize();
		static void Finalize();

		None();
		~None();

		Object* __integer__() override;
		Object* __string__() override;
};

} // namespace Pycp

#endif // PYCP_NONE_HPP