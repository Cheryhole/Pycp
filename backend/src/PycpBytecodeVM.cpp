#include "PycpBytecodeVM.hpp"
#include "PycpABI.hpp"
#include "PycpGC.hpp"
#include "PycpInteger.hpp"
#include "PycpString.hpp"
#include "PycpNone.hpp"
#include "PycpFunction.hpp"
#include "PycpException.hpp"

#include <vector>

namespace Pycp::BC {

// =============================================================
// VM 构造 / 析构
// =============================================================

VM::VM(Module* module) : module_(module) {
	global_env_ = std::make_shared<Environment>();
	global_env_->globals = new std::unordered_map<std::string, Object*>();

	// 注册内建函数到全局环境（print 等）
	if (BuiltinFunction::print != nullptr) {
		(*global_env_->globals)["print"] = BuiltinFunction::print;
		Incref(BuiltinFunction::print);
	}

	// 若 runtime_consts 为空（编译路径 Compile 未填充），则从 const_pool 构建。
	// 反序列化路径 Deserialize 已填充，则跳过。
	if (module_ && module_->runtime_consts.empty() && !module_->const_pool.empty()) {
		module_->runtime_consts.reserve(module_->const_pool.size());
		for (const auto& c : module_->const_pool) {
			switch (c.kind) {
				case ConstKind::INTEGER:
					module_->runtime_consts.push_back(Integer_FromLong(c.int_value));
					break;
				case ConstKind::STRING:
					module_->runtime_consts.push_back(String_FromString(c.str_value.c_str()));
					break;
				case ConstKind::NONE:
					module_->runtime_consts.push_back(None::instance);
					Incref(None::instance);
					break;
				default:
					throw Exception("BytecodeVM: unknown constant kind.");
			}
			GC_AddRoot(module_->runtime_consts.back());
		}
	}
}

VM::~VM() {
	if (global_env_ && global_env_->globals) {
		for (auto& kv : *global_env_->globals) {
			Decref(kv.second);
		}
		delete global_env_->globals;
		global_env_->globals = nullptr;
	}
	// 清理 runtime_consts 的 GC root（这些常量对象生命周期绑定到 Module）
	if (module_) {
		for (Object* c : module_->runtime_consts) {
			if (c != nullptr) {
				GC_RemoveRoot(c);
				Decref(c);
			}
		}
		module_->runtime_consts.clear();
	}
}

Object* VM::run() {
	if (module_ == nullptr || module_->code_objects.empty())
		throw Exception("BytecodeVM: empty module.");
	// 顶层代码对象即 code_objects[0]
	CodeObject* top = &module_->code_objects[0];
	std::shared_ptr<Environment> env = std::make_shared<Environment>();
	env->globals = global_env_->globals;
	// 顶层视为一个无参函数，locals 用于存储全局代码中的临时（实际全部走 globals）
	return execute(top, env, nullptr, 0);
}

Object* VM::call(size_t co_idx, Object** argv, std::size_t argc,
                 std::shared_ptr<Environment> captured) {
	if (module_ == nullptr || co_idx >= module_->code_objects.size())
		throw Exception("BytecodeVM: invalid code object index.");
	CodeObject* co = &module_->code_objects[co_idx];

	std::shared_ptr<Environment> env = std::make_shared<Environment>();
	env->globals = global_env_->globals;
	env->captured = captured;
	// 局部变量：参数名 + 编译期确定的其他局部名
	env->local_names = co->names;
	env->locals.assign(co->nlocals, nullptr);

	return execute(co, env, argv, argc);
}

// =============================================================
// 执行循环核心
// =============================================================

Object* VM::execute(CodeObject* co,
                    std::shared_ptr<Environment> env,
                    Object** argv, std::size_t argc) {
	std::vector<Object*> stack;
	stack.reserve(64);

	// 参数绑定到局部槽（locals[0..nparams)）
	for (std::size_t i = 0; i < argc && i < env->locals.size(); ++i) {
		env->locals[i] = argv[i];
		if (argv[i] != nullptr) Incref(argv[i]);
	}

	auto is_false = [](Object* v) -> bool {
		if (v == nullptr) return true;
		if (v->type == Type::NONE) return true;
		if (v->type == Type::INTEGER)
			return static_cast<Integer*>(v)->get_value() == 0;
		return false;
	};

	std::size_t dbg_pc = 0;
	auto push = [&](Object* v) {
		stack.push_back(v);
		if (v != nullptr) Incref(v);
	};
	auto pop = [&]() -> Object* {
		if (stack.empty())
			throw Exception("BytecodeVM: stack underflow at pc=" + std::to_string(dbg_pc)
			                + " in '" + co->name + "'.");
		Object* v = stack.back();
		stack.pop_back();
		return v;
	};

	// 局部变量名表：编译期已填入 co->names（参数 + 局部名，顺序与 nlocals 对齐）
	// 常量池运行时对象（反序列化后已 GC root）
	const std::vector<Object*>& consts = module_->runtime_consts;

	const std::size_t pc_end = co->code.size();
	for (std::size_t pc = 0; pc < pc_end; ++pc) {
		dbg_pc = pc;
		const Instruction& ins = co->code[pc];

		switch (ins.op) {
			// ---- 加载 / 存储 ----
			case Op::LOAD_CONST: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= consts.size())
					throw Exception("BytecodeVM: constant index out of range.");
				push(consts[idx]);
				break;
			}
			case Op::LOAD_NONE:
				push(None::instance);
				break;

			case Op::LOAD_VAR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw Exception("BytecodeVM: symbol index out of range.");
				const std::string& name = module_->symtab[idx];

				long local_idx = env->find_local(name);
				if (local_idx >= 0) {
					push(env->locals[local_idx]);
					break;
				}
				// 闭包捕获链
				std::shared_ptr<Environment> cap = env->captured;
				while (cap) {
					long c = cap->find_local(name);
					if (c >= 0) { push(cap->locals[c]); goto loaded; }
					cap = cap->captured;
				}
				// 全局
				if (env->globals) {
					auto it = env->globals->find(name);
					if (it != env->globals->end()) { push(it->second); break; }
				}
				throw Exception("NameError: name '" + name + "' is not defined.");
			loaded:
				break;
			}

