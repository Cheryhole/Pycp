#include "PycpFile.hpp"
#include "PycpException.hpp"
#include "PycpGC.hpp"
#include "PycpString.hpp"
#include "PycpNone.hpp"
#include "PycpInteger.hpp"
#include "PycpList.hpp"
#include "PycpABI.hpp"
#include "PycpMagic.hpp"

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
    bool success;
    
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

Object* File::write(Object* arg) {
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
        file_.flush();
    } else if (out_stream_ != nullptr) {
        *out_stream_ << static_cast<String*>(s)->get_value();
        out_stream_->flush();
    } else {
        if (s != arg) Decref(s);
        throw TypeError("file '" + name_ + "' not writable.");
    }
    
    if (s != arg) {
        Decref(s);
    }
    return None::instance;
}

Object* File::writelines(Object* arg) {
    EnsureWritable();
    
    List* lines = dynamic_cast<List*>(arg);
    if (lines == nullptr) {
        throw TypeError("writelines() expects a List.");
    }
    
    for (std::size_t i = 0; i < lines->size(); ++i) {
        Object* line = lines->at(i);
        write(line);
    }
    return None::instance;
}

// =============================================================
// 方法绑定（__get_attribute__）
// =============================================================

namespace {
    // write 方法原生实现
    Object* _file_write(Object* self [[maybe_unused]], Object** argv, std::size_t argc) {
        File* f = static_cast<File*>(argv[0]);
        if (argc != 2) {
            throw TypeError("write() expects exactly 1 argument.");
        }
        return f->write(argv[1]);
    }

    // read 方法原生实现
    Object* _file_read(Object* self [[maybe_unused]], Object** argv, std::size_t argc) {
        File* f = static_cast<File*>(argv[0]);
        if (argc == 1) {
            return f->read();
        } else if (argc == 2) {
            Object* size_arg = argv[1];
            if (!size_arg->is_type("Integer")) {
                throw TypeError("read() argument must be integer.");
            }
            std::size_t size = static_cast<Integer*>(size_arg)->get_value();
            return f->read(size);
        }
        throw TypeError("read() expects 0 or 1 argument.");
    }

    // readline 方法原生实现
    Object* _file_readline(Object* self [[maybe_unused]], Object** argv, std::size_t argc) {
        File* f = static_cast<File*>(argv[0]);
        if (argc != 1) {
            throw TypeError("readline() expects no arguments.");
        }
        return f->readline();
    }

    // readlines 方法原生实现
    Object* _file_readlines(Object* self [[maybe_unused]], Object** argv, std::size_t argc) {
        File* f = static_cast<File*>(argv[0]);
        if (argc != 1) {
            throw TypeError("readlines() expects no arguments.");
        }
        return f->readlines();
    }

    // close 方法原生实现
    Object* _file_close(Object* self [[maybe_unused]], Object** argv, std::size_t argc) {
        File* f = static_cast<File*>(argv[0]);
        if (argc != 1) {
            throw TypeError("close() expects no arguments.");
        }
        f->close();
        return None::instance;
    }

    // open 方法原生实现
    Object* _file_open(Object* self [[maybe_unused]], Object** argv, std::size_t argc) {
        File* f = static_cast<File*>(argv[0]);
        if (argc < 2 || argc > 3) {
            throw TypeError("open() expects 1 or 2 arguments (path [, mode]).");
        }
        Object* path_obj = argv[1];
        if (!path_obj->is_type("String")) {
            throw TypeError("open() path must be string.");
        }
        std::string path = static_cast<String*>(path_obj)->get_value();
        std::string mode = "r";
        if (argc == 3) {
            Object* mode_obj = argv[2];
            if (!mode_obj->is_type("String")) {
                throw TypeError("open() mode must be string.");
            }
            mode = static_cast<String*>(mode_obj)->get_value();
        }
        f->open(path, mode);
        return None::instance;
    }
} // anonymous namespace

// ---- 实例方法函数访问器：暴露给 io.cpp 注册 File 类型类方法 ----
PycpNativeFunction File_write_fn()   { return _file_write; }
PycpNativeFunction File_read_fn()    { return _file_read; }
PycpNativeFunction File_readline_fn(){ return _file_readline; }
PycpNativeFunction File_readlines_fn(){ return _file_readlines; }
PycpNativeFunction File_close_fn()   { return _file_close; }
PycpNativeFunction File_open_fn()    { return _file_open; }

Object* File::__get_attribute__(const std::string& attr_name) {
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
        }
        if (is_binary_) mode_str += "b";
        return String::FromCString(mode_str.c_str());
    }
    
    // 3) 方法
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
    
    // 4) 魔术方法
    if (Object* magic = GetMagicMethodFunction(attr_name)) {
        return magic;
    }
    
    throw AttributeError("file \"" + name_ + "\" has no attribute '" + attr_name + "'");
}

Object* File::__members__() {
	// File 对象的成员名：只读属性 + 公开方法（含魔术方法）。
	std::vector<std::string> names = {
		"closed", "name", "mode",
		"write", "read", "readline", "readlines", "close", "open",
		"__string__", "__get_attribute__", "__members__"
	};
	return BuildNameList(names);
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
    }
    if (is_binary_) mode_str += "b";
    return String::FromCString(("<File \"" + name_ + "\" mode=\"" + mode_str + "\" " + status + ">").c_str());
}

void File::foreach_ref(const std::function<void(Object*)>& visit) {
    if (write_fn_) visit(write_fn_);
    if (read_fn_) visit(read_fn_);
    if (readline_fn_) visit(readline_fn_);
    if (close_fn_) visit(close_fn_);
    if (readlines_fn_) visit(readlines_fn_);
}

} // namespace Pycp