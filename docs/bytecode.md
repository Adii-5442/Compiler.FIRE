# Fire bytecode

The instruction set the `fire` VM executes, and what `fire emit --bytecode`
prints.

## Encoding

Each instruction is one opcode byte followed by fixed-width little-endian
operands. There is no variable-length encoding, which keeps the decoder a
switch over one byte and the disassembler exact.

| Operand | Width | Used for |
| --- | --- | --- |
| `u8` | 1 byte | argument counts |
| `u16` | 2 bytes | constant indices, slot numbers, function and builtin ids |
| `u32` | 4 bytes | jump targets |

**Jump targets are absolute byte offsets within the chunk**, not relative
displacements. Two extra bytes per jump, in exchange for removing a class of
off-by-one bugs from the compiler's patching code.

## Chunks, functions and modules

A `Chunk` is a byte vector, a constant pool, and a run-length map from byte
offset to source span. The span map is what lets a runtime error point at the
expression that caused it.

A `CompiledFunction` is a chunk plus a name, an arity and a frame size. A
`Module` is the script body (itself a `CompiledFunction` with arity 0), the
list of functions, and the number of globals.

## The value stack

One stack holds both locals and temporaries. A frame's `base` is the index of
its slot 0. Arguments are pushed by the caller and are therefore already in
slots `0 .. arity-1` when the callee starts; the VM resizes the stack to
`base + local_count` on entry, so the remaining slots exist and hold `0`.

Two invariants hold everywhere:

1. **Every expression leaves exactly one value.** A call to a `void` function
   pushes a zero, so a call site never has to know what it called.
2. **The stack is balanced at every statement boundary.** An expression
   statement is exactly one evaluation followed by one `POP`.

## Instructions

### Stack

| Opcode | Operands | Effect |
| --- | --- | --- |
| `CONST` | `u16` | push `constants[i]` |
| `TRUE` / `FALSE` | | push the boolean |
| `POP` | | discard the top |
| `DUP` | | duplicate the top |
| `DUP2` | | duplicate the top two, order preserved |

`DUP2` exists for `xs[f()] += 1`, which must evaluate the array and the index
once and then use each twice.

### Variables

| Opcode | Operands | Effect |
| --- | --- | --- |
| `GET_LOCAL` | `u16` | push `stack[base + slot]` |
| `SET_LOCAL` | `u16` | pop into `stack[base + slot]` |
| `GET_GLOBAL` | `u16` | push `globals[slot]` |
| `SET_GLOBAL` | `u16` | pop into `globals[slot]` |

### Arithmetic

| Opcode | Operands | Effect |
| --- | --- | --- |
| `ADD_I` `SUB_I` `MUL_I` `DIV_I` `MOD_I` | | integer, wrapping; `DIV_I`/`MOD_I` fault on a zero divisor |
| `NEG_I` | | integer negation |
| `ADD_F` `SUB_F` `MUL_F` `DIV_F` `NEG_F` | | IEEE-754 double |
| `CONCAT` | | string concatenation |

Arithmetic is type-specialised: the VM never inspects a tag to decide what an
operator means, because the type checker already proved it.

### Bitwise and logic

| Opcode | Effect |
| --- | --- |
| `BIT_AND` `BIT_OR` `BIT_XOR` `BIT_NOT` | integer bitwise |
| `SHL` `SHR` | shifts; `SHR` is arithmetic; a count outside 0–63 faults |
| `NOT` | boolean negation |

### Comparison

| Opcode | Effect |
| --- | --- |
| `EQ` `NE` | structural equality over any type, recursing into arrays |
| `LT_I` `LE_I` `GT_I` `GE_I` | ordered integer comparison |
| `LT_F` `LE_F` `GT_F` `GE_F` | ordered float comparison |
| `LT_S` `LE_S` `GT_S` `GE_S` | byte-order string comparison |

### Aggregates

| Opcode | Operands | Effect |
| --- | --- | --- |
| `MAKE_ARRAY` | `u16 n` | pop `n` values, push an array of them |
| `INDEX_GET` | | pop index and target, push the element |
| `INDEX_SET` | | pop value, index and target; store |

Both index instructions bounds-check and fault with the index and the length.

### Control flow

| Opcode | Operands | Effect |
| --- | --- | --- |
| `JUMP` | `u32` | unconditional |
| `JUMP_IF_FALSE` | `u32` | pop; jump when false |
| `JUMP_IF_FALSE_PEEK` | `u32` | jump when false, *leaving* the value |
| `JUMP_IF_TRUE_PEEK` | `u32` | jump when true, *leaving* the value |

The peeking forms implement `&&` and `||`. When the jump is taken, the left
operand is still on the stack — and that value is exactly the short-circuit
result, so no extra shuffling is needed:

```
        <left>
        JUMP_IF_FALSE_PEEK end     ; a && b
        POP
        <right>
end:
```

### Calls

| Opcode | Operands | Effect |
| --- | --- | --- |
| `CALL` | `u16 fn`, `u8 argc` | call a user function |
| `CALL_NATIVE` | `u16 id`, `u8 argc` | call a builtin |
| `RET` | | return the top of the stack |
| `RET_VOID` | | return, pushing a zero in the caller |
| `HALT` | | end the script body |

## Reading a disassembly

```
$ fire emit --bytecode examples/hello.fire
```

```
; Fire bytecode module: 2 globals, 0 functions

== <script> (0 params, 0 slots, 41 bytes) ==
000000      3  CONST                    0  ; "Hello, Fire!"
000003    |    CALL_NATIVE              1  ; println, 1 args
000007    |    POP
000008      6  CONST                    1  ; "world"
```

Columns are the byte offset, the source line (repeated only when it changes),
the opcode, its operands, and a comment naming constants and builtins.

`fire run --trace` prints the same lines as each instruction executes.

## Adding an opcode

1. Add it to `OpCode` in `include/fire/chunk.hpp`.
2. Give it a name in `opcode_name` and a length in `opcode_length`.
3. Emit it in `src/codegen.cpp`.
4. Execute it in `src/vm.cpp`.
5. Give it a case in `src/disasm.cpp` if it has operands.

Steps 2 and 4 are switches over the enum with no `default`, so the compiler
will tell you if you forget them.
