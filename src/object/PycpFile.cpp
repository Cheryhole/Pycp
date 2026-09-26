#include "object/PycpFile.hpp"
#include "object/PycpClass.hpp"
#include "object/PycpException.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpString.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpList.hpp"
#include "abi/PycpABI.hpp"
#include "object/PycpMagic.hpp"
#include "object/PycpFixedList.hpp"
#include "object/PycpExtension.hpp" // 扩展唯一对外头（参数规范框架 / set_*）
#include "object/PycpModule.hpp"    // Module::set_type（FileTypeClass 用）
#include "object/PycpBoolean.hpp"   // Boolean::True / False

#include <sstream>
#include <cstring>

namespace Pycp {

// =============================================================
// 模式解析
// =============================================================

FileMode File::ParseMode(const std::string& mode_str) {
    if (mode_str.empty()) {
        return FileMode::READ;
    }

    bool has_binary = false;
    std::string base_mode;
    
    for (char c : mode_str) {
        if (c == 'b') {
            has_binary = true;
        } else {
            base_mode += c;
        }
    }

    FileMode mode;
    if (base_mode == "r" || base_mode.empty()) {
        mode = FileMode::READ;
    } else if (base_mode == "w") {
        mode = FileMode::WRITE;
    } else if (base_mode == "a") {
        mode = FileMode::APPEND;
    } else if (base_mode == "r+") {
        mode = FileMode::READ_WRITE;
    } else if (base_mode == "w+") {
        mode = FileMode::WRITE_READ;
    } else if (base_mode == "a+") {
        mode = FileMode::APPEND_READ;
    } else {
        throw ValueError("invalid file mode: '" + mode_str + "'");
    }

    // 存储二进制标志
    if (has_binary) {
        // 二进制模式暂时通过 is_binary_ 标记，实际读取时使用 binary
        // 但为了与现有逻辑一致，这里保留
    }
    return mode;
}

std::ios::openmode File::ToOpenMode(FileMode mode) const {
    std::ios::openmode om = std::ios::in;
    switch (mode) {
        case FileMode::READ:
            om = std::ios::in;
            break;
        case FileMode::WRITE:
            om = std::ios::out | std::ios::trunc;
            break;
        case FileMode::APPEND:
            om = std::ios::out | std::ios::app;
            break;
        case FileMode::READ_WRITE:
            om = std::ios::in | std::ios::out;
            break;
        case FileMode::WRITE_READ:
            om = std::ios::in | std::ios::out | std::ios::trunc;
            break;
        case FileMode::APPEND_READ:
            om = std::ios::in | std::ios::out | std::ios::app;
            break;
				default:
						break;
    }
    if (is_binary_) {
        om |= std::ios::binary;
    }
    return om;
}

void File::EnsureOpen() const {
    if (!is_open_) {
        throw ValueError("I/O operation on closed file.");
    }
}

void File::EnsureReadable() const {
    EnsureOpen();
    if (!(mode_ == FileMode::READ || mode_ == FileMode::READ_WRITE ||
          mode_ == FileMode::WRITE_READ || mode_ == FileMode::APPEND_READ)) {
        throw TypeError("file '" + name_ + "' not readable.");
    }
}

void File::EnsureWritable() const {
    EnsureOpen();
    if (!(mode_ == FileMode::WRITE || mode_ == FileMode::APPEND ||
          mode_ == FileMode::READ_WRITE || mode_ == FileMode::WRITE_READ ||
          mode_ == FileMode::APPEND_READ)) {
        throw TypeError("file '" + name_ + "' not writable.");
    }
}

// =============================================================
// 构造函数 / 析构函数
// =============================================================

// 构造函数：使用路径作为显示名称
File::File(const std::string& path, const std::string& mode)
    : Object("File"),
      name_(path),
      path_(path),
      mode_(FileMode::READ),
      is_open_(false),
      is_binary_(false),
      owns_stream_(true),
      in_stream_(nullptr),
      out_stream_(nullptr),
      write_fn_(nullptr),
      read_fn_(nullptr),
      readline_fn_(nullptr),
      close_fn_(nullptr),
      readlines_fn_(nullptr) {
    set_type_info(PycpTypeId::File, PycpTypeFlag::Mutable);
    open(path, mode);
}

// 构造函数：路径 + 模式 + 自定义显示名称
File::File(const std::string& path, const std::string& mode, const std::string& name)
    : Object("File"),
      name_(name),
      path_(path),
      mode_(FileMode::READ),
      is_open_(false),
      is_binary_(false),
      owns_stream_(true),
      in_stream_(nullptr),
      out_stream_(nullptr),
      write_fn_(nullptr),
      read_fn_(nullptr),
      readline_fn_(nullptr),
      close_fn_(nullptr),
      readlines_fn_(nullptr) {
    set_type_info(PycpTypeId::File, PycpTypeFlag::Mutable);
    open(path, mode);
}

// 从已有流构造（用于 stdin/stdout/stderr），使用指定的显示名称
File::File(const std::string& name, std::istream* in, std::ostream* out)
    : Object("File"),
      name_(name),
      path_(name),  // 特殊文件路径设为名称
      mode_(FileMode::READ_WRITE),
      is_open_(true),
      is_binary_(false),
      owns_stream_(false),
      in_stream_(in),
      out_stream_(out),
      write_fn_(nullptr),
      read_fn_(nullptr),
      readline_fn_(nullptr),
      close_fn_(nullptr),
      readlines_fn_(nullptr) {
    set_type_info(PycpTypeId::File, PycpTypeFlag::Mutable);
    // 特殊流（stdin/stdout/stderr）始终打开，不能真正关闭
    // close() 只标记状态，不关闭底层流
}

File::~File() {
    if (is_open_ && owns_stream_) {
        close();
    }
    if (write_fn_ != nullptr) Decref(write_fn_);
    if (read_fn_ != nullptr) Decref(read_fn_);
    if (readline_fn_ != nullptr) Decref(readline_fn_);
    if (close_fn_ != nullptr) Decref(close_fn_);
    if (readlines_fn_ != nullptr) Decref(readlines_fn_);
}

// =============================================================
// 静态工厂
// =============================================================

File* File::FromStream(const std::string& name, void* in, void* out) {
    return New<File>(name, static_cast<std::istream*>(in),
                     static_cast<std::ostream*>(out));
}

// =============================================================
// 核心方法
// =============================================================

void File::open(const std::string& path, const std::string& mode) {
    if (is_open_ && owns_stream_) {
        close();
    }
    
    // 如果当前是外部流（stdin/stdout），不能重新打开
    if (!owns_stream_) {
        throw IOError("cannot reopen special file: " + name_);
    }
    
    path_ = path;
    if (name_ == path_ || name_.empty()) {
        name_ = path;
    }
    is_binary_ = (mode.find('b') != std::string::npos);
    mode_ = ParseMode(mode);
    
    auto openmode = ToOpenMode(mode_);
    
    // 重置 file_ 并打开
    file_.close();
    file_.clear();
    file_.open(path_, openmode);
    
    if (!file_.is_open()) {
        throw IOError("failed to open file: " + path_);
    }
    
    is_open_ = true;
    owns_stream_ = true;
    in_stream_ = nullptr;
    out_stream_ = nullptr;
}

void File::close() {
    if (!is_open_) {
        return;
    }
    
    if (owns_stream_) {
        file_.close();
    }
    // 外部流（stdin/stdout/stderr）不真正关闭，只标记状态
    is_open_ = false;
}

Object* File::read() {
    EnsureReadable();
    
    if (owns_stream_) {
        std::stringstream ss;
        ss << file_.rdbuf();
        return String::FromCString(ss.str().c_str());
    } else if (in_stream_ != nullptr) {
        std::stringstream ss;
        ss << in_stream_->rdbuf();
        return String::FromCString(ss.str().c_str());
    }
    throw TypeError("file '" + name_ + "' not readable.");
}

Object* File::read(std::size_t size) {
    EnsureReadable();
    
    if (size == 0) {
        return String::FromCString("");
    }
    
    char* buffer = new char[size + 1];
    
    if (owns_stream_) {
        file_.read(buffer, size);
    } else if (in_stream_ != nullptr) {
        in_stream_->read(buffer, size);
    } else {
        delete[] buffer;
        throw TypeError("file '" + name_ + "' not readable.");
    }
    
    std::streamsize actual = owns_stream_ ? file_.gcount() : in_stream_->gcount();
    buffer[actual] = '\0';
    
    Object* result = String::FromCString(buffer);
    delete[] buffer;
    return result;
}

Object* File::readline() {
    EnsureReadable();
    
    std::string line;
    if (owns_stream_) {
        if (!std::getline(file_, line)) {
            line.clear();
        }
    } else if (in_stream_ != nullptr) {
        if (!std::getline(*in_stream_, line)) {
            line.clear();
        }
    } else {
        throw TypeError("file '" + name_ + "' not readable.");
    }
    return String::FromCString(line.c_str());
}

Object* File::readlines() {
    EnsureReadable();
    
    List* lines = New<List>();
    std::string line;
    [[maybe_unused]] bool success;

    if (owns_stream_) {
        while (std::getline(file_, line)) {
            Object* line_str = String::FromCString(line.c_str());
            lines->append(line_str);
            Decref(line_str);
        }
    } else if (in_stream_ != nullptr) {
        while (std::getline(*in_stream_, line)) {
            Object* line_str = String::FromCString(line.c_str());
            lines->append(line_str);
            Decref(line_str);
        }
    } else {
        Decref(lines);
        throw TypeError("file '" + name_ + "' not readable.");
    }
    return lines;
}

Object* File::write(Object* arg, bool flush_after) {
    EnsureWritable();

    if (arg == nullptr) {
        throw TypeError("write() argument is null.");
    }

    Object* s = arg->__string__();
    if (s == nullptr || !s->is_type("String")) {
        if (s != arg) Decref(s);
        throw TypeError("__string__ did not return a String.");
    }

    if (owns_stream_) {
        file_ << static_cast<String*>(s)->get_value();
        if (flush_after) file_.flush();
    } else if (out_stream_ != nullptr) {
        *out_stream_ << static_cast<String*>(s)->get_value();
        if (flush_after) out_stream_->flush();
    } else {
        if (s != arg) Decref(s);
        throw TypeError("file '" + name_ + "' not writable.");
    }

    if (s != arg) {
        Decref(s);
    }
    return None::instance;
}

void File::flush() {
    EnsureOpen();
    if (owns_stream_) {
        file_.flush();
    } else if (out_stream_ != nullptr) {
        out_stream_->flush();
    }
}

// =============================================================
// 方法绑定（__get_attribute__）
// =============================================================

namespace {
    // 以下方法均为容器形态：接收者经 self 注入，实参已收集为 FixedList；
    // 参数个数由规范表统一校验，类型判断（需要时）在业务逻辑内完成。

