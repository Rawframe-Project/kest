# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any minor release may change the language, the C API, the ABI and the JSON the command line writes; each entry says what a program or a host has to do about it.

## 0.0.1 (2026-10-27)


### Fixed

* **build:** limit a release binary's name to portable characters ([#14](https://github.com/Rawframe-Project/kest/issues/14)) ([8cef4d6](https://github.com/Rawframe-Project/kest/commit/8cef4d65bde2c8507c5c324f5107ecbe6ef22877)), closes [#13](https://github.com/Rawframe-Project/kest/issues/13)
* keep a run of formatted appends inside its buffer ([#17](https://github.com/Rawframe-Project/kest/issues/17)) ([7f28428](https://github.com/Rawframe-Project/kest/commit/7f284282874fc13f37887053f6b6ce6281d8de17)), closes [#16](https://github.com/Rawframe-Project/kest/issues/16)
* take no offset from a null pointer ([#6](https://github.com/Rawframe-Project/kest/issues/6)) ([b13166c](https://github.com/Rawframe-Project/kest/commit/b13166c27d9741f019f30c74fa08cfa2121c258a)), closes [#7](https://github.com/Rawframe-Project/kest/issues/7)

## 0.0.1

The first release: a statically typed language for the gameplay half of a game, a bytecode machine an engine embeds through one C header, and a C backend for release builds.

### Added

- The language, edition 2026: integers and floats at every width with defined wrapping, `text`, structs, fixed runs, arrays, stores and references, enums that carry values, sets of flags, optionals, vectors, generics, function values, blocks, `defer`, `match`, working memory with `scratch`, and bodies that wait between frames.
- Promises the compiler proves over every body a function reaches: `no.alloc`, `no.host` and `deterministic`, the last under the `kest-det` profile 3, which gives the same bits on Linux, macOS and Windows on x86-64 and arm64.
- The embedding API in `include/kest.h` (ABI 4): building a program, machines with ceilings on stack, call depth, heap and steps, doors a program calls into its host, lending host memory, worlds kept across frames, and reloading a program under a running world.
- A mode for code nobody trusts: only the doors a host opens, a verifier over every compiled module, and ceilings refused in words.
- The command line: `check`, `run`, `call`, `tick`, `emit`, `fmt`, `profile`, `new`, `build`, `test`, `doc`, `doctor`, `hostile`, `debug`, `lsp` and `dap`, with JSON output (schema 4) for every command.
- `kest build --release` and `kest emit --c`, which write a program as C for a release build that answers what the machine answers.
- The standard library in `lib/std`, written in Kest.
- A VS Code extension with the language server and the debugger, and a playground that runs the command line in the browser.
- Release archives for Linux on x86-64 and arm64, macOS on arm64 and Windows on x86-64.
