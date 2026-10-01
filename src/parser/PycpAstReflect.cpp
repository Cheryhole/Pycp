#include "parser/PycpAstReflect.hpp"

#include "parser/PycpAstNode.hpp"
#include "object/PycpNone.hpp"
#include "object/PycpInteger.hpp"
#include "object/PycpBoolean.hpp"
#include "object/PycpString.hpp"
#include "object/PycpList.hpp"
#include "object/PycpGC.hpp"
#include "object/PycpException.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

// 由 Flex/Bison 生成的解析器提供（字符串解析 + 错误计数 + 当前源路径）。
extern Pycp::Ast::Node* parse(const std::string& src);
extern int Pycp_parse_error_count;
extern std::string g_current_source_path;

namespace Pycp {
namespace Ast {

namespace {

// =============================================================
// 底层 C++ AST 的所有权容器
// -------------------------------------------------------------
// 由全部映射出的节点对象经 shared_ptr 共同持有：只要还有任一节点存活，
// 底层 AST 就存活；节点对象的 __string__() 惰性调用 node_->to_string()。
// 这样避免了「预计算每个节点的 to_string 造成 O(n^2)」以及悬垂指针。
// =============================================================
struct AstHolder {
	Node* root = nullptr;
	~AstHolder() { delete root; }
};
using AstHolderPtr = std::shared_ptr<AstHolder>;

// =============================================================
// 节点对象（pycp Object 子类）
// -------------------------------------------------------------
// type_name = 节点类型名；字段写入基类 members_：
//   - 属性访问：Object::__get_attribute__ 会查 members_（Incref 返回）；
//   - 生命周期：Object::~Object 统一 Decref members_ 中的值；
//   - GC：Object::foreach_ref / __inspect__ 均基于 members_。
// 故无需自行管理字段的引用计数与遍历。
// =============================================================
class ApiNode : public Object {
public:
	ApiNode(const std::string& node_type, AstHolderPtr holder, Node* node)
		: Object(node_type), holder_(std::move(holder)), node_(node) {}

	// 写入字段（接管 value 所有权；重复写入先释放旧值）。
	void SetField(const std::string& name, Object* value) {
		auto it = members_.find(name);
		if (it != members_.end() && it->second != nullptr) {
			Decref(it->second);
		}
		members_[name] = value;
	}

	Object* __string__() override {
		if (node_ == nullptr) return String::FromCString("");
		return String::FromCString(node_->to_string().c_str());
	}
	Object* __raw_string__() override { return __string__(); }

private:
	AstHolderPtr holder_;
	Node* node_;
};

// =============================================================
// 枚举 -> 名称（供 pycp 层按字符串分派）
// =============================================================
const char* UnaryOpName(UnaryOp op) {
	switch (op) {
		case UnaryOp::UMINUS: return "UMINUS";
		case UnaryOp::NOT:    return "NOT";
	}
	return "UNKNOWN";
}

const char* BinaryOpName(BinaryOp op) {
	switch (op) {
		case BinaryOp::PLUS:          return "PLUS";
		case BinaryOp::MINUS:         return "MINUS";
		case BinaryOp::MULTIPLY:      return "MULTIPLY";
		case BinaryOp::DIVIDE:        return "DIVIDE";
		case BinaryOp::POWER:         return "POWER";
		case BinaryOp::LESS_THAN:     return "LESS_THAN";
		case BinaryOp::GREATER_THAN:  return "GREATER_THAN";
		case BinaryOp::LESS_EQUAL:    return "LESS_EQUAL";
		case BinaryOp::GREATER_EQUAL: return "GREATER_EQUAL";
		case BinaryOp::EQUAL:         return "EQUAL";
		case BinaryOp::NOT_EQUAL:     return "NOT_EQUAL";
		case BinaryOp::IS_IN:         return "IS_IN";
	}
	return "UNKNOWN";
}

const char* ParamKindName(ParamKind k) {
	switch (k) {
		case ParamKind::Positional:    return "Positional";
		case ParamKind::VarPositional: return "VarPositional";
		case ParamKind::KeywordOnly:   return "KeywordOnly";
		case ParamKind::VarKeyword:    return "VarKeyword";
		case ParamKind::BareStar:      return "BareStar";
	}
	return "UNKNOWN";
}

const char* RepeatModeName(RepeatMode m) {
	switch (m) {
		case RepeatMode::INFINITE: return "INFINITE";
		case RepeatMode::WHILE:    return "WHILE";
		case RepeatMode::COUNT:    return "COUNT";
		case RepeatMode::RANGE:    return "RANGE";
	}
	return "UNKNOWN";
}

// =============================================================
// 映射器
// =============================================================
class Mapper {
public:
	explicit Mapper(AstHolderPtr holder) : holder_(std::move(holder)) {}