			case Op::STORE_VAR: {
				std::size_t idx = static_cast<std::size_t>(ins.operand);
				if (idx >= module_->symtab.size())
					throw Exception("BytecodeVM: symbol index out of range.");
				const std::string& name = module_->symtab[idx];
				Object* value = pop(); // 所有权

				long local_idx = env->find_local(name);
				if (local_idx >= 0) {
					if (env->locals[local_idx]) Decref(env->locals[local_idx]);
					env->locals[local_idx] = value;
					break;
				}
				std::shared_ptr<Environment> cap = env->captured;
				while (cap) {
					long c = cap->find_local(name);
					if (c >= 0) {
						if (cap->locals[c]) Decref(cap->locals[c]);
						cap->locals[c] = value;
						goto stored;
					}
					cap = cap->captured;
				}
				if (env->globals) {
					auto it = env->globals->find(name);
					if (it != env->globals->end()) {
						if (it->second) Decref(it->second);
						it->second = value;
					} else {
						(*env->globals)[name] = value;
					}
					break;
				}
				Decref(value);
				throw Exception("NameError: name '" + name + "' is not defined.");
			stored:
				break;
			}

			case Op::POP_TOP: {
				Object* v = pop();
				Decref(v);
				break;
			}
			case Op::DUP_TOP: {
				if (stack.empty()) throw Exception("BytecodeVM: stack underflow.");
				push(stack.back());
				break;
			}

