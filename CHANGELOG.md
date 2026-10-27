# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any minor release may change the language, the C API, the ABI and the JSON the command line writes; each entry says what a program or a host has to do about it.

## [0.0.2](https://github.com/Rawframe-Project/kest/compare/v0.0.1...v0.0.2) (2026-10-27)


### Fixed

* **build:** start a release's compiler without a shell ([#21](https://github.com/Rawframe-Project/kest/issues/21)) ([f7fd4c5](https://github.com/Rawframe-Project/kest/commit/f7fd4c5df6a58c9d8cb911d3084e8bf73707500f)), closes [#15](https://github.com/Rawframe-Project/kest/issues/15)
* **cli:** hold doc, hostile and debug to schema 4 under --json ([#23](https://github.com/Rawframe-Project/kest/issues/23)) ([c49bf3f](https://github.com/Rawframe-Project/kest/commit/c49bf3f61f28a22c295c60256faaca33030723bd)), closes [#20](https://github.com/Rawframe-Project/kest/issues/20)
* **cli:** refuse fmt, lex and parse cleanly when memory runs out ([#27](https://github.com/Rawframe-Project/kest/issues/27)) ([c734add](https://github.com/Rawframe-Project/kest/commit/c734add3ef5cc54d013d4528412b7b7d5b862680)), closes [#26](https://github.com/Rawframe-Project/kest/issues/26)
* **cli:** write doc's page whole or not at all ([#25](https://github.com/Rawframe-Project/kest/issues/25)) ([b1839dc](https://github.com/Rawframe-Project/kest/commit/b1839dcdfb29fb1aa4aa68dcde59bfbe84aaf0d4)), closes [#24](https://github.com/Rawframe-Project/kest/issues/24)

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