	// 主映射：按【具体 struct 类型】分派（不依赖 NodeType 枚举值——该枚举
	// 存在值 15 重复占用 BOOLEAN_LITERAL / IF_STATEMENT、值 23 缺失的缺陷）。
	Object* MapNode(Node* n) {
		if (n == nullptr) return None::instance;

		// ---- 顶层 / 语句 ----
		if (auto* p = dynamic_cast<Program*>(n)) {
			ApiNode* o = Make("Program", p);
			o->SetField("statements", StmtList(p->statements));
			return o;
		}
		if (auto* a = dynamic_cast<AssignmentStatement*>(n)) {
			ApiNode* o = Make("AssignmentStatement", a);
			o->SetField("target", Maybe(a->target));
			o->SetField("value", Maybe(a->value));
			o->SetField("decorators", ExprList(a->decorators));
			return o;
		}
		if (auto* r = dynamic_cast<ReturnStatement*>(n)) {
			ApiNode* o = Make("ReturnStatement", r);
			o->SetField("expression", Maybe(r->expression));
			return o;
		}
		if (auto* es = dynamic_cast<ExpressionStatement*>(n)) {
			ApiNode* o = Make("ExpressionStatement", es);
			o->SetField("expression", Maybe(es->expression));
			return o;
		}
		if (auto* br = dynamic_cast<BreakStatement*>(n)) {
			return Make("BreakStatement", br);
		}
		if (auto* del = dynamic_cast<DeleteStatement*>(n)) {
			ApiNode* o = Make("DeleteStatement", del);
			o->SetField("target", Maybe(del->target));
			return o;
		}
		if (auto* fe = dynamic_cast<ForeachStatement*>(n)) {
			ApiNode* o = Make("ForeachStatement", fe);
			o->SetField("var_name", OptStr(fe->var_name));
			o->SetField("iterable", Maybe(fe->iterable));
			o->SetField("body", Maybe(fe->body));
			return o;
		}
		if (auto* rp = dynamic_cast<RepeatStatement*>(n)) {
			ApiNode* o = Make("RepeatStatement", rp);
			o->SetField("mode", Str(RepeatModeName(rp->mode)));
			o->SetField("count_expr", Maybe(rp->count_expr));
			o->SetField("start_expr", Maybe(rp->start_expr));
			o->SetField("end_expr", Maybe(rp->end_expr));
			o->SetField("step_expr", Maybe(rp->step_expr));
			o->SetField("cond_expr", Maybe(rp->cond_expr));
			o->SetField("var_name", OptStr(rp->var_name));
			o->SetField("body", Maybe(rp->body));
			return o;
		}
		if (auto* is = dynamic_cast<IfStatement*>(n)) {
			ApiNode* o = Make("IfStatement", is);
			o->SetField("if_branch", Maybe(is->if_branch));
			List* elifs = List::New();
			if (is->elif_branches != nullptr) {
				for (IfBranch* b : *is->elif_branches) {
					Object* item = Maybe(b);
					elifs->append(item);
					Decref(item);
				}
			}
			o->SetField("elif_branches", elifs);
			o->SetField("else_body", Maybe(is->else_body));
			return o;
		}
		if (auto* ib = dynamic_cast<IfBranch*>(n)) {
			ApiNode* o = Make("IfBranch", ib);
			o->SetField("condition", Maybe(ib->condition));
			o->SetField("body", Maybe(ib->body));
			o->SetField("is_elif", Bool(ib->is_elif));
			return o;
		}
		if (auto* im = dynamic_cast<ImportStatement*>(n)) {
			ApiNode* o = Make("ImportStatement", im);
			o->SetField("module_name", OptStr(im->module_name));
			o->SetField("alias", OptStr(im->alias));
			return o;
		}
		if (auto* fi = dynamic_cast<FromImportStatement*>(n)) {
			ApiNode* o = Make("FromImportStatement", fi);
			o->SetField("module_name", OptStr(fi->module_name));
			o->SetField("names", StrList(fi->names));
			return o;
		}
		if (auto* cd = dynamic_cast<ClassDefinition*>(n)) {
			ApiNode* o = Make("ClassDefinition", cd);
			o->SetField("name", OptStr(cd->name));
			o->SetField("parent_name", OptStr(cd->parent_name));
			o->SetField("member_variables", StmtList(cd->member_variables));
			o->SetField("methods", StmtList(cd->methods));
			o->SetField("decorators", ExprList(cd->decorators));
			return o;
		}
		if (auto* mv = dynamic_cast<MemberVariable*>(n)) {
			ApiNode* o = Make("MemberVariable", mv);
			o->SetField("name", OptStr(mv->name));
			o->SetField("value", Maybe(mv->value));
			o->SetField("decorators", ExprList(mv->decorators));
			return o;
		}
		if (auto* md = dynamic_cast<MethodDefinition*>(n)) {
			ApiNode* o = Make("MethodDefinition", md);
			o->SetField("function", Maybe(md->function));
			o->SetField("decorators", ExprList(md->decorators));
			return o;
		}

		// ---- 表达式 ----
		if (auto* ue = dynamic_cast<UnaryExpression*>(n)) {
			ApiNode* o = Make("UnaryExpression", ue);
			o->SetField("op", Str(UnaryOpName(ue->op)));
			o->SetField("operand", Maybe(ue->operand));
			return o;
		}
		if (auto* be = dynamic_cast<BinaryExpression*>(n)) {
			ApiNode* o = Make("BinaryExpression", be);
			o->SetField("op", Str(BinaryOpName(be->op)));
			o->SetField("left", Maybe(be->left));
			o->SetField("right", Maybe(be->right));
			return o;
		}
		if (auto* fe = dynamic_cast<FunctionExpression*>(n)) {
			ApiNode* o = Make("FunctionExpression", fe);
			o->SetField("name", Str(fe->name));
			o->SetField("params", ParamList(fe->params));
			o->SetField("body", Maybe(fe->body));
			o->SetField("decorators", ExprList(fe->decorators));
			return o;
		}
		if (auto* ce = dynamic_cast<CallExpression*>(n)) {
			ApiNode* o = Make("CallExpression", ce);
			o->SetField("callee", Maybe(ce->callee));
			o->SetField("arguments", ExprList(ce->arguments));
			o->SetField("keyword_arguments", KeywordList(ce->keyword_arguments));
			return o;
		}
		if (auto* id = dynamic_cast<IdentifierExpression*>(n)) {
			ApiNode* o = Make("IdentifierExpression", id);
			o->SetField("name", OptStr(id->name));
			return o;
		}
		if (auto* at = dynamic_cast<AttributeExpression*>(n)) {
			ApiNode* o = Make("AttributeExpression", at);
			o->SetField("target", Maybe(at->target));
			o->SetField("attr", OptStr(at->attr));
			return o;
		}
		if (auto* ix = dynamic_cast<IndexExpression*>(n)) {
			ApiNode* o = Make("IndexExpression", ix);
			o->SetField("target", Maybe(ix->target));
			o->SetField("index", Maybe(ix->index));
			return o;
		}
		if (auto* ll = dynamic_cast<ListLiteral*>(n)) {
			ApiNode* o = Make("ListLiteral", ll);
			o->SetField("elements", ExprList(ll->elements));
			return o;
		}
		if (auto* ml = dynamic_cast<MapLiteral*>(n)) {
			ApiNode* o = Make("MapLiteral", ml);
			o->SetField("pairs", PairList(ml->pairs));
			return o;
		}
		if (auto* cl = dynamic_cast<ClassExpression*>(n)) {
			ApiNode* o = Make("ClassExpression", cl);
			o->SetField("parent_name", OptStr(cl->parent_name));
			o->SetField("member_variables", StmtList(cl->member_variables));
			o->SetField("methods", StmtList(cl->methods));
			return o;
		}

		// ---- 字面量（具体类型先于 Literal 基类）----
		if (auto* il = dynamic_cast<IntegerLiteral*>(n)) {
			ApiNode* o = Make("IntegerLiteral", il);
			o->SetField("value", OptStr(il->value));
			return o;
		}
		if (auto* fl = dynamic_cast<FloatLiteral*>(n)) {
			ApiNode* o = Make("FloatLiteral", fl);
			o->SetField("value", OptStr(fl->value));
			return o;
		}
		if (auto* dl = dynamic_cast<DecimalLiteral*>(n)) {
			ApiNode* o = Make("DecimalLiteral", dl);
			o->SetField("value", OptStr(dl->value));
			return o;
		}
		if (auto* sl = dynamic_cast<StringLiteral*>(n)) {
			ApiNode* o = Make("StringLiteral", sl);
			o->SetField("value", OptStr(sl->value));
			return o;
		}
		if (auto* bl = dynamic_cast<BooleanLiteral*>(n)) {
			ApiNode* o = Make("BooleanLiteral", bl);
			o->SetField("value", Bool(bl->value));
			return o;
		}
		if (auto* nl = dynamic_cast<NoneLiteral*>(n)) {
			return Make("NoneLiteral", nl);
		}

		// ---- 基类兜底（正常不会被命中）----
		if (auto* lit = dynamic_cast<Literal*>(n))   return Make("Literal", lit);
		if (auto* ex = dynamic_cast<Expression*>(n)) return Make("Expression", ex);
		if (auto* st = dynamic_cast<Statement*>(n))  return Make("Statement", st);
		return Make("Node", n);
	}

private:
	// 建节点：type_name + 通用属性 type / lineno。
	ApiNode* Make(const char* type_name, Node* n) {
		ApiNode* o = New<ApiNode>(std::string(type_name), holder_, n);
		o->SetField("type", Str(type_name));
		o->SetField("lineno", Int(n != nullptr ? n->lineno : -1));
		return o;
	}

