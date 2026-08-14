#ifndef PYCP_EXCEPTION_HPP
#define PYCP_EXCEPTION_HPP

#include <stdexcept>
#include <string>

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
class Exception : public std::runtime_error{
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

} // namespace Pycp

#endif // PYCP_EXCEPTION_HPP
