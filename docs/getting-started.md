# Getting started

A walkthrough from an empty directory to a running program, then to the same
program as a native executable, then a look at what the compiler did in
between.

## 1. Build the compiler

You need a C++20 compiler and `make`. Nothing else.

```console
$ git clone https://github.com/adityaXsharma/Compiler.FIRE.git
$ cd Compiler.FIRE
$ make
built bin/fire
```

Check it:

```console
$ ./bin/fire version
fire 1.0.0
```

Optionally put it on your `PATH`:

```console
$ sudo make install          # installs to /usr/local/bin/fire
$ make install PREFIX=~/.local
```

The rest of this page writes `fire`; use `./bin/fire` if you did not install
it.

### Optional: the native backend

`fire build` shells out to `nasm` and `ld`. Everything else works without them.

```console
$ sudo apt install nasm binutils     # Debian, Ubuntu
$ brew install nasm                  # macOS, Linuxbrew
```

## 2. Your first program

Create `hello.fire`:

```fire
println("Hello, Fire!");
```

Run it:

```console
$ fire run hello.fire
Hello, Fire!
```

`fire hello.fire` works too — with no command, `run` is assumed.

## 3. A real program

Replace the file with something that exercises the language. This computes the
first twenty rows of Pascal's triangle and prints them centred.

```fire
// pascal.fire — Pascal's triangle

fn next_row(previous: [int]) -> [int] {
    let row = [1];
    for i in 1..len(previous) {
        push(row, previous[i - 1] + previous[i]);
    }
    push(row, 1);
    return row;
}

fn width_of(row: [int]) -> int {
    let total = 0;
    for value in row {
        total += len(str(value)) + 1;
    }
    return total;
}

const rows = 12;

// Build every row first, so the widest one can set the centring.
let triangle: [[int]] = [[1]];
for _i in 1..rows {
    push(triangle, next_row(triangle[len(triangle) - 1]));
}

const widest = width_of(triangle[len(triangle) - 1]);

for row in triangle {
    let cells: [str] = [];
    for value in row {
        push(cells, str(value));
    }
    const text = join(cells, " ");
    println(repeat(" ", (widest - len(text)) / 2) + text);
}
```

```console
$ fire run pascal.fire
                   1
                  1 1
                 1 2 1
                1 3 3 1
               1 4 6 4 1
             1 5 10 10 5 1
            1 6 15 20 15 6 1
          1 7 21 35 35 21 7 1
         1 8 28 56 70 56 28 8 1
      1 9 36 84 126 126 84 36 9 1
  1 10 45 120 210 252 210 120 45 10 1
1 11 55 165 330 462 462 330 165 55 11 1
```

Things worth noticing in that program:

- **No `main`.** Top-level statements run in order.
- **Types are inferred but real.** `let row = [1];` gives `row` the type
  `[int]`. Pushing a `str` into it later would be a compile error.
- **Arrays are references.** `next_row` receives the previous row, does not
  copy it, and builds a new one.
- **`const` where nothing changes.** The compiler will stop you assigning to
  those names.

## 4. Let the compiler catch a mistake

Change `push(row, 1);` to `push(row, "1");` and run it again:

```
error[E0302]: cannot push `str` onto `[int]`
 --> pascal.fire:8:15
  |
8 |     push(row, "1");
  |               ^^^ expected `int`
```

Nothing ran. Every error the compiler finds looks like that: the line, a caret
under the exact expression, and where it helps, the fix.

Type-check without running, which is what an editor or a pre-commit hook wants:

```console
$ fire check pascal.fire
ok: pascal.fire type-checks
```

## 5. Compile it to a native executable

Programs inside the native backend's subset — `int` and `bool`, string
literals, all the operators and control flow, and functions — can be compiled
to a standalone Linux binary with no runtime at all:

```console
$ fire build examples/native.fire -o collatz
wrote collatz
$ ./collatz
longest Collatz chain under 10000
  start: 6171  steps: 261
...
$ file collatz
collatz: ELF 64-bit LSB executable, x86-64, statically linked
$ ldd collatz
        not a dynamic executable
```

That binary contains no libc. Its entry point is `_start` and it talks to the
kernel with two syscalls.

Programs outside the subset are told so by name, rather than miscompiled:

```console
$ fire build pascal.fire
error[N0001]: the native backend does not support arrays
  --> pascal.fire:23:25
   |
23 | let triangle: [[int]] = [[1]];
   |                         ^^^^^ not available when compiling to a native executable
   |
   = note: arrays need a heap allocator this backend has none of
   = help: run this program on the Fire VM instead: `fire run <file.fire>`
```

## 6. Look inside the compiler

Every intermediate form is one flag away.

**The tokens:**

```console
$ fire emit --tokens hello.fire
LINE:COL  KIND      TOKEN                 VALUE
------------------------------------------------------------
1:1       IDENT     identifier            "println"
1:8       PUNCT     (
1:9       STR       string literal        "Hello, Fire!"
1:23      PUNCT     )
1:24      PUNCT     ;
2:1       EOF       end of file
```

**The typed syntax tree** — note the resolved storage and the inferred type on
every node:

```console
$ fire emit --ast hello.fire
(program globals=0 script-slots=0
  (expr
    (call println native#1 :void
      (str "Hello, Fire!" :str)
    )
  )
)
```

**The bytecode:**

```console
$ fire emit --bytecode hello.fire
; Fire bytecode module: 0 globals, 0 functions

== <script> (0 params, 0 slots, 9 bytes) ==
000000      1  CONST                    0  ; "Hello, Fire!"
000003      |  CALL_NATIVE              1  ; println, 1 args
000007      |  POP
000008      |  HALT
```

**The assembly:**

```console
$ fire emit --asm examples/native.fire | head -30
```

**Every instruction as it executes:**

```console
$ fire run --trace hello.fire
```

## 7. The REPL

```console
$ fire repl
Fire 1.0.0 — interactive session
type an expression to see its value, `:help` for commands, Ctrl-D to leave.

fire> let xs = [5, 3, 8];
fire> len(xs)
3
fire> fn total(values: [int]) -> int {
  ...     let sum = 0;
  ...     for v in values { sum += v; }
  ...     return sum;
  ... }
fire> total(xs)
16
```

Declarations persist between lines, an unfinished `{` continues the prompt, and
a bare expression prints its value.

## 8. Where to go next

| | |
| --- | --- |
| Every feature in one file | [`examples/tour.fire`](../examples/tour.fire) |
| The full language | [language-reference.md](language-reference.md) |
| The formal syntax | [grammar.md](grammar.md) |
| How the compiler works | [architecture.md](architecture.md) |
| The instruction set | [bytecode.md](bytecode.md) |
| Every command and flag | [cli.md](cli.md) |
| What an error code means | [diagnostics.md](diagnostics.md) |

Run the examples to see the language doing real work:

```console
$ fire run examples/calculator.fire     # a parser, written in Fire
$ fire run examples/mandelbrot.fire     # float maths and escape-time rendering
$ fire run examples/sorting.fire        # four sorts that check their own results
$ fire run examples/wordfreq.fire < README.md
```

And run the suite:

```console
$ make test
87 passed, 0 failed, 87 registered
22 passed, 0 failed
all end-to-end tests passed
```
