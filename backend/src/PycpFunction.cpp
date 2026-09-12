#include "PycpFunction.hpp"
#include "PycpFixedList.hpp"   // FixedList：_builtin_print 的可变参数容器
#include "PycpMap.hpp"         // Map：关键字参数容器
#include "PycpExtension.hpp"   // Extension::MakeArgs / EmptyArgs / EmptyKwargs
#include "PycpMagic.hpp"   // BuildNameList / __call__ 等魔术方法名

namespace Pycp {

// 将指针格式化为十六进制地址字符串（0x...）。
static std::string ptr_address(const void* p) {
	std::ostringstream oss;
	oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(p);
	return oss.str();
}

Function* BuiltinFunction::print = nullptr;

// 支持多参数：以空格分隔打印全部实参，末尾换行。
// 容器形态：全部位置实参已收集为 FixedList，本函数只负责遍历打印。
Object* _builtin_print([[maybe_unused]] Object* self,
                       FixedList* args,
                       [[maybe_unused]] Map* kwargs){
	for (std::size_t i = 0; i < args->size(); ++i){
		if (i > 0) std::cout << " ";
		std::cout << AsString(args->at(i));
	}
	std::cout << std::endl;
	return None::instance;
}

Function::Function() : Function("", nullptr){}

Function::Function(const char* name)
		: Object("Function"), kind(FunctionKind::Native), name(name), native(nullptr){
	set_type_info(PycpTypeId::Function, PycpTypeFlag::Callable);
}

Function::Function(const char* name, PycpCFunction func)
		: Object("Function"), kind(FunctionKind::Native), name(name), native(func){
	set_type_info(PycpTypeId::Function, PycpTypeFlag::Callable);
}

Object* Function::__string__(){
	// 匿名函数沿用与普通函数完全相同的格式，仅名字位置显示 @anonymous：
	//   <function "@anonymous" at 0xADDR>
	// 空名亦归一化为 @anonymous：BoundMethod 的底层方法为 nullptr 时以空串
	// 构造 Function（见 PycpClass.cpp），否则会输出 <function "" at ...>。
	const bool anon = name.empty() || name == ANONYMOUS_FUNCTION;
	const std::string display = anon ? ANONYMOUS_FUNCTION : name;
	// 普通函数："<function \"name\" at 0xADDR>"。
	return String::FromCString(("<function \"" + display +
	                          "\" at " + ptr_address(this) + ">").c_str());
}

Object* Function::__inspect__() {
	// Function / BoundMethod 支持的魔术方法（属性钩子继承自 Object）。
	std::vector<std::string> names = {
		"__string__", "__call__",
		"__get_attribute__", "__set_attribute__", "__delete_attribute__",
		"__inspect__", "__class__",
	};
	return BuildNameList(names);
}

Object* Function::invoke(Object* self, FixedList* args, Map* kwargs){
	if (this->native != nullptr){
		return this->native(self, args, kwargs);
	}
	return None::instance;
}

// 数组形态便捷重载：打包位置实参为 FixedList 后转调容器形态入口。
// 「未绑定方法调用」（Class.method(obj, ...)）：self 缺省时把首个实参提升为
// 接收者——语义与旧实现中 BoundMethod 之外的手工 argv[0] 约定一致。
Object* Function::invoke(Object* self, Object** argv, std::size_t argc){
	const bool no_args = (argv == nullptr || argc == 0);
	FixedList* args = no_args ? Extension::EmptyArgs()
	                          : Extension::MakeArgs(argv, argc);
	FixedList* tail = nullptr;

	if (self == nullptr && owner_class_ != nullptr && args->size() > 0) {
		self = args->at(0);
		std::vector<Object*> rest;
		rest.reserve(args->size() - 1);
		for (std::size_t i = 1; i < args->size(); ++i) {
			Incref(args->at(i));   // FixedList 接管引用（不 Incref）
			rest.push_back(args->at(i));
		}
		tail = FixedList::New(rest);
	}

	Object* result = invoke(self, tail != nullptr ? tail : args,
	                        Extension::EmptyKwargs());
	if (tail != nullptr) Decref(tail);
	if (!no_args) Decref(args);
	return result;
}

// 兼容旧 tree-walking 解释器：单参数形态转调统一入口（无实参调用）。
Object* Function::__call__([[maybe_unused]] Object* args){
	return this->invoke(nullptr, static_cast<Object**>(nullptr), 0);
}

void Function::Initialize(){
	// 内建打印：容器形态原生函数，实参已在调用门收集完毕。
	BuiltinFunction::print = New<Function>(BUILTIN_PRINT, &_builtin_print);
	GC_AddRoot(BuiltinFunction::print);
}

void Function::Finalize(){
	if (BuiltinFunction::print != nullptr){
		GC_RemoveRoot(BuiltinFunction::print);
		Decref(BuiltinFunction::print);
		BuiltinFunction::print = nullptr;
	}
}

} // namespace Pycp