	// null -> None（否则映射为节点对象）。
	Object* Maybe(Node* n) {
		return n != nullptr ? MapNode(n) : static_cast<Object*>(None::instance);
	}

	// 语句列表 -> List
	Object* StmtList(const std::vector<Statement*>* v) {
		List* lst = List::New();
		if (v != nullptr) {
			for (Statement* s : *v) {
				Object* item = Maybe(s);
				lst->append(item);
				Decref(item);
			}
		}
		return lst;
	}

	// 表达式列表 -> List（指针 / 值两种来源）
	Object* ExprList(const std::vector<Expression*>* v) {
		return v != nullptr ? ExprList(*v) : static_cast<Object*>(List::New());
	}
	Object* ExprList(const std::vector<Expression*>& v) {
		List* lst = List::New();
		for (Expression* e : v) {
			Object* item = Maybe(e);
			lst->append(item);
			Decref(item);
		}
		return lst;
	}

	// 字符串列表 -> List
	Object* StrList(const std::vector<std::string*>* v) {
		List* lst = List::New();
		if (v != nullptr) {
			for (std::string* s : *v) {
				Object* item = OptStr(s);
				lst->append(item);
				Decref(item);
			}
		}
		return lst;
	}

	// 形参列表 -> List（合成 "Param" 节点）
	Object* ParamList(const std::vector<Param>& v) {
		List* lst = List::New();
		for (const Param& prm : v) {
			ApiNode* po = New<ApiNode>(std::string("Param"), holder_, nullptr);
			po->SetField("type", Str("Param"));
			po->SetField("lineno", Int(prm.line));
			po->SetField("name", OptStr(prm.name));
			po->SetField("kind", Str(ParamKindName(prm.kind)));
			po->SetField("default", Maybe(prm.default_value));
			lst->append(po);
			Decref(po);
		}
		return lst;
	}

