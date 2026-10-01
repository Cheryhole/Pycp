# Literals

Literals produce constant values of the built-in types.

## Strings (double-quoted only)

Strings are delimited by double quotes. Single-quoted strings are **not** supported.

```pycp
let_greeting = "hello"
path = "C:/temp/out.txt"
```

### Escape sequences

| Escape | Meaning |
| --- | --- |
| `\n` | newline |
| `\t` | tab |
| `\r` | carriage return |
| `\0` | null byte |
| `\\` | backslash |
| `\"` | double quote |
| `\'` | single quote |
| `\a` | bell |
| `\b` | backspace |
| `\f` | form feed |
| `\v` | vertical tab |
| `\xHH` | byte with two-digit hex value `HH` |
| `\uXXXX` | Unicode code point `XXXX` (emitted as UTF-8) |

Unknown escape sequences are kept verbatim (backslash + character) without error.

## Numbers

| Form | Example | Type |
| --- | --- | --- |
| Integer | `42` | `Integer` |
| Hexadecimal | `0xFF` | `Integer` (decimal value) |
| Float | `3.14` | `Float` |
| Float suffix | `3.14f` / `2f` | `Float` |
| Decimal suffix | `0.10d` / `2d` | `Decimal` (exact) |

- A decimal literal **without** a suffix is a `Float`, not a `Decimal`. To get exact
  decimals, always use the `d` / `D` suffix.
- Hexadecimal literals out of range are a lexer error.
- `Decimal` is constructed from the exact source text (e.g. `0.10d` keeps two decimal
  places), unlike `Float` which uses binary floating point.

## Boolean & None literals

`True` and `False` are boolean literals (type `Boolean`, a subtype of `Integer`).
`None` is the null literal. These are keywords, not identifiers.

```pycp
flag = True
empty = None
```

## Container literals

List, FixedList, and Map literals are described in
[containers.md](containers.md) because they involve type-level behavior rather than
pure syntax.
