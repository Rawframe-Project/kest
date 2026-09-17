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
boundary — in about a tenth of a second. `make check` is the whole gate and
takes minutes.

## What is and is not there

Version 0.1.0. Three states, and nothing is in the first that has not been run.

**Implemented.** Whole numbers and floats at every width with defined wrapping
and narrowing; `text`; `bool`; structs; fixed runs; arrays; `store<T>` handing
out generation-checked `ref<T>`; enums that carry values; sets of bits;
optionals; functions as values; one body written for many types, a copy
compiled per set; `defer`; `match`; `for` and `while`; the `no.alloc` and
`no.host` cost contracts, proved by the compiler rather than trusted; a
bytecode VM of 152 instructions; diagnostics with stable codes, spans, notes,
suggested fixes and `--json`, all of a file's mistakes in one pass; one
canonical source form and a formatter that holds it; a C embedding API of 60
doors covering compile, start, call, layout introspection, lent memory and
per-machine limits; and a standard library of eight modules written in Kest and
held to the same rules as a program.

**Partial.** The standard library is nine modules — `io`, `math`, `text`,
`table`, `sort`, `random`, `vec`, `hash`, `os` — which is small. Change
detection for reload is there — per-file and whole-program marks, and the list
of files a build actually read — and it is detection and rebuild only; there is
a host-mediated schema migration prototype outside this repository and nothing
in it. `check --json` carries a fingerprint per declaration, folded from the
qualified name, the types and the promises: it is a signature fingerprint and
not a semantic identity, and it does not survive a rename.

**Resource control.** A host sets four ceilings — stack slots, call depth, heap
bytes and a budget in steps — and each is refused in words at the instruction
that crossed it. `kest_cancel` stops a running program from another thread. That
is enough to stop a program that will not stop; it is **not** a claim that this
is safe to run code you do not trust, which would need a threat model and fuzz
evidence this project does not have.

**Not implemented.** Live code replacement in a running machine, and any
migration of live state across a rebuild: a host reloads by building again and
starting a new machine, and what the old one held is the host's problem.
Cross-platform bitwise determinism: `sin`, `cos`, `pow` and `atan2` are the
host's libm and two platforms may round them differently; `sqrt`, `floor`,
`ceil` and all integer and `f32`/`f64` arithmetic are exactly specified and do
not have that problem. `no.host` is not determinism — it says a body does not
cross the boundary, which is a different thing, though every operation that
could differ between platforms is behind a door it forbids. No package manager,
no debugger, no language server, no JIT, no concurrency, no networking, no
graphics.

**Tested on** x86-64 Linux with GCC 15.2 only. The code is C11 and libc and
nothing else, so it should build elsewhere; nobody has, and this project does
not call a thing that was never run a thing that works. Windows is unverified;
`docs/language.md` says what a port would read first.

What it costs to run, measured rather than remembered. `make time` takes four
numbers on the machine it is run on:

```
117 ns per entity per step, 5 ns of it the two calls it makes, best of 7 over 10000, spread 13%
19 ns for a call and 25 ns for a crossing, which is 6 ns more, best of 7 over 1000000 calls, spread 3%
10 ns for a hop of the loop, 12 ns with an index read and 31 ns with a read through a reference, which is 19 ns more, best of 7 over 200000 reads, spread 14%
15 ns for a call in from a host and 18 ns for one the program makes in a loop, best of 7 over 1000000 calls, spread 6%
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
