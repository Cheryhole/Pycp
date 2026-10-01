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

## Precedence (low → high)

1. Comparison / membership (non-associative): `<` `>` `<=` `>=` `==` `!=` `in`
2. Addition / subtraction (left-assoc): `+` `-`
3. Multiplication / division (left-assoc): `*` `/`
4. Power (right-assoc): `**`
5. Unary minus (right-assoc): `-x`

Because power binds tighter than unary minus, `-2 ** 2` evaluates as `-(2 ** 2)`.

Comparison binds looser than arithmetic, so `a + 1 > b * 2` parses as
`(a + 1) > (b * 2)`.

## Not supported

- Logical: `and` `or` `not` (and `&&` `||` `!`). Compose conditions with nested `if`
  or methods instead.
- Modulo: `%`.
- Bitwise: `&` `|` `^` `~` `<<` `>>`.
- Augmented assignment: `+=` `-=` etc. (see [variables-and-assignment.md](variables-and-assignment.md)).
- Comparison chaining (`a < b < c`) and the ternary `a if c else b`.
- `not in` (the `not` operator is not implemented — see [features.md](../features.md)).