    // write 方法原生实现
    Object* _file_write(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs(
            "write", { Extension::Arg::Required("data") });
        Extension::ArgResult r = spec.Bind(args, kwargs);
        File* f = static_cast<File*>(self);
        return f->write(r["data"]);
    }

    // read 方法原生实现：size 为可选参数，省略表示读至末尾；给出时须为整数
    // （Integer 精确类型，Boolean 不接受——与既有语义一致）。
    Object* _file_read(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs(
            "read", { Extension::Arg::Optional("size") });   // 省略 -> None
        Extension::ArgResult r = spec.Bind(args, kwargs);
        File* f = static_cast<File*>(self);
        Object* size = r["size"];
        if (!r.given("size") || size == nullptr || size == None::instance) {
            return f->read();
        }
        if (!IsIntegerExact(size)) {
            throw TypeError("read(): argument 'size' expects an integer, got '" +
                            size->type_name() + "'.");
        }
        int64_t n = static_cast<Integer*>(size)->get_value();
        if (n < 0) {
            throw TypeError("read() size must be non-negative.");
        }
        return f->read(static_cast<std::size_t>(n));
    }

    // readline 方法原生实现
    Object* _file_readline(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("readline", {});
        spec.Bind(args, kwargs);
        File* f = static_cast<File*>(self);
        return f->readline();
    }

