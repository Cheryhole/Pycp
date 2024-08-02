#ifndef __PYCP_OBJECT_HPP__
#define __PYCP_OBJECT_HPP__


#include "PycpDefines.h"
#include "PycpHashmap.hpp"
#include <string_view>
#include <string>
#include <limits>
#include <functional>

namespace Pycp{

class PycpTypeObject;
class PycpObject;

class PycpTypeObject{
	private:

	public:
		std::string_view name; // name of the type
		std::string_view document; // documentation string
		PycpObject* map;

		PycpTypeObject(const PycpTypeObject&) = delete;
		PycpTypeObject& operator=(const PycpTypeObject&) = delete;

		PycpTypeObject(std::string_view name, std::string_view doc);
		~PycpTypeObject(void){;}

}; // class PycpTypeObject

class PycpObject{
	private:
		PycpSize_t ReferecenConut; // uint64_t

	protected:
		PycpObject(const PycpObject&) = delete;
		PycpObject& operator=(const PycpObject&) = delete;

		PycpObject(void);
		PycpObject(const char* name);
		~PycpObject(void);
		
	public:
		PycpTypeObject* type;

		void incref(void);
		void decref(void);
		virtual PycpSize_t hash(void) const;
		PycpObject* GetAttr(std::string_view name);
		int SetAttr(std::string_view name, PycpObject* obj);


}; // class PycpObject


} // namespace Pycp

#endif // __PYCP_OBJECT_HPP__
