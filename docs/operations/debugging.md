# Debugging

Finding what went wrong in the compiler, the machine or a host.

## The checking build

`kest-debug`, in every build but MSVC's, is the command line with the library's own checks (`KEST_CHECKED`) under AddressSanitizer and UndefinedBehaviorSanitizer. It is told where every host allocation ends, poisons arena blocks around each allocation, and holds every allocation to arriving as zeros, so a read one past the end is a report rather than whatever was next. Run the failing program under it first:

```sh
out/dev/kest-debug run program.kest
```

`examples/embed-debug` and `examples/engine-debug` are the example hosts built the same way, which is where the public boundary is crossed in both directions. `tools/fuzz-debug` is the fuzzer under the same build.

## Switches

These work in the checking build only:

| Variable | Does |
| --- | --- |
| `KEST_DEEP=1` | counts every instruction and pair of instructions and prints them, with the machine's own questions about what it ran |
| `KEST_WALK_EVERY=1` | collects at every allocation, which finds a handle held where the walk does not look |
| `KEST_REFUSE_AT=n` | refuses the n-th allocation and every one after it, to walk an out-of-memory path |
| `KEST_REFUSE_ARENA=n` | refuses the n-th arena, so a stage that takes an arena of its own is seen being told no |

These work in every build:

| Variable | Does |
| --- | --- |
| `KEST_PLAIN=1` | no fused instructions and no carried bodies |
| `KEST_NOOPT=1` | no IR rewrites |
| `KEST_IRSAY=1`, `KEST_CSAY=1` | print what the IR builder and the C backend decided per body |

A program that answers differently with `KEST_PLAIN` or `KEST_NOOPT` set is a miscompilation in the part the switch turns off. One that answers differently compiled to C (`kest emit --c`) is a defect in the backend; `tools/check-c.sh` runs that comparison over the tree.

## Looking inside

- `kest emit file.kest` prints every body's bytecode; `kest emit --json` gives the same with each body's hash, and `kest emit --c` the C the backend writes.
- `kest check --cost file.kest` says what was proved about each body: whether it reaches the heap or the host, whether it is outside the deterministic profile, and which promise it keeps.
- `kest profile file.kest` runs a program and counts budget steps, calls per body, crossings and what the heap holds.
- `kest debug file.kest` takes `break <line>`, `run`, `continue`, `step`, `next`, `where` and `locals` on standard input; `kest dap` is the same debugger for an editor.

## Fuzz findings

A finding is a seed, a count and a target. `out/dev/tools/fuzz-debug <seed> <count> <file> <target>` replays exactly the same inputs on any machine, under the checking build, writing each input to `<file>` before it is handed over, so after a crash `<file>` holds the input that broke; reduce it by hand to the smallest program that still shows the defect, and add that program to `test/regress` (it answers 0 once fixed) or `test/refused` (its first line names the code it is refused with). See [Checks](checks.md#fuzzing) for the targets.

## Threads

`KEST_RACES=ON` (the `linux-gcc` preset) builds the objects for ThreadSanitizer, and the gate runs four machines of one build on four threads under it. A machine belongs to one thread at a time; a race report almost always means two threads touched one machine or one build was freed under its machines.
