#include "PycpObject.hpp"
#include "PycpString.hpp"
#include "PycpList.hpp"
#include "PycpBoolean.hpp"
#include "PycpMap.hpp"
#include "PycpClass.hpp"
#include "PycpGC.hpp"
#include "PycpMagic.hpp"

#include <sstream>
#include <unordered_set>

namespace Pycp{

// 将指针格式化为十六进制地址字符串（0x...）。
static std::string ptr_address(const void* p) {
	std::ostringstream oss;
	oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(p);
	return oss.str();
}

Object::Object(const std::string& type_name)
	: type_name_(type_name){
	this->refcount = 0;
	this->gc_flags = GCFlag::NONE;
	this->private_ = false;
}

Object::~Object(){
	// 释放成员字典中持有的引用。
	for (auto& kv : members_) {
		if (kv.second != nullptr) Decref(kv.second);
	}
	members_.clear();
}

Object* Object::__integer__(){
  throw TypeError("Unsupported to convert to integer.");
}

const char* Object::get_name() const {
  return ANONYMOUS_FUNCTION;
}

Class* Object::get_type_class() {
  // 按运行时类型名查注册表（String/Integer/.../None/Function/File），
  // 未登记则惰性合成一个同名普通 Class（如 <class "None">）。
  return LookupTypeClass(type_name_);
}

Object* Object::__string__(){
  // 默认表示："<name at 0xADDR>"（作为所有未显式定义 __string__ 的
  // 对象的兜底输出；匿名对象 name 为 @anonymous）。
  return String::FromCString(("<" + std::string(get_name()) +
                            " at " + ptr_address(this) + ">").c_str());
}

Object* Object::__negation__(){
  throw TypeError("Unsupported to negate.");
}

Object* Object::__boolean__(){
  // 基类默认：视为真（true）。
  return Boolean::True();
}

Object* Object::__get_attribute__(const std::string& name){
  // 0) 内建只读属性 __name__：返回类型名（type_name()）对应的 String。
  if (name == "__name__") {
    return GetNameAttribute(this);
  }
  // 0.1) 只读 __class__：返回所属类型类对象（注册表持有，Borrowed）。
  if (name == "__class__") {
    return get_type_class();
  }
  // 1) 先从成员字典中查找。
  auto it = members_.find(name);
  if (it != members_.end() && it->second != nullptr) {
    Incref(it->second);
    return it->second;
  }
  // 2) 魔术方法：回退到通用分派（可调用 C++ 虚方法）。非魔术方法名抛错。
  if (Object* magic = GetMagicMethodFunction(name)) {
    return magic;
  }
  throw AttributeError("Unsupported attribute access.");
}

void Object::__set_attribute__(const std::string& name, Object* value){
  // __class__ 只读：拒绝赋值（消费 value 引用，与调用方所有权约定一致）。
  if (name == "__class__") {
    if (value != nullptr) Decref(value);
    throw AttributeError("'__class__' is read-only.");
  }
  // 对象冻结（readonly 装饰器）：拒绝任何动态属性写入。
  if (is_readonly()) {
    if (value != nullptr) Decref(value);
    throw AttributeError("'" + name + "' is read-only on a readonly object.");
  }
  // 写入成员字典：若已存在则释放旧引用。
  auto it = members_.find(name);
  if (it != members_.end()) {
    if (it->second != nullptr) Decref(it->second);
    it->second = value;
  } else {
    members_[name] = value;
  }
  if (value != nullptr) Incref(value);
}

Object* Object::__call__([[maybe_unused]] Object* args){
  throw TypeError("Unsupported to call.");
}

Object* Object::__addition__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to add.");
}

Object* Object::__subtraction__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to subtract.");
}

Object* Object::__multiplication__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to multiply.");
}

Object* Object::__division__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to divide.");
}

Object* Object::__power__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to power.");
}

Object* Object::__less_than__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__less_equal__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__equal__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__not_equal__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__greater_than__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__greater_equal__([[maybe_unused]] Object* other){
  throw TypeError("Unsupported to compare.");
}

Object* Object::__get_item__([[maybe_unused]] Object* key){
  throw TypeError("Unsupported to get item.");
}

Object* Object::__set_item__([[maybe_unused]] Object* key,
                             [[maybe_unused]] Object* value){
  throw TypeError("Unsupported to set item.");
}

Object* Object::__list__(){
  throw TypeError("Unsupported to convert to list.");
}

Object* Object::__map__(){
  // 返回绑定本对象的成员字典视图（仅含 members_）。
  return Map::NewView(this);
}

std::vector<std::pair<std::string, Object*>> Object::member_pairs() const {
	// 合并数据成员（members_）与方法名（来自 __inspect__()，含魔术方法
	// 与类型特有方法），使 __map__ 视图同时包含属性与方法（对齐 Instance/Class）。
	// 方法名对应 value 填 nullptr，视图读取时经 __get_attribute__ 动态取可调用对象。
	std::vector<std::pair<std::string, Object*>> out;
	std::unordered_set<std::string> seen;
	out.reserve(members_.size());
	for (const auto& kv : members_) {
		if (kv.second == nullptr) continue;
		if (seen.insert(kv.first).second) {
			out.emplace_back(kv.first, kv.second);
		}
	}
	// 收集方法名：__inspect__() 为虚调用，子类已正确枚举各自方法名。
	Object* mlist = const_cast<Object*>(this)->__inspect__();
	if (mlist != nullptr) {
		List* lst = static_cast<List*>(mlist);
		for (std::size_t i = 0; i < lst->size(); ++i) {
			Object* name_obj = lst->at(i);
			if (name_obj == nullptr) continue;
			std::string name = AsString(name_obj);
			if (seen.insert(name).second) {
				out.emplace_back(name, nullptr);
			}
		}
		Decref(mlist);
	}
	return out;
}

Object* Object::__hash__(){
  throw TypeError("unhashable type: " + type_name_);
}

Object* Object::__delete__(){
  throw TypeError("object does not support deletion.");
}

void Object::__delete_attribute__(const std::string& name){
  // 对象冻结（readonly 装饰器）：拒绝属性删除。
  if (is_readonly()) {
    throw AttributeError("'" + name + "' is read-only on a readonly object.");
  }
  // 默认从成员字典删除（释放旧引用）；不存在抛 AttributeError。
  auto it = members_.find(name);
  if (it == members_.end() || it->second == nullptr) {
    throw AttributeError("'" + name + "' not found.");
  }
  Decref(it->second);
  members_.erase(it);
}

Object* Object::__delete_item__([[maybe_unused]] Object* key) {
  throw TypeError("object does not support item deletion.");
}

Object* Object::__iterator__(){
  throw TypeError("object is not iterable");
}

Object* Object::__next__(){
  throw TypeError("object is not an iterator");
}

Object* Object::__inspect__(){
  // 默认返回成员字典中所有 key 的名称列表（含已设置的成员，可能含方法）。
  List* lst = Pycp::New<List>();
  for (const auto& kv : members_) {
    lst->append(String::FromCString(kv.first.c_str()));
  }
  return lst;
}

void Object::foreach_ref([[maybe_unused]] const std::function<void(Object*)>& visit){
  // 默认遍历成员字典中的引用。
  for (auto& kv : members_) {
    if (kv.second != nullptr) visit(kv.second);
  }
}

}