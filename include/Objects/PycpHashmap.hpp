#ifndef __PYCP_HASHMAP_HPP__
#define __PYCP_HASHMAP_HPP__

#include "PycpObject.hpp"
#include <unordered_map>
#include <cstring>

namespace Pycp{

struct PycpHashCaller{
	template <PycpObject*>
	size_t operator()(const PycpObject* obj) const {
		return obj->hash();
	}
};

class PycpHashmap : public PycpObject{
	private:
		std::unordered_map<PycpObject*, PycpObject*, PycpHashCaller>* container;

	public:
		PycpHashmap();
		~PycpHashmap();

		PycpObject* get(PycpObject* key) const;
		void set(PycpObject* key, PycpObject* value);
		void remove(PycpObject* key);
		void clear();
		PycpSize_t length() const;
		virtual PycpSize_t hash() const;

		virtual PycpObject* __string__() const;
		virtual PycpObject* __boolean__() const;

}; // class PycpHashmap

} // namespace Pycp


#endif // __PYCP_HASHMAP_HPP__