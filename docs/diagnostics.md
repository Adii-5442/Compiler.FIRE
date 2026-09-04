# Diagnostic index

Every message the compiler emits carries a stable code. Codes are grouped by
the pass that produces them, so the prefix tells you where in the pipeline a
problem was found.

| Range | Pass |
| --- | --- |
| `E00xx` | lexical |
| `E01xx` | syntax |
| `E02xx` | types and name resolution |
| `E03xx` | builtin call signatures |
| `N00xx` | native backend limitations |
| `R00xx` | runtime |
| `W00xx` | warnings |

## Lexical — `E00xx`

| Code | Message | Cause |
| --- | --- | --- |
| `E0004` | unterminated block comment | a `/*` with no matching `*/`. Fire's block comments nest, so each one needs its own closer. |
| `E0005` | integer literal is out of range | the value does not fit in a signed 64-bit `int`. |
| `E0006` | numeric literal has a base prefix but no digits | `0x`, `0b` or `0o` with nothing after it. |
| `E0007` | bad escape sequence | an unknown `\x`, a malformed `\xNN`, or a `\u{...}` that is empty, too long, or beyond `\u{10FFFF}`. |
| `E0008` | unterminated string literal | a missing closing `"`. A string may not contain a raw newline; use `\n`. |
| `E0009` | unexpected `.` | Fire has no member access. The range operator is `..`. |
| `E0010` | unexpected character | a byte that begins no Fire token. Repeated identical bytes collapse into one diagnostic. |

## Syntax — `E01xx`

| Code | Message | Cause |
| --- | --- | --- |
| `E0101` | expected `X`, found `Y` | the general "wrong token here" error. The message names both. |
| `E0102` | expected a type | a type annotation position holding something that is not `int`, `float`, `bool`, `str`, `void` or `[T]`. |
| `E0103` | nested function declarations are not allowed | `fn` inside a block. Move it to the top level; it will still be visible. |
| `E0104` | a variable must be given an initial value | `let x;` — Fire has no uninitialised bindings. |
| `E0105` | invalid assignment target | the left of `=` is not a name or an index expression. |
| `E0106` | `int` is a type, not a value | a bare type keyword in expression position. To convert, write `int(x)`. |
| `E0107` | expected an expression | an expression position holding a token that cannot start one. |

## Types and names — `E02xx`

| Code | Message | Cause |
| --- | --- | --- |
| `E0201` | cannot apply `op` to `T` and `U` | an operator's operand types do not fit. The help names the conversion to write. |
| `E0202` | already declared in this scope | two declarations of one name in one block. Shadowing in a *nested* block is fine. |
| `E0203` | mismatched types | the general assignability failure: an initialiser, an argument, an assigned value or a returned value whose type is not the one expected. |
| `E0204` | builtin cannot be redefined | a `fn` whose name is already a builtin. |
| `E0205` | function defined more than once | two `fn` declarations with one name. |
| `E0206` | not every path returns a value | a function with a return type that can fall off its end. `exit`, `panic` and `while true` without a `break` all count as leaving. |
| `E0207` | `break`/`continue` outside of a loop | |
| `E0208` | cannot bind to a `void` value | `let x = f();` where `f` returns nothing. |
| `E0209` | cannot assign to a `const` | including compound assignment. `const` binds the name, not the contents of an array. |
| `E0210` | cannot apply `op=` | a compound assignment whose operand types do not fit. |
| `E0211` | cannot iterate over `T` | `for x in` something that is not an array or a `str`. To count, use `for i in 0..n`. |
| `E0212` | `return` outside of a function | top-level code cannot return. Use `exit(code)`. |
| `E0213` | `return` without a value | in a function that declares a return type. |
| `E0214` | returning a value from a `void` function | add `-> T` to the signature. |
| `E0215` | cannot infer the element type of an empty array | write it down: `let xs: [int] = [];`. |
| `E0216` | array elements must all have the same type | |
| `E0217` | cannot find name in this scope | with a "did you mean" suggestion when a close name exists. |
| `E0218` | cannot apply a unary operator to `T` | `-` needs a number, `!` a `bool`, `~` an `int`. |
| `E0219` | wrong number of arguments to a function | the note points at the declaration. |
| `E0220` | wrong number of arguments to a builtin | |
| `E0221` | cannot find function | with a suggestion, or a note if the name is a variable. |
| `E0222` | cannot index into `T` | only arrays and strings can be indexed. |
| `E0223` | condition needs a `bool` | Fire has no truthiness. The help suggests the comparison to write. |

## Builtin signatures — `E03xx`

| Code | Message | Cause |
| --- | --- | --- |
| `E0301` | cannot print a `void` value | `println(f())` where `f` returns nothing. |
| `E0302` | cannot push `T` onto `[U]` | the element does not match the array. |
| `E0303` | both arguments must have the same type | `min` and `max` do not mix `int` with `float`. |
| `E0304` | argument N has the wrong type | the general builtin argument mismatch. |

## Native backend — `N00xx`

| Code | Message | Cause |
| --- | --- | --- |
| `N0001` | the native backend does not support X | `fire build` met a construct outside its subset — a `float`, an array, a computed string, an unsupported builtin. Reported once per feature, with a pointer to `fire run`. |

## Runtime — `R00xx`

`R0001` covers every runtime fault. The program stops with status 70 and prints
the failing source line plus a stack backtrace. The message says which:

- division or remainder by zero
- integer overflow in division (`INT64_MIN / -1`)
- a shift count outside 0–63
- an index outside an array or a string
- `pop` or `remove` on an empty array
- a string that will not convert to a number
- `sqrt` of a negative number
- `chr` outside the Unicode range, `ord` of an empty string
- a failed `assert`, or an explicit `panic`
- call stack overflow

## Warnings — `W00xx`

| Code | Message | Cause |
| --- | --- | --- |
| `W0001` | unused variable | a `let` or `const` nothing reads. Prefix the name with `_` to silence it. |
| `W0002` | unreachable code | a statement after one that always returns, exits or panics. |

Warnings never stop compilation.
