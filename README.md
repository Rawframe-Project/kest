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
language itself is in [docs/language.md](docs/language.md). What actually runs
today is in [docs/worklog.md](docs/worklog.md).

What runs: whole numbers and floats at every width, text, `bool`, structs,
fixed runs, arrays, stores that hand out references, enums that carry values,
sets of bits, optionals, functions as values, one body written for many types,
`defer`, `match`, `for` and `while`, and cost contracts the compiler proves
rather than trusts. A host binds what a program asks of it, is told what every
shape is laid out as, and is refused in words when it does something it may
not. The standard library is written in Kest and held to the same rules as a
program.

What it costs to run, measured rather than remembered. `make time` takes three
numbers on the machine it is run on:

```
128 ns per entity per step, best of 7 over 10000, spread 1%
20 ns for a call and 27 ns for a crossing, which is 7 ns more, best of 7 over 1000000 calls, spread 2%
11 ns for a hop of the loop, 14 ns with an index read and 30 ns with a read through a reference, which is 16 ns more, best of 7 over 200000 reads, spread 15%
16 ns for a call in from a host and 19 ns for one the program makes in a loop, best of 7 over 1000000 calls, spread 11%
```

A frame step per entity, a call against a crossing out, a loop hop against an
index and a reference, and a crossing in against a call. The arithmetic that makes them mean something: on this machine a
frame of ten thousand entities is about one and a quarter milliseconds, so a
sixty-hertz budget holds roughly thirteen of those frames; a crossing out costs
seven nanoseconds over a call, which is about an eighteenth of a step, so
`no.host` is worth having where a frame crosses many times an entity and worth
little where it crosses once; a reference an entity is sixteen nanoseconds
against the same hundred and twenty-eight, which is about an eighth of a step,
so a world of entities that can be removed costs about an eighth of a frame more
than a run of entities that cannot; and a crossing *in* costs less than a hop of
a program's own loop, so a host that drives a program a call at a time is not
paying for the privilege. The numbers are one machine's; the shape of them is what carries. The
[reference](docs/language.md#what-running-costs) says what each leaves out and
how to read one against another.

What this is not yet: a language anybody should ship a game on. There is one
machine, one target and no optimiser worth the name, the library is small, and
the numbers in `docs/worklog.md` are all taken on one developer's machine.

`docs/worklog.md` says what was built and in what order; its last entry is what
is being worked on now.
