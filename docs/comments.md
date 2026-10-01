# Comments

Pycp supports two comment styles. A third token, `#`, is **not** a comment — it is
reserved for the preprocessor (see [preprocessor.md](preprocessor.md)).

## Single-line comment `//`

Everything from `//` to the end of the line is ignored.

```pycp
func add(a, b) {
    // returns the sum of a and b
    return a + b
}
```

## Block comment `/* ... */`

Enclosed text is ignored and may span multiple lines. Unterminated block comments
are a lexer error (`Unterminated comment.`).

```pycp
/*
 * This is a multi-line comment.
 * It can contain * and / freely.
 */
func f() {}
```

## `#` is NOT a comment

A bare `#` at the start of a line is a preprocessor directive, not a comment. A lone
`#` with no recognized directive keyword is a lexer error
(`Unexpected character`). Within ordinary code you may encounter line directives
such as `#lineno` / `#filename`, which the preprocessor expands to the current
physical line number / source path.

## Quirks

- Comments are skipped by the preprocessor before expansion, so macros are **not**
  expanded inside `//` or `/* */` text.
- Inside parentheses `() [] {}`, newlines are ignored (implicit line continuation),
  so a `//` comment still terminates at the end of its physical line even inside a
  bracketed expression.
