# Control Flow

Pycp uses `if` / `elif` / `else`, `repeat` loops, `for ... in`, and `break`. There is
no `while` keyword and no `continue`.

## if / elif / else

```pycp
if x > 0 {
    io.print("positive")
} elif x == 0 {
    io.print("zero")
} else {
    io.print("negative")
}
```

- `else if` is **not** valid; use `elif`.
- The condition is a comparison/boolean expression (no `and`/`or`; nest `if` instead).

## repeat loops

`repeat` is the only loop construct. It has several forms:

| Form | Semantics |
| --- | --- |
| `repeat if <cond> { }` | while: run while `cond` is true |
| `repeat <N> { }` | run `N` times (no loop variable) |
| `repeat <N> as <i> { }` | run `N` times, `i` = `0 .. N-1` |
| `repeat from <a> to <b> { }` | range over `[a, b]` (both endpoints) |
| `repeat from <a> to <b> as <i> { }` | range, `i` takes each endpoint value |
| `repeat from <a> to <b> by <s> { }` | range with explicit step `s` |
| `repeat from <a> to <b> by <s> as <i> { }` | range with step and loop variable |

```pycp
repeat if running {
    step()
}

repeat 3 as i {
    io.print(i)        // 0, 1, 2
}

repeat from 1 to 5 as n {
    io.print(n)        // 1, 2, 3, 4, 5
}

repeat from 0 to 10 by 2 as n {
    io.print(n)        // 0, 2, 4, 6, 8, 10
}
```

- The iteration variable (`as <i>`) is bound to the current value and may be used in
  the body.
- `to` is inclusive: the `to` value is the last element of the range.
- `by` may be negative; the loop direction is derived from the sign of the step.
- The bare infinite form `repeat { }` has been removed; use `repeat if true { }`.
- `repeat` is the structured-loop keyword; there is no separate `while`.

## for ... in

Iterate over a container or other iterable:

```pycp
for item in my_list {
    io.print(item)
}
```

This uses the `__iterator__` / `__next__` protocol — see
[magic-methods.md](magic-methods.md).

## break

`break` exits the innermost loop:

```pycp
repeat if true {
    if done {
        break
    }
}
```

`continue` is **not** supported; restructure with `if`.

## Quirks

- Statements are separated by newlines; a block body is wrapped in `{}`.
- Inside `() [] {}`, newlines are ignored (implicit line continuation), so a loop
  header may span lines only within brackets.
