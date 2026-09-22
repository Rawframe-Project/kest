# Kest

A small statically typed language for the gameplay and simulation half of a
game, run by a bytecode machine a native engine embeds. C11, no dependency
beyond libc.

What it has that its neighbours have not is contracts the compiler proves
rather than conventions a reader keeps: whether a body reaches the heap,
whether it calls the host, and whether it answers the same on every machine.
Identity is a `ref<T>` the machine checks rather than an index somebody
remembers, and every ceiling a host sets is refused in words at the line that
crossed it.

**Where it sits, and what it is not.** It is a guest language, next to
Daslang, Luau, Lua, AngelScript and Quirrel. It is not a systems language and
replaces no C++, Rust or Zig: the engine stays the engine and this runs inside
it. It is not an engine, it is not for hard real time — there is a collector,
its pause is measured and written down, and a pause is a pause — it is not a
scientific-computing ecosystem, and it is not a sandbox for code that is
trying to get out. `bench/run.sh` compares it against whichever of C++,
Luau or Daslang is on the machine and leaves out the rows it cannot run;
AngelScript and Quirrel are in the set above and are not in the bench, which
is an evidence gap rather than a result. See D980 and D1061.

```
make
./kest run examples/math.kest

make embed && ./examples/embed
```

```
make PREFIX=/usr/local
sudo make install PREFIX=/usr/local
```

`make release` on Linux and `tools\package.bat` on Windows each write one
archive with a checksum beside it: the command line, the header, the static
library, the standard library, the modular source a host vendors, the VS Code
extension, the documents, the licence, and a `VERSION` written by asking the
binary in the archive what it is. There is nothing built from a program in it —
the bytecode is not a format, so what ships is the source beside the runtime.
See D989, D998 and D1000.

Installing one is unpacking it. Nothing is written outside the directory it
lands in and nothing has to be: `bin/kest` looks for the standard library in
`lib/kest` beside it, so a host that would rather not install anything at all
adds `bin` to its path, or names the binary where it is. A host embedding the
runtime compiles against `include/kest.h` and links `lib/libkest.a` — or
`lib\kest.lib` on Windows — out of the same directory, and needs nothing else
installed. `make uninstall` takes away exactly what `make install` put there,
and an unpacked archive is taken away by deleting it.

Every commit unpacks both archives into an empty directory and asks the three
questions that matter: the binary says what it is, a program it has never seen
runs through it, and a host compiles against the header and library that are in
the archive rather than the ones in this tree.

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

The reference is [the language document](docs/language.md) and reads front to
back:
what a program is made of, then values, then promises, then the boundary a host
crosses. `examples/` is the programs that run, each checking itself and
answering with which of its checks failed; the reference says what each of them
is for, and that table is held to the directory.

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

Version 0.0.1. Unstable on purpose: this said `1.0.0` for a day and withdrew
it. Three states, and nothing is in the first that has not been run.

**Implemented.** Whole numbers and floats at every width with defined wrapping
and narrowing; `text`; `bool`; structs; fixed runs; arrays; `store<T>` handing
out generation-checked `ref<T>`; enums that carry values; sets of bits;
optionals; functions as values; one body written for many types, a copy
compiled per set; `defer`; `match`; `for` and `while`; the `no.alloc`,
`no.host` and `deterministic` promises, proved by the compiler rather than
trusted; a `scratch { }` block whose working memory goes back where it was, and
which the compiler proves nothing escapes from; a bytecode VM of 173
instructions, one of which nothing compiles to and a debugger writes; diagnostics with stable codes,
spans, notes, suggested fixes and `--json`, all of a file's mistakes in one
pass, with the shape of every object a command writes versioned; one canonical
source form and a formatter that holds it; a C embedding API of 107 doors
covering compile, start, call, layout introspection with field names, lent
memory, a frame's working memory marked and put back, per-machine limits, what
a program may do, what a run did, and stopping a machine and asking it where it
is;
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
is safe to run code you do not trust. What it *is* a claim about is written
where a reader meets the ceilings, under *What a program may do* in the
reference: a boundary for code the host wrote or trusts to be cooperative, and
not a sandbox for code that is trying to get out. There is a fuzzer now, over
the six boundaries somebody else's bytes arrive through, and it has found
nothing in 1.2 million inputs — which is evidence about those six boundaries
and not a threat model.

**Not implemented, and not planned.** Live code replacement in a running
machine: a host reloads by building again and starting a new machine, and moving
the world across is the host's, through a save the program writes — the whole
protocol is in `examples/engine.c` and seven edits are driven through it by the
gate (D985). Cross-platform bitwise determinism for `sin`, `cos`, `pow` and
`atan2`: they are the host's libm and two platforms may round them differently,
where `sqrt`, `floor`, `ceil` and all integer and `f32`/`f64` arithmetic are
exactly specified and do not have that problem. `no.host` is not determinism —
that is what `deterministic` is, and the two are separate promises. No package
registry, no JIT, no generated-C path (D987), no concurrency inside the
language (D988), no networking, no graphics.

