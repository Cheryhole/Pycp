# Functions

## Definition

Define a named function with `func`:

```pycp
func greet(name) {
    io.print("hello, " + name)
}
```

Functions may have parameters, defaults, variadic args, and keyword-only args — see
[parameters.md](parameters.md).

## Return

`return` exits the function and yields a value. Omitting `return` (or `return`
alone) yields `None`.

```pycp
func square(x) {
    return x * x
}

func noop() {
    // returns None
}
```

## Anonymous functions & closures

A function literal (no name) can be assigned or passed directly. It closes over the
enclosing scope:

```pycp
adder = func(n) {
    return func(x) { return x + n }
}
add5 = adder(5)
io.print(add5(10))   // 15
```

Anonymous functions are first-class values and can be stored in variables, put in
containers, returned, and called.

## Calling

```pycp
greet("world")
value = square(3)
result = apply(func(x) { return x * 2 }, 4)
```

Positional and keyword arguments follow Python-like rules — see
[parameters.md](parameters.md). A trailing comma in the argument list is allowed.

## Callable objects

A user type may become callable by defining the `__call__` magic method — see
[magic-methods.md](magic-methods.md).

## Decorators

Functions (and classes / variables) may be wrapped with decorators — see
[decorators.md](decorators.md).

## Quirks

- The function body is a block; statements inside are newline-separated.
- There is no `lambda` keyword; use `func(...) { ... }` instead.
- Default argument values are evaluated once, at function definition time.
