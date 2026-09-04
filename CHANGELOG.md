# Changelog

Notable changes to Fire. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[semantic versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] — 2026-09-04

The first complete release. Fire went from a tutorial-scale program that could
compile `exit(12);` to a language with a type system, a virtual machine and a
native backend.

### The language

- **Types**: `int` (64-bit, wrapping), `float`, `bool`, `str` (immutable UTF-8)
  and `[T]` arrays, with local type inference and optional annotations.
- **Declarations**: `let` and `const`, block scoping with shadowing, file-scope
  declarations as globals visible inside functions.
- **Functions**: typed parameters, an optional `-> T`, recursion and mutual
  recursion, visible throughout the file regardless of declaration order.
- **Control flow**: `if`/`elif`/`else`, `while`, `for i in a..b`, `for x in xs`
  over arrays and strings, `break`, `continue`, `return`.
- **Operators**: eleven precedence levels — logical, comparison, bitwise,
  shifts, arithmetic, unary, indexing — with short-circuiting `&&` and `||`,
  and compound assignment.
- **Literals**: decimal, hex, binary and octal integers with `_` separators;
  floats with exponents; strings with `\n \t \r \0 \e \\ \" \xNN \u{...}`;
  array literals.
- **39 builtins** covering I/O, arrays, strings, conversion, maths, randomness,
  assertions and process control.

### The compiler

- Hand-written lexer with recovery, nesting block comments, and range-checked
  numeric conversion.
- Recursive-descent parser with panic-mode recovery, so one missing semicolon
  reports once.
- Semantic analysis: name resolution, frame layout with slot reuse across
  sibling scopes, full type checking with a poison type, exhaustive-return
  analysis, and unused-variable and unreachable-code warnings.
- Bytecode compiler and stack VM with type-specialised arithmetic, absolute
  jump targets, and runtime faults reported with a source caret and a stack
  backtrace.
- Native x86-64 backend emitting freestanding NASM assembly — no libc — for a
  documented subset of the language, assembled and linked by `fire build`.
- rustc-style diagnostics with stable codes, secondary labels, notes, helps and
  "did you mean" suggestions from a bounded edit distance.
- `fire` CLI: `run`, `build`, `check`, `emit` (`--tokens`, `--ast`,
  `--bytecode`, `--asm`), `repl`, `builtins`.
- Incremental REPL with persistent declarations and expression echoing.

### Tooling

- Dependency-free Makefile with `test`, `examples`, `debug` (ASan + UBSan),
  `install` and `format` targets; CMake for IDEs.
- 87 unit tests and 22 end-to-end cases, including a suite that compiles
  programs with **both** backends and diffs the results.
- GitHub Actions across GCC and Clang on Linux and macOS, plus a sanitizer job
  and a formatting check.
- 13 example programs and six documents: getting started, language reference,
  grammar, architecture, bytecode and CLI references, and a diagnostic index.

### Notes

- `exit(12);`, the only statement the original compiler could handle, is still
  valid Fire and is the first case in the end-to-end suite.