**The embedding ABI has a number and a policy.** `KEST_ABI_VERSION` is what
shape the doors are in and `kest_abi_version()` reads the same number out of the
library, so a host compares the two before it crosses and finds out before it
reads memory that means something else. What moves it, and what moves the other
three numbers beside it, is D983 and D974. Nothing about them is frozen while
this is `0.0.x` — what the reference keeps under *What 0.0.x promises, which is
nothing* is the design a stability promise would be made of rather than the
promise: that the doors a host compiled against would not change, and that a
door added at the end of the header would not be a change.

## Where this is

**v0.0.1**, and unstable on purpose. Kest published `1.0.0` on 2026-09-18 and
withdrew it a day later: nobody outside this project had written a program in
it, two independent readings from outside found foundational things still worth
changing, and one of them reproduced on the first try — a reference could name
somebody else's object once a process had made sixty-five thousand machines.
Nothing here is frozen while this is `0.0.x`. The history is intact and
`CHANGELOG.md` says what was withdrawn and on what evidence.

What is below is what is behind the number, which is the part that did not
change when the number did.

**What is decided and finished.** One resolved representation the backend reads
(D962) and the other backend built, measured and rejected by a predeclared rule
(D963). Text that carries its own length and is UTF-8 where it arrives (D964,
D971). A persistent memory story with a trial behind it: a non-moving
mark-and-sweep heap that gives every place nothing can reach back, whose pause
is 0.68 milliseconds for every megabyte still reachable and whose trigger a
host sets (D996, D1045). `scratch { }` with what it will not let out proved
over the bodies (D966, D972). `store` and `ref` placed, with a
rollover policy that refuses rather than wraps (D975). Four version numbers and
what moves each (D974, D983). A capability boundary that is the receiver of an
extern, and one honest trust claim (D981). VM-only, with the AOT trigger
measured and not met (D987).

**What it ships beside the compiler.** A language server that *is* the compiler
(D977), a source debugger whose breakpoints cost a running machine nothing
(D991), a profiler that counts and does not time (D979), a cost report that says
what was proved (D976), a formatter, a project manifest and four commands
(D982), a VS Code extension (D978), and an archive with a checksum on each of
the two platforms that have one, unpacked, run and built against in CI (D989,
D1000).

**What it is tested on.** Linux x86-64, Linux arm64, Windows x86-64 and macOS
arm64, all four built and run in CI, and held to writing the same bytes for
every example — which is what makes the deterministic profile a claim rather
than a hope, and what says it is not an x86 claim. The whole gate runs on
Linux x86-64, and the four families of workload run on both instruction sets; a fuzzer of 19200 inputs over six boundaries runs
under the sanitisers, and a longer campaign of 1.2 million was run before the
release; the thread sanitiser runs four machines of one build at once.

**What 0.0.1 does not mean.** Nothing is frozen. The C ABI has a number and a
rule for when it moves and no promise that it will not; the same is true of the
JSON a tool reads and of the deterministic profile. Every break is a decision
that says what it supersedes and why the old thing was worse, and `CHANGELOG.md`
says what a reader with a program has to do about it. That is a record, not a
guarantee. The bytecode is internal and is not a compatibility boundary at all
— what ships is the source beside the runtime, and a program is compiled by the
compiler that runs it.

What it is not a claim about: the standard library is small, there is one
machine, and the optimizer is one layer of four transformations rather than
anything a compiler writer would call an optimizer. And nobody outside this
project has written a program in it, which is the one thing a repository cannot
do for itself.

## Trying it in an hour

Everything below runs from a clean checkout, and nothing needs anything but a C
compiler. The lines are written for Linux and macOS; on Windows the build is
`tools\build.bat` from a Developer Command Prompt — there is no `make` there and
no second build system either, just the same file list written the one way MSVC
can be told it — and the rest run the same with `build\win\kest.exe` in place
of `kest`. The gate itself is Linux's: `make check` is a shell script.

1. `make && make fast` — the build and the loop, about a quarter of a second.
2. `kest new game && cd game && kest build && kest run` — a project from
   nothing to running.
3. `make engine && ./examples/engine` — a host in the shape a host has: a world
   kept between frames, memory lent a frame at a time, a save written and a
   reload done under it.
4. `make embed && ./examples/embed` — the other host, which asks every door
   this language has one after another. Read it when writing your own.
5. `kest profile examples/colony.kest` — what a run did, in counts.
6. `kest check --cost examples/colony.kest` — what the compiler proved about
   each body, and which promise it keeps and does not make.
7. `KEST_CPP=1 sh bench/run.sh` — four workloads beside a C++ baseline. Set
   `KEST_LUAU` and `KEST_DAS` too if you have them.
8. `editors/vscode` — a grammar and a client for `kest lsp`, which is this
   compiler.

Where it sits against other languages, measured on one machine and written
down with the workloads beside it, is [D980](docs/decisions.md). Why there is no
AOT path is [D987](docs/decisions.md). What it does not claim is in
*What a program may do* in [the reference](docs/language.md).

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
