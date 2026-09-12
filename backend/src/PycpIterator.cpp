#include "PycpIterator.hpp"

namespace Pycp {

// =============================================================
// ListIterator
// =============================================================

ListIterator::ListIterator(List* source)
	: Object("ListIterator"), source_(source), index_(0) {
	set_type_info(PycpTypeId::ListIterator, PycpTypeFlag::Iterable);
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

Object* ListIterator::__inspect__() {
	// 定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	CollectUniqueName(names, "__next__");
	CollectUniqueName(names, "__iterator__");
	CollectUniqueName(names, "__get_attribute__");
	CollectUniqueName(names, "__set_attribute__");
	CollectUniqueName(names, "__delete_attribute__");
	CollectUniqueName(names, "__inspect__");
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

void ListIterator::foreach_ref(const std::function<void(Object*)>& visit) {
	if (source_ != nullptr) visit(source_);
}

// =============================================================
// StringIterator
// =============================================================

StringIterator::StringIterator(String* source)
	: Object("StringIterator"), source_(source), index_(0) {
	set_type_info(PycpTypeId::StringIterator, PycpTypeFlag::Iterable);
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

Object* StringIterator::__inspect__() {
	// 定型为 FixedList。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	CollectUniqueName(names, "__next__");
	CollectUniqueName(names, "__get_attribute__");
	CollectUniqueName(names, "__set_attribute__");
	CollectUniqueName(names, "__inspect__");
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

void StringIterator::foreach_ref(const std::function<void(Object*)>& visit) {
	if (source_ != nullptr) visit(source_);
}

// =============================================================
// FixedListIterator
// =============================================================

FixedListIterator::FixedListIterator(FixedList* source)
	: Object("FixedListIterator"), source_(source), index_(0) {
	set_type_info(PycpTypeId::FixedListIterator, PycpTypeFlag::Iterable);
	if (source_ != nullptr) Incref(source_);
}

FixedListIterator::~FixedListIterator() {
	if (source_ != nullptr) Decref(source_);
	source_ = nullptr;
}

Object* FixedListIterator::__next__() {
	if (source_ == nullptr) throw StopIteration();
	std::size_t n = source_->size();
	if (index_ >= n) throw StopIteration();
	// at() 返回 Borrowed；转为 Owned 返回。
	Object* elem = source_->at(index_);
	++index_;
	Incref(elem);
	return elem;
}

Object* FixedListIterator::__inspect__() {
	// 返回 FixedList（方法名列表）。
	std::vector<Object*> names;
	for (const auto& kv : members_) CollectUniqueName(names, kv.first);
	CollectUniqueName(names, "__next__");
	CollectUniqueName(names, "__iterator__");
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

void FixedListIterator::foreach_ref(const std::function<void(Object*)>& visit) {
	if (source_ != nullptr) visit(source_);
}

} // namespace Pycp