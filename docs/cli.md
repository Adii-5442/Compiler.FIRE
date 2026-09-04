# The `fire` command

```
fire <command> [options] <file.fire> [-- program args]
fire <file.fire>                       shorthand for `fire run`
```

## Commands

### `fire run <file>`

Compile and execute on the Fire VM. This is the normal way to run a program and
supports the whole language.

```console
$ fire run examples/fizzbuzz.fire
$ fire run app.fire -- --verbose input.txt
```

Arguments after `--` are passed to the program, where `args()` returns them.
The process exit status is whatever `exit(n)` was called with, `0` on normal
completion, `1` if compilation failed, or `70` after a runtime error.

### `fire build <file> [-o <path>]`

Compile to a standalone native executable through the x86-64 backend. Requires
`nasm` and `ld`. The output defaults to the input's base name.

```console
$ fire build examples/native.fire -o collatz
wrote collatz
$ ./collatz
```

The backend supports a subset of the language — `int`, `bool`, string literals
passed to `print`/`println`, all operators, all control flow, and functions.
Anything outside it is reported by name with a pointer to `fire run`.

`-S` stops after writing `<output>.asm` instead of assembling and linking.

### `fire check <file>`

Type-check and report, without running or generating anything. This is the fast
path for an editor or a pre-commit hook.

```console
$ fire check app.fire
ok: app.fire type-checks
```

### `fire emit <file> [--form]`

Print an intermediate representation.

| Flag | Output |
| --- | --- |
| `--tokens` | the token stream with positions and decoded values |
| `--ast` | the typed syntax tree, with resolved storage and inferred types |
| `--bytecode` | disassembled bytecode (the default) |
| `--asm` | x86-64 assembly, NASM syntax |

`-o <path>` writes to a file instead of stdout.

```console
$ fire emit --ast examples/hello.fire
$ fire emit --asm examples/native.fire -o native.asm
```

### `fire repl`

An interactive session. Declarations persist between lines and a bare
expression prints its value.

```console
$ fire repl
Fire 1.0.0 — interactive session
fire> let xs = [3, 1, 2];
fire> len(xs)
3
fire> fn double(n: int) -> int { return n * 2; }
fire> double(21)
42
```

`:help` lists the session commands, `:builtins` lists the library, `:quit` or
Ctrl-D leaves.

### `fire builtins`

Print every builtin with its arity and a one-line description.

### `fire version`, `fire help`

Self-explanatory. `-v` and `-h` are accepted too.

## Options

| Option | Effect |
| --- | --- |
| `-o <path>` | output path for `build` and `emit` |
| `-S` | with `build`, stop after writing the assembly |
| `--trace` | disassemble each instruction as the VM executes it |
| `--color` / `--no-color` | force diagnostic colouring on or off |
| `--` | everything after this goes to the program, not the compiler |

Colour is enabled automatically when stderr is a terminal and `NO_COLOR` is
unset.

## Exit statuses

| Status | Meaning |
| --- | --- |
| `0` | success |
| `1` | compilation failed |
| `64` | bad usage — unknown option, missing file |
| `70` | runtime error |
| other | whatever the program passed to `exit(n)` |

These follow `sysexits.h`, so `fire` composes with shell scripts and CI in the
way those expect.
