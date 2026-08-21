#include "PycpIterator.hpp"

namespace Pycp {

// =============================================================
// ListIterator
// =============================================================

ListIterator::ListIterator(List* source)
	: Object("ListIterator"), source_(source), index_(0) {
	if (source_ != nullptr) Incref(source_);
}

ListIterator::~ListIterator() {
	if (source_ != nullptr) Decref(source_);
	source_ = nullptr;
}

Object* ListIterator::__next__() {
	if (source_ == nullptr) throw StopIteration();
	std::size_t n = source_->size();
	if (index_ >= n) throw StopIteration();
	// at() 返回 Borrowed；转为 Owned 返回。
	Object* elem = source_->at(index_);
	++index_;
	Incref(elem);
	return elem;
}

Object* ListIterator::__members__() {
	List* lst = static_cast<List*>(Object::__members__());
	std::vector<std::string> extra = {
		"__next__", "__iterator__",
		"__get_attribute__", "__set_attribute__", "__members__",
	};
	for (const auto& n : extra) {
		bool found = false;
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* elem = lst->at(i);
			if (elem != nullptr && elem->is_type("String") &&
			    static_cast<String*>(elem)->get_value() == n) {
				found = true;
				break;
			}
		}
		if (!found) lst->append(String::FromCString(n.c_str()));
	}
	return lst;
}

void ListIterator::foreach_ref(const std::function<void(Object*)>& visit) {
	if (source_ != nullptr) visit(source_);
}

// =============================================================
// StringIterator
// =============================================================

StringIterator::StringIterator(String* source)
	: Object("StringIterator"), source_(source), index_(0) {
	if (source_ != nullptr) Incref(source_);
}

StringIterator::~StringIterator() {
	if (source_ != nullptr) Decref(source_);
	source_ = nullptr;
}

Object* StringIterator::__next__() {
	if (source_ == nullptr) throw StopIteration();
	const std::string& s = source_->get_value();
	if (index_ >= s.size()) throw StopIteration();
	// 单字符 String：用 std::string 临时承接，避免 c_str 悬垂。
	std::string one(1, s[index_]);
	++index_;
	return String::FromCString(one.c_str());
}

Object* StringIterator::__members__() {
	List* lst = static_cast<List*>(Object::__members__());
	std::vector<std::string> extra = {
		"__next__",
		"__get_attribute__", "__set_attribute__", "__members__",
	};
	for (const auto& n : extra) {
		bool found = false;
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* elem = lst->at(i);
			if (elem != nullptr && elem->is_type("String") &&
			    static_cast<String*>(elem)->get_value() == n) {
				found = true;
				break;
			}
		}
		if (!found) lst->append(String::FromCString(n.c_str()));
	}
	return lst;
}

void StringIterator::foreach_ref(const std::function<void(Object*)>& visit) {
	if (source_ != nullptr) visit(source_);
}

} // namespace Pycp