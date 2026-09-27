# Pycp Language Guide & Built-in API Reference

> **Audience**: This document is written for *models / tooling* that need to read, reason about, or generate Pycp source code. It is the authoritative description of the language surface and the built-in type API as actually implemented in the runtime (verified against the method tables in `src/object/*_method_table()` and the standard-library modules in `src/stdlib/*/src/*.cpp`).
>
> Pycp is a Python-*like* language: it borrows Python's syntax and object model but is **not** a Python superset. Several Python features are deliberately absent or different — read **§10 Known limitations & gotchas** before generating code.

---

## 1. Overview

- Pycp source is `.pycp` (text). It is compiled to a stack-based bytecode `.cpycp` (like Python's `.pyc`) and executed by an embedded VM.
- The compiler/interpreter is a single binary `pycp`. There is **no REPL-only mode requirement**; `pycp file.pycp` runs it.
- Naming convention for type names uses **uppercase** built-in type names (`Integer`, `String`, `List`, `None`, `File`, `Function`, `Class`, `Module`, `Map`, `FixedList`, `Decimal`, `Boolean`). User-defined class `class Foo {}` has type name `Foo` (preserved verbatim, with case).
- There are **no built-in functions injected** into the global namespace. Everything is reached through modules (`io`, `pycp`, `classtools`, `moduletools`, `maths`, `aot`).
- **Convention**: all user-facing runtime/CLI error messages are English (project rule). Source code comments in the repo are Chinese; that is unrelated to generated-code text.

---

## 2. Lexical rules

| Element | Rule |
|---------|------|
| Line comment | `// ...` to end of line |
| Block comment | `/* ... */` (nestable content, unterminated → lexer error) |
| **`#` is NOT a comment** | `#` is reserved for preprocessor line directives (`// __pycp_pp# <lineno> "<file>"`). A bare `#` is an *unexpected character* error. |
| Identifiers | `[a-zA-Z_][a-zA-Z0-9_]*` |
| Integer literals | `[0-9]+`; hexadecimal `0x[0-9a-fA-F]+` (e.g. `0xFF`) |
| Float literals | `[0-9]+\.[0-9]+` (e.g. `3.14`) |
| Float/Decimal suffixes | `1.0f`/`1.0F` → `Float`; `1.0d`/`1.0D` → `Decimal`; `1f`/`1d` (int+f/d) → `Float`/`Decimal` |
| String literals | **Double-quoted only**: `"..."`. Single quotes `'...'` are *not* a token (error). Supports `\n \t \r \0 \\ \"`, `\xHH`, `\uXXXX` escapes. |
| Whitespace | space/tab ignored; newlines are statement separators (except inside brackets) |
| Implicit line continuation | Inside `( [ {` (depth > 0) newlines are ignored (Python-style). A statement **cannot** span lines outside brackets. |

---

## 3. Program structure & statements

```pycp
import io
import pycp
from pycp import Integer

func greet(name) {
    return "Hello, " + name
}

io.print(greet("world"))
```

- **No `var`/`let` keyword.** Assignment is `name = expr`. Variables are dynamically typed and may be reassigned to any type.
- **No augmented assignment** (`+=`, `-=`, `++`, `--` are not tokens). Write `x = x + 1`.
- **No type annotations** (`x: int` is unsupported).
- A top-level file is a module. Statements are separated by newlines (or `;` is not a statement separator — rely on newlines / blocks).
- Bare expression statements are allowed (e.g. a function call on its own line).
- `delete obj` / `delete obj.name` / `delete obj[key]` is supported (keyword `delete`).

---

## 4. Expressions & operators

| Category | Operators (as tokens) |
|----------|----------------------|
| Arithmetic | `+  -  *  /  **` (power) and unary `-` |
| Comparison | `<  <=  >  >=  ==  !=` |
| **No logical operators** | `and`/`or`/`not`/`&&`/`||`/`!` do **not** exist. Boolean conditions are combined by **nesting `if` blocks** (see §10). |
| **No integer-division operator** | `//` is a *line comment*, not floor division. Use `maths.floor(a / b)` or `maths.divmod(a, b)`. |
| Membership | `value in container` / `value in map` (desugars to the `contains`/`has` method) |
| Subscription | `obj[key]` (read), `obj[key] = v` (write), `delete obj[key]` |
| Attribute | `obj.name` (read/write), `delete obj.name` |
| Call | `f(args, kw = val)` |
| Grouping | `( expr )` — parentheses are optional around `if`/`while` conditions but idiomatic |

String concatenation: `"a" + "b"`. String repetition: `"a" * 3` → `"aaa"`. List concatenation: `[1] + [2]`.

---

## 5. Control flow

### `if` / `elif` / `else`

```pycp
if (x > 0) {
    io.print("pos")
} elif (x < 0) {
    io.print("neg")
} else {
    io.print("zero")
}
```

- **`else if` is NOT valid syntax.** Use `elif`. After a `}` the parser is in an `elif`-expecting state; only `elif`, `else`, comments, or a fresh statement are accepted.
- The condition expression may be parenthesized (`if (cond)`) or bare (`if cond`); parentheses are just grouping.

### Loops

```pycp
repeat if (cond) {            // while loop; exits when cond is falsy
    ...
}

repeat 10 { ... }             // count 10 times (no index bound)
repeat 10 as i { ... }        // i = 0 .. 9

repeat from 1 to 5 { ... }            // inclusive range [1, 5]
repeat from 1 to 10 by 2 as i { ... } // step 2, i takes endpoint values
repeat from 1 to 5 as i { ... }       // i = 1 .. 5

for item in some_iterable {   // foreach over an iterable (List/Map/FixedList/String)
    io.print(item)
}

break                        // supported; there is NO `continue`
```

- There is **no `while` keyword** — use `repeat if (cond)`.
- The bare `repeat { }` infinite form was removed; use `repeat if (True) { }`.
- `repeat from a to b` is **inclusive** of both endpoints.

---

## 6. Functions

```pycp
func add(a, b) {
    return a + b
}

func f(a, b = 1) { ... }              // default value (only trailing params may default)
func f(a, *rest) { ... }              // *rest collects extra positional args into a FixedList
func f(a, *, b, c = 9) { ... }        // bare * : b/c are keyword-only
func f(a, *rest, key = 0) { ... }     // keyword-only after *rest
func f(**kwargs) { ... }              // **kwargs collects unconsumed keywords into a Map

g = func(x) { return x * 2 }          // anonymous function (closure)
add(1, 2)
add(1, b = 3)                         // keyword argument (positionals must come first)
```

- Default values are arbitrary expressions, evaluated **once** at function-definition time. A shared mutable default (e.g. `b = []`) is shared across calls (Python semantics).
- Illegal: a required plain param after a defaulted param (`func f(a, b = 1, c)` → compile error).
- Call-side unpacking `f(*a)` / `f(**k)` is **not** supported.
- A function returns `None` if there is no `return` (or `return;` with no value).
- Anonymous functions capture outer local variables (closures).

---

## 7. Classes & object-oriented programming

```pycp
class Animal {
    @public
    func __initialize__(self, age) {
        self.age = age
    }
    @public
    func sound(self) {
        return "..."
    }
    @public
    func __string__(self) {
        return "Animal(" + pycp.String(self.age) + ")"
    }
}

class Dog inherits Animal {            // single inheritance only
    func sound(self) {
        return "woof"
    }
    func parent_sound(self) {
        return super().sound(self)    // super() (from classtools) returns the parent Class
    }
}

d = Dog(3)
io.print(d.sound())    // "woof"
io.print(d.age)        // 3 (inherited member)
```

- Methods take `self` as the first parameter explicitly.
- Constructor: `__initialize__(self, ...)` — called automatically on instantiation.
- Single inheritance via `class Child inherits Parent`. Subclass copies parent members/methods (with visibility).
- `super()` (from the `classtools` module) returns the **parent Class** of the current method's class; call it as `super().method(self, ...)`.
- **No multiple inheritance.**
- Member visibility decorators `@private` / `@public` / `@readonly` (import from `pycp` or `classtools`). `@private` members are inaccessible outside the class; `@readonly` members reject reassignment. A module-top-level `@private` symbol is invisible to `import`.
- Operator overloading & stringification via magic methods (§9), e.g. `__addition__`, `__string__`, `__get_item__`.
- Default `String` representation: objects without `__string__` print as `<Name at 0xADDR>`; class instances `<Name instance at 0xADDR>`; classes `<class "Name">`; functions `<function "name" at 0xADDR>`; modules `<module "name">`.

### Decorators

```pycp
from pycp import public, private, readonly

@public
func greet(name) { return "Hello " + name }

@private
@readonly
func secret() { return 5 }
```

- `@decorator` syntax sugar: passes the decorated object to the decorator function and replaces it with the return value.
- **Stacked decorators** are supported (`@d1` then `@d2`); the one closest to the target applies first (i.e. `@d1` `@d2` `target` ≡ `d1(d2(target))`). Works on functions, variables (`@d NAME = value`), classes, and class members.
- `readonly` / `private` / `public` can also be **called directly**: `pycp.readonly(x)`, `pycp.private(f)` (argument must be a module-top-level bare identifier).

---

## 8. Modules & imports

```pycp
import io
import foo as bar
from foo import a, b
```

- **`import a.b` is NOT supported** (`.` is member access only). Use `from a import b`, or `import a` then `a.b` as attribute access, or `import b`.
- Module folders (packages): a directory containing `package.mpycp` (manifest) plus submodules. Run as a program with `pycp -m <pkg> arg1 arg2` (arguments after the folder go verbatim to the package's `main(argv)`; see §11). `import pkg` treats it as a library.
- Package roles: declare with `moduletools.as_program()` / `as_library()` in the manifest. Default: interpreter run → program; AOT → library. Conflict (program imported / library executed) → error.
- Lookup order (first hit wins): (1) process cache / already-linked symbol `PycpModule_<name>`; (2) cwd; (3) script directory; (4) `<exe>/stdlib/`. Within each layer, `.pycp` source takes priority over a same-named `.so`.
- `pycp.argv`: `List[String]` of command-line arguments. Interpreter: `argv[0]` is the script name. AOT binary: `argv[0]` is the program path. REPL: `[]`.

---

## 9. Magic methods (29, shared by all types)

These dunder names are the single, authoritative dispatch table (`src/object/PycpMagic.cpp`). Ordinary user methods must **not** collide with them.

| Magic method | Triggered by |
|-------------|--------------|
| `__integer__` | convert to Integer (e.g. `pycp.Integer(x)`) |
| `__string__` | convert to String (`io.print`, string concat) |
| `__raw_string__` | raw representation (container element/key rendering) |
| `__boolean__` | truthiness test (used by `if`/`repeat if`) |
| `__list__` | convert to `List` (e.g. `pycp.List(x)`) |
| `__map__` | convert to `Map` |
| `__hash__` | hash (used as `Map`/`FixedList` key) |
| `__iterator__` / `__next__` | iteration (`for` / `repeat`) |
| `__negation__` | unary `-` on the object |
| `__inspect__` | `pycp.insp(obj)` returns the member-name `List` |
| `__delete__` | object destruction hook |
| `__addition__` `__subtraction__` `__multiplication__` `__division__` `__power__` | binary `+ - * / **` |
| `__less_than__` `__less_equal__` `__equal__` `__not_equal__` `__greater_than__` `__greater_equal__` | comparison operators |
| `__get_item__` `__set_item__` `__delete_item__` | subscript read / write / delete `obj[k]` |
| `__get_attribute__` `__set_attribute__` `__delete_attribute__` | attribute read / write / delete `obj.name` |

---

## 10. Built-in type API

> Method tables below are the exact registered methods. `-> T` is the return type. `[x]` = optional. Constructors accept `pycp.Type(x)` (convert) and `pycp.Type()` (blank/empty).

### 10.1 `None`
- Type name `None` (singleton). No ordinary methods; only magic methods. Used as the "missing"/empty result.

### 10.2 `Boolean`
- `pycp.Boolean(x)`; `pycp.Boolean()` → `False`.
- **Inherits `Integer` and reuses the Integer method table** — no ordinary methods, only magic (`__boolean__`, `__integer__`, `__equal__`, arithmetic, comparison, etc.).
- Truthiness: `True`/`False`. Non-empty containers and non-zero numbers are truthy.

### 10.3 `Integer` / `10.4` `Float` / `10.5` `Decimal`
- `pycp.Integer(x)` → `0` when blank; `pycp.Float(x)` → `0.0`; `pycp.Decimal(x)` → `0` (exact decimal, mpdecimal backend, 28 significant digits by default).
- **None of these numeric types has ordinary methods.** All math lives in the `maths` module (§12.5); numeric conversion uses the constructors.
- Magic methods: arithmetic `+ - * / **`, unary `-`, comparisons, `__integer__`/`__float__` casts, `__string__`, `__boolean__`, `__hash__`.

### 10.6 `String` (immutable)
`pycp.String(x)`; `pycp.String()` → `""`. All methods return a new string.

| Method | Signature | Semantics |
|--------|-----------|-----------|
| `length` | `() -> Integer` | character count |
| `upper` | `() -> String` | to upper case |
| `lower` | `() -> String` | to lower case |
| `strip` | `([chars]) -> String` | strip leading/trailing whitespace (`chars` = explicit charset) |
| `lstrip` | `([chars]) -> String` | strip leading |
| `rstrip` | `([chars]) -> String` | strip trailing |
| `split` | `([sep [, maxsplit]]) -> List[String]` | split; `sep=None` splits on any whitespace and drops empty strings |
| `join` | `(iterable) -> String` | join elements (each must be `String`) with this string |
| `replace` | `(old, new [, count]) -> String` | replace (`count < 0` = all) |
| `find` | `(sub [, start [, end]]) -> Integer` | first index of `sub`, or `-1` if not found |
| `count` | `(sub) -> Integer` | number of non-overlapping occurrences |
| `startswith` | `(prefix) -> Boolean` | whether it starts with `prefix` |
| `endswith` | `(suffix) -> Boolean` | whether it ends with `suffix` |
| `contains` | `(sub) -> Boolean` | whether it contains `sub` (same as `sub in s`) |

Magic: `+` concat, `*` repeat, `[i]` index (negative indices supported), `==`, iteration, `__integer__` (parse), `__string__`, `__boolean__` (non-empty = true), `__hash__`.

### 10.7 `List` (mutable sequence)
`pycp.List()` → `[]`; `pycp.List(iterable)` builds from an iterable.

| Method | Signature | Semantics |
|--------|-----------|-----------|
| `length` | `() -> Integer` | number of elements |
| `append` | `(item) -> None` | append to end (in place) |
| `extend` | `(iterable) -> None` | extend (in place) |
| `insert` | `(index, item) -> None` | insert (out-of-range clamped, Python-style) |
| `remove` | `(value) -> None` | remove first match (not found → `ValueError`) |
| `pop` | `([index]) -> Object` | pop (default last; empty/out-of-range → `IndexError`) |
| `index` | `(value) -> Integer` | first index (not found → `ValueError`) |
| `count` | `(value) -> Integer` | occurrence count |
| `contains` | `(value) -> Boolean` | membership test |
| `reverse` | `() -> None` | reverse (in place) |
| `sort` | `([reverse]) -> None` | natural-order sort (`reverse=True` → descending); in place |
| `clear` | `() -> None` | remove all |
| `copy` | `() -> List` | shallow copy |

Magic: `obj[i]` read/write (negative indices), `+` concat, iteration, `__list__` (returns self), `__boolean__` (non-empty = true), `__string__`/`__raw_string__`.

### 10.8 `FixedList` (immutable tuple)
`pycp.FixedList()` → `Fixed[]`; `pycp.FixedList(iterable)` or `pycp.FixedList(a, b, ...)`. Fixed-length and immutable (no `__set_item__` / `append`).

| Method | Signature | Semantics |
|--------|-----------|-----------|
| `length` | `() -> Integer` | element count |
| `index` | `(value) -> Integer` | first index (not found → `ValueError`) |
| `count` | `(value) -> Integer` | occurrence count |
| `contains` | `(value) -> Boolean` | membership test |

Magic: `t[i]` read-only index (negative supported), `+` concat, iteration, `__hash__` (hashable), `__boolean__`, `__string__`/`__raw_string__`. To get a mutable copy use `pycp.List(fixedlist)`.

### 10.9 `Map` (dictionary)
`pycp.Map()` → `{}`; `pycp.Map(other)` builds from another `Map`. Keys must be hashable (numbers / strings / booleans / `FixedList` / `None`).

| Method | Signature | Semantics |
|--------|-----------|-----------|
| `length` | `() -> Integer` | number of key/value pairs |
| `keys` | `() -> List` | all keys |
| `values` | `() -> List` | all values |
| `items` | `() -> List[FixedList[k, v]]` | key/value pairs (each a 2-element `FixedList`) |
| `get` | `(key [, default]) -> Object` | value, or `None` if absent |
| `has` | `(key) -> Boolean` | whether key exists (same as `key in d`) |
| `remove` | `(key) -> Object` | delete and return the removed value (absent → `KeyError`) |
| `update` | `(other) -> None` | merge another `Map` (in place) |
| `clear` | `() -> None` | remove all |
| `copy` | `() -> Map` | shallow copy |

Magic: `m[k]` read/write, `delete m[k]`, `==`, `__map__`, `__boolean__` (non-empty = true), `__string__`/`__raw_string__`. (`Map` is not hashable.)

### 10.10 `File`
`io.File(path [, mode])` — `path` is **required** (no blank construction). Predefined streams: `io.stdin` / `io.stdout` / `io.stderr`.

| Method | Signature | Semantics |
|--------|-----------|-----------|
| `open` | `(path [, mode]) -> None` | open a file (failure → `ValueError`) |
| `read` | `([size]) -> String` | read all or `size` chars (not open / not readable → `ValueError`) |
| `readline` | `() -> String` | read one line |
| `readlines` | `() -> List[String]` | read all lines |
| `write` | `(data) -> Object` | write (not writable → `ValueError`) |
| `writelines` | `(lines) -> None` | write multiple lines (each must be `String`) |
| `close` | `() -> None` | close (idempotent if already closed) |
| `flush` | `() -> None` | flush (not open → `ValueError`) |
| `seek` | `(offset [, whence]) -> Integer` | seek (`whence ∈ {0,1,2}`; not seekable → `ValueError`) |
| `tell` | `() -> Integer` | current position |
| `readable` | `() -> Boolean` | is readable |
| `writable` | `() -> Boolean` | is writable |
| `seekable` | `() -> Boolean` | is seekable |
| `is_closed` | `() -> Boolean` | is closed |

Read-only attributes (not methods; access as `f.closed`): `closed`, `name`, `mode`.

### 10.11 `Object` (base class)
`pycp.Object` is the base class (like Python's `object`). `pycp.Object()` returns an instance; subclasses can call `super().__initialize__(self)` to invoke its empty initializer.

### 10.12 `Function` / `Class` / `Module`
- `Function`: a callable object (named or anonymous). Assignable to variables; closures capture outer locals.
- `Class`: a class object. `pycp.typeof(obj)` returns its class; `classtools.super()` returns the parent `Class`.
- `Module`: a loaded module object. Its `__name__` is the module name.

---

## 11. Command-line interface & AOT

```
pycp [options] <input_file>
```

| Option | Meaning |
|--------|---------|
| `-h, --help` | help |
| `-c, --compile` / `-b, --bytecode` | compile to `.cpycp` (do not run) |
| `-i, --interpret` | interpret (default) |
| `-o, --output <f>` | output path (`-c/-b` → `.cpycp`; otherwise project path for `-m aot`) |
| `-m, --module <name> ...` | treat `<name>` as a module folder and run it. **Everything after `-m` is passed verbatim to the package's `main(argv)`** — the host does not parse `--compile-*` / `--show-imports` etc. |
| `-d, --dump` | dump bytecode (constant pool / symbol table / code objects / instructions) |

Run: `pycp hello.pycp` · compile: `pycp -c hello.pycp -o hello.cpycp` · inspect: `pycp -d hello.pycp`.

### AOT (`.pycp` → compilable C++ project)
AOT is driven entirely by the standard-library `aot` module:

```bash
pycp -m aot hello.pycp                 # emit ./hello/ (default ./<entry basename>/)
pycp -m aot hello.pycp ./my_project   # explicit output dir
pycp -m aot hello.pycp --compile-runtime=static --compile-modules=static
pycp -m aot hello.pycp --compile-module:b_dep=static
pycp -m aot hello.pycp --show-imports
pycp -m aot --help                     # usage (exit 0)
pycp -m aot                           # no entry → prints usage, exit 2
```

The generated directory contains `__pycp_main.gen.cpp` (entry with `main`), one `<name>.gen.cpp` per imported module, and a `CMakeLists.txt`. Link against `PycpRuntime` (`-I dist/include -L dist/lib -lPycpRuntime -Wl,-rpath,'$ORIGIN/lib'`).

Link-kind options: `--compile-runtime=shared|static` (runtime lib), `--compile-modules=shared|static` (global kind of translated modules), `--compile-module:<name>=shared|static` (per-module override), `--show-imports` (print resolution manifest + per-module link-kind table).

---

## 12. Standard-library modules

### 12.1 `pycp`
Type constructors (0-arg = blank): `String`, `Integer`, `Boolean`, `Float`, `Decimal`, `List`, `FixedList`, `Map`, plus base class `Object`.

| Name | Signature | Semantics |
|------|-----------|-----------|
| `pycp.insp` | `(obj) -> List[String]` | all member names of `obj` (incl. methods) |
| `pycp.typeof` | `(obj) -> Class` | the class object of `obj` |
| `pycp.exec` | `(code [, globals]) -> Object` | execute a source-string / bytecode-module object |
| `pycp.argv` | `List[String]` | command-line arguments (see §8) |

Decorators (shared impl with `classtools`): `pycp.public` / `pycp.private` / `pycp.readonly` (usable as `@` or called directly).

### 12.2 `classtools`
| Name | Signature | Semantics |
|------|-----------|-----------|
| `classtools.super` | `() -> Class` | parent `Class` of the current method's class (call inside a method) |
| `classtools.public` / `private` / `readonly` | decorator | same as `pycp` equivalents |

### 12.3 `io`
| Name | Type / Signature | Semantics |
|------|------------------|-----------|
| `io.stdin` / `io.stdout` / `io.stderr` | `File` | standard streams |
| `io.print` | `(*args, sep=" ", end="\n", file=io.stdout, flush=False)` | print (args joined by `sep`; `file` is duck-typed and must have `write`) |
| `io.input` | `(prompt) -> String` | print prompt (no newline) then read a line |

### 12.4 `moduletools` (package-only)
| Name | Semantics |
|------|-----------|
| `moduletools.this()` | the current package module object |
| `moduletools.as_program()` / `as_library()` | declare role (program / library) |
| `moduletools.role()` / `is_program()` / `is_library()` | query role |
| `moduletools.Project()` | AOT config object (`set_executable_name`, `[name].static()`, `[name].shared()`) |

### 12.5 `maths` (numeric math)
Constants: `pi`, `e`, `tau`, `inf`, `nan` (all `Float`).

Functions (type-dispatched; raising `TypeError` when the numeric type does not apply):
- **Common (Integer / Float / Decimal)**: `abs`, `floor`, `ceil`, `round([ndigits])`, `pow`, `divmod`, `trunc`
- **Float / Decimal**: `fabs`, `copysign`, `fmod`, `modf`, `frexp`, `ldexp`, `isnan`, `isinf`, `isfinite`, `isclose`, `sqrt`, `exp`, `log([base])`, `log2`, `log10`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `hypot`, `degrees`, `radians`, `fsum`, `prod`, `is_integer`, `as_integer_ratio`
- **Integer only**: `bit_length`, `conjugate`, `gcd`, `lcm`, `isqrt`, `factorial`

Example: `import maths; maths.sqrt(2); maths.gcd(12, 18)`.

### 12.6 `json` (encode / decode, string-based)
Two string-level functions, mirroring Python's `json` module. Arrays parse to `List`; objects parse to `Map`. Numbers without `.`/`e` become `Integer`; numbers with `.`/`e`/`E` become `Decimal` (exact decimal). See `docs/JSON_MODULE.md` for the full contract.

| Name | Signature | Semantics |
|------|-----------|-----------|
| `json.load` | `(s: String) -> Object` | parse a JSON `String` into Pycp objects; invalid JSON raises `ValueError` (message includes the position) |
| `json.dump` | `(obj, indent=None, sort_keys=False, separators=None) -> String` | serialize a Pycp object to a JSON `String` |

- **`indent`**: `Integer` = that many spaces (clamped to 0–16); `String` `"tab"` or one containing `\t` = tab indentation; `None` = compact (one line). Pretty output inserts newlines + indentation between elements.
- **`sort_keys`**: `Boolean` — when true, `Map` keys are emitted sorted by their string form (stable; applies recursively).
- **`separators`**: a 2-element `List`/`FixedList` of `String`s `(item_sep, key_sep)` overriding the default separators. Default compact = `(", ", ": ")`; default pretty = `(",", ": ")`.
- **Type errors**: `dump` raises `TypeError` for a non-serializable type (e.g. `Class`, `File`, `Function`) or for a non-`String` `Map` key. `dump` raises `ValueError` for a non-finite `Float` (inf/nan).
- **String escaping**: `dump` always escapes non-ASCII as `\uXXXX` (Python's `ensure_ascii=True` default); `\uXXXX` is accepted on `load` and decoded to UTF-8.

Example: `import json; s = json.dump({"b": 2, "a": [1, 1.5, true, null]}, sort_keys=True, indent=2); obj = json.load(s)`.

---

## 13. Known limitations & gotchas (read before generating code)

1. **Comments are `//` and `/* */` only.** A bare `#` is an *unexpected character* error, not a comment. The project's own `README.md` examples using `#` comments are misleading.
2. **Strings are double-quoted only.** `'single'` is not a token.
3. **No logical operators.** `and`/`or`/`not`/`&&`/`||`/`!` do not exist. Combine boolean conditions by **nesting `if` blocks** (e.g. test `a` then inside test `b`). There is no boolean `not`; negate by comparing to `False` or using `==`.
4. **No `//` integer division** (it is a line comment). Use `maths.floor(a / b)`.
5. **No `else if`** — use `elif`. **No `while`** — use `repeat if (cond)`. **No `continue`** — only `break`.
6. **No `var`/`let`, no augmented assignment (`+=` etc.), no `++`/`--`, no type annotations, no ternary operator, no comparison chaining (`a < b < c`).**
7. **No `import a.b`.** `.` is member access; use `from a import b` or `import a` + `a.b`.
8. **No multi-line expression continuation** outside `() [] {}`. Inside brackets, newlines are ignored (Python-style).
9. **No multi-inheritance**, no `map`/set literals in syntax (build `Map` via `pycp.Map()` / dict methods), no call-side `*args`/`**kwargs` unpacking.
10. **`None`, numeric types, `Object`, `Function`, `Class`, `Module` have no ordinary methods** — only magic methods. All math is in `maths`.
11. A statement must be on its own line; the tokenizer emits `NEWLINE` as a statement separator. Scripts that need cross-line constructs must wrap them in brackets.
12. Boolean is a subtype of Integer at the C++ level (`dynamic_cast<Integer*>(bool)` is true) — but the type system's `check_type` is exact, so do not rely on `is_type("Integer")` matching `Boolean`.

---

## 14. Source map (for navigation)

| Topic | Primary file(s) |
|-------|-----------------|
| Lexer / tokens | `src/parser/PycpLexer.l` |
| Grammar / parser | `src/parser/PycpParser.y` |
| String API | `src/object/PycpString.cpp` (`String_method_table`) |
| List API | `src/object/PycpList.cpp` (`List_method_table`) |
| FixedList API | `src/object/PycpFixedList.cpp` (`FixedList_method_table`) |
| Map API | `src/object/PycpMap.cpp` (`Map_method_table`) |
| File API | `src/object/PycpFile.cpp` (`File_method_table`) |
| Numeric (Integer/Float/Decimal/Boolean) | `src/object/Pycp{Integer,Float,Decimal}.cpp` |
| Magic-method dispatch | `src/object/PycpMagic.cpp` (`magic_thunks`) |
| `pycp` module | `src/stdlib/pycp/src/PycpModule.cpp` |
| `classtools` module | `src/stdlib/classtools/src/classtools.cpp` |
| `io` module | `src/stdlib/io/src/io.cpp`, `PycpFile.cpp` |
| `moduletools` module | `src/stdlib/moduletools/src/moduletools.cpp` |
| `maths` module | `src/stdlib/maths/src/maths.cpp` |
| `json` module | `src/stdlib/json/src/json.cpp` |
| `aot` module | `src/stdlib/aot/package.mpycp` |
| Host CLI | `src/main/PycpMain.cpp` |
| Build / dist | `CMakeLists.txt`, `cmake/PycpDist.cmake` |

---

*This document reflects the language/runtime state as verified against source on 2026-09-27. Where it disagrees with `README.md` (notably comments, string quotes, and logical operators), this document is authoritative.*
