# Operators & Precedence

Pycp has a small operator set. There are **no** logical operators, no modulo, and no
bitwise operators.

## Arithmetic

| Operator | Meaning | Example |
| --- | --- | --- |
| `+` | addition | `a + b` |
| `-` | subtraction | `a - b` |
| `*` | multiplication | `a * b` |
| `/` | division | `a / b` |
| `**` | power (right-associative) | `a ** b` |
| unary `-` | negation | `-a` |

Behavior is defined by the operands' magic methods; see
[magic-methods.md](magic-methods.md). For built-in numbers, `/` is true division.

```pycp
area = 3.14 * r ** 2
neg = -x
```

## Comparison

| Operator | Meaning |
| --- | --- |
| `==` | equal |
| `!=` | not equal |
| `<` | less than |
| `<=` | less than or equal |
| `>` | greater than |
| `>=` | greater than or equal |

```pycp
if a == b {
    // ...
}
```

### Return types (Pycp-specific)

- `==` / `!=` return a `Boolean`.
- `<` `<=` `>` `>=` return an `Integer` of `1` or `0` (not a `Boolean`). This differs
  from Python, where ordering comparisons also return `bool`.

## Logical negation (`not` / `!`)

| Operator | Meaning | Example |
| --- | --- | --- |
| `not` | logical NOT (prefix) | `not x` |
| `!` | alias of `not` | `!x` |

Returns a `Boolean`. Precedence follows Python: `not` binds **looser** than
comparison, so `not a == b` parses as `not (a == b)` and `not x in y` parses as
`not (x in y)`. Both forms may be chained (`!!x`, `not not x`).

```pycp
if not done {
    // ...
}
```

Truthiness follows `__boolean__` (see [magic-methods.md](magic-methods.md)): `not 0`,
`not None`, `not ""` and `not []` are all `True`.

## Membership

| Operator | Meaning | Example |
| --- | --- | --- |
| `in` | membership test | `value in container` |

```pycp
if x in items {
    // ...
}
```

`x in obj` calls `obj.__contains__(x)`. If the object does not define
`__contains__` but is iterable (has `__iterator__`), it is iterated and each element
compared with `==` until a match is found. If neither applies, a `TypeError` is
raised. The result is a `Boolean`.

Supported right-hand operands: `List`, `FixedList`, `String` (substring semantics),
the results of `Map.keys()` / `Map.values()`, and any user class defining
`__contains__`. A bare `Map` is **not** iterable and does not support `in` — test
`map.keys()` / `map.values()` instead. See [containers.md](containers.md) and
[magic-methods.md](magic-methods.md).

## Member access (`of`)

`a of b` is reverse member access: it is equivalent to `b.a`. The left operand must
be a bare identifier (the attribute name, not a value), and the right operand is any
expression. `of` is **right-associative**: `a of b of c` parses as `a of (b of c)`,
i.e. `(c.b).a`.

```pycp
io.print(name of user)          // user.name
io.print(val of inner of root)  // (root.inner).val
io.print((getVal of obj)())     // obj.getVal()
```

Like `.`, `of` binds tightest (postfix); the right operand may carry a full postfix
chain (`a of b.c`, `a of obj[0]`, `a of obj.method()`).

## Precedence (low → high)

1. Logical negation (prefix, right-assoc): `not` `!`
2. Comparison / membership (non-associative): `<` `>` `<=` `>=` `==` `!=` `in`
3. Addition / subtraction (left-assoc): `+` `-`
4. Multiplication / division (left-assoc): `*` `/`
5. Power (right-assoc): `**`
6. Unary minus (right-assoc): `-x`
7. Member access / call / index (highest): `.` `of` `()` `[]`

Because power binds tighter than unary minus, `-2 ** 2` evaluates as `-(2 ** 2)`.

Comparison binds looser than arithmetic, so `a + 1 > b * 2` parses as
`(a + 1) > (b * 2)`.

Because `not` is looser than comparison, `not 2 == 1` parses as `not (2 == 1)`.

## Not supported

- Logical: `and` `or` (and `&&` `||`). Compose conditions with nested `if` or
  methods instead.
- Modulo: `%`.
- Bitwise: `&` `|` `^` `~` `<<` `>>`.
- Augmented assignment: `+=` `-=` etc. (see [variables-and-assignment.md](variables-and-assignment.md)).
- Comparison chaining (`a < b < c`) and the ternary `a if c else b`.
- `not in` — write `not (a in b)` instead.
