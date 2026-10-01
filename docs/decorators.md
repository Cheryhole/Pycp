# Decorators & Visibility

## Decorators

A decorator is a function applied to a definition (function, class, or variable).
Write it with `@` above the target. Decorators may be stacked; the one closest to the
target is applied **first**.

```pycp
@trace
@cache
func compute(x) {
    // equivalent to: compute = cache(trace(compute))
    return x * x
}
```

- A decorator expression may be a single identifier (`@trace`) or a dotted path
  (`@classtools.private`).
- Stacking order: `@a @b target` means `a(b(target))`.
- Decorators also work on class members and on variable bindings.

## Visibility markers

`@private`, `@public`, and `@readonly` are **not** language keywords — they are
decorator functions (from `pycp` and `classtools`). When written with `@` on a
top-level binding, the compiler emits a `MARK_BINDING` instruction that records the
visibility on the name.

```pycp
@readonly
const_pi = 3.14159

@private
secret = "hidden"

@public
api = make_api()
```

| Marker | Effect |
| --- | --- |
| `@readonly` | the binding / object cannot be reassigned |
| `@private` | restricts cross-module attribute access |
| `@public` | explicitly opens a binding for cross-module access |

They can also be applied by calling the function directly, e.g. `pycp.readonly(x)`,
when the single argument is a bare top-level identifier.

## Quirks

- Visibility is a binding-level (and, for objects, attribute-level) feature, not a
  type system. It is enforced by the runtime, not the type checker.
- Because these are ordinary functions, you can define your own decorators that take
  and return the target object.
- `@readonly` on a binding prevents later `=` reassignment (see
  [variables-and-assignment.md](variables-and-assignment.md)).
