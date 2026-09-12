#include "PycpExtension.hpp"

#include <string>
#include <utility>
#include <vector>

namespace Pycp {
namespace Extension {

namespace {

// 错误消息的函数名前缀：无名字时退化为 "()"。
std::string fn_prefix(const std::string& name) {
	return name.empty() ? std::string("()") : (name + "()");
}

// 合法参数名：[A-Za-z_][A-Za-z0-9_]*
bool is_valid_param_name(const char* n) {
	if (n == nullptr || n[0] == '\0') return false;
	const unsigned char c0 = static_cast<unsigned char>(n[0]);
	if (!((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z') || c0 == '_')) {
		return false;
	}
	for (std::size_t i = 1; n[i] != '\0'; ++i) {
		const unsigned char c = static_cast<unsigned char>(n[i]);
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		      (c >= '0' && c <= '9') || c == '_')) {
			return false;
		}
	}
	return true;
}

// Owned 引用守卫：析构时释放（异常路径同样生效）。
struct OwnedRef {
	Object* obj = nullptr;
	explicit OwnedRef(Object* o) : obj(o) {}
	~OwnedRef() { if (obj != nullptr) Decref(obj); }
	OwnedRef(const OwnedRef&) = delete;
	OwnedRef& operator=(const OwnedRef&) = delete;
};

} // anonymous namespace

// =============================================================
// 常驻空容器
// =============================================================
FixedList* EmptyArgs() {
	static FixedList* empty = [] {
		FixedList* f = FixedList::New(std::vector<Object*>{});
		Incref(f);
		GC_AddRoot(f);
		return f;
	}();
	return empty;
}

Map* EmptyKwargs() {
	static Map* empty = [] {
		Map* m = Map::New();
		Incref(m);
		GC_AddRoot(m);
		return m;
	}();
	return empty;
}

FixedList* MakeArgs(Object* const* items, std::size_t n) {
	std::vector<Object*> v;
	v.reserve(n);
	for (std::size_t i = 0; i < n; ++i) {
		if (items[i] != nullptr) {
			Incref(items[i]);   // FixedList 构造函数接管引用（不 Incref）
			v.push_back(items[i]);
		}
	}
	return FixedList::New(v);
}

Object* Invoke(Function* fn, Object* self,
               std::initializer_list<Object*> args, Map* kwargs) {
	if (fn == nullptr) {
		throw TypeError("cannot invoke a null function.");
	}
	if (kwargs == nullptr) kwargs = EmptyKwargs();
	if (args.size() == 0) {
		return fn->invoke(self, EmptyArgs(), kwargs);
	}
	FixedList* a = MakeArgs(args);
	Object* r = fn->invoke(self, a, kwargs);
	Decref(a);
	return r;
}

// =============================================================
// ArgResult
// =============================================================
ArgResult::~ArgResult() {
	for (Object* o : owned_) {
		if (o != nullptr) Decref(o);
	}
}

ArgResult::ArgResult(ArgResult&& other) noexcept
	: owner_(other.owner_),
	  values_(std::move(other.values_)),
	  given_(std::move(other.given_)),
	  owned_(std::move(other.owned_)) {
	other.owner_ = nullptr;
}

ArgResult& ArgResult::operator=(ArgResult&& other) noexcept {
	if (this != &other) {
		for (Object* o : owned_) {
			if (o != nullptr) Decref(o);
		}
		owner_  = other.owner_;
		values_ = std::move(other.values_);
		given_  = std::move(other.given_);
		owned_  = std::move(other.owned_);
		other.owner_ = nullptr;
	}
	return *this;
}

Object* ArgResult::operator[](const char* name) const {
	if (owner_ == nullptr) {
		throw KeyError("argument result is not bound to any spec.");
	}
	if (name == nullptr) {
		throw KeyError("argument name is null.");
	}
	auto it = owner_->index_.find(name);
	if (it == owner_->index_.end()) {
		throw KeyError(fn_prefix(owner_->name_) + " has no argument named '" +
		               std::string(name) + "'.");
	}
	return values_[it->second];
}

Object* ArgResult::operator[](std::size_t index) const {
	if (index >= values_.size()) {
		throw IndexError("argument index out of range.");
	}
	return values_[index];
}

bool ArgResult::given(const char* name) const {
	if (owner_ == nullptr || name == nullptr) return false;
	auto it = owner_->index_.find(name);
	if (it == owner_->index_.end()) return false;
	return given_[it->second] != 0;
}

// =============================================================
// CompileArgs：校验 + 预计算（一次性）
// =============================================================
ArgTable CompileArgs(const char* fn_name, std::initializer_list<ArgSpec> specs) {
	ArgTable t;
	t.name_ = (fn_name != nullptr) ? fn_name : "";

	bool seen_optional = false;
	bool seen_rest     = false;
	bool seen_kw       = false;
	std::size_t required = 0;
	std::size_t optional = 0;

	for (const ArgSpec& s : specs) {
		// 1) 名字合法性
		if (!is_valid_param_name(s.name)) {
			throw ValueError(std::string("invalid parameter name '") +
			                 (s.name != nullptr ? s.name : "<null>") + "'.");
		}
		const std::size_t slot = t.specs_.size();

		// 2) 顺序与唯一性
		switch (s.kind) {
		case ArgKind::Required:
			if (seen_rest) {
				throw ValueError("required parameter '" + std::string(s.name) +
				                 "' cannot follow *rest parameter.");
			}
			if (seen_optional) {
				throw ValueError("required parameter '" + std::string(s.name) +
				                 "' cannot follow an optional parameter.");
			}
			++required;
			break;
		case ArgKind::Optional:
			if (seen_rest) {
				throw ValueError("optional parameter '" + std::string(s.name) +
				                 "' cannot follow *rest parameter.");
			}
			seen_optional = true;
			++optional;
			break;
		case ArgKind::Rest:
			if (seen_rest) {
				throw ValueError("duplicate *rest parameter ('" +
				                 std::string(s.name) + "').");
			}
			if (seen_kw) {
				throw ValueError("*rest parameter must precede **keywords parameter.");
			}
			seen_rest     = true;
			t.rest_index_ = slot;
			break;
		case ArgKind::Keyword:
			if (seen_kw) {
				throw ValueError("duplicate **keywords parameter ('" +
				                 std::string(s.name) + "').");
			}
			seen_kw     = true;
			t.kw_index_ = slot;
			break;
		}

		// 3) 重名检查
		if (t.index_.find(s.name) != t.index_.end()) {
			throw ValueError("duplicate parameter name '" + std::string(s.name) + "'.");
		}

		ArgSpec stored = s;
		if (stored.kind == ArgKind::Optional && stored.default_value == nullptr) {
			stored.default_value = None::instance;
		}
		t.specs_.push_back(stored);
		t.names_.push_back(s.name);
		t.index_.emplace(s.name, slot);
	}

	t.min_args_ = required;
	t.max_args_ = (t.rest_index_ != ArgTable::kNone) ? kUnbounded
	                                                : (required + optional);

	// 4) Optional 默认值常驻（数量极少，与既有常驻缓存同策略）
	for (ArgSpec& s : t.specs_) {
		if (s.kind == ArgKind::Optional && s.default_value != nullptr) {
			Incref(s.default_value);
			GC_AddRoot(s.default_value);
		}
	}
	return t;
}

// =============================================================
// ArgTable::Bind
// =============================================================
ArgResult ArgTable::Bind(FixedList* args, Map* kwargs) const {
	const std::size_t n = (args != nullptr) ? args->size() : 0;

	// 1) 位置参数上界（无 *rest 时生效；下界由逐槽必填检查给出更精确的报错）
	if (max_args_ != kUnbounded && n > max_args_) {
		const std::string head = fn_prefix(name_) + " takes ";
		if (max_args_ == 0) {
			throw TypeError(head + "no positional arguments (" +
			                std::to_string(n) + " given).");
		}
		throw TypeError(head + "at most " + std::to_string(max_args_) +
		                " positional arguments (" + std::to_string(n) + " given).");
	}

	ArgResult r;
	r.owner_ = this;
	r.values_.assign(specs_.size(), nullptr);
	r.given_.assign(specs_.size(), 0);

	// 2) 位置填充：第 i 个实参 -> 第 i 个槽（遇 *rest / **kw 停止）
	std::size_t filled = 0;
	for (; filled < n && filled < specs_.size(); ++filled) {
		const ArgKind kind = specs_[filled].kind;
		if (kind == ArgKind::Rest || kind == ArgKind::Keyword) break;
		r.values_[filled] = args->at(filled);   // Borrowed
		r.given_[filled]  = 1;
	}

	// 3) *rest：剩余实参收集为 FixedList（零个 -> 空元组）
	if (rest_index_ != kNone) {
		std::vector<Object*> items;
		if (n > filled) items.reserve(n - filled);
		for (std::size_t k = filled; k < n; ++k) {
			Object* item = args->at(k);
			if (item != nullptr) {
				Incref(item);   // FixedList 接管引用（不 Incref）
				items.push_back(item);
			}
		}
		FixedList* rest = FixedList::New(items);
		r.owned_.push_back(rest);
		r.values_[rest_index_] = rest;
	}

	// 4) 关键字参数：按名回填位置槽，未匹配项进 **kw（与 *rest 互不混淆）
	if (kwargs != nullptr && kwargs->size() > 0) {
		OwnedRef keys(kwargs->keys());
		FixedList* key_list = static_cast<FixedList*>(keys.obj);
		for (std::size_t k = 0; k < key_list->size(); ++k) {
			Object* key = key_list->at(k);
			if (key == nullptr || !IsString(key)) continue;   // 非字符串键不参与匹配
			const std::string nm = AsString(key);

			auto it = index_.find(nm);
			const bool named_slot =
				(it != index_.end() &&
				 specs_[it->second].kind != ArgKind::Rest &&
				 specs_[it->second].kind != ArgKind::Keyword);
			if (named_slot) {
				const std::size_t slot = it->second;
				if (r.values_[slot] != nullptr) {
					throw TypeError(fn_prefix(name_) +
					                " got multiple values for argument '" + nm + "'.");
				}
				r.values_[slot] = kwargs->__get_item__(key);   // Borrowed
				r.given_[slot]  = 1;
			} else if (kw_index_ == kNone) {
				throw TypeError(fn_prefix(name_) +
				                " got an unexpected keyword argument '" + nm + "'.");
			} else {
				if (r.values_[kw_index_] == nullptr) {
					Map* kw = Map::New();
					r.owned_.push_back(kw);
					r.values_[kw_index_] = kw;
				}
				static_cast<Map*>(r.values_[kw_index_])
					->__set_item__(key, kwargs->__get_item__(key));
			}
		}
	}
	if (kw_index_ != kNone && r.values_[kw_index_] == nullptr) {
		Map* kw = Map::New();
		r.owned_.push_back(kw);
		r.values_[kw_index_] = kw;
	}

	// 5) Optional 默认值 / Required 缺失检查
	for (std::size_t k = 0; k < specs_.size(); ++k) {
		const ArgKind kind = specs_[k].kind;
		if (kind == ArgKind::Optional) {
			if (r.values_[k] == nullptr) r.values_[k] = specs_[k].default_value;
		} else if (kind == ArgKind::Required) {
			if (r.values_[k] == nullptr) {
				throw TypeError(fn_prefix(name_) +
				                " missing required argument: '" + names_[k] + "'.");
			}
		}
	}
	return r;
}

} // namespace Extension
} // namespace Pycp
