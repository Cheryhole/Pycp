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

constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);

// 位置段长度 = 第一个 *args 或首个显式关键字-only 参数的槽位（都没有时即形参总数）。
std::size_t positional_count(const ArgSpec* specs, std::size_t nspec) {
	for (std::size_t i = 0; i < nspec; ++i) {
		if (specs[i].kind == ArgKind::Rest || specs[i].kind == ArgKind::Keyword) {
			return i;
		}
	}
	return nspec;
}

// 按名字查槽位（形参数量极小，线性扫描比建哈希表更快，且无需额外分配）。
std::size_t find_slot(const ArgSpec* specs, std::size_t nspec,
                      const std::string& name) {
	for (std::size_t i = 0; i < nspec; ++i) {
		if (specs[i].name != nullptr && name == specs[i].name) return i;
	}
	return kNoSlot;
}

// 原生规范表的错误文案（沿用既有风格；新增关键字-only 缺参一条）。
std::string format_bind_error(const std::string& name, const BindError& err) {
	const std::string head = fn_prefix(name);
	const std::string first = err.names.empty() ? std::string() : err.names[0];
	switch (err.code) {
	case BindErrorCode::TooManyPositional:
		if (err.expected == 0) {
			return head + " takes no positional arguments (" +
			       std::to_string(err.got) + " given).";
		}
		return head + " takes at most " + std::to_string(err.expected) +
		       " positional arguments (" + std::to_string(err.got) + " given).";
	case BindErrorCode::MissingPositional:
		return head + " missing required argument: '" + first + "'.";
	case BindErrorCode::MissingKeywordOnly:
		return head + " missing required keyword-only argument: '" + first + "'.";
	case BindErrorCode::MultipleValues:
		return head + " got multiple values for argument '" + first + "'.";
	case BindErrorCode::UnexpectedKeyword:
		return head + " got an unexpected keyword argument '" + first + "'.";
	case BindErrorCode::None:
		break;
	}
	return head + " invalid arguments.";
}

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
// BoundArgs（内核产出：声明顺序槽位 + 新建容器）
// =============================================================
BoundArgs::~BoundArgs() {
	ReleaseOwned();
}

void BoundArgs::ReleaseOwned() {
	for (Object* o : owned) {
		if (o != nullptr) Decref(o);
	}
	owned.clear();
}

BoundArgs::BoundArgs(BoundArgs&& other) noexcept
	: slots(std::move(other.slots)),
	  given(std::move(other.given)),
	  owned(std::move(other.owned)) {
	other.owned.clear();   // 已转移，避免 other 析构时重复 Decref
}

BoundArgs& BoundArgs::operator=(BoundArgs&& other) noexcept {
	if (this != &other) {
		ReleaseOwned();
		slots = std::move(other.slots);
		given = std::move(other.given);
		owned = std::move(other.owned);
		other.owned.clear();
	}
	return *this;
}

