#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpList.hpp"
#include "PycpBoolean.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"

#include <cmath>
#include <stdexcept>

namespace Pycp {

Integer* Integer::instances[PYCP_INTEGER_INSTANCES] = {0};

Integer::Integer() : Integer(INT64_C(0)){}

Integer::Integer(int64_t value) : Object("Integer"){
	this->_value = value;
}

Integer::Integer(const std::string& value) : Object("Integer"){
	try{
		this->_value = std::stoll(value);  // 使用 64 位解析，避免 stoi 截断
	} catch (const std::invalid_argument&){
		throw ValueError("Invalid literal for integer: \"" + value + "\"");
	} catch (const std::out_of_range&){
		throw ValueError("Integer literal out of range: \"" + value + "\"");
	}
}

Integer::Integer(Integer* value) : Integer(value->get_value()){}

Integer::Integer(Object* obj) : Object("Integer"){
	if (obj == nullptr){
		throw TypeError("Cannot construct Integer from null object.");
	}
	Integer* i = static_cast<Integer*>(obj->__integer__());
	this->_value = i->get_value();
}

Integer::~Integer(){}

int64_t Integer::get_value() const{
	return this->_value;
}

Object* Integer::FromLong(long long value){
	return New<Integer>(static_cast<int64_t>(value));
}

Object* Integer::__integer__(){
	return this;
}

Object* Integer::__string__(){
	return New<String>(std::to_string(this->_value));
}

Object* Integer::__hash__(){
	// 哈希值即整数本身（对齐 Python：hash(42) == 42）。
	return Integer::FromLong(this->_value);
}

Object* Integer::__boolean__(){
	// 非零为 True，零为 False。
	return this->_value == 0 ? Boolean::False() : Boolean::True();
}

Object* Integer::__negation__(){
	return New<Integer>(-(this->_value));
}

Object* Integer::__addition__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to add.");
	}
	Integer* i = static_cast<Integer*>(other);
	return New<Integer>(this->_value + i->_value);
}

Object* Integer::__subtraction__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to subtract.");
	}
	Integer* i = static_cast<Integer*>(other);
	return New<Integer>(this->_value - i->_value);
}

Object* Integer::__multiplication__(Object* other){
	if (other == nullptr) throw TypeError("Unsupported to multiply.");

	if (dynamic_cast<Integer*>(other) != nullptr){
		Integer* i = static_cast<Integer*>(other);
		return New<Integer>(this->_value * i->_value);
	}
	else if (other->is_type("String")){
		String* s = static_cast<String*>(other);
		std::string str = s->get_value();
		std::string res;
		for (int64_t i = 0; i < this->_value; i++){
			res += str;
		}
		return New<String>(res);
	}

	throw TypeError("Unsupported to multiply.");
}

Object* Integer::__division__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to divide.");
	}
	Integer* i = static_cast<Integer*>(other);
	if (i->_value == 0){
		throw ValueError("Division by zero.");
	}
	return New<Integer>(this->_value / i->_value);
}

Object* Integer::__power__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to power.");
	}
	Integer* i = static_cast<Integer*>(other);
	if (i->_value < 0){
		throw ValueError("Negative exponent is not supported.");
	}
	return New<Integer>(static_cast<int64_t>(
		std::pow(static_cast<double>(this->_value), static_cast<double>(i->_value))));
}

// 比较运算符：仅支持同类型 Integer。返回小整数池 Integer 0/1（PERMANENT，
// 由 ABI Compare 返回给 VM，栈持有引用但无需额外 Decref——池对象常驻）。
Object* Integer::__less_than__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value < static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

Object* Integer::__less_equal__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value <= static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

Object* Integer::__equal__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		// 不同类型直接判不等（对齐 Python: 42 == "x" -> False）。
		return Boolean::False();
	}
	return this->_value == static_cast<Integer*>(other)->_value
		? Boolean::True() : Boolean::False();
}

Object* Integer::__not_equal__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value != static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

Object* Integer::__greater_than__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value > static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

Object* Integer::__greater_equal__(Object* other){
	if (other == nullptr || dynamic_cast<Integer*>(other) == nullptr){
		throw TypeError("Unsupported to compare.");
	}
	return this->_value >= static_cast<Integer*>(other)->_value
		? instances[1] : instances[0];
}

Object* Integer::__inspect__() {
	// 先收集基类 members_ 中的 key，再添加 integer 特有的魔术方法名。
	List* lst = static_cast<List*>(Object::__inspect__());
	std::vector<std::string> extra = {
		"__integer__", "__string__", "__boolean__", "__negation__", "__addition__",
		"__subtraction__", "__multiplication__", "__division__", "__power__",
		"__less_than__", "__less_equal__", "__equal__", "__not_equal__",
		"__greater_than__", "__greater_equal__",
		"__get_attribute__", "__set_attribute__", "__delete_attribute__", "__inspect__",
		"__map__", "__hash__",
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

void Integer::Initialize(){
	for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++){
		Integer::instances[i] = New<Integer>(i);
		// 小整数池为常驻对象，永不参与回收
		Integer::instances[i]->_set_gc_flags(
			Integer::instances[i]->_gc_flags() | GCFlag::PERMANENT);
		GC_AddRoot(Integer::instances[i]);
	}
}

void Integer::Finalize(){
	for (int i = 0; i < PYCP_INTEGER_INSTANCES; i++){
		if (Integer::instances[i] != nullptr){
			GC_RemoveRoot(Integer::instances[i]);
			Integer::instances[i]->_set_gc_flags(
				Integer::instances[i]->_gc_flags() | GCFlag::NONE);
			Decref(Integer::instances[i]);
			Integer::instances[i] = nullptr;
		}
	}
}

} // namespace Pycp