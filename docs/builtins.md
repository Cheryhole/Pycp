# Standard Library (no built-in functions or objects)

Pycp has **no built-in functions and no built-in objects**. Nothing exists in the
global scope without an `import`. In particular, `print` is **not** built-in: it lives
in the `io` module as `io.print` and must be imported first.

```pycp
import io
io.print("hello", "world")
```

or, to use the short name:

```pycp
from io import print
print("hello", "world")
```

All functionality is provided by standard-library modules you import. There is no
global `len`, `type`, `range`, etc. — use the corresponding module functions.

## Standard library modules

| Module | Notable functions |
| --- | --- |
| `io` | `print`, `input`, `File`, `io.stdout`, `io.stdin`, `io.stderr` |
| `pycp` | `insp`, `typeof`, `exec`, and visibility decorators `private`/`public`/`readonly` |
| `classtools` | `super`, and visibility decorators `private`/`public`/`readonly` |
| `moduletools` | `this`, `as_program`, `as_library`, `role`, `is_program`, `is_library` |
| `maths` | `abs`, `floor`, `ceil`, `round`, `pow`, `divmod`, `sqrt`, `exp`, `log`, `sin`, `cos`, `gcd`, `lcm`, `isqrt`, `factorial`, ... |
| `filesystem` | `mkdir`, `listdir`, `join`, `exists`, `is_file`, `is_dir`, `getenv`, `cwd`, `exe_dir`, `stdlib_dir` |
| `json` | `load`, `dump` |
| `bytecode` | `compile`, `dump`, `eval_manifest`, `sanitize_module_name`, ... |
| `ast` | `parse`, `dump`, `type_of` |

### `io.print`

```pycp
import io
io.print("a", "b", sep=", ", end="!\n", file=io.stdout, flush=False)
```

- `sep` (default `" "`) and `end` (default `"\n"`) are keyword-only.
- `file` is duck-typed: any object with a `write` method works.
- `flush` is coerced via `__boolean__`.

### `pycp` introspection

```pycp
import pycp
pycp.insp(obj)      // list attribute names
pycp.typeof(obj)    // type name
```

### `maths`

Numeric math is intentionally **not** on the core numeric types; use `maths`:

```pycp
import maths
n = maths.floor(3.9)   // 3
r = maths.sqrt(2)
```

## Quirks

- `this`, `super`, `readonly`, `private`, `public` are **module functions /
  decorators**, not language keywords and not built-ins.
- Numeric operators are defined by magic methods (see
  [magic-methods.md](magic-methods.md)); arithmetic helpers (floor, sqrt, etc.) come
  from `maths`.
- Because there are no built-ins, every program begins by importing what it needs.