    // readlines 方法原生实现
    Object* _file_readlines(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("readlines", {});
        spec.Bind(args, kwargs);
        File* f = static_cast<File*>(self);
        return f->readlines();
    }

    // close 方法原生实现
    Object* _file_close(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("close", {});
        spec.Bind(args, kwargs);
        File* f = static_cast<File*>(self);
        f->close();
        return None::instance;
    }

    // flush 方法原生实现（无参；对齐 Python file.flush()）
    Object* _file_flush(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("flush", {});
        spec.Bind(args, kwargs);
        File* f = static_cast<File*>(self);
        f->flush();
        return None::instance;
    }

    // open 方法原生实现：path 必填且须为 String；mode 可选，省略时默认 "r"。
    Object* _file_open(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs(
            "open", { Extension::Arg::Required("path"),
                      Extension::Arg::Optional("mode", String::FromCString("r")) });
        Extension::ArgResult r = spec.Bind(args, kwargs);
        Object* path = r["path"];
        if (path == nullptr || !IsString(path)) {
            throw TypeError("open(): argument 'path' expects a string, got '" +
                            (path != nullptr ? path->type_name() : std::string("None")) +
                            "'.");
        }
        Object* mode = r["mode"];
        if (mode == nullptr || !IsString(mode)) {
            throw TypeError("open(): argument 'mode' expects a string, got '" +
                            (mode != nullptr ? mode->type_name() : std::string("None")) +
                            "'.");
        }
        File* f = static_cast<File*>(self);
        f->open(AsString(path), AsString(mode));
        return None::instance;
    }

