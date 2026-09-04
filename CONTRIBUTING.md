# Contributing to Fire

Thanks for looking. This file covers how to build and test, what the code
expects of you, and where to start for the changes people most often want to
make.

## Build and test

```console
$ make                  # bin/fire
$ make test             # unit + end-to-end
$ make debug            # -O0 -g with ASan and UBSan
$ make examples         # runs everything in examples/
$ make format           # clang-format, in place
```

A C++20 compiler and `make` are the only requirements. `nasm` and `ld` are
needed to exercise `fire build`; without them the native tests skip themselves
rather than failing.

Before opening a pull request:

```console
$ make clean && make && make test && make examples
$ make debug && make examples          # under the sanitizers
$ make format
```

## What the code expects

**Warnings are errors in practice.** The tree builds clean under `-Wall
-Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`, and CI keeps it
that way. If a change introduces a warning, fix the change rather than the
warning set.

**Comments explain decisions, not syntax.** A comment that restates the line
below it is noise. A comment saying *why* the loop starts at `n * n`, or why
jump targets are absolute, is the reason anyone can read this code later.
Match the density already in the file you are editing.

**One pass, one file, one job.** If a change makes the lexer care about types
or the code generator report a diagnostic, it probably belongs somewhere else.
Codegen in particular reports nothing: everything it needs was decided by
semantic analysis, and its invariants are asserted rather than checked.

**Every new behaviour gets a test.** Bug fixes get a test that fails before the
fix. See below for where each kind goes.

## Where to start

| To change… | Edit | Then |
| --- | --- | --- |
| a keyword or operator | `include/fire/token.hpp`, `src/lexer.cpp`, `src/parser.cpp` | add cases to `tests/unit/test_lexer.cpp` |
| a statement or expression form | `include/fire/ast.hpp` | follow the compile errors; every switch over the node kind has no `default` |
| a typing rule | `src/sema.cpp` | assert the diagnostic *code* in `tests/unit/test_sema.cpp` |
| an opcode | `include/fire/chunk.hpp`, `src/codegen.cpp`, `src/vm.cpp`, `src/disasm.cpp` | [docs/bytecode.md](docs/bytecode.md) has the checklist |
| a builtin | `include/fire/natives.hpp`, `src/natives.cpp`, `src/natives_runtime.cpp` | the table asserts against `NativeId::Count`, so you cannot forget one half |
| native code generation | `src/backend_x86_64.cpp` | add a case to `tests/native/`, which is diffed against the VM |

Adding an AST node deliberately breaks every switch that must handle it. That
is the intended way to find them all.

## Tests

**Unit tests** live in `tests/unit/` and use the small framework in
`framework.hpp`:

```cpp
FIRE_TEST(sema, const_cannot_be_reassigned)
{
    FIRE_CHECK(contains(diagnose("const x = 1; x = 2;"), "E0209"));
}
```

`run_source` compiles and runs a snippet with stdout and stdin captured;
`diagnose` returns only the compiler's output. Assert diagnostic **codes**, not
wording, so messages can be improved without breaking the suite.

**End-to-end tests** live in `tests/cases/` and are ordinary Fire programs that
carry their own expectations:

```fire
//exit 70
//in some stdin
//= an expected line of stdout
//~ a substring expected in stderr
```

Programs under `tests/cases/errors/` must fail to compile and assert codes with
`//error E0201`. Programs under `tests/native/` are compiled by *both* backends
and diffed against each other — that is what keeps them honest.

## Diagnostics

New errors need a new code, added to [docs/diagnostics.md](docs/diagnostics.md)
in the range for their pass (`E00xx` lexical, `E01xx` syntax, `E02xx` types,
`E03xx` builtin signatures, `N00xx` native backend, `W00xx` warnings).

A good diagnostic says what is wrong, points a caret at the smallest expression
responsible, and — where there is an obvious one — names the fix:

```cpp
diags().error("E0223", "`if` needs a `bool`, found `int`", expr.span)
    .label("expected `bool`")
    .help("Fire has no truthiness; write an explicit comparison such as `x != 0`");
```

Report and keep going. Passes do not abort on the first problem, and an
expression whose type could not be determined takes the poison `Error` type so
that one mistake produces exactly one message.

## Language changes

Fire is meant to stay small. A proposal that adds a feature should say what it
lets someone write that they cannot write today, and what it costs in the type
checker, both backends, and the documentation.

The things it deliberately does not have — closures, structs, maps, generics,
modules, an optimiser — are listed at the end of the README with the reasoning.
Reopening one of those is a fine thing to propose; it just needs to engage with
the reason it was closed.

## Commits

One logical change per commit. The subject line is imperative and under about
seventy characters, prefixed with the area: `feat(sema):`, `fix(lex):`,
`docs:`, `test:`, `build:`, `ci:`, `perf:`, `refactor:`.

The body explains *why*. `git log` is the only place some decisions are
recorded, so a message that says what the diff already shows has wasted the
opportunity.
