# Magic Methods

Magic methods customize operator and protocol behavior for user-defined types. The
authoritative list is `magic_thunks()` in `src/object/PycpMagic.cpp` (29 entries).

## Arithmetic & comparison (1 parameter `value`)

| Method | Trigger |
| --- | --- |
| `__addition__` | `self + value` |
| `__subtraction__` | `self - value` |
| `__multiplication__` | `self * value` |
| `__division__` | `self / value` |
| `__power__` | `self ** value` |
| `__less_than__` | `self < value` |
| `__less_equal__` | `self <= value` |
| `__equal__` | `self == value` |
| `__not_equal__` | `self != value` |
| `__greater_than__` | `self > value` |
| `__greater_equal__` | `self >= value` |

## Indexing & attributes

| Method | Trigger | Parameters |
| --- | --- | --- |
| `__get_item__` | `self[key]` | `value` |
| `__set_item__` | `self[key] = value` | `key`, `value` |
| `__delete_item__` | `del self[key]` | `value` |
| `__get_attribute__` | `self.attr` | `value` |
| `__set_attribute__` | `self.attr = value` | `key`, `value` |
| `__delete_attribute__` | `del self.attr` | `value` |

## Conversion & introspection (0 parameters)

| Method | Trigger / meaning |
| --- | --- |
| `__integer__` | convert to `Integer` |
| `__string__` | convert to `String` (used by `print`) |
| `__raw_string__` | raw/repr form (used when rendering containers) |
| `__boolean__` | truthiness test |
| `__list__` | convert to `List` |
| `__map__` | convert to `Map` |
| `__hash__` | hash value (for map keys) |
| `__iterator__` | return an iterator |
| `__next__` | return the next element (iteration protocol) |
| `__negation__` | `-self` |
| `__inspect__` | list attribute names (dir-like) |
| `__delete__` | object deletion hook |

## Special methods (not in the thunk table)

These are dispatched directly by the VM/codegen:

- `__initialize__(self, ...)` — constructor, called on instantiation.
- `__call__(self, ...)` — makes an instance callable.
- `__name__` / `__class__` — pseudo-attributes returning the type name / class.

## Example

```pycp
class Vector {
    func __initialize__(self, x, y) {
        self.x = x
        self.y = y
    }
    func __addition__(self, other) {
        return Vector(self.x + other.x, self.y + other.y)
    }
    func __string__(self) {
        return "(" + self.x + ", " + self.y + ")"
    }
}

v = Vector(1, 2) + Vector(3, 4)
io.print(v)   // (4, 6)
```

## Quirks

- Equality (`==` / `!=`) should return a `Boolean`; ordering comparisons return an
  `Integer` `1` / `0` — see [operators.md](operators.md).
- `__raw_string__` is used when a value appears inside a container's rendered form,
  while `__string__` is used for `print` of the value directly.
- Numeric types do **not** define arithmetic magic methods of their own; math lives in
  the `maths` standard library instead.
