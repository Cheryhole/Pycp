# Pycp `json` module

> Authoritative spec for the standard-library `json` module (`src/stdlib/json/`).
> Audience: models / tooling that read or generate Pycp code using `json`, and
> maintainers of the module. Indexed from `docs/LANGUAGE_GUIDE.md` §12.6.

## 1. Purpose

Encode (serialize) Pycp objects to a JSON `String` and decode (parse) a JSON
`String` back into Pycp objects. The API mirrors Python's `json` module but is
**string-based**: there are no file-handle variants. `load` takes a `String`
and returns an object; `dump` takes an object and returns a `String`.

```py
import json
obj = json.load("{\"a\": 1, \"b\": [2, 3.5]}")
s   = json.dump(obj, indent=2, sort_keys=True)
```

## 2. Functions

### `json.load(s: String) -> Object`
Parse a JSON text `String`. Raises `ValueError` (message includes `at position N`)
on any malformed input (unterminated string, bad literal, trailing data, etc.).
The argument must be a `String`, otherwise `TypeError`.

### `json.dump(obj, indent=None, sort_keys=False, separators=None) -> String`
Serialize `obj` to a JSON `String`. Keyword-only flags:

| Flag | Type | Meaning |
|------|------|---------|
| `indent` | `Integer` / `String` / `None` | `Integer` = that many **space** characters per level (clamped 0–16); `String` equal to `"tab"` or containing `\t` = tab per level; any other `String` is used verbatim as the indent unit; `None` = compact single line. |
| `sort_keys` | `Boolean` | When true, `Map` keys are emitted sorted by their string form (stable; applies recursively to nested objects). |
| `separators` | `List`/`FixedList` of 2 `String`s | `(item_sep, key_sep)` override. Default compact = `(", ", ": ")`; default pretty = `(",", ": ")`. |

Errors:
- `TypeError` for a non-serializable type (e.g. `Class`, `File`, `Function`, `Module`) or for a non-`String` `Map` key (message: `json.dump() cannot serialize '<type>' object` / `json.dump(): keys must be str`).
- `ValueError` for a non-finite `Float` (inf/nan).

## 3. Type mapping (round-trip)

| JSON | Pycp (load) | Pycp (dump) | JSON |
|------|-------------|-------------|------|
| object `{...}` | `Map` | `Map` | object |
| array `[...]` | `List` | `List` or `FixedList` | array |
| string `"..."` | `String` | `String` | string |
| integer `123` | `Integer` | `Integer` | integer |
| number `1.5` / `1e3` | `Decimal` | `Decimal` | number |
| `true` / `false` | `Boolean` | `Boolean` | `true` / `false` |
| `null` | `None` | `None` | `null` |

Notes:
- **Numbers**: an integer literal (no `.`, `e`, `E`) becomes `Integer`; a literal
  with a fractional part or exponent becomes `Decimal` (exact decimal, backed by
  `mpdecimal`). JSON `1e3` → `Decimal` → serialized as `1500` (the sci-notation
  internal form is normalized to plain decimal so the output stays readable and
  valid JSON). This is the deliberate choice for the project (financial/precise
  decimals); Python's `json` would use `float` here.
- **Boolean precedes Integer** in the `dump` dispatch, because `Boolean` is a C++
  subclass of `Integer` — otherwise `true` would serialize as `1`.
- **Strings**: `dump` escapes `"`, `\`, control characters, and **all non-ASCII
  as `\uXXXX`** (Python's `ensure_ascii=True` default; not currently exposed as a
  flag). `\uXXXX` (including surrogate pairs) is decoded back to UTF-8 on `load`.
- **Arrays** decode to the mutable `List`. Both `List` and `FixedList` serialize
  to a JSON array. `Map` key order in compact output follows the `Map`'s internal
  iteration order; use `sort_keys=True` for deterministic output.

## 4. Examples

```py
import json

// decode
v = json.load("[1, 2.5, true, null, \"hi\"]")   // List[Integer, Decimal, Boolean, None, String]

// encode compact
json.dump({"a": 1, "b": 2})                       // "{"a": 1, "b": 2}"

// encode pretty
json.dump([1, 2, 3], indent=2)
// [
//   1,
//   2,
//   3
// ]

// sorted keys
json.dump({"b": 2, "a": 1}, sort_keys=True)       // "{"a": 1, "b": 2}"

// custom separators
json.dump({"a": 1}, separators=[",", ":"])        // "{"a":1}"

// error: non-string key
json.dump({1: 2})                                  // TypeError: json.dump(): keys must be str
```

## 5. Implementation notes

- Single recursive-descent parser + single recursive pre-order serializer, both in
  `src/stdlib/json/src/json.cpp`. The parser is a one-pass cursor over the input
  `String` (no intermediate token container). The serializer builds a `std::string`
  with `reserve` and recurses over the object tree.
- Reference counting: every parsed child is an Owned value (factory refcount = 1);
  `Map::put_item` / `List::append` Incref internally, so the local reference is
  `Decref`'d after insertion. Returned top-level value is Owned.
- The module is a native C++ extension (dynamic `json.so` + static
  `PycpExt_json.a`), collected into `dist/stdlib/` / `dist/lib/` by `pycp-dist`,
  and linkable for AOT `--static`.

## 6. Source map
| Topic | File |
|-------|------|
| Module entry / registration | `src/stdlib/json/src/json.cpp` (`make_json_module`, `PYCP_EXPORT_MODULE(json)`) |
| Parser | `src/stdlib/json/src/json.cpp` (`JsonParser`) |
| Serializer + escaping | `src/stdlib/json/src/json.cpp` (`serialize`, `json_escape_string`, `normalize_decimal_string`) |
| Build | `src/stdlib/json/CMakeLists.txt`, registered in `src/stdlib/CMakeLists.txt` |
| Tests | `tests/json/run.sh` (+ `json_basic.pycp`, `err_invalid.pycp`, `err_type.pycp`, `err_key.pycp`) |
