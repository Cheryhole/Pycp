#include "PycpMagic.hpp"
#include "PycpFunction.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpList.hpp"
#include "PycpString.hpp"

#include <unordered_map>

namespace Pycp {

namespace {

// 惰性创建并缓存各魔术方法对应的 Function（GC 常驻，仅创建一次）。
// key: 魔术方法名；value: Function(name, _magic_fn)。
std::unordered_map<std::string, Function*>& magic_cache() {
	static std::unordered_map<std::string, Function*> cache;
	return cache;
}

// 0 参魔术方法：分派到接收者的 C++ 虚方法。argv[0] = 接收者（BoundMethod 绑定）。
Object* _magic0(Object* receiver, const std::string& m) {
	if (m == "__integer__")   return receiver->__integer__();
	if (m == "__string__")    return receiver->__string__();
	if (m == "__list__")      return receiver->__list__();
	if (m == "__iterator__")  return receiver->__iterator__();
	if (m == "__next__")      return receiver->__next__();
	if (m == "__negation__")  return receiver->__negation__();
	if (m == "__members__")   return receiver->__members__();
	throw AttributeError("unknown magic method '" + m + "'");
}

// 1 参魔术方法：argv[1] 为参数。比较/算术/下标等。
Object* _magic1(Object* receiver, const std::string& m, Object* arg) {
	if (m == "__addition__")       return receiver->__addition__(arg);
	if (m == "__subtraction__")    return receiver->__subtraction__(arg);
	if (m == "__multiplication__") return receiver->__multiplication__(arg);
	if (m == "__division__")       return receiver->__division__(arg);
	if (m == "__power__")          return receiver->__power__(arg);
	if (m == "__less_than__")      return receiver->__less_than__(arg);
	if (m == "__less_equal__")     return receiver->__less_equal__(arg);
	if (m == "__equal__")          return receiver->__equal__(arg);
	if (m == "__not_equal__")      return receiver->__not_equal__(arg);
	if (m == "__greater_than__")   return receiver->__greater_than__(arg);
	if (m == "__greater_equal__")  return receiver->__greater_equal__(arg);
	if (m == "__get_item__")       return receiver->__get_item__(arg);
	if (m == "__get_attribute__")  return receiver->__get_attribute__(AsString(arg));
	throw AttributeError("unknown magic method '" + m + "'");
}

// 2 参魔术方法：argv[1] 为 key, argv[2] 为 value。
Object* _magic2(Object* receiver, const std::string& m, Object* arg1, Object* arg2) {
	if (m == "__set_item__")       return receiver->__set_item__(arg1, arg2);
	if (m == "__set_attribute__")  {
		receiver->__set_attribute__(AsString(arg1), arg2);
		return None::instance;
	}
	throw AttributeError("unknown magic method '" + m + "'");
}

bool is_zero_arg_magic(const std::string& m) {
	return m == "__integer__" || m == "__string__" || m == "__list__" ||
	       m == "__iterator__" || m == "__next__" || m == "__negation__" ||
	       m == "__members__";
}

bool is_one_arg_magic(const std::string& m) {
	return m == "__addition__" || m == "__subtraction__" ||
	       m == "__multiplication__" || m == "__division__" ||
	       m == "__power__" || m == "__less_than__" ||
	       m == "__less_equal__" || m == "__equal__" ||
	       m == "__not_equal__" || m == "__greater_than__" ||
	       m == "__greater_equal__" || m == "__get_item__" ||
	       m == "__get_attribute__";
}

bool is_two_arg_magic(const std::string& m) {
	return m == "__set_item__" || m == "__set_attribute__";
}

// 统一 native 入口：fn->get_name() 即魔术方法名，argv[0] 为接收者。
Object* _magic_fn(Object* fn, Object** argv, std::size_t argc) {
	if (fn == nullptr || argv == nullptr || argc < 1) {
		throw TypeError("magic method requires a receiver.");
	}
	std::string m = static_cast<Function*>(fn)->get_name();
	Object* receiver = argv[0];
	if (is_zero_arg_magic(m)) {
		if (argc != 1) throw TypeError(m + "() takes no arguments.");
		return _magic0(receiver, m);
	}
	if (is_one_arg_magic(m)) {
		if (argc != 2) throw TypeError(m + "() takes exactly 1 argument.");
		return _magic1(receiver, m, argv[1]);
	}
	if (is_two_arg_magic(m)) {
		if (argc != 3) throw TypeError(m + "() takes exactly 2 arguments.");
		return _magic2(receiver, m, argv[1], argv[2]);
	}
	throw AttributeError("unknown magic method '" + m + "'");
}

} // anonymous namespace

bool IsMagicMethodName(const std::string& name) {
	return is_zero_arg_magic(name) || is_one_arg_magic(name) || is_two_arg_magic(name);
}

Object* GetMagicMethodFunction(const std::string& name) {
	if (!IsMagicMethodName(name)) return nullptr;
	auto& cache = magic_cache();
	auto it = cache.find(name);
	if (it != cache.end()) return it->second;
	Function* f = New<Function>(name.c_str(), _magic_fn);
	cache[name] = f;
	GC_AddRoot(f); // 常驻缓存，避免被回收
	return f;
}

Object* BuildNameList(const std::vector<std::string>& names) {
	List* lst = Pycp::New<List>();
	for (const std::string& n : names) {
		lst->append(String::FromCString(n.c_str()));
	}
	return lst;
}

Object* GetNameAttribute(Object* receiver) {
	// 属性访问 __name__：返回该对象类型名（type_name()）对应的 String。
	// 返回 Owned 引用（新创建 String），由调用方管理。
	return String::FromCString(receiver->type_name().c_str());
}

} // namespace Pycp