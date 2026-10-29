# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any minor release may change the language, the C API, the ABI and the JSON the command line writes; each entry says what a program or a host has to do about it.

## 0.0.3 (2026-10-28)

### Fixed

- A machine that cannot get its heap arena, or the bookkeeping its program needs, says `K0639`; `K0638` is said only for the stack or frames a host picked. A host that saw `K0638` for a missing heap arena sees `K0639` instead. `emit` and every build no longer hand on a module whose proof could not start, `emit --c` and `build --release` free the C writer's scratch memory when compiling stops partway, and `hostile`, `new`, `doctor`, `lsp` and `dap` say `K0639` when their own memory cannot be made, instead of `K0701`, `K0605` or nothing. ([#30](https://github.com/Rawframe-Project/kest/pull/30))
- `kest lsp` answers every request. A message that is not JSON is answered with `-32700` and no id; one there was no memory to hold, or whose answer could not be written, with `-32603` and `K0639`, under its own id when that is a number or a plain name of at most 64 characters. `kest dap` says a message it could not read in the debug console and refuses a request whose response could not be written. A `Content-Length` of 2^64 - 1 no longer reads past the end of a buffer. ([#31](https://github.com/Rawframe-Project/kest/pull/31))
- `lsp` and `dap` refuse a message nested more than 64 deep and say the limit, instead of running out of stack and crashing. ([#34](https://github.com/Rawframe-Project/kest/pull/34))
- A build that runs out of memory while naming a local says `K0639` instead of crashing the compiler. ([#37](https://github.com/Rawframe-Project/kest/pull/37))
- A host that starts a machine with a door missing (`K0606`) no longer prints freed memory in the build report when memory runs short; the report says `K0639`. ([#38](https://github.com/Rawframe-Project/kest/pull/38))
- `check` and `emit` hand on their listing, and `call --json` its `needs`, only when it is whole. With no room for a part they used to print `?` for a type (and a different `signature` under `--json`), or leave lines out, and come back 0; now the run says `K0639`, and under `--json` the listing's fields are `null`. This holds on every target, the WebAssembly build the playground runs included. ([#40](https://github.com/Rawframe-Project/kest/pull/40), [#42](https://github.com/Rawframe-Project/kest/pull/42), [#44](https://github.com/Rawframe-Project/kest/pull/44))
- `fmt`, `parse`, `lex`, `check` and `run` say `K0639` (`K0658` under `--room`) and come back 1 when reading a file runs out of memory, instead of saying the file could not be read (`K0701`), or printing nothing and coming back 0. ([#48](https://github.com/Rawframe-Project/kest/pull/48))
- A function that `resumes` is no longer read as one that does not when memory runs short: `fmt` and `fmt -w` no longer drop `resumes c.at`, `parse` keeps it, and the checker and the compiler no longer blame the program with `K0368` or `K0505`. ([#49](https://github.com/Rawframe-Project/kest/pull/49))
- In a project, a manifest there was no room to read says `K0639` (`K0658` under `--room`) instead of being taken for no manifest, which made an import from one of its sources say `K0701`. ([#51](https://github.com/Rawframe-Project/kest/pull/51))
- `fmt --json` no longer hands an editor an edit that replaces the changed bytes with `""` when memory runs short, which deleted that part of the file on save. `lex --json` no longer writes `""` for a token or comment it had no room to copy or leaves out `comments`, and `lex` no longer drops every comment from its listing; each says `K0639` instead. Under `--json`, a span that holds a NUL byte is now written whole, with the byte as `\u0000`, where it used to be cut off at that byte. `lex --json`'s `cost`, `held` and `askings` now count the comment list: one allocation more, which is what `lex` takes. ([#52](https://github.com/Rawframe-Project/kest/pull/52))

### Changed

- A build in a project reads its manifest once instead of twice, and a `--room` ceiling counts it once. Under a ceiling, a manifest line with a name kest does not know can now be refused as no room rather than as an unknown name. ([#54](https://github.com/Rawframe-Project/kest/pull/54))

## 0.0.2 (2026-10-27)

### Fixed

- `kest build --release` starts the C compiler directly with an argument list instead of through `sh` or `cmd.exe`, so no path has to be quoted for a shell; on Windows a `CC` naming a `.bat` or `.cmd` file is still run by `cmd.exe`, as Windows runs every batch file. `CC` is split into words at spaces and tabs, the first naming the compiler and the rest passed as flags before kest's own; a run in double quotes stays one word, so a compiler under `C:/Program Files` can be named. A compiler that refuses and a compiler that is not there both still give `K0663`. ([#21](https://github.com/Rawframe-Project/kest/pull/21))
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
