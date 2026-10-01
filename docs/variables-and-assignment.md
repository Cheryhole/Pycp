# Variables & Assignment

## Assignment

Bind a value to a name with `=`. The target may be a plain identifier or an
attribute/index access.

```pycp
x = 10
name = "pycp"
obj.field = 5
items[0] = "first"
```

- There is **no** `var` / `let` keyword. Assignment both declares and (re)binds.
- Assignment is a statement, separated from other statements by a newline.
- Multiple assignment / tuple unpacking is **not** supported in the core language.

## Augmented assignment

Augmented assignment operators (`+=`, `-=`, `*=`, `++`, etc.) are **not** supported.
Rewrite as a normal assignment:

```pycp
// instead of: i += 1
i = i + 1
```

## Deleting a binding

`delete` removes a name, attribute, or element:

```pycp
delete x
delete obj.field
delete items[0]
```

Deleting a name that does not exist, or an attribute/element that cannot be removed,
is an error.

## Quirks

- A name is visible from the point of its first assignment onward within its scope.
- Read-before-assign raises a "not defined" error.
- Assignment to an attribute or index routes through the `__set_attribute__` /
  `__set_item__` magic methods when defined — see [magic-methods.md](magic-methods.md).
- A binding marked `@readonly` (or created via `pycp.readonly`) cannot be reassigned
  — see [decorators.md](decorators.md).
