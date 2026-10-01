# Parameters

Function parameters follow Python-like rules, verified against
`src/parser/PycpParser.y`.

## Forms

```pycp
func f(pos,             // positional / keyword
       opt = 10,        // default value
       *args,           // variadic positional
       kwonly,          // keyword-only (after bare *)
       **kwargs) {      // variadic keyword
    // ...
}
```

| Form | Syntax | Collected into |
| --- | --- | --- |
| Positional / keyword | `name` | a single parameter |
| Default | `name = value` | single parameter, value evaluated once at def time |
| Variadic positional | `*args` | a `FixedList` of extra positional args |
| Keyword-only | name after a bare `*` | single parameter, no positional binding |
| Variadic keyword | `**kwargs` | a `Map` of extra keyword args |
| Bare `*` | `*` | separator marking subsequent params keyword-only |

## Keyword-only separator

A bare `*` marks all following parameters as keyword-only:

```pycp
func f(a, *, b, c = 2) {
    // a may be positional; b and c must be passed by keyword
}
f(1, b=3)
```

## Calling with keywords

Keyword arguments use `name = value` at the call site; the name must be a plain
identifier:

```pycp
f(1, opt=20, kwonly=5, extra=9)
```

- A positional argument may **not** follow a keyword argument.
- A keyword name may **not** be repeated.

## Validation rules (parser errors)

- `**kwargs` must be the last parameter.
- `*args` and the bare `*` may each appear at most once.
- A bare `*` must be followed by at least one keyword-only parameter.
- A parameter without a default may not follow one with a default.
- Parameter names must be unique.

## Quirks

- `*args` and `**kwargs` collect into a `FixedList` and `Map` respectively (see
  [containers.md](containers.md)).
- Trailing commas are allowed in call argument lists and in parameter lists.
- Default values are computed once, when the function is defined (not per call).
