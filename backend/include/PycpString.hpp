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

std::string AsString(Object*);

// String 全部方法（全部魔术方法）的唯一权威清单。
// 类型类注册（register_object）与实例 __inspect__ 均从它派生。
const std::vector<MethodEntry>& String_method_table();

} // namespace Pycp

#endif // PYCP_STRING_HPP