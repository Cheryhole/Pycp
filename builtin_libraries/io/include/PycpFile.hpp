#ifndef PYCP_FILE_HPP
#define PYCP_FILE_HPP

// =============================================================
// Pycp 文件对象（FileObject）—— io 内建库专属对象
//
// 对应 Python 的文件对象：io 模块的 stdin / stdout / stderr 即此类型。
// 支持 .write（仅字符串）、.readline 方法，方法经 BoundMethod 自动绑定。
//
// 文件对象不拥有底层流（默认 owned_ = false），由调用方（如标准库
// std::cin/std::cout/std::cerr）保证流的存活；若 owned_ = true 则析构
// 时 delete 底层流。
// =============================================================

#include "PycpObject.hpp"
#include "PycpFunction.hpp"

#include <istream>
#include <ostream>
#include <string>

namespace Pycp {

class FileObject : public Object {
private:
	std::string name_;        // 文件描述名（如 "<stdout>"）
	std::istream* in_;        // 输入流（可空）
	std::ostream* out_;       // 输出流（可空）
	bool owned_;              // 是否拥有底层流（true 时析构 delete）
	Function* write_fn_;      // write 方法对象（懒创建）
	Function* readline_fn_;   // readline 方法对象（懒创建）

public:
	FileObject(const std::string& name, std::istream* in, std::ostream* out);
	FileObject(const std::string& name, std::istream* in, std::ostream* out,
	           bool owned);
	~FileObject() override;

	Object* write(Object* arg);
	Object* readline();

	Object* __getattr__(const std::string& name) override;
	Object* __string__() override;
};

} // namespace Pycp

#endif // PYCP_FILE_HPP
