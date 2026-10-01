# Identifiers & Keywords

## Identifiers

An identifier is `[a-zA-Z_][a-zA-Z0-9_]*`. Identifiers name variables, functions,
classes, modules, and parameters.

```pycp
count = 0
my_value_2 = "x"
_func = func() {}
```

## Keywords (reserved words)

The following 20 tokens are reserved and cannot be used as identifiers:

```
func  return  if  elif  else  None  True  False
import  from  as  class  inherits  repeat  to  break
delete  by  for  in
```

## Not keywords (common pitfalls)

- **`self`** is not a keyword. It is merely the conventional name for the first
  parameter of a method. You may name it anything.
- **`this`** is not a keyword. It is the module function `moduletools.this`, which
  returns the current module object.
- **`super`** is not a keyword. It is the `classtools.super` function used for
  superclass method dispatch.
- **`private` / `public` / `readonly`** are not keywords. They are decorator
  functions (`pycp.private` / `pycp.public` / `pycp.readonly`, also exported by
  `classtools`). Written as `@private` etc. they apply a binding-level visibility
  marker — see [decorators.md](decorators.md).
- **`and` / `or` / `not`** are not language keywords. `not` exists only inside
  preprocessor `#if` conditions.

## Quirks

- Comparison and arithmetic operators are not identifiers; they are separate tokens.
- Module names in `import` / `from` are single identifiers (no dot syntax); use
  packages for hierarchical modules — see [modules.md](modules.md).
