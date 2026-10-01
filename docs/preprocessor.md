# Preprocessor

The preprocessor runs before the main lexer/parser. Directives begin with `#` at the
start of a line. (A lone `#` is **not** a comment — see
[comments.md](comments.md).)

## Text replacement & macros

| Directive | Syntax | Effect |
| --- | --- | --- |
| `# replace` | `# replace NAME with VALUE` | textual substitution of `NAME` with the rest of the line (C `#define`-like) |
| `# stop replacing` | `# stop replacing NAME` | stop substituting `NAME` (idempotent) |
| `# define` | `# define NAME [VALUE]` | record a macro definition (for conditionals; value not text-substituted) |
| `# undefine` | `# undefine NAME` | remove a macro definition (idempotent) |

```pycp
# replace MAX with 100
# define DEBUG

x = MAX          // becomes x = 100
```

## File inclusion

| Directive | Syntax | Effect |
| --- | --- | --- |
| `# expand` | `# expand NAME` | `NAME` must be a `# replace` for a file path; the file's content is inserted (recursively preprocessed, cycle-guarded) |

## Conditional compilation

| Directive | Syntax | Effect |
| --- | --- | --- |
| `# if` | `# if COND` | push a frame; active if `COND` is true |
| `# elif` | `# elif COND` | re-evaluate if the frame is not yet matched |
| `# else` | `# else` | active if the frame is not yet matched |
| `# end` | `# end` | pop the current frame |

`COND` supports `defined NAME`, `not defined NAME`, parentheses, and `not (...)`.

```pycp
# if defined DEBUG
io.print("debug build")
# else
io.print("release build")
# end
```

## Output metadata

| Directive | Syntax | Effect |
| --- | --- | --- |
| `# set` | `# set NAME to VALUE` | set output metadata: `lineno` overrides the line-directive line, `filename` overrides the file name (quotes optional); other names are ignored |

## Diagnostics

| Directive | Syntax | Effect |
| --- | --- | --- |
| `# send error` | `# send error "TEXT"` | report an error and stop compilation |
| `# send warning` | `# send warning "TEXT"` | emit a warning |
| `# send message` | `# send message "TEXT"` | emit a message |

## Inline line directives

Inside ordinary code, `#lineno` and `#filename` expand to the current physical line
number and source path (quoted). The preprocessor emits `// __pycp_pp# <lineno>
"<file>"` line directives so the main lexer reports correct positions.

## Quirks

- Directive lines are consumed and produce no output; adjacent duplicate line
  directives are merged.
- Macros are **not** expanded inside strings or comments (`//`, `/* */`).
- An unrecognized `#` directive is an error (`unknown preprocessor directive: #<name>`).
- `not` exists only inside preprocessor conditions, not in the core language.
