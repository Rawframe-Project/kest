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
another project looks. The compiler finds the library at `$KEST_LIB`, or
beside itself, or where the build it came from was told it would be put.

The second command above is a host that is not the command line: it compiles a file, makes a
machine with its own limits, and keeps a world between frames.

Fast, easy to use, and good to work on with an AI. What that means concretely,
and what it cost to decide, is in [docs/decisions.md](docs/decisions.md). The
language itself is in [docs/language.md](docs/language.md). What actually runs
today is in [docs/worklog.md](docs/worklog.md).

Status: early. Scalars, control flow and calls run; structs and arrays do
not yet.
