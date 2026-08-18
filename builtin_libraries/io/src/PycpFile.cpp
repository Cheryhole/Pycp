#include "PycpFile.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpString.hpp"
#include "PycpNone.hpp"
#include "PycpInteger.hpp"
#include "PycpABI.hpp"

namespace Pycp {

namespace {

// write 方法的原生实现：仅接受字符串，写入后返回 None。
// 接收者（FileObject）经 BoundMethod 作为 argv[0] 传入（self 为方法对象）。
Object* _file_write(Object* self, Object** argv, std::size_t argc) {
	FileObject* f = static_cast<FileObject*>(argv[0]);
	if (argc != 2) {
		throw TypeError("write() expects exactly 1 argument.");
	}
	return f->write(argv[1]);
}

// readline 方法的原生实现：读取一行，返回 String。
Object* _file_readline(Object* self, Object** argv, std::size_t argc) {
	FileObject* f = static_cast<FileObject*>(argv[0]);
	if (argc != 1) {
		throw TypeError("readline() expects no arguments.");
	}
	return f->readline();
}

} // anonymous namespace

FileObject::FileObject(const std::string& name, std::istream* in, std::ostream* out)
	: Object(Type::FILE), name_(name), in_(in), out_(out), owned_(false),
	  write_fn_(nullptr), readline_fn_(nullptr) {}

FileObject::FileObject(const std::string& name, std::istream* in, std::ostream* out,
                       bool owned)
	: Object(Type::FILE), name_(name), in_(in), out_(out), owned_(owned),
	  write_fn_(nullptr), readline_fn_(nullptr) {}

FileObject::~FileObject() {
	if (write_fn_ != nullptr) Decref(write_fn_);
	if (readline_fn_ != nullptr) Decref(readline_fn_);
	write_fn_ = nullptr;
	readline_fn_ = nullptr;
	if (owned_) {
		delete in_;
		delete out_;
	}
	in_ = nullptr;
	out_ = nullptr;
}

Object* FileObject::write(Object* arg) {
	if (out_ == nullptr) {
		throw TypeError("file '" + name_ + "' is not writable.");
	}
	if (arg == nullptr) {
		throw TypeError("write() argument is null.");
	}
	// 接受任意类型参数，写入时自动转换为字符串（调用 __string__）。
	Object* s = arg->__string__();
	if (s == nullptr || s->type != Type::STRING) {
		throw TypeError("__string__ did not return a String.");
	}
	(*out_) << static_cast<String*>(s)->get_value();
	out_->flush();
	// __string__ 返回 Owned 对象时释放临时引用；返回自身（Borrowed）则不 Decref。
	if (s != arg) {
		Decref(s);
	}
	return None::instance;
}

Object* FileObject::readline() {
	if (in_ == nullptr) {
		throw TypeError("file '" + name_ + "' is not readable.");
	}
	std::string line;
	if (!std::getline(*in_, line)) {
		// EOF 或读取失败：返回空字符串。
		line.clear();
	}
	return String_FromString(line.c_str());
}

Object* FileObject::__getattr__(const std::string& name) {
	if (name == "write") {
		if (write_fn_ == nullptr) {
			// New 返回 refcount=1，由成员 write_fn_ 持有（析构 Decref）。
			write_fn_ = New<Function>("write", _file_write);
		}
		return write_fn_;
	}
	if (name == "readline") {
		if (readline_fn_ == nullptr) {
			readline_fn_ = New<Function>("readline", _file_readline);
		}
		return readline_fn_;
	}
	throw AttributeError("file '" + name_ + "' has no attribute '" + name + "'");
}

Object* FileObject::__string__() {
	return String_FromString(("<" + name_ + ">").c_str());
}

} // namespace Pycp
