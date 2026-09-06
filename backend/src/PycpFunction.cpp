#include "PycpFunction.hpp"
#include "PycpMagic.hpp"   // BuildNameList / __call__ 等魔术方法名

namespace Pycp {

// 将指针格式化为十六进制地址字符串（0x...）。
static std::string ptr_address(const void* p) {
	std::ostringstream oss;
	oss << "0x" << std::hex << reinterpret_cast<uintptr_t>(p);
	return oss.str();
}

Function* BuiltinFunction::print = nullptr;

Object* _builtin_print([[maybe_unused]] Object* self, Object** argv, std::size_t argc){
	// 支持多参数：以空格分隔打印全部实参，末尾换行
	if (argv != nullptr){
		for (std::size_t i = 0; i < argc; ++i){
			if (i > 0) std::cout << " ";
			std::cout << AsString(argv[i]);
		}
	}
	std::cout << std::endl;
	return None::instance;
}

Function::Function() : Function("", nullptr){}

Function::Function(const char* name)
		: Object("Function"), kind(FunctionKind::Native), name(name), native(nullptr){}

Function::Function(const char* name, PycpNativeFunction func)
		: Object("Function"), kind(FunctionKind::Native), name(name), native(func){}

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
		"__inspect__",
	};
	return BuildNameList(names);
}

Object* Function::invoke(Object** argv, std::size_t argc){
	if (this->native != nullptr){
		return this->native(this, argv, argc);
	}
	return None::instance;
}

// 兼容旧 tree-walking 解释器：单参数形态转调统一入口
Object* Function::__call__([[maybe_unused]] Object* args){
	return this->invoke(nullptr, 0);
}

void Function::Initialize(){
	BuiltinFunction::print = New<Function>(BUILTIN_PRINT, _builtin_print);
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
