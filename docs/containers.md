# Containers

Pycp has three built-in container types: `List`, `FixedList`, and `Map`.

## List

A `List` is a mutable, growable sequence. Literal syntax uses square brackets.

```pycp
nums = [1, 2, 3]
empty = []
nums.append(4)
nums[0] = 10
first = nums[0]        // positive index
last = nums[-1]        // negative index counts from the end
size = nums.length()
```

- Indexing supports negative indices.
- Mutation (`append`, element assignment) routes through `__set_item__` /
  `__get_item__` when defined.

## FixedList

A `FixedList` is an **immutable, fixed-length** sequence (the analog of Python's
`tuple`). It is produced by:

- variadic `*args` collection in function calls — see [parameters.md](parameters.md);
- explicit construction `FixedList(items...)`.

```pycp
pair = FixedList(1, 2)
a = pair[0]
combined = pair + FixedList(3)   // returns a new FixedList
```

- It does **not** support `append` or element assignment.
- It supports read-only indexing (including negative), concatenation with `+`, and
  `__hash__` (so it can be used as a `Map` key).
- Elements are not incref'd on construction; treat it as a value type.

## Map

A `Map` is a mutable key/value mapping (the analog of Python's `dict`). Literal syntax
uses braces.

```pycp
m = {"a": 1, "b": 2}
empty = {}
m["c"] = 3
val = m["a"]
has = m.contains("a")
keys = m.keys()
```

- Keys must be hashable (implement `__hash__`); `FixedList` is hashable, `List` is not.
- `Map` conversion aligns with the `Map(...)` constructor and the `__map__` magic
  method.

## Membership (`in`)

Test membership with `in`:

```pycp
import io
import pycp

io.print(1 in [1, 2, 3])                  // True   (List)
io.print(1 in pycp.FixedList([1, 2, 3]))  // True   (FixedList)
io.print("ell" in "hello")                // True   (String substring)
m = {"a": 1, "b": 2}
io.print("a" in m.keys())                 // True   (Map key)
io.print(1 in m.values())                 // True   (Map value)
```

- `List` / `FixedList` scan their elements with `==` (identity for nested containers).
- `String` uses substring semantics; a non-String left operand raises `TypeError`.
- `Map` itself does **not** support `in` (it is not iterable). Test keys/values via
  `m.keys()` (returns a `FixedList`) or `m.values()` (returns a `List`).
- A user class may define `__contains__`; otherwise an iterable object is scanned
  element-wise. A non-iterable object without `__contains__` raises `TypeError`.

See [operators.md](operators.md) and [magic-methods.md](magic-methods.md).

## Container literals & line continuation

Inside `[]` and `{}`, newlines are ignored, so multi-line literals are allowed:

```pycp
matrix = [
    1, 2, 3,
    4, 5, 6,
]
```

A trailing comma is permitted in list and map literals.

## Quirks

- There is no tuple-vs-list distinction in mutation expectations at the type level;
  use `FixedList` when you need immutability.
- `Map` literal syntax `{}` is distinct from a code block `{}`; context decides which
  is meant (expression vs. statement position).
- Comprehensions are **not** part of the core language.
