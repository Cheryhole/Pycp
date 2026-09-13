#ifndef PYCP_STRING_HPP
#define PYCP_STRING_HPP

#include "PycpObject.hpp"
#include "PycpMethodTable.hpp"   // MethodEntry / MethodTableFn
#include <string>

namespace Pycp{

class PYCP_API String : public Object{
	private:
		std::string _value;

	public:
		String();
		String(const std::string&);
		String(String*);
		String(Object*);
		virtual ~String();

		std::string get_value() const;

		// 静态工厂：从 C 字符串构造 String（返回 Owned，refcount=1）。
		static Object* FromCString(const char* value);

		Object* __integer__();
		Object* __string__();
		// 原始字符串形式（repr）：返回带双引号并完整转义的新 String（Owned）。
		Object* __raw_string__() override;
		Object* __equal__(Object* other) override;
		Object* __boolean__() override;
		Object* __addition__(Object*);
		Object* __multiplication__(Object*);
		Object* __get_item__(Object* key) override;
	Object* __list__() override;
	Object* __iterator__() override;
	Object* __inspect__() override;
	Object* __hash__() override;

	static void Initialize();
		static void Finalize();

};

// 取对象的字符串形式（__string__）的值。借用语义：内部以 Incref/Decref
// 包围，不接管所有权（__string__ 可能返回 Borrowed 或 Owned）。
std::string AsString(Object*);

// 取对象的原始字符串形式（__raw_string__）的值。借用语义同上。
std::string AsRawString(Object*);

// CPython repr 风格转义：外层加双引号，转义 \\ 与 \"，把 \n / \r / \t 转成
// 可读形式，其余 < 0x20 与 0x7f 的字节写作 \xHH；>= 0x80 的字节原样保留
// （不破坏 UTF-8 中文）。
std::string EscapeForRepr(const std::string& value);

// String 全部方法（全部魔术方法）的唯一权威清单。
// 类型类注册（register_object）与实例 __inspect__ 均从它派生。
const std::vector<MethodEntry>& String_method_table();

// 类型萃取特化：String（str 族，无子类）。
template <> struct TypeTraits<String> {
	static constexpr PycpTypeId   id            = PycpTypeId::String;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::StringSubclass | PycpTypeFlag::Hashable | PycpTypeFlag::Iterable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif // PYCP_STRING_HPP