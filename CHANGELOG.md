# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any minor release may change the language, the C API, the ABI and the JSON the command line writes; each entry says what a program or a host has to do about it.

## 0.0.2 (2026-10-27)

### Fixed

- `kest build --release` starts the C compiler directly with an argument list instead of through `sh` or `cmd.exe`, so no path has to be quoted for a shell and a path holding a newline works. `CC` is split into words at spaces and tabs, the first naming the compiler and the rest passed as flags before kest's own; a run in double quotes stays one word, so a compiler under `C:/Program Files` can be named. A compiler that refuses and a compiler that is not there both still give `K0663`. ([#21](https://github.com/Rawframe-Project/kest/pull/21))
- Under `--json`, `doc` now writes a schema 4 object with `schema`, `diagnostics` and `errors`; `hostile` writes one object with `file`, `seed`, `calls`, `unmakeable` and the written file as `text` instead of ignoring the option; and `debug --json` is refused with `K0649`, pointing at `dap`. The command-line docs name the commands that report `cost` and `work`. ([#23](https://github.com/Rawframe-Project/kest/pull/23))
- `doc` writes its page whole or not at all: running out of memory partway through no longer leaves half an object before the refusal, and `doc` honours `--room`. Output with enough room is unchanged. ([#25](https://github.com/Rawframe-Project/kest/pull/25))
- `fmt`, `lex` and `parse` are refused cleanly when a file's arena cannot be made, instead of crashing, and `fmt` running out of memory now says `K0639` in text and in `--json` rather than reporting a fault in the formatter or nothing at all. ([#27](https://github.com/Rawframe-Project/kest/pull/27))

## 0.0.1 (2026-10-27)

The first release: a statically typed language for the gameplay half of a game, a bytecode machine an engine embeds through one C header, and a C backend for release builds.

### Added

- The language, edition 2026: integers and floats at every width with defined wrapping, `text`, structs, fixed runs, arrays, stores and references, enums that carry values, sets of flags, optionals, vectors, generics, function values, blocks, `defer`, `match`, working memory with `scratch`, and bodies that wait between frames.
- Promises the compiler proves over every body a function reaches: `no.alloc`, `no.host` and `deterministic`, the last under the `kest-det` profile 3, which gives the same bits on Linux on x86-64 and arm64, macOS on arm64 and Windows on x86-64.
- The embedding API in `include/kest.h` (ABI 4): building a program, machines with ceilings on stack, call depth, heap and steps, doors a program calls into its host, lending host memory, worlds kept across frames, and reloading a program under a running world.
- A mode for code nobody trusts: only the doors a host opens, a verifier over every compiled module, and ceilings refused in words.
- The command line: `check`, `run`, `call`, `tick`, `emit`, `fmt`, `lex`, `parse`, `profile`, `new`, `build`, `test`, `doc`, `doctor`, `hostile`, `debug`, `lsp` and `dap`, with `--json` output (schema 4) from the commands that check, run, build and format a program.
- `kest build --release` and `kest emit --c`, which write a program as C for a release build that answers what the machine answers.
- A release build's name is the program's base name and may use letters, digits, `.`, `_` and `-`, not starting with `-`; any other name is refused with `K0663` before anything is written. Every path in the compiler's command is quoted for the shell, and a long `CC` or a deep install is never cut short.
- The standard library in `lib/std`, written in Kest.
- A VS Code extension with the language server and the debugger, and a playground that runs the command line in the browser.
- Release archives for Linux on x86-64 and arm64, macOS on arm64 and Windows on x86-64, and a source archive.
