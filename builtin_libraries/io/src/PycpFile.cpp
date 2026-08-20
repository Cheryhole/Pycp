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
// 接收者（File）经 BoundMethod 作为 argv[0] 传入（self 为方法对象）。
Object* _file_write(Object* self [[maybe_unused]], Object** argv, std::size_t argc) {
	File* f = static_cast<File*>(argv[0]);
	if (argc != 2) {
		throw TypeError("write() expects exactly 1 argument.");
	}
	return f->write(argv[1]);
}

// readline 方法的原生实现：读取一行，返回 String。
Object* _file_readline(Object* self [[maybe_unused]], Object** argv, std::size_t argc) {
	File* f = static_cast<File*>(argv[0]);
	if (argc != 1) {
		throw TypeError("readline() expects no arguments.");
	}
	return f->readline();
}

} // anonymous namespace

File::File(const std::string& name, std::istream* in, std::ostream* out)
	: Object("File"), name_(name), in_(in), out_(out), owned_(false),
	  write_fn_(nullptr), readline_fn_(nullptr) {}

File::File(const std::string& name, std::istream* in, std::ostream* out,
           bool owned)
	: Object("File"), name_(name), in_(in), out_(out), owned_(owned),
	  write_fn_(nullptr), readline_fn_(nullptr) {}

File::~File() {
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

Object* File::write(Object* arg) {
	if (out_ == nullptr) {
		throw TypeError("file '" + name_ + "' is not writable.");
	}
	if (arg == nullptr) {
		throw TypeError("write() argument is null.");
	}
	// 接受任意类型参数，写入时自动转换为字符串（调用 __string__）。
	Object* s = arg->__string__();
	if (s == nullptr || !s->is_type("String")) {
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

Object* File::readline() {
	if (in_ == nullptr) {
		throw TypeError("file '" + name_ + "' is not readable.");
	}
	std::string line;
	if (!std::getline(*in_, line)) {
		// EOF 或读取失败：返回空字符串。
		line.clear();
	}
	return String::FromCString(line.c_str());
}

Object* File::__getattr__(const std::string& name) {
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

Object* File::__string__() {
	return String::FromCString(("<" + name_ + ">").c_str());
}

void File::foreach_ref(const std::function<void(Object*)>& visit) {
	// 注意：write_fn_ / readline_fn_ 为懒创建的绑定方法对象，其生命周期
	// 由 BoundMethod 引用管理。这里遍历可能访问到已被 Decref 的对象，
	// 故仅当非空且确为有效引用时遍历。为稳妥起见暂不遍历（GC 标记非必需）。
	(void)visit;
}

} // namespace Pycp
