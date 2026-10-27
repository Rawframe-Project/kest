# The command line

`kest` is the compiler, the runner and every tool, and it is a host like any other: it provides the doors `std.io` and `std.os` declare. `kest help` prints the same list as this page.

## Contents

- [Commands that read a program](#commands-that-read-a-program)
- [Commands that read a file](#commands-that-read-a-file)
- [Projects and tools](#projects-and-tools)
- [Options](#options)
- [Output, JSON and exit status](#output-json-and-exit-status)
- [Editors](#editors)
- [The debugger](#the-debugger)

## Commands that read a program

These read the files named and everything they import. The first file settles where imports resolve from.

| Command | What it does |
| --- | --- |
| `check <file>...` | resolves everything and reports every error and warning at once. Prints what the first file declares, and one line per imported module. `--cost` adds what the compiler proved about each function |
| `run <file>...` | compiles and runs `main`; the exit status is what `main` answered |
| `call <file> <fn> [argument]...` | calls one function and prints what it gives. Arguments are read as literals (`min 3 7` is the `i32` one, `min 3.5 7.5` the `f32` one); a name with a dot is a function of an imported module, written as `check` prints it |
| `tick <file> [n]` | calls `onEvents(events: [i32])` once with `n` events and `onEvent(event: i32)` `n` times, whichever the file defines, and reports the crossings and what the heap did. `4,5,6` instead of a count lends those events. `--reset` throws the heap away between events |
| `emit <file>...` | prints the bytecode, with what each function needs to be called; `--c` prints the program as C instead |
| `profile <file>` | runs the program and reports, in counts, the steps of the budget, the calls of each function, the crossings into the host and the heap. There are no durations: a clock is the host's |
| `debug <file>` | runs the program with breakpoints, see [below](#the-debugger) |
| `hostile <file> [seed]` | writes the file back with calls to every door it declares, with hostile arguments, for a host to run; see [Running untrusted code](untrusted-code.md#what-the-host-owns) |

```
$ kest call examples/math.kest factorial 5
120
$ kest profile examples/math.kest
42 step(s), 15 call(s), 2 crossing(s) into the host, 48 byte(s) of heap and 48 at most
  examples.math.factorial#i32              4 call(s)
  examples.math.gcd#i32,i32                6 call(s)
```

## Commands that read a file

These read each file on its own and follow no imports, since what a file is does not depend on what it imports. One file that cannot be read does not stop the rest.

| Command | What it does |
| --- | --- |
| `fmt <file>...` | prints the file in the one form. `-w` rewrites each file and names the ones it changed; `--check` names the files `-w` would rewrite and exits non-zero. A file that does not parse is reported and never rewritten. A rewrite goes through a file beside the target and a rename, so an interrupted run leaves the original |
| `parse <file>...` | prints the syntax tree |
| `lex <file>...` | prints the tokens, whatever else is wrong with the file |
| `doc <file>` | prints what the module declares, each declaration as written with the comment above it, as Markdown. A comment directly above a declaration is about it; comments before the first declaration are about the file |

## Projects and tools

| Command | What it does |
| --- | --- |
| `new <name>` | makes a directory with a manifest, a program and a test |
| `build [file]` | compiles and says nothing when it compiles. Without a file, the project's entry. `--release` makes a binary, see [Release builds](release-builds.md) |
| `test [file]...` | runs each program and reports which answered 0; without files, the project's tests |
| `doctor [dir]` | prints the version, where the standard library is looked for and whether it was found, and what the project says about itself |
| `lsp` | the language server, see [Editors](#editors) |
| `dap` | the debugger for an editor, over the Debug Adapter Protocol |

## Options

| Option | Meaning |
| --- | --- |
| `--json` | everything the command says, as JSON |
| `--room <amount>` | the most memory the whole command may ask for, compiling and the heap the program runs on together: bytes, or a number with `K`, `M` or `G`. Compiling past it is `K0658`; the program past it is `K0617` |
| `--work <units>` | the most work compiling may do, counted the same on every machine (a word read, a piece of the tree, an instruction laid down or proved). Past it is `K0666` |
| `--fuel <steps>` | the most steps `run`, `tick` and `call` may take; past it is `K0659` |
| `--` | everything after it is the program's own; `std.os.args` hands it over |
| `--version` | prints the version, ABI, JSON schema, profile and edition |

Without `--room`, `--work` or `--fuel` there is no ceiling.

## Output, JSON and exit status

What is wrong goes to standard error and what a program or command answers goes to standard output, so `kest check x.kest > declared` keeps the answer and shows the mistakes. A command that worked writes nothing to standard error except warnings. With `--json`, the object goes to standard output and what the program itself printed goes to standard error; with `call`, the value goes to standard output alone, so `$(kest call ...)` is the value.

Every JSON object starts with `schema`, the version of the JSON's shape (4 today), and has `diagnostics` and `errors`. Each diagnostic has `severity`, `code`, `file`, `line`, `column`, `offset`, `length`, `message`, and where they apply `notes`, `suggestion` and `leftOut`. `check --json` adds every type with its layout, every function with its parameters, result, promises, what was proved about it and a `signature` hash that changes only when a caller would see a change, and every constant. `emit --json` adds the instructions, the layouts with their marks, the doors the program asks for, and what each function needs to be called. `doc --json` adds the module, what the file says about itself and every declaration. `hostile --json` adds `seed`, `calls`, `unmakeable` (the doors taking what a program cannot make) and the file it writes as `text`. `debug` is a conversation with a person and refuses `--json`; `dap` is the debugger an editor drives, and `lsp` and `dap` speak their own protocols. Every command that compiles a program (`check`, `emit`, `build`, `run`, `call`, `tick`, `profile`, `lex` and `parse`) reports `cost`, the bytes reading and compiling took, and `work`, the units of work.

The exit status is 1 when anything was refused, and otherwise what `main` returned (0 to 255). A warning is not a refusal.

## Editors

`kest lsp` answers an editor over the Language Server Protocol on standard input and output. It is the same build as `kest check`, so what the editor shows and what the command line says cannot differ. It provides diagnostics, hover, go to definition, references, document and workspace symbols, rename within a file, completion and formatting. It reads unsaved buffers, every open one, and a file importing an edited buffer is checked against what is on screen. Every request is answered: one it ran out of memory answering gets error `-32603` saying so, and a message it cannot read gets an error with no id (`-32700` when it is not JSON).

The VS Code extension is in `editors/vscode` and ships in the release archive. Its one setting, `kest.path`, is the `kest` command to run. It starts `kest lsp`, highlights Kest, and has a *Kest: debug this file* command that starts `kest dap`.

## The debugger

```
$ kest debug game.kest
> break 42
  breakpoint at game.kest:42:9 in game.step
> run
stopped at game.kest:42:9 in game.step
> where
  -> game.kest:42:9 in game.step
     game.kest:88:12 in game.main
> locals
  world                slot 0  12
> next
stopped at game.kest:43:5 in game.step
> continue
```

`break <line>` sets a breakpoint in the named file and `break other.kest:12` in another; `step` stops at the next instruction anywhere, `next` steps over calls, `where` lists the frames innermost first, and `locals` shows each slot under the name the body gave it (`not written yet` for one the body has not reached). A breakpoint is written into the bytecode and taken out again when hit, so a machine nobody is debugging pays nothing. A breakpoint inside a call a host makes back into the program from a door cannot stop there (`K0708`), because the host's C frame is gone by the time anyone could resume.

`kest dap` is the same debugger driven by an editor: breakpoints in the gutter, the call stack, a frame's variables, continue, step over, step into and step out. The program's output goes to the editor's debug console. It cannot pause a running program. A message it cannot read is said in the debug console, and every request it reads is answered.
