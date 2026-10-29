# Getting started

Kest is a statically typed language for the gameplay half of a game: rules, AI, quests and simulation. A program is compiled to bytecode and run by a small machine that an engine embeds through one C header. The same `kest` command line runs programs on their own, which is how this page uses it.

## Getting the command line

**From a release.** Each release on the [releases page](https://github.com/Rawframe-Project/kest/releases) carries an archive for Linux on x86-64 and arm64, macOS on arm64 and Windows on x86-64, with every file's checksum in `SHA256SUMS` and a build provenance attestation (`gh attestation verify <file> -R Rawframe-Project/kest`). Unpack it anywhere and put its `bin` directory on the path. The archive holds the command line, the header, the static library, the standard library and the VS Code extension; nothing else needs to be installed.

**In the browser.** The [playground](https://rawframe-project.github.io/kest/) runs the command line built as WebAssembly inside the page.

**From source.** Kest is C11 with no dependency beyond the C library; it builds with CMake 3.25 or newer. On Linux and macOS, with Ninja:

```sh
git clone https://github.com/Rawframe-Project/kest.git
cd kest
cmake --preset dev
cmake --build --preset dev
out/dev/kest run examples/math.kest
```

On Windows, with Visual Studio, use the `windows-msvc` preset; the command line is then `out\windows-msvc\kest.exe`. Everything a build makes is under `out/<preset>`, next to a link to the standard library, so the command line there finds it.

To install into a prefix, with the standard library where the command line looks for it:

```sh
cmake -S . -B out/install -DCMAKE_INSTALL_PREFIX=/usr/local -DKEST_CHECKED=OFF
cmake --build out/install
sudo cmake --install out/install
```

`out/install/install_manifest.txt` lists every file the install put there.

## A first program

A file with a `main` is a program. `main` takes nothing and gives an `i32`, which becomes the exit status, or gives nothing, which exits with 0.

```kest
import std.io

fn main() -> i32 {
    io.print("hello from kest")
    return 0
}
```

```text
hello from kest
```

`kest run hello.kest` compiles and runs it. Printing is not built into the language: `std.io` declares it as a door the host provides, and the command line is a host that provides it. [Embedding Kest](embedding.md) explains doors.

The commands used every day:

| Command | What it does |
| --- | --- |
| `kest run file.kest` | compiles the program and runs `main` |
| `kest check file.kest` | reports everything wrong with the program at once, without running it |
| `kest fmt -w file.kest` | rewrites the file in the one layout the language has |
| `kest test` | runs the project's tests |
| `kest doc file.kest` | prints what a module declares, with the comment above each declaration |

Every command takes `--json` and then reports in JSON for tools. [The command line](command-line.md) lists every command and option.

## A project

A single file needs nothing around it. A directory becomes a project when it has a `kest.project` file, and `kest new` makes one:

```sh
kest new demo
cd demo
kest run
kest test
```

That gives a manifest, a program in `src/main.kest` and a test in `tests/adding.kest`. Inside a project, `kest check`, `kest build` and `kest run` work on the entry the manifest names when no file is given, and `kest test` runs every `.kest` file directly under the tests directory.

`kest.project` is lines of `name value`, and `#` starts a comment:

| Line | Meaning |
| --- | --- |
| `project demo` | the project's name |
| `entry src/main.kest` | the program `check`, `build` and `run` use when no file is named |
| `source src` | a directory the project's modules live under; one line per directory |
| `tests tests` | the directory `kest test` runs |
| `kest 0.0.1` | the version the project was written against |
| `profile kest-det 3` | the deterministic profile it is written under, see [Determinism](determinism.md) |
| `edition 2026` | the edition of the language it is written in, see [Versions and editions](editions.md) |

An unknown name in the manifest is an error rather than a line that is skipped: every command run in the project refuses it with `K0709`, and `kest doctor` says what it read.

**Modules and sources.** A module's name is where its file is: `module game.npc` lives in `game/npc.kest` under one of the `source` directories. Inside a project the `source` lines are the whole list of places an import is looked for, in the order they are written. A dependency is another `source` line pointing at wherever its code was put; there is no package registry and nothing is downloaded. A module found under two sources is an error, because the import line cannot say which one it means.

**Tests.** A test is a program that checks itself and answers with which check failed, so 0 is a pass:

```kest
module adding

fn add3(a: i32) -> i32 {
    return a + 3
}

fn main() -> i32 {
    if add3(1) != 4 {
        return 1
    }
    if add3(-3) != 0 {
        return 2
    }
    return 0
}
```

`kest test adding.kest` runs it and reports it passed. A project whose manifest names a tests directory with no tests in it fails `kest test`, so a run of nothing is never reported as a pass.

## When something is off

`kest doctor` prints the version, where the command line looks for the standard library and whether it found it, and what the project in the current directory says about itself.

The standard library is found at `$KEST_LIB` if that is set, then in `lib/` beside the command line (a source tree), then in `../lib/kest/` beside it (an install or an unpacked archive), and last where the build was told it would be installed. The first one that exists is used.

## Where to go next

- [Coming from Lua or Rust](coming-from-lua-or-rust.md) lists what is different here, from the mistakes newcomers make.
- [Language basics](language-basics.md), [Data](data.md), [Text](text.md) and [Functions](functions.md) cover the language.
- [Promises](promises.md) covers what the compiler proves about a function.
- [Embedding Kest](embedding.md) covers running Kest inside an engine.
