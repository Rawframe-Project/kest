# Kest

A programming language for games, simulations, real-time systems, and engine
embedding. Bytecode VM, C11, no dependencies.

```
make
./kest run examples/math.kest

make embed && ./examples/embed
```

```
make PREFIX=/usr/local
sudo make install PREFIX=/usr/local
```

## Starting

Once `kest` is on the path, one command makes something that runs:

```
kest new game
cd game
kest build
kest run
kest test tests/*.kest
kest doctor
```

`kest new` writes a project, a program and a test. `kest.project` is lines of
`name value` and says what to build; being inside a project is why `kest build`
and `kest run` need no file after them. `kest doctor` is what to run when
something is wrong and it is not obvious what: it says what this command line
is, where it looks for the standard library and whether it found it.

For an editor, `editors/vscode` is a grammar and a client that starts
`kest lsp` — which is this compiler, so what an editor says about a file and
what `kest check` says about it cannot differ.

[docs/language.md](docs/language.md) is the reference and reads front to back:
what a program is made of, then values, then promises, then the boundary a host
crosses. `examples/` is thirty-six programs that run, each checking itself.

## Installing

Installing puts `kest`, `kest.h`, `libkest.a` and the standard library where
another project looks. The compiler looks for the library in four places, in
this order: `$KEST_LIB`, then `lib/` beside itself, which is where it is in a
source tree, then `../lib/kest/`, which is where installing puts it, and last
where the build it came from was told it would be put — which is the one a host
that is not this command line falls back on.

The second command above is a host that is not the command line: it compiles a file, makes a
machine with its own limits, and keeps a world between frames.

Fast, easy to use, and good to work on with an AI. What that means concretely,
and what it cost to decide, is in [docs/decisions.md](docs/decisions.md). The
language itself is in [docs/language.md](docs/language.md).

`make fast` is the loop — build, every example, the one form, a diagnostic, the
boundary — in about a quarter of a second. `make check` is the whole gate and
takes about ten minutes on the machine this was written on.

## What is and is not there

Version 0.1.0. Three states, and nothing is in the first that has not been run.

**Implemented.** Whole numbers and floats at every width with defined wrapping
and narrowing; `text`; `bool`; structs; fixed runs; arrays; `store<T>` handing
out generation-checked `ref<T>`; enums that carry values; sets of bits;
optionals; functions as values; one body written for many types, a copy
compiled per set; `defer`; `match`; `for` and `while`; the `no.alloc`,
`no.host` and `deterministic` promises, proved by the compiler rather than
trusted; a `scratch { }` block whose working memory goes back where it was, and
which the compiler proves nothing escapes from; a bytecode VM of 157
instructions; diagnostics with stable codes,
spans, notes, suggested fixes and `--json`, all of a file's mistakes in one
pass, with the shape of every object a command writes versioned; one canonical
source form and a formatter that holds it; a C embedding API of 70 doors
covering compile, start, call, layout introspection with field names, lent
memory, a frame's working memory marked and put back, and per-machine limits;
and a standard library of nine modules written in Kest and held to the same
rules as a program.

**Partial.** The standard library — `io`, `math`, `text`, `table`, `sort`,
`random`, `vec`, `hash`, `os` — is small. Reload is host-mediated and the whole
protocol is in `examples/engine.c`: detect with the per-file and whole-program
marks, build the candidate beside the running one, refuse if a shape's mark says
it moved, make the world again from numbers the program wrote, and publish only
then — but what carries a world across is a save the program writes and reads,
not anything this language does for it. `check --json` carries a `signature` per
declaration, folded from the qualified name, the types and the promises: it is a
signature fingerprint, it is named one, and it does not survive a rename.

**Resource control.** A host sets four ceilings — stack slots, call depth, heap
bytes and a budget in steps — and each is refused in words at the instruction
that crossed it. `kest_cancel` stops a running program from another thread. That
is enough to stop a program that will not stop; it is **not** a claim that this
is safe to run code you do not trust, which would need a threat model and fuzz
evidence this project does not have.