// =============================================================
// BindParams：语言层字节码函数 与 C++ 规范表 共用的唯一绑定算法
// =============================================================
bool BindParams(const ArgSpec* specs, std::size_t nspec,
                Object* const* pos, std::size_t npos,
                Map* kwargs, BoundArgs& out, BindError& err) {
	err = BindError{};
	out.ReleaseOwned();
	out.slots.assign(nspec, nullptr);
	out.given.assign(nspec, 0);

	const std::size_t cap = positional_count(specs, nspec);
	std::size_t rest_index = kNoSlot;
	std::size_t kw_index   = kNoSlot;
	for (std::size_t i = 0; i < nspec; ++i) {
		if (specs[i].kind == ArgKind::Rest) rest_index = i;
		else if (specs[i].kind == ArgKind::RestKeywords) kw_index = i;
	}

	// 1) 位置实参依序填位置段。
	const std::size_t fill = (npos < cap) ? npos : cap;
	for (std::size_t i = 0; i < fill; ++i) {
		out.slots[i] = pos[i];
		out.given[i] = 1;
	}
	// 位置实参过多且无 *args 接收 -> 报错（优先于关键字相关错误，与旧行为一致）。
	if (npos > cap && rest_index == kNoSlot) {
		err.code     = BindErrorCode::TooManyPositional;
		err.expected = cap;
		err.got      = npos;
		return false;
	}

	// 2) *args：剩余位置实参收集为 FixedList（零个 -> 空元组）。
	if (rest_index != kNoSlot) {
		std::vector<Object*> items;
		if (npos > cap) items.reserve(npos - cap);
		for (std::size_t k = cap; k < npos; ++k) {
			if (pos[k] != nullptr) {
				Incref(pos[k]);   // FixedList 构造函数接管引用（不 Incref）
				items.push_back(pos[k]);
			}
		}
		FixedList* rest = FixedList::New(items);
		out.owned.push_back(rest);
		out.slots[rest_index] = rest;
	}

	// 3) 关键字实参：按名回填位置段 / 关键字-only 段，未匹配项进 **kwargs。
	if (kwargs != nullptr && kwargs->size() > 0) {
		OwnedRef keys(kwargs->keys());
		FixedList* key_list = static_cast<FixedList*>(keys.obj);
		for (std::size_t k = 0; k < key_list->size(); ++k) {
			Object* key = key_list->at(k);
			if (key == nullptr || !IsString(key)) continue;   // 非字符串键不参与匹配
			const std::string nm = AsString(key);

			const std::size_t slot = find_slot(specs, nspec, nm);
			const bool named_slot =
				slot != kNoSlot && (specs[slot].kind == ArgKind::Required ||
				                    specs[slot].kind == ArgKind::Optional ||
				                    specs[slot].kind == ArgKind::Keyword);
			if (named_slot) {
				if (out.slots[slot] != nullptr) {
					err.code = BindErrorCode::MultipleValues;
					err.names.push_back(nm);
					return false;
				}
				out.slots[slot] = kwargs->__get_item__(key);   // Borrowed
				out.given[slot] = 1;
			} else if (kw_index == kNoSlot) {
				err.code = BindErrorCode::UnexpectedKeyword;
				err.names.push_back(nm);
				return false;
			} else {
				if (out.slots[kw_index] == nullptr) {
					Map* m = Map::New();
					out.owned.push_back(m);
					out.slots[kw_index] = m;
				}
				static_cast<Map*>(out.slots[kw_index])
					->__set_item__(key, kwargs->__get_item__(key));
			}
		}
	}
	if (kw_index != kNoSlot && out.slots[kw_index] == nullptr) {
		Map* m = Map::New();
		out.owned.push_back(m);
		out.slots[kw_index] = m;
	}

	// 4) 默认值填充 + 缺参检查（位置缺参与关键字-only 缺参分开，按声明顺序聚合）。
	std::vector<std::string> missing_pos;
	std::vector<std::string> missing_kw;
	for (std::size_t k = 0; k < nspec; ++k) {
		if (specs[k].kind == ArgKind::Optional) {
			if (out.slots[k] == nullptr) {
				out.slots[k] = (specs[k].default_value != nullptr)
				                   ? specs[k].default_value
				                   : None::instance;
			}
		} else if (specs[k].kind == ArgKind::Keyword) {
			// 关键字-only：default_value 为 nullptr 即必填（见 ArgSpec 注释）。
			if (out.slots[k] == nullptr) {
				if (specs[k].default_value != nullptr) {
					out.slots[k] = specs[k].default_value;
				} else {
					missing_kw.push_back((specs[k].name != nullptr) ? specs[k].name : "");
				}
			}
		} else if (specs[k].kind == ArgKind::Required &&
		           out.slots[k] == nullptr) {
			const std::string nm = (specs[k].name != nullptr) ? specs[k].name : "";
			if (k < cap) missing_pos.push_back(nm);
			else         missing_kw.push_back(nm);
		}
	}
	if (!missing_pos.empty()) {
		err.code  = BindErrorCode::MissingPositional;
		err.names = std::move(missing_pos);
		return false;
	}
	if (!missing_kw.empty()) {
		err.code  = BindErrorCode::MissingKeywordOnly;
		err.names = std::move(missing_kw);
		return false;
	}
	return true;
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

	bool seen_optional = false;   // 位置段是否已出现带默认值的形参
	bool seen_rest     = false;   // 是否已出现 *args
	bool seen_kw       = false;   // 是否已出现 **kwargs
	bool seen_kwonly   = false;   // 是否已进入关键字-only 段（显式 Keyword）
	std::size_t kwonly_start = ArgTable::kNone;   // 关键字-only 段起点
	std::size_t required = 0;     // 位置必填个数
	std::size_t optional = 0;     // 位置可选个数

	for (const ArgSpec& s : specs) {
		// 1) 名字合法性
		if (!is_valid_param_name(s.name)) {
			throw ValueError(std::string("invalid parameter name '") +
			                 (s.name != nullptr ? s.name : "<null>") + "'.");
		}
		const std::size_t slot = t.specs_.size();

		// 2) 顺序与唯一性
		switch (s.kind) {
		// 关键字-only 有两种表达：显式 Keyword，或 seen_rest 之后的 Required / Optional。
		case ArgKind::Required:
			if (seen_kw) {
				throw ValueError("required parameter '" + std::string(s.name) +
				                 "' cannot follow **keywords parameter.");
			}
			if (seen_kwonly) {
				throw ValueError("positional parameter '" + std::string(s.name) +
				                 "' cannot follow a keyword-only parameter.");
			}
			if (!seen_rest) {
				if (seen_optional) {
					throw ValueError("required parameter '" + std::string(s.name) +
					                 "' cannot follow an optional parameter.");
				}
				++required;
			}
			break;
		case ArgKind::Optional:
			if (seen_kw) {
				throw ValueError("optional parameter '" + std::string(s.name) +
				                 "' cannot follow **keywords parameter.");
			}
			if (seen_kwonly) {
				throw ValueError("positional parameter '" + std::string(s.name) +
				                 "' cannot follow a keyword-only parameter.");
			}
			if (!seen_rest) {
				seen_optional = true;
				++optional;
			}
			break;
		case ArgKind::Rest:
			if (seen_rest) {
				throw ValueError("duplicate *args parameter ('" +
				                 std::string(s.name) + "').");
			}
			if (seen_kw) {
				throw ValueError("*args parameter must precede **keywords parameter.");
			}
			if (seen_kwonly) {
				throw ValueError("*args parameter must precede keyword-only parameters.");
			}
			seen_rest     = true;
			t.rest_index_ = slot;
			break;
		case ArgKind::Keyword:
			if (seen_kw) {
				throw ValueError("keyword-only parameter '" + std::string(s.name) +
				                 "' cannot follow **keywords parameter.");
			}
			if (!seen_kwonly) {
				seen_kwonly  = true;
				kwonly_start = slot;
			}
			break;
		case ArgKind::RestKeywords:
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
	// 关键字-only 段起点：显式 Keyword 的槽位，或 *args 的下一槽位；两者皆无
	// 则不存在关键字-only 段（起点取形参总数）。
	if (kwonly_start == ArgTable::kNone) {
		kwonly_start = (t.rest_index_ != ArgTable::kNone) ? t.rest_index_ + 1
		                                                 : t.specs_.size();
	}
	t.kwonly_start_ = kwonly_start;

	// 4) Optional / 关键字-only 可选形参的默认值常驻（数量极少，与既有常驻缓存同策略）
	for (ArgSpec& s : t.specs_) {
		const bool has_default = (s.kind == ArgKind::Optional) ||
		                         (s.kind == ArgKind::Keyword);
		if (has_default && s.default_value != nullptr) {
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

	// 统一内核完成全部绑定：位置填充 -> *args 收集 -> 关键字分拣 ->
	// 默认值填充 -> 缺参检查（关键字-only 由内核按位置隐含判定）。
	BoundArgs bound;
	BindError err;
	if (!BindParams(specs_.data(), specs_.size(),
	                (n > 0) ? args->data() : nullptr, n,
	                kwargs, bound, err)) {
		throw TypeError(format_bind_error(name_, err));
	}

	ArgResult r;
	r.owner_  = this;
	r.values_ = std::move(bound.slots);
	r.given_  = std::move(bound.given);
	r.owned_  = std::move(bound.owned);
	bound.owned.clear();   // 所有权已转移，避免 bound 析构时重复释放
	return r;
}

} // namespace Extension
} // namespace Pycp
