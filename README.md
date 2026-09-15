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

What this is not yet: a language anybody should ship a game on. There is one
machine, one target and no optimiser worth the name, the library is small, and
the numbers in `docs/worklog.md` are all taken on one developer's machine.

`docs/worklog.md` says what was built and in what order; its last entry is what
is being worked on now.