**Not implemented.** Live code replacement in a running machine: a host reloads
by building again and starting a new machine, and moving the world across is the
host's, through a save the program writes. Cross-platform bitwise determinism for `sin`, `cos`, `pow` and
`atan2`: they are the host's libm and two platforms may round them differently,
where `sqrt`, `floor`, `ceil` and all integer and `f32`/`f64` arithmetic are
exactly specified and do not have that problem. `no.host` is not determinism —
that is what `deterministic` is, and the two are separate promises. No package
manager, no debugger, no language server, no JIT, no concurrency inside the
language, no networking, no graphics.

**Experimental.** Everything about the embedding ABI. It has changed four times
this month — a promise added to what a host may ask about, names on the pieces
of a layout, a shape's own mark, a door for reading text, and marking the heap —
and it will change again before it is called stable. A host written against it
today is a host that recompiles.

## Where this is on the way to v1

Four stages, and what each of them asks for. This is **v0.x**: an experimental
language that can be used, on a semantic baseline that has been reproduced and
repaired, with an ABI that is still moving.

**Alpha** wants the defects reproduced in `docs/state.md` fixed — they are — a
resolved representation in use, a bounded story for temporary memory, a backend
chosen at semantic parity, a real host example, and the deterministic profile
implemented on a tested platform. All six are here: the repairs (D927–D939), the
resolved bodies the backend reads (D962), the memory story (D940, D954, D956,
D957, and D966 and D967 for a world whose text changes), the backend chosen by
building the other one and weighing it (D963), the host (`examples/engine.c`,
D949) and the profile (D941–D943, answered by a run and asked again in D968).

**Beta** wants a second platform built and tested, an ABI stabilisation
candidate, versioned tooling output, host-mediated migration validated, and a
generated-C path only if evidence asked for it. Versioned output is here (D947)
and so is migration, validated by a host that does the whole protocol (D949).
Windows is not: it is unverified and marked so rather than claimed.

**v1** wants a documented supported subset that is correct, an embedding ABI
stable enough to write against, bounded representative workloads, no known
critical defects of the reviewed classes, a practical validation loop, and an
architecture a reader can understand without a worklog. The validation loop is
here — a quarter of a second and ten minutes, and what each of them is for is
written down. The rest waits on alpha and beta.

**Tested on** x86-64 Linux with GCC 15.2 only. The code is C11 and libc and
nothing else, so it should build elsewhere; nobody has, and this project does
not call a thing that was never run a thing that works. Windows is unverified;
`docs/language.md` says what a port would read first.

What it costs to run, measured rather than remembered. `make time` takes four
numbers on the machine it is run on:

```
118 ns per entity per step, 7 ns of it the two calls it makes, best of 7 over 10000, spread 21%
19 ns for a call and 26 ns for a crossing, which is 7 ns more, best of 7 over 1000000 calls, spread 6%
13 ns for a hop of the loop, 17 ns with an index read and 33 ns with a read through a reference, which is 16 ns more, best of 7 over 200000 reads, spread 3%
23 ns for a call in from a host and 19 ns for one the program makes in a loop, best of 7 over 1000000 calls, spread 6%
```

A frame step per entity, a call against a crossing out, a loop hop against an
index and a reference, and a crossing in against a call. The arithmetic that makes them mean something: on this machine a
frame of ten thousand entities is about one and a fifth milliseconds, so a
sixty-hertz budget holds roughly fourteen of those frames; a crossing out costs
six nanoseconds over a call, which is about a twentieth of a step, so `no.host`
is worth having where a frame crosses many times an entity and worth little
where it crosses once; a reference an entity is nineteen nanoseconds against the
same hundred and seventeen, which is about a sixth of a step,
so a world of entities that can be removed costs about an eighth of a frame more
than a run of entities that cannot; a crossing *in* costs less than a hop of a
program's own loop, so a host that drives a program a call at a time is not
paying for the privilege; and the two calls a frame step makes are five of its
hundred and seventeen, which is what writing a frame as helpers rather than as
one body costs. The numbers are one machine's; the shape of them is what carries. The
[reference](docs/language.md#what-running-costs) says what each leaves out and
how to read one against another.

What this is not yet: a language anybody should ship a game on. There is one
machine, one target and no optimiser worth the name, the library is small, and
the numbers in `docs/worklog.md` are all taken on one developer's machine.

`docs/worklog.md` says what was built and in what order. It is a record, not a
queue: what is worked on next comes from whoever is directing the work.
