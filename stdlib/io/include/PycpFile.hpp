#ifndef PYCP_FILE_HPP
#define PYCP_FILE_HPP

#include "PycpObject.hpp"
#include "PycpFunction.hpp"
#include "PycpMethodTable.hpp"   // MethodEntry / MethodTableFn

#include <fstream>
#include <string>
#include <unordered_map>

namespace Pycp {

// 文件打开模式（参照 Python）
enum class FileMode {
    READ,           // "r"  只读（默认）
    WRITE,          // "w"  写入（覆盖）
    APPEND,         // "a"  追加
    READ_WRITE,     // "r+" 读写
    WRITE_READ,     // "w+" 读写（覆盖）
    APPEND_READ,    // "a+" 读写（追加）
    BINARY          // 二进制模式标志（与上述组合）
};

class File : public Object {
private:
    std::string name_;          // 显示名称（如 "<stdout>"）
    std::string path_;          // 实际文件路径（如 "/dev/stdout"）
    FileMode mode_;             // 打开模式
    bool is_open_;              // 是否已打开
    bool is_binary_;            // 是否二进制模式
    bool owns_stream_;          // 是否拥有流（false 表示 stdin/stdout/stderr 等）
    std::fstream file_;         // 文件流（仅当 owns_stream_ == true 时使用）
    std::istream* in_stream_;   // 外部输入流指针（仅当 owns_stream_ == false）
    std::ostream* out_stream_;  // 外部输出流指针（仅当 owns_stream_ == false）
    Function* write_fn_;        // write 方法对象（懒创建）
    Function* read_fn_;         // read 方法对象（懒创建）
    Function* readline_fn_;     // readline 方法对象（懒创建）
    Function* close_fn_;        // close 方法对象（懒创建）
    Function* readlines_fn_;    // readlines 方法对象（懒创建）

    // 将模式字符串转换为 FileMode 枚举
    static FileMode ParseMode(const std::string& mode_str);
    // 将 FileMode 转换为 std::ios::openmode
    std::ios::openmode ToOpenMode(FileMode mode) const;
    // 检查文件是否打开，若未打开则抛出异常
    void EnsureOpen() const;
    // 检查是否可读
    void EnsureReadable() const;
    // 检查是否可写
    void EnsureWritable() const;

public:
    // 构造函数：路径 + 模式（默认只读），使用路径作为显示名称
    File(const std::string& path, const std::string& mode = "r");
    // 构造函数：路径 + 模式 + 自定义显示名称
    File(const std::string& path, const std::string& mode, const std::string& name);
    // 从已有流构造（用于 stdin/stdout/stderr），使用指定的显示名称
    File(const std::string& name, std::istream* in, std::ostream* out);
    ~File() override;

    // 静态工厂：从已有流构造（用于 stdin/stdout/stderr）
    static File* FromStream(const std::string& name, void* in, void* out);

    // ---------- 公开方法 ----------
    
    // 打开文件（如果已打开则先关闭）
    void open(const std::string& path, const std::string& mode = "r");
    // 关闭文件
    void close();
    // 判断文件是否已关闭
    bool closed() const { return !is_open_; }

    // 读取全部内容（返回 String）
    Object* read();
    // 读取指定字节数（返回 String）
    Object* read(std::size_t size);
    // 读取一行（返回 String）
    Object* readline();
    // 读取所有行（返回 List）
    Object* readlines();
    // 写入内容（返回 None）
    Object* write(Object* arg);

    // ---------- Object 虚方法重写 ----------

    Object* __get_attribute__(const std::string& name) override;
    Object* __string__() override;
    Object* __inspect__() override;
    void foreach_ref(const std::function<void(Object*)>& visit) override;
};

// File 全部方法（公开方法 write/read/readline/readlines/close/open +
// 全部魔术方法）的唯一权威清单。io 的类型类注册（RegisterTypeObject）
// 与实例 __inspect__ 均从它派生。
const std::vector<MethodEntry>& File_method_table();

// 类型萃取特化：File（可变）。
template <> struct TypeTraits<File> {
	static constexpr PycpTypeId   id            = PycpTypeId::File;
	static constexpr PycpTypeFlag flags         = PycpTypeFlag::Mutable;
	static constexpr PycpTypeFlag subclass_flag = PycpTypeFlag::None;
};

} // namespace Pycp

#endif // PYCP_FILE_HPP