    // writelines 方法原生实现：逐元素写出（元素经 __string__ 渲染，对齐
    // Python file.writelines——它要求元素为 str，此处沿用 write 的渲染语义）。
    Object* _file_writelines(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs(
            "writelines", { Extension::Arg::Required("lines") });
        Extension::ArgResult r = spec.Bind(args, kwargs);
        File* f = static_cast<File*>(self);
        Object* lines = r["lines"];
        if (lines == nullptr) {
            throw TypeError("writelines(): 'lines' is null.");
        }
        Object* it = lines->__iterator__();
        if (it == nullptr) {
            throw TypeError("writelines(): 'lines' is not iterable.");
        }
        for (;;) {
            Object* e = nullptr;
            try {
                e = it->__next__();
            } catch (const StopIteration&) {
                break;
            }
            try {
                f->write(e);   // 返回 Borrowed None，无需释放
            } catch (...) {
                Decref(e);
                Decref(it);
                throw;
            }
            Decref(e);
        }
        Decref(it);
        return None::instance;
    }

    // seek 方法原生实现：offset 必填（Integer），whence 可选（0/1/2）。
    Object* _file_seek(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs(
            "seek", { Extension::Arg::Required("offset"),
                      Extension::Arg::Optional("whence") });
        Extension::ArgResult r = spec.Bind(args, kwargs);
        Object* oo = r["offset"];
        if (oo == nullptr || !IsIntegerExact(oo)) {
            throw TypeError("seek(): 'offset' must be an Integer.");
        }
        long long off = static_cast<Integer*>(oo)->get_value();
        int whence = 0;
        Object* wo = r["whence"];
        if (wo != nullptr && wo->type_id() != PycpTypeId::None) {
            if (!IsIntegerExact(wo)) {
                throw TypeError("seek(): 'whence' must be an Integer.");
            }
            whence = static_cast<int>(static_cast<Integer*>(wo)->get_value());
        }
        return Integer::FromLong(static_cast<File*>(self)->seek(off, whence));
    }

    Object* _file_tell(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("tell", {});
        spec.Bind(args, kwargs);
        return Integer::FromLong(static_cast<File*>(self)->tell());
    }

    Object* _file_readable(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("readable", {});
        spec.Bind(args, kwargs);
        return static_cast<File*>(self)->readable() ? Boolean::True()
                                                    : Boolean::False();
    }

    Object* _file_writable(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("writable", {});
        spec.Bind(args, kwargs);
        return static_cast<File*>(self)->writable() ? Boolean::True()
                                                    : Boolean::False();
    }

    Object* _file_seekable(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("seekable", {});
        spec.Bind(args, kwargs);
        return static_cast<File*>(self)->seekable() ? Boolean::True()
                                                    : Boolean::False();
    }

