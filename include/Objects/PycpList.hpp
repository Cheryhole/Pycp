#ifndef __PYCP_LIST_HPP__
#define __PYCP_LIST_HPP__

#include "PycpObject.hpp"
#include <vector>

namespace Pycp{

class PycpList : public PycpObject{
	private:
		std::vector<PycpObject*>* container;

	public:
		PycpList(const PycpList&) = delete;
		PycpList& operator=(const PycpList&) = delete;

		PycpList(PycpTuple* tuple);
		PycpList(std::vector<PycpObject*> items);
		PycpList();
		~PycpList();

		inline PycpObject* get(int index);
		inline void set(int index, PycpObject* value);
		inline void insert(int index, PycpObject* value);
		inline void append(PycpObject* value);
		inline PycpSize_t length();
		inline void clear();

		virtual PycpObject* __string__();
		virtual PycpObject* __boolean__();
		virtual PycpObject* __list__();
		virtual PycpObject* __tuple__();

		virtual PycpObject* __addition__(PycpObject* obj);
		virtual PycpObject* __multiplication__(PycpObject* obj);
};


} // namespace Pycp


#endif // __PYCP_LIST_HPP__