	// 关键字实参列表 -> List（合成 "Keyword" 节点）
	Object* KeywordList(const std::vector<std::pair<std::string*, Expression*>>& v) {
		List* lst = List::New();
		for (const auto& kv : v) {
			ApiNode* ko = New<ApiNode>(std::string("Keyword"), holder_, nullptr);
			ko->SetField("type", Str("Keyword"));
			ko->SetField("lineno", Int(-1));
			ko->SetField("name", OptStr(kv.first));
			ko->SetField("value", Maybe(kv.second));
			lst->append(ko);
			Decref(ko);
		}
		return lst;
	}

	// map 字面量的键值对 -> List（合成 "Pair" 节点）
	Object* PairList(const std::vector<std::pair<Expression*, Expression*>>* v) {
		List* lst = List::New();
		if (v != nullptr) {
			for (const auto& pr : *v) {
				ApiNode* po = New<ApiNode>(std::string("Pair"), holder_, nullptr);
				po->SetField("type", Str("Pair"));
				po->SetField("lineno", Int(-1));
				po->SetField("key", Maybe(pr.first));
				po->SetField("value", Maybe(pr.second));
				lst->append(po);
				Decref(po);
			}
		}
		return lst;
	}

	// 小工具（均返回 Owned / 常驻单例）
	Object* Str(const std::string& s) { return String::FromCString(s.c_str()); }
	Object* OptStr(const std::string* s) {
		return s != nullptr ? Str(*s) : static_cast<Object*>(None::instance);
	}
	Object* Bool(bool b) {
		return b ? static_cast<Object*>(Boolean::True())
		         : static_cast<Object*>(Boolean::False());
	}
	Object* Int(long long v) { return Integer::FromLong(v); }

	AstHolderPtr holder_;
};

} // anonymous namespace

Object* ParseToObjects(const std::string& source, const std::string& filename) {
	// 与 ModuleLoader::compile_string 一致：复位错误计数与源路径，使
	// parser/lexer 的报错定位到本次解析的 filename。
	Pycp_parse_error_count = 0;
	g_current_source_path = filename;

	Node* ast = parse(source);
	if (ast == nullptr || Pycp_parse_error_count > 0) {
		delete ast;
		throw Pycp::Exception(""); // 词法/语法错误已由 parser 打印
	}

	AstHolderPtr holder = std::make_shared<AstHolder>();
	holder->root = ast;
	Mapper mapper(holder);
	return mapper.MapNode(ast); // Owned
}

} // namespace Ast
} // namespace Pycp