			// ---- 运算 ----
			case Op::BINARY_ADD: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Add(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::BINARY_SUB: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Sub(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::BINARY_MUL: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Mul(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::BINARY_DIV: {
				Object* rhs = pop(); Object* lhs = pop();
				Object* res = Div(lhs, rhs);
				Decref(lhs); Decref(rhs);
				push(res); Decref(res);
				break;
			}
			case Op::UNARY_NEG: {
				Object* v = pop();
				Object* res = v->__negation__();
				Decref(v);
				push(res); Decref(res);
				break;
			}

			// ---- 比较 ----
			case Op::COMPARE_OP: {
				Object* rhs = pop(); Object* lhs = pop();
				CompareOp cop = static_cast<CompareOp>(ins.operand);
				bool result = false;

				if (lhs->type == Type::INTEGER && rhs->type == Type::INTEGER) {
					int64_t a = static_cast<Integer*>(lhs)->get_value();
					int64_t b = static_cast<Integer*>(rhs)->get_value();
					switch (cop) {
						case CompareOp::LT: result = a < b; break;
						case CompareOp::LE: result = a <= b; break;
						case CompareOp::EQ: result = a == b; break;
						case CompareOp::NE: result = a != b; break;
						case CompareOp::GT: result = a > b; break;
						case CompareOp::GE: result = a >= b; break;
					}
				} else if (lhs->type == Type::STRING && rhs->type == Type::STRING) {
					std::string a = static_cast<String*>(lhs)->get_value();
					std::string b = static_cast<String*>(rhs)->get_value();
					switch (cop) {
						case CompareOp::LT: result = a < b; break;
						case CompareOp::LE: result = a <= b; break;
						case CompareOp::EQ: result = a == b; break;
						case CompareOp::NE: result = a != b; break;
						case CompareOp::GT: result = a > b; break;
						case CompareOp::GE: result = a >= b; break;
					}
				} else {
					switch (cop) {
						case CompareOp::EQ: result = false; break;
						case CompareOp::NE: result = true; break;
						default:
							Decref(lhs); Decref(rhs);
							throw Exception("TypeError: cannot compare different types.");
					}
				}

				Decref(lhs); Decref(rhs);
				Object* res = result ? Integer::instances[1] : Integer::instances[0];
				push(res);
				break;
			}

			// ---- 控制流 ----
			case Op::JUMP: {
				pc = pc + static_cast<std::size_t>(ins.operand) - 1;
				if (pc >= pc_end) throw Exception("BytecodeVM: jump out of range.");
				break;
			}
			case Op::JUMP_IF_FALSE: {
				Object* cond = pop();
				bool f = is_false(cond);
				Decref(cond);
				if (f) {
					pc = pc + static_cast<std::size_t>(ins.operand) - 1;
					if (pc >= pc_end) throw Exception("BytecodeVM: jump out of range.");
				}
				break;
			}
			case Op::JUMP_IF_TRUE: {
				Object* cond = pop();
				bool f = is_false(cond);
				Decref(cond);
				if (!f) {
					pc = pc + static_cast<std::size_t>(ins.operand) - 1;
					if (pc >= pc_end) throw Exception("BytecodeVM: jump out of range.");
				}
				break;
			}

			// ---- 函数与调用 ----
			case Op::MAKE_FUNCTION: {
				std::size_t fidx = static_cast<std::size_t>(ins.operand);
				if (fidx >= module_->code_objects.size())
					throw Exception("BytecodeVM: function index out of range.");
				BytecodeFunction* fn = new BytecodeFunction(this, fidx, env);
				GC_Track(fn);
				push(fn);
				// 函数对象所有权归栈顶，函数指针由 push 持有
				Decref(fn); // push 已 Incref，释放"新建 Owned"这 1 份
				break;
			}

			case Op::CALL: {
				std::size_t nargs = static_cast<std::size_t>(ins.operand);
				if (stack.size() < nargs + 1)
					throw Exception("BytecodeVM: call stack underflow.");

				std::vector<Object*> args(nargs);
				for (std::size_t i = 0; i < nargs; ++i)
					args[nargs - 1 - i] = pop();
				Object* callee = pop();

				if (callee->type != Type::FUNCTION) {
					for (Object* a : args) Decref(a);
					Decref(callee);
					throw Exception("TypeError: object is not callable.");
				}
				Function* fn = static_cast<Function*>(callee);
				Object* ret = fn->invoke(args.data(), nargs);

				for (Object* a : args) Decref(a);
				Decref(callee);
				push(ret);
				Decref(ret);
				break;
			}

			case Op::RETURN: {
				Object* ret = pop();
				for (Object* v : env->locals) if (v) Decref(v);
				env->locals.clear();
				for (Object* v : stack) Decref(v);
				return ret;
			}
			case Op::RETURN_NONE: {
				for (Object* v : env->locals) if (v) Decref(v);
				env->locals.clear();
				for (Object* v : stack) Decref(v);
				return None::instance;
			}

			case Op::HALT: {
				for (Object* v : stack) Decref(v);
				return None::instance;
			}

			default:
				throw Exception("BytecodeVM: unknown opcode.");
		}
	}

	for (Object* v : env->locals) if (v) Decref(v);
	env->locals.clear();
	for (Object* v : stack) Decref(v);
	return None::instance;
}

} // namespace Pycp::BC

// =============================================================
// BytecodeFunction 实现（namespace Pycp）
// =============================================================

namespace Pycp {

BytecodeFunction::BytecodeFunction(BC::VM* vm_, std::size_t code_idx_,
                                   std::shared_ptr<BC::Environment> captured_)
	: Function(""), vm(vm_), code_idx(code_idx_), captured(std::move(captured_)) {
	// 标记为 Bytecode 种类，并取函数名
	kind = FunctionKind::Bytecode;
	if (vm && code_idx < vm->get_module()->code_objects.size()) {
		name = vm->get_module()->code_objects[code_idx].name.c_str();
	}
}

Object* BytecodeFunction::invoke(Object** argv, std::size_t argc) {
	return vm->call(code_idx, argv, argc, captured);
}

} // namespace Pycp
