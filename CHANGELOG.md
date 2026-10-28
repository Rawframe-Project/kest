# Changelog

All notable changes to this project are recorded here, grouped as Added, Changed, Fixed, Removed and Security. The project uses [Semantic Versioning](https://semver.org/). Before 1.0.0, any minor release may change the language, the C API, the ABI and the JSON the command line writes; each entry says what a program or a host has to do about it.

## [0.0.3](https://github.com/Rawframe-Project/kest/compare/v0.0.2...v0.0.3) (2026-10-28)


### Fixed

* **api:** never carry a machine's words past its arena ([#38](https://github.com/Rawframe-Project/kest/issues/38)) ([800d4db](https://github.com/Rawframe-Project/kest/commit/800d4db4385f7689a544e59a37a37255464ccd13)), closes [#36](https://github.com/Rawframe-Project/kest/issues/36)
* **cli:** never hand on a listing with a part it had no room for ([#40](https://github.com/Rawframe-Project/kest/issues/40)) ([495af8c](https://github.com/Rawframe-Project/kest/commit/495af8cc9d17b4cb022c491046017f402d6968a7)), closes [#39](https://github.com/Rawframe-Project/kest/issues/39)
* **cli:** never hand on a partial lex or fmt listing ([#52](https://github.com/Rawframe-Project/kest/issues/52)) ([4f1c4a4](https://github.com/Rawframe-Project/kest/commit/4f1c4a4368a790f670562572d386023f4f99d7eb)), closes [#47](https://github.com/Rawframe-Project/kest/issues/47)
* **cli:** say a manifest had no room rather than that it is not there ([#51](https://github.com/Rawframe-Project/kest/issues/51)) ([92d72bd](https://github.com/Rawframe-Project/kest/commit/92d72bdfe2129312dc9aa1beaafa02740a3f7d98)), closes [#50](https://github.com/Rawframe-Project/kest/issues/50)
* **cli:** say K0639 when reading a file runs out of memory ([#48](https://github.com/Rawframe-Project/kest/issues/48)) ([a7e4422](https://github.com/Rawframe-Project/kest/commit/a7e4422b36329a5b7a27dbd90b0ddd100eecb00e)), closes [#45](https://github.com/Rawframe-Project/kest/issues/45)
* **cli:** say K0639 wherever a command runs out of memory ([#30](https://github.com/Rawframe-Project/kest/issues/30)) ([d89156c](https://github.com/Rawframe-Project/kest/commit/d89156c22b1507183902090342c01b47bf466a17)), closes [#28](https://github.com/Rawframe-Project/kest/issues/28)
* **cli:** say when call --json had no room for its needs ([#42](https://github.com/Rawframe-Project/kest/issues/42)) ([2e794dd](https://github.com/Rawframe-Project/kest/commit/2e794dde4912a78551632a71558e745b52bfe564)), closes [#41](https://github.com/Rawframe-Project/kest/issues/41)
* **cli:** set a listing aside in memory on every target ([#44](https://github.com/Rawframe-Project/kest/issues/44)) ([59b5c9e](https://github.com/Rawframe-Project/kest/commit/59b5c9e3bc3272488eebc1dbebe00c80b1729e6a)), closes [#43](https://github.com/Rawframe-Project/kest/issues/43)
* **compiler:** count a local only once it has its name ([#37](https://github.com/Rawframe-Project/kest/issues/37)) ([2c3b3f8](https://github.com/Rawframe-Project/kest/commit/2c3b3f8abcfd7bcd9a034ad178ea11be8cc8faa9)), closes [#35](https://github.com/Rawframe-Project/kest/issues/35)
* **lsp:** answer every request, even one that could not be read ([#31](https://github.com/Rawframe-Project/kest/issues/31)) ([602210d](https://github.com/Rawframe-Project/kest/commit/602210de5b37d205a8c76c1f10bb2caa153c76e6)), closes [#29](https://github.com/Rawframe-Project/kest/issues/29)
* **lsp:** refuse a message nested past a fixed depth ([#34](https://github.com/Rawframe-Project/kest/issues/34)) ([ea8521f](https://github.com/Rawframe-Project/kest/commit/ea8521fff00d25e3958d6990182abf5d201ab291)), closes [#32](https://github.com/Rawframe-Project/kest/issues/32)
* **parser:** read a resumes clause back without memory ([#49](https://github.com/Rawframe-Project/kest/issues/49)) ([13ad1a5](https://github.com/Rawframe-Project/kest/commit/13ad1a5048c5d856367330016df56d035a322fb7)), closes [#46](https://github.com/Rawframe-Project/kest/issues/46)


### Changed

* **cli:** read a project's manifest once ([#54](https://github.com/Rawframe-Project/kest/issues/54)) ([6bba3bb](https://github.com/Rawframe-Project/kest/commit/6bba3bbd3ffb57abba844dd77d850b5c9e2b3af9)), closes [#53](https://github.com/Rawframe-Project/kest/issues/53)

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
