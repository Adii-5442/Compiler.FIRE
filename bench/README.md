# Benchmarks

Microbenchmarks for the interpreter loop. They exist to answer "did that change
help?", not "how fast is Fire?" — comparing them against another language's
numbers would be meaningless, since none of these programs are large enough to
say anything about a real workload.

```console
$ tools/bench.sh          # best of 5 runs, VM and native side by side
$ tools/bench.sh 15       # more repetitions on a noisy machine
```

| Program | Measures |
| --- | --- |
| `fib.fire` | call and frame overhead — 1.3M calls, almost no other work |
| `loops.fire` | dispatch and local access — 8M iterations of a tight arithmetic loop |
| `arrays.fire` | indexed reads and array growth |
| `iterate.fire` | builtin call cost, since `for x in xs` calls `len` every iteration |

`fib` and `loops` are inside the native backend's subset, so `tools/bench.sh`
times both backends for those and shows the gap. `arrays` and `iterate` use
arrays and run on the VM only.

Take the minimum of several runs rather than the mean: interference from other
processes can only make a run slower, never faster.
