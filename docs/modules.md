# Modules & Packages

## import

Import a module by a single identifier. Optionally rename it with `as`.

```pycp
import io
import json as j
```

- Module names are single identifiers (no dot syntax). `import a.b` is **not**
  supported; use packages instead.
- After import, the module's exported names are accessed via the module object
  (e.g. `io.print`, `json.load`).

## from ... import

Import specific names from a module into the current scope:

```pycp
from io import print, input
from json import load, dump
```

Multiple names are comma-separated, with an optional trailing comma.

## Packages (module folders)

A package is a directory containing a manifest file named `package.mpycp` plus
submodule source files (`.pycp`). The manifest is itself a normal Pycp source file.

- Submodules are addressed by a dotted qualified name, e.g. `pkg.sub`. The dot is the
  module-name separator; internally it maps to directory levels.
- Run a package as a program: `pycp -m <dir>`. Import it as a library: `import <pkg>`.
  The intended role (program vs. library) can be declared in the manifest via
  `moduletools.as_program()` / `moduletools.as_library()`; if undeclared, `-m` treats
  it as a program, `import` as a library, and AOT as a library.
- A package's program entry point is a top-level `func main(argv)` in the manifest;
  its return value is converted via `__integer__()` and used as the process exit code
  (`None` → `0`).

## Role conflicts

- A module declared as a **program** cannot be `import`ed (ImportError).
- A module declared as a **library** cannot be executed with `-m` (RuntimeError).
- An AOT library that sets an executable name is an error.

## Lookup order

When a name is imported, the runtime searches (first match wins):

1. Process-wide module cache.
2. Process symbols (`PycpModule_<name>` via `dlsym`) plus the AOT static registry.
3. Current working directory (`.pycp` source preferred over same-named `.so`).
4. The script's directory (skipped if it is `"."`).
5. The executable's `stdlib/` directory (source preferred).

## Quirks

- Source `.pycp` is preferred over a same-named `.so` at each layer, so editing source
  takes effect without rebuilding the extension.
- A package's `__get_attribute__` / `__set_attribute__` / `__string__` hooks apply
  only to the package module.