    Object* _file_is_closed(Object* self, FixedList* args, Map* kwargs) {
        static const Extension::ArgTable spec = Extension::CompileArgs("is_closed", {});
        spec.Bind(args, kwargs);
        return static_cast<File*>(self)->is_closed() ? Boolean::True()
                                                     : Boolean::False();
    }
} // anonymous namespace

// File 全部方法的方法表（公开方法 write/read/readline/readlines/close/open +
// 全部魔术方法）。公开方法指向本文件 anonymous namespace 内的实现；魔术方法
// native 为 nullptr，注册时经 GetMagicMethodFunction 统一分派。io 的类型类
// 注册（Module::set_type）与实例 __inspect__ 均以此表为唯一权威来源。
const std::vector<MethodEntry>& File_method_table() {
	static const std::vector<MethodEntry> table = {
		{"write",                _file_write},
		{"read",                 _file_read},
		{"readline",             _file_readline},
		{"readlines",            _file_readlines},
		{"close",                _file_close},
		{"flush",                _file_flush},
		{"open",                 _file_open},
		{"writelines",           _file_writelines},
		{"seek",                 _file_seek},
		{"tell",                 _file_tell},
		{"readable",             _file_readable},
		{"writable",             _file_writable},
		{"seekable",             _file_seekable},
		{"is_closed",            _file_is_closed},
		{"__string__",           nullptr},
		{"__raw_string__",       nullptr},
		{"__inspect__",          nullptr},
		{"__get_attribute__",    nullptr},
		{"__set_attribute__",    nullptr},
		{"__delete_attribute__", nullptr},
		{"__map__",              nullptr},
		{"__boolean__",          nullptr},
	};
	return table;
}

// 通用方法分派入口（见 Object::__get_attribute__）。
MethodTableFn File::method_table() const {
	return File_method_table;
}

// —— 供方法表实现使用的 io 语义 ——

bool File::readable() const {
	return mode_ == FileMode::READ || mode_ == FileMode::READ_WRITE ||
	       mode_ == FileMode::WRITE_READ || mode_ == FileMode::APPEND_READ;
}

bool File::writable() const {
	return mode_ == FileMode::WRITE || mode_ == FileMode::APPEND ||
	       mode_ == FileMode::READ_WRITE || mode_ == FileMode::WRITE_READ ||
	       mode_ == FileMode::APPEND_READ;
}

bool File::seekable() const {
	return owns_stream_ && is_open_;
}

long long File::seek(long long offset, int whence) {
	EnsureOpen();
	if (!owns_stream_) {
		throw ValueError("file '" + name_ + "' is not seekable.");
	}
	std::ios_base::seekdir dir = std::ios::beg;
	if (whence == 1) {
		dir = std::ios::cur;
	} else if (whence == 2) {
		dir = std::ios::end;
	} else if (whence != 0) {
		throw ValueError("seek(): invalid whence value.");
	}
	file_.clear();
	file_.seekg(static_cast<std::streamoff>(offset), dir);
	file_.seekp(static_cast<std::streamoff>(offset), dir);
	auto pos = file_.tellg();
	if (pos < 0) pos = file_.tellp();
	return static_cast<long long>(pos);
}

long long File::tell() {
	EnsureOpen();
	if (!owns_stream_) {
		throw ValueError("file '" + name_ + "' is not seekable.");
	}
	auto pos = file_.tellg();
	if (pos < 0) pos = file_.tellp();
	return static_cast<long long>(pos);
}

// File(path [, mode]) 的内建构造器（io / filesystem 共用，对齐 Python open）。
Object* FileConstructor(Object* /*self*/, FixedList* args, Map* kwargs) {
	static const Extension::ArgTable spec = Extension::CompileArgs(
		"File", { Extension::Arg::Required("path"),
		          Extension::Arg::Optional("mode", String::FromCString("r")) });
	Extension::ArgResult r = spec.Bind(args, kwargs);
	Object* pv = r["path"];
	Object* mv = r["mode"];
	if (pv == nullptr || !IsString(pv)) {
		throw TypeError("File: argument 'path' expects a string.");
	}
	if (mv == nullptr || !IsString(mv)) {
		throw TypeError("File: argument 'mode' expects a string.");
	}
	return New<File>(AsString(pv), AsString(mv));
}

// 运行时唯一的 File 类型类：io.File 与 filesystem.File 均绑定此对象。
Class* FileTypeClass() {
	static Class* cls = nullptr;
	if (cls == nullptr) {
		// 用一个内部模块承载类型类（命名空间与类型类注册表各持一份引用），
		// 并 root 二者，使类型类在进程生命周期内常驻。
		static Module* owner = Module::New("<runtime:file>");
		GC_AddRoot(owner);
		cls = owner->set_type("File", FileConstructor, /*initialize=*/nullptr,
		                      File_method_table);
		GC_AddRoot(cls);
	}
	return cls;
}

Object* File::__get_attribute__(const std::string& attr_name) {
    // 0) 只读 __class__：返回 File 类型类对象（Borrowed，注册表持有）。
    if (attr_name == "__class__") {
        return get_type_class();
    }
    // 1) 成员字典
    auto itm = members_.find(attr_name);
    if (itm != members_.end() && itm->second != nullptr) {
        Incref(itm->second);
        return itm->second;
    }
    
    // 2) 属性
    if (attr_name == "closed") {
        return closed() ? Integer::instances[1] : Integer::instances[0];
    }
    if (attr_name == "name") {
        return String::FromCString(name_.c_str());
    }
    if (attr_name == "mode") {
        std::string mode_str;
        switch (mode_) {
            case FileMode::READ: mode_str = "r"; break;
            case FileMode::WRITE: mode_str = "w"; break;
            case FileMode::APPEND: mode_str = "a"; break;
            case FileMode::READ_WRITE: mode_str = "r+"; break;
            case FileMode::WRITE_READ: mode_str = "w+"; break;
            case FileMode::APPEND_READ: mode_str = "a+"; break;
						default: mode_str = "?"; break;
				}
        if (is_binary_) mode_str += "b";
        return String::FromCString(mode_str.c_str());
    }
    
    // 3) 方法
    // 懒注册同样经框架的 thunk（与 File_method_table 共用同一绑定）。
    if (attr_name == "write") {
        if (write_fn_ == nullptr) {
            write_fn_ = New<Function>("write", _file_write);
        }
        Incref(write_fn_);
        return write_fn_;
    }
    if (attr_name == "read") {
        if (read_fn_ == nullptr) {
            read_fn_ = New<Function>("read", _file_read);
        }
        Incref(read_fn_);
        return read_fn_;
    }
    if (attr_name == "readline") {
        if (readline_fn_ == nullptr) {
            readline_fn_ = New<Function>("readline", _file_readline);
        }
        Incref(readline_fn_);
        return readline_fn_;
    }
    if (attr_name == "readlines") {
        if (readlines_fn_ == nullptr) {
            readlines_fn_ = New<Function>("readlines", _file_readlines);
        }
        Incref(readlines_fn_);
        return readlines_fn_;
    }
    if (attr_name == "close") {
        if (close_fn_ == nullptr) {
            close_fn_ = New<Function>("close", _file_close);
        }
        Incref(close_fn_);
        return close_fn_;
    }
    if (attr_name == "open") {
        Function* fn = New<Function>("open", _file_open);
        Incref(fn);
        return fn;
    }
    if (attr_name == "flush") {
        // 照 open 的模式每次新建（flush 调用频率低，无需懒缓存成员）。
        Function* fn = New<Function>("flush", _file_flush);
        Incref(fn);
        return fn;
    }
    
    // 3.1) 方法表分派：本类型的公开方法（writelines/seek/tell/readable/...）
    if (Object* method = GetTableMethodFunction(File_method_table(), attr_name)) {
        return method;
    }
    // 4) 魔术方法
    if (Object* magic = GetMagicMethodFunction(attr_name)) {
        return magic;
    }
    
    throw AttributeError("file \"" + name_ + "\" has no attribute '" + attr_name + "'");
}

Object* File::__inspect__() {
	// 只读属性（closed/name/mode）+ 方法表全部方法名 + 通用属性名（__class__）。
	std::vector<Object*> names;
	const char* attrs[] = {"closed", "name", "mode"};
	for (const char* a : attrs) CollectUniqueName(names, a);
	for (const MethodEntry& e : File_method_table()) CollectUniqueName(names, e.name);
	for (const std::string& n : CommonInspectNames()) CollectUniqueName(names, n);
	return FixedList::New(names);
}

Object* File::__string__() {
    std::string status = is_open_ ? "open" : "closed";
    std::string mode_str;
    switch (mode_) {
        case FileMode::READ: mode_str = "r"; break;
        case FileMode::WRITE: mode_str = "w"; break;
        case FileMode::APPEND: mode_str = "a"; break;
        case FileMode::READ_WRITE: mode_str = "r+"; break;
        case FileMode::WRITE_READ: mode_str = "w+"; break;
        case FileMode::APPEND_READ: mode_str = "a+"; break;
				default: mode_str = "?"; break;
    }
    if (is_binary_) mode_str += "b";
    return String::FromCString(("<File \"" + name_ + "\" mode=\"" + mode_str + "\" " + status + ">").c_str());
}

Object* File::__raw_string__() {
    // repr 与 str 同形：<File "name" mode="r" open>。
    return __string__();
}

void File::foreach_ref(const std::function<void(Object*)>& visit) {
    if (write_fn_) visit(write_fn_);
    if (read_fn_) visit(read_fn_);
    if (readline_fn_) visit(readline_fn_);
    if (close_fn_) visit(close_fn_);
    if (readlines_fn_) visit(readlines_fn_);
}

} // namespace Pycp