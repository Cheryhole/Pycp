# Pycp Language Documentation

This directory contains one document per Pycp language feature. Each file describes
the syntax, semantics, and notable quirks of a single feature, verified against the
lexer (`src/parser/PycpLexer.l`), parser (`src/parser/PycpParser.y`), and runtime.

## Feature index

| Feature | Document |
| --- | --- |
| Comments | [comments.md](comments.md) |
| Literals (strings, numbers, None/True/False) | [literals.md](literals.md) |
| Identifiers & keywords | [identifiers-and-keywords.md](identifiers-and-keywords.md) |
| Variables & assignment | [variables-and-assignment.md](variables-and-assignment.md) |
| Operators & precedence | [operators.md](operators.md) |
| Control flow (if/elif/else, repeat, for, break) | [control-flow.md](control-flow.md) |
| Functions (def, anonymous, closures) | [functions.md](functions.md) |
| Parameters (*args, **kwargs, defaults, keyword-only) | [parameters.md](parameters.md) |
| Decorators & visibility (@private/@public/@readonly) | [decorators.md](decorators.md) |
| Classes & inheritance | [classes.md](classes.md) |
| Magic methods (operator/operator overloading) | [magic-methods.md](magic-methods.md) |
| Containers (List, FixedList, Map) | [containers.md](containers.md) |
| Modules & packages (import, from, package.mpycp) | [modules.md](modules.md) |
| Preprocessor (# directives) | [preprocessor.md](preprocessor.md) |
| Standard library (no built-in functions/objects) | [builtins.md](builtins.md) |

## Quick orientation

Pycp is a Python-like language implemented in C++17. It does **not** aim to be a
Python superset: several Python constructs are intentionally absent. The most
important differences for readers of these docs:

- No logical operators `and` / `or` / `not` and no `&&` / `||` / `!`; boolean
  conditions are expressed with comparison expressions and nested `if`.
- No augmented assignment (`+=`, `++`), no `while` (use `repeat if`), no `continue`
  (only `break`), no ternary expression, no comparison chaining, no type hints.
- Statements are separated by **newlines**; parentheses `() [] {}` allow implicit
  line continuation inside them.
- `self` and `this` are **not** keywords: `self` is just the conventional first
  parameter name of a method, and `this` is the module function `moduletools.this`.

See each feature document for details.
