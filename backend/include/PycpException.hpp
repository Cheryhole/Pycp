#ifndef PYCP_EXCEPTION_HPP
#define PYCP_EXCEPTION_HPP

#include <stdexcept>
#include <string>

#include "PycpConfig.hpp"

namespace Pycp{

class Object;

// =============================================================
// Pycp 统一异常体系
//
// 所有 Pycp 抛出的错误均继承自 Exception。Exception 除携带错误
// 描述（msg）外，可选携带出错文件路径（file）与行号（lineno），
// 用于按如下两行格式输出：
//
//   File "<file>", line <lineno>
//   <error>
//
// 其中 <file> 为出错文件路径，<lineno> 为行号，<error> 为具体描述。
// what() 仅返回错误描述（不带任何前缀），format() 负责拼装两行格式。
// =============================================================
// 标记 PYCP_API：Windows shared 运行时下导出，保证跨 DLL 抛出/捕获异常时
// RTTI 一致（MSVC 跨 DLL catch 自定义异常类型须该类导出）。
class PYCP_API Exception : public std::runtime_error{
	public:
		std::string file;   // 出错文件路径（空表示无位置信息）
		int lineno = -1;    // 出错行号（-1 表示未知）

		explicit Exception(const std::string& msg)
				: std::runtime_error(msg) {}

		Exception(const std::string& file_, int lineno_, const std::string& msg)
				: std::runtime_error(msg), file(file_), lineno(lineno_) {}

		// 输出两行格式；无文件路径时仅输出错误描述。
		std::string format() const {
			if (file.empty()) return what();
			return "File \"" + file + "\", line " + std::to_string(lineno) + "\n" + what();
		}
};

// 类型错误：对不支持的类型的对象执行运算、调用非可调用对象等
class TypeError : public Exception {
	public:
		explicit TypeError(const std::string& msg)
				: Exception("TypeError: " + msg) {}
		TypeError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "TypeError: " + msg) {}
};

// 值错误：除零、负指数、非法字面量等
class ValueError : public Exception {
	public:
		explicit ValueError(const std::string& msg)
				: Exception("ValueError: " + msg) {}
		ValueError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "ValueError: " + msg) {}
};

// 迭代结束：迭代器耗尽后调用 __next__ 抛出。仅 foreach（FOR_ITER opcode）
// 内部捕捉作为循环结束信号，其他场景不捕捉（照常向外传播）。
class StopIteration : public Exception {
	public:
		explicit StopIteration(const std::string& msg = "iterating over a depleted iterator")
				: Exception("StopIteration: " + msg) {}
		StopIteration(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "StopIteration: " + msg) {}
};

// 名称错误：引用未定义的变量
class NameError : public Exception {
	public:
		explicit NameError(const std::string& msg)
				: Exception("NameError: " + msg) {}
		NameError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "NameError: " + msg) {}
};

// 字节码格式错误：.cpycp 序列化 / 反序列化 / 格式校验失败
class BytecodeError : public Exception {
	public:
		explicit BytecodeError(const std::string& msg)
				: Exception("BytecodeError: " + msg) {}
		BytecodeError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "BytecodeError: " + msg) {}
};

// VM 运行时错误：栈下溢、索引越界、未知操作码、跳转越界等
class VMError : public Exception {
	public:
		explicit VMError(const std::string& msg)
				: Exception("VMError: " + msg) {}
		VMError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "VMError: " + msg) {}
};

// 导入错误：import 目标模块不存在或加载失败
class ImportError : public Exception {
	public:
		explicit ImportError(const std::string& msg)
				: Exception("ImportError: " + msg) {}
		ImportError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "ImportError: " + msg) {}
};

// 索引错误：下标越界 / 非法索引
class IndexError : public Exception {
	public:
		explicit IndexError(const std::string& msg)
				: Exception("IndexError: " + msg) {}
		IndexError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "IndexError: " + msg) {}
};

// 属性错误：对象不存在指定属性 / 方法
class AttributeError : public Exception {
	public:
		explicit AttributeError(const std::string& msg)
				: Exception("AttributeError: " + msg) {}
		AttributeError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "AttributeError: " + msg) {}
};

// 原生扩展错误：动态库加载 / 符号解析 / 原生扩展初始化失败
class NativeExtensionError : public Exception {
	public:
		explicit NativeExtensionError(const std::string& msg)
				: Exception("NativeExtensionError: " + msg) {}
		NativeExtensionError(const std::string& file_, int lineno_, const std::string& msg)
				: Exception(file_, lineno_, "NativeExtensionError: " + msg) {}
};

} // namespace Pycp

#endif // PYCP_EXCEPTION_HPP
