# The Fire language reference

Fire is a small, statically typed, compiled language. Programs are files with a
`.fire` extension. This document describes the language; [grammar.md](grammar.md)
gives the formal syntax and [`examples/tour.fire`](../examples/tour.fire) shows
all of it working in one file.

- [Program structure](#program-structure)
- [Comments](#comments)
- [Types](#types)
- [Literals](#literals)
- [Variables](#variables)
- [Operators](#operators)
- [Control flow](#control-flow)
- [Functions](#functions)
- [Arrays](#arrays)
- [Strings](#strings)
- [The builtin library](#the-builtin-library)
- [Errors](#errors)
- [Design decisions](#design-decisions)

## Program structure

A Fire file is a sequence of function declarations and statements. Statements at
the top level run in order, from the top of the file down; there is no required
`main`.

```fire
fn double(n: int) -> int {
    return n * 2;
}

println(double(21));   // 42
```

Functions may only be declared at the top level, and are visible everywhere in
the file regardless of order, so mutual recursion needs no forward
declarations.

## Comments

```fire
// to the end of the line

/* a block comment
   /* which nests, so commenting out a region containing
      another comment works */
   and ends here */
```

## Types

| Type    | Description                                   | Example            |
| ------- | --------------------------------------------- | ------------------ |
| `int`   | signed 64-bit integer, wrapping on overflow    | `42`, `0xFF`       |
| `float` | IEEE-754 double                               | `3.14`, `1e-9`     |
| `bool`  | `true` or `false`                             | `true`             |
| `str`   | immutable UTF-8 text                          | `"hello"`          |
| `[T]`   | growable array with reference semantics       | `[1, 2, 3]`        |
| `void`  | the absence of a value; only a return type    | —                  |

Types are written the same way in annotations and in the compiler's messages:
`int`, `[str]`, `[[int]]`.

There are **no implicit conversions**. `1 + 2.0` is a compile error; write
`float(1) + 2.0`. See [design decisions](#design-decisions) for why.

## Literals

```fire
42            1_000_000     0xDEAD_BEEF    0b1010_1010    0o755
3.14          1e9           2.5e-3
true          false
"text"        "with \n \t \" \\ \x41 \u{1F525} escapes"
[1, 2, 3]     ["a", "b"]    [[1, 2], [3, 4]]
```

`_` may separate digits anywhere after the first one. An empty array literal
has no type of its own, so it needs an annotation: `let xs: [int] = [];`.

## Variables

```fire
let count = 0;            // mutable, type inferred as int
let name: str = "fire";   // mutable, type written down
const limit = 100;        // immutable
```

`let` and `const` differ only in whether the binding may be assigned to again.
`const` does not freeze what an array *contains* — `const xs = [1]; push(xs, 2);`
is legal, because it changes the array, not the binding.

A declaration in an inner block shadows an outer one. Two declarations of the
same name in the *same* block is an error.

A `let` or `const` at file scope is a **global**, visible inside every function.
One inside any block is a **local** of the enclosing function.

```fire
let total = 0;            // global

fn add(n: int) {
    total += n;           // functions can see globals
}
```

## Operators

Loosest to tightest. Every binary operator is left associative.

| Level | Operators              | Operand types           | Result       |
| ----- | ---------------------- | ----------------------- | ------------ |
| 1     | `\|\|`                 | `bool`                  | `bool`       |
| 2     | `&&`                   | `bool`                  | `bool`       |
| 3     | `==` `!=`              | two of the same type    | `bool`       |
| 4     | `<` `<=` `>` `>=`      | two `int`/`float`/`str` | `bool`       |
| 5     | `\|`                   | `int`                   | `int`        |
| 6     | `^`                    | `int`                   | `int`        |
| 7     | `&`                    | `int`                   | `int`        |
| 8     | `<<` `>>`              | `int`                   | `int`        |
| 9     | `+` `-`                | two `int`/`float`; `+` also two `str` | same |
| 10    | `*` `/` `%`            | two `int`/`float`; `%` is `int` only | same |
| 11    | `-` `!` `~` (prefix)   | numeric / `bool` / `int`| same         |
| 12    | `[]`                   | array or `str`          | element/`str`|

`&&` and `||` short-circuit: the right operand is not evaluated when the left
already determines the answer.

`==` on arrays compares element by element, so two distinct arrays with equal
contents are equal. `<` on strings compares byte order.

Integer `/` truncates toward zero and `%` takes the sign of the dividend, so
`-7 / 2` is `-3` and `-7 % 2` is `-1`. Integer overflow wraps. Division or
remainder by zero is a runtime error; float division by zero follows IEEE-754
and produces an infinity or a NaN.

`>>` is an arithmetic shift: the sign bit is replicated. Shifting by less than
0 or more than 63 is a runtime error.

### Assignment

```fire
x = 1;
x += 1;   x -= 1;   x *= 2;   x /= 2;   x %= 3;
xs[0] = 9;
xs[i] += 1;         // `i` is evaluated exactly once
```

Assignment is a statement, not an expression, so `if x = 1` cannot be written
by accident.

## Control flow

```fire
if condition {
    // ...
} elif other {
    // ...
} else {
    // ...
}

while condition {
    // ...
}

for i in 0..10 { }        // integer range, upper bound excluded
for x in array { }        // each element
for c in "text" { }       // each character, as a one-character str

break;      // leave the innermost loop
continue;   // next iteration
```

A condition must be a `bool`. Fire has no truthiness: write `if n != 0`, not
`if n`.

`for i in a..b` evaluates `b` once, before the first iteration. `for x in xs`
re-reads the length each time round, so growing or shrinking `xs` inside the
loop is observable rather than undefined.

## Functions

```fire
fn name(first: int, second: str) -> bool {
    return len(second) == first;
}

fn shout(text: str) {          // no `->` means it returns nothing
    println(upper(text));
}
```

Parameters must be annotated. Arguments are passed by value, except that an
array value is a reference to the array, so a callee's `push` is visible to its
caller.

Every path through a function with a return type must return. The compiler
understands that `exit`, `panic` and `while true` without a `break` all count
as leaving.

Recursion is supported, including mutual recursion. The call depth is capped at
4096 frames; exceeding it is a runtime error rather than a crash.

## Arrays

```fire
let xs = [3, 1, 2];
let empty: [str] = [];
let grid: [[int]] = [[1, 2], [3, 4]];

push(xs, 4);          // append
pop(xs);              // remove and return the last
insert(xs, 0, 9);     // insert at an index
remove(xs, 1);        // remove and return at an index
len(xs);              // length
slice(xs, 1, 3);      // a new array from [1, 3)
xs[0];                // element access
xs[0] = 5;            // element assignment
```

Arrays have **reference semantics**. `let b = a;` makes `b` another name for the
same array, not a copy. Copy explicitly when you need to:

```fire
fn copy_of(xs: [int]) -> [int] {
    let out: [int] = [];
    for x in xs { push(out, x); }
    return out;
}
```

Indexing outside `0 .. len(xs) - 1` is a runtime error that names the index and
the length.

## Strings

Strings are immutable UTF-8. `+` concatenates, `len` counts **bytes**, and
`s[i]` yields the one-byte substring at `i` — so indexing is byte-oriented while
`chr` and `ord` work in whole code points.

```fire
let s = "fire";
len(s);                  // 4
s[0];                    // "f"
s + "!";                 // "fire!"
upper(s);                // "FIRE"
ord("A");                // 65
chr(128293);             // "🔥"
ord(chr(128293));        // 128293
```

## The builtin library

`fire builtins` prints this list with arities at any time.

### Output and input

| Function | Signature | Description |
| --- | --- | --- |
| `print` | `(...) -> void` | write values separated by spaces |
| `println` | `(...) -> void` | the same, with a trailing newline |
| `input` | `() -> str` | read one line from stdin, without its newline |
| `eof` | `() -> bool` | true once stdin has no more input |

### Arrays

| Function | Signature | Description |
| --- | --- | --- |
| `len` | `([T]) -> int` / `(str) -> int` | number of elements or bytes |
| `push` | `([T], T) -> void` | append |
| `pop` | `([T]) -> T` | remove and return the last element |
| `insert` | `([T], int, T) -> void` | insert at an index |
| `remove` | `([T], int) -> T` | remove and return the element at an index |
| `slice` | `([T], int, int) -> [T]` / `(str, int, int) -> str` | half-open sub-range, clamped |

### Strings

| Function | Signature | Description |
| --- | --- | --- |
| `find` | `(str, str) -> int` | byte index of a substring, or `-1` |
| `split` | `(str, str) -> [str]` | split on a separator; `""` splits into bytes |
| `join` | `([str], str) -> str` | join with a separator |
| `trim` | `(str) -> str` | strip leading and trailing whitespace |
| `upper` / `lower` | `(str) -> str` | change case (ASCII) |
| `replace` | `(str, str, str) -> str` | replace every occurrence |
| `repeat` | `(str, int) -> str` | concatenate a string with itself |
| `chr` | `(int) -> str` | UTF-8 encode one code point |
| `ord` | `(str) -> int` | code point of the first character |

### Conversion

| Function | Signature | Description |
| --- | --- | --- |
| `int` | `(int\|float\|bool\|str) -> int` | truncates floats; fails on unparseable strings |
| `float` | `(int\|float\|str) -> float` | |
| `bool` | `(int\|float\|bool\|str) -> bool` | false for `0`, `0.0`, `""` |
| `str` | `(any) -> str` | the same rendering `println` uses |

### Maths

| Function | Signature | Description |
| --- | --- | --- |
| `abs` | `(int) -> int` / `(float) -> float` | |
| `min` / `max` | `(T, T) -> T` for numeric `T` | both arguments must have the same type |
| `pow` | `(numeric, numeric) -> float` | |
| `sqrt` | `(numeric) -> float` | fails on a negative argument |
| `floor` / `ceil` / `round` | `(numeric) -> int` | |
| `seed` | `(int) -> void` | seed the RNG; the sequence is then reproducible |
| `rand_int` | `(int, int) -> int` | uniform in `[lo, hi]` |

### Process

| Function | Signature | Description |
| --- | --- | --- |
| `exit` | `(int) -> void` | stop with a status; never returns |
| `panic` | `(str) -> void` | stop with a message and status 70; never returns |
| `assert` | `(bool)` / `(bool, str)` | abort unless the condition holds |
| `clock` | `() -> float` | monotonic seconds, for timing |
| `args` | `() -> [str]` | arguments after `--` on the command line |

## Errors

Fire distinguishes two kinds, and reports both the same way — with the source
line, a caret under the offending expression, and often a suggested fix.

**Compile-time errors** are everything the type checker can prove: mismatched
types, unknown names, wrong arity, a path that fails to return. Nothing runs.

```
error[E0201]: cannot apply `+` to `int` and `str`
 --> app.fire:4:13
  |
4 |     let x = 1 + "two";
  |             ^^^^^^^^^ arithmetic needs two `int` or two `float`
  |
  = help: convert the other operand with `str(x)` to concatenate
```

**Runtime errors** are what types cannot prove: division by zero, an index out
of bounds, a string that does not parse as a number, a recursion that never
ends. These stop the program with status 70 and print the failing line plus a
stack backtrace.

The full index is in [diagnostics.md](diagnostics.md).

## Design decisions

**No implicit numeric conversion.** `1 + 2.0` is an error. Implicit widening
means a program can silently lose precision or change behaviour when a literal
is edited, and the fix — `float(1) + 2.0` — is short and says exactly what
happens. The compiler's message names the conversion to write.

**No truthiness.** `if n` on an integer is an error suggesting `n != 0`. Every
language that allows it eventually has to explain whether `0.0`, `""` and `[]`
are true, and every answer surprises somebody.

**Arrays are references, values are values.** One rule, no hidden copies of
large data, and passing an array to a function to fill in is the obvious thing.
The cost is that `let b = a;` aliases, which is stated plainly rather than
smoothed over.

**Assignment is a statement.** `if (x = 1)` cannot be typed by accident.

**Functions are not values.** Fire has no closures and no function types. That
removes an entire layer — captured environments, escape analysis, a garbage
collector for them — from a language whose purpose is to be readable end to
end. Calls therefore resolve at compile time, which is also what lets the
native backend emit a direct `call`.

**Statically typed, dynamically tagged.** Values carry a runtime tag even
though the checker has already proved every operation. It costs eight bytes and
buys generic `print`, structural array equality, and the guarantee that a
compiler bug surfaces as a diagnostic rather than as corrupted memory.
