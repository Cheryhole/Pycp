#ifndef __PYCP_OBJECT_HPP__
#define __PYCP_OBJECT_HPP__

#include "Pycp.hpp"
#include "PycpBytes.hpp"
#include "PycpBoolean.hpp"
#include "PycpDecimal.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpTuple.hpp"
#include "PycpList.hpp"
#include "PycpNone.hpp"

#include "Exceptions/PycpException.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <limits>
#include <functional>
#include <typeinfo>

namespace Pycp{

struct PycpHashCaller;
class PycpObject;
typedef std::unordered_map<PycpObject*, PycpObject*, PycpHashCaller> PycpObjectMap;

class PycpObject{
	private:
		PycpSize_t ReferecenConut; // uint64_t
		
	protected:
		PycpObject(const char* name);
		PycpObjectMap obmap;
		
	public:
		char* name; // name of the type
		std::string_view document; // documentation string

	public:
		PycpObject(const PycpObject&) = delete;
		PycpObject& operator=(const PycpObject&) = delete;

		PycpObject();
		~PycpObject();

		inline void incref();
		inline void decref();
		inline virtual PycpSize_t hash() const;
		inline virtual PycpSize_t length() const;
		virtual PycpObject* call(PycpObject* args);
		PycpObject* GetAttr(const char* name);
		void DelAttr(const char* name);
		void SetAttr(const char* name, PycpObject* obj);

	// methods
		virtual PycpObject* __string__() const; // default to return PycpString(<'objectname' at 'address'>)
		virtual PycpObject* __integer__(); // default to return nullptr
		virtual PycpObject* __decimal__(); // default to return nullptr
		virtual PycpObject* __boolean__(); // default to return PycpBoolean(true)
		virtual PycpObject* __tuple__(); // default to return nullptr
		virtual PycpObject* __list__(); // default to return nullptr
		virtual PycpObject* __bytes__(); // default to return nullptr

		virtual PycpObject* __addition__(PycpObject* obj); // default to return nullptr
		virtual PycpObject* __subtraction__(PycpObject* obj); // default to return nullptr
		virtual PycpObject* __multiplication__(PycpObject* obj); // default to return nullptr
		virtual PycpObject* __division__(PycpObject* obj); // default to return nullptr

		PycpObject* operator+(PycpObject* obj);
		PycpObject* operator-(PycpObject* obj);
		PycpObject* operator*(PycpObject* obj);
		PycpObject* operator/(PycpObject* obj);
}; // class PycpObject

PycpNone* PycpNoneRef;
PycpBoolean* PycpTrueRef;
PycpBoolean* PycpFalseRef;

} // namespace Pycp

#endif // __PYCP_OBJECT_HPP__
