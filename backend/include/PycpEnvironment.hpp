#ifndef PYCP_ENVIRONMENT_HPP
#define PYCP_ENVIRONMENT_HPP

// =============================================================
// Pycp 执行环境（作用域）抽象 —— ABI 层统一接口
//
// Environment 承载一次函数调用 / 模块执行的变量作用域：
//   - locals       : 局部变量槽（函数调用时分配，大小 = nlocals）
//   - local_names  : 局部变量名 -> slots 索引（编译期确定，与 locals 对齐）
//   - captured     : 捕获的外部环境（闭包，可能成链）
//   - globals      : 全局变量（共享可变 map；函数与模块共用同一实例）
//
// 变量查找顺序统一为：局部 -> captured 链 -> 全局。
// 该结构由 VM 与 AOT 生成的代码共同使用，保证两者作用域语义一致。
// =============================================================

#include "PycpObject.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Pycp {
namespace BC {

struct Environment {
	// 局部变量槽（函数调用时分配，大小 = nlocals）
	std::vector<Object*> locals;
	// 局部变量名 -> slots 索引（编译期确定，与 locals 对齐）
	std::vector<std::string> local_names;
	// 捕获的外部环境（闭包，可能成链）
	std::shared_ptr<Environment> captured;
	// 全局变量（共享可变 map；函数与模块共用同一实例）
	std::unordered_map<std::string, Object*>* globals = nullptr;
	// 被闭包捕获的局部变量名集合：帧退出（RETURN/HALT）时这些槽位【不】释放，
	// 改由本环境析构统一释放，避免闭包持有的外层变量成为悬垂引用。
	// 由 MAKE_FUNCTION 创建闭包时经 Environment_KeepNames* 登记。
	std::unordered_set<std::string> keep_names;

	// 析构：释放仍持有的局部槽（含因被闭包捕获而保留的槽位）。
	// 未登记的槽位已在帧退出时由 Environment_ReleaseFrame 释放并置空，
	// 此处不会重复释放。
	~Environment() {
		for (Object* v : locals) {
			if (v != nullptr) Decref(v);
		}
		locals.clear();
	}

	// 在局部作用域查找变量，返回槽索引（-1 未找到）
	long find_local(const std::string& name) const {
		for (std::size_t i = 0; i < local_names.size(); ++i) {
			if (local_names[i] == name) return static_cast<long>(i);
		}
		return -1;
	}
};

} // namespace BC
} // namespace Pycp

#endif // PYCP_ENVIRONMENT_HPP
