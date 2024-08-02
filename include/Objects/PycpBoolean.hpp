#ifndef __PYCP_BOOLEAN_HPP__
#define __PYCP_BOOLEAN_HPP__

#include "PycpObject.hpp"

namespace Pycp {

class PycpBoolean : public PycpObject{
	private:
		bool value;

	public:
		PycpBoolean(bool value);
		virtual ~PycpBoolean();

		PycpSize_t hash() const;

		virtual PycpObject* __string__();
		virtual PycpObject* __integer__();
		virtual PycpObject* __decimal__();
		virtual PycpObject* __boolean__();
		virtual PycpObject* __bytes__();

		virtual PycpObject* __addition__(PycpObject* obj);
		virtual PycpObject* __subtraction__(PycpObject* obj);
		virtual PycpObject* __multiplication__(PycpObject* obj);
		virtual PycpObject* __division__(PycpObject* obj);
};

} // namespace pycp

#endif // __PYCP_BOOLEAN_HPP__ 