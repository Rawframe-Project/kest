# Kest

Kest is a programming language for games, simulations, real-time systems, and
engine embedding. It is implemented as a bytecode virtual machine in C11 with
zero dependencies.

Three goals, in priority order when they conflict:

1. **Fast.** It runs inside a frame budget. Cost is visible and provable.
2. **Easy to use.** One obvious way to write a thing. No ceremony the compiler
   could have inferred.
3. **Good with AI.** Familiar syntax, machine-readable diagnostics, all errors
   in one pass.

## Working rules

**Write code first, docs after.** A decision is recorded once the code that
implements it exists. This project's predecessor produced 33 MB of research and
zero language; do not repeat it. No benchmark harnesses, no hypothesis boards,
no test suites for things that do not exist yet.

**Everything in the repository is English.** Code, comments, commits, docs,
identifiers, error messages. No exceptions.

**Read the source before deciding.** The predecessor's first architecture
decision was superseded the day it was accepted because it was written from a
summary instead of the documents it cited.

**Four documents, and that is all.**

| File | Holds |
| --- | --- |
| `CLAUDE.md` | This file. Rules and conventions. |
| `docs/decisions.md` | Decisions and why. Append-only; supersede, do not delete. |
| `docs/language.md` | Syntax and semantics reference. |
| `docs/worklog.md` | What was built, in order. Newest last. |

## Layout

```
include/kest.h     Public embedding API. The only header a host includes.
libkest.a          The language. `kest` is one host of it and
                   `examples/embed.c` is another.
src/               Implementation. One module per .c/.h pair.
docs/              The four documents above.
lib/std/           The standard library, written in Kest and held to the
                   same rules as a program.
examples/          .kest programs that must keep working.
tools/             Build and development scripts.
                   `check-fmt.sh` holds the formatter to what it has to be:
                   its output parses, means the same, and formats to itself.
                   `check-commands.sh` holds every command to producing
                   something, because one that prints nothing looks the same
                   as one that works.
```

Pipeline, in dependency order. Each module depends only on those above it:

```
diag    diagnostics, source spans, JSON output
mem     arena allocator, growable buffers
str     string interning
lexer   source -> tokens
ast     syntax tree node definitions
parser  tokens -> ast
loader  follows imports and parses every file reachable
types   type representation, declarations, name lookup
check    function bodies against those declarations
contract proves the `no.alloc` promises
value   runtime values, the instruction set, the disassembler
fmt     ast -> the one form the language has
compile ast -> bytecode
vm      bytecode execution
build   the stages as one thing, which is what a host has
main    CLI
```

## Modularity

- One module is one `.c` and one `.h` with the same name. No orphan headers.
- A `.c` file over ~600 lines is a signal to split. Not a hard error, a signal.
- No cyclic dependencies. If two modules need each other, a third is missing.
- No global mutable state. Every module hangs off a context struct the caller
  owns and passes explicitly. This makes the VM re-entrant and embeddable.
- A header includes only what its own declarations need. Implementation
  includes go in the `.c`.
- Compile-time memory (tokens, AST, types) lives in an arena and is freed in
  one call. Do not write per-node `free`.

## C conventions

C11, `-std=c11 -Wall -Wextra -Werror`, no dependencies beyond libc.

| Thing | Style | Example |
| --- | --- | --- |
| Public function | `kest_<module>_<verb>` | `kest_lexer_next` |
| Internal function | `static`, plain snake_case | `scan_string` |
| Type | `Kest` + PascalCase | `KestToken`, `KestVm` |
| Enum constant | `KEST_` + SCREAMING_SNAKE | `KEST_TOK_IDENT` |
| Struct field | snake_case | `line_start` |
| Macro | SCREAMING_SNAKE | `KEST_MAX_LOCALS` |
| File | lowercase, no underscores | `lexer.c`, `vm.c` |

- Braces always, including one-line bodies.
- Declare variables at first use, not at block top.
- `size_t` for sizes and counts, fixed-width types (`uint32_t`) when the width
  is part of the format.
- Functions that can fail return a status or a tagged result. Never a bare
  sentinel that the caller can forget to check.
- No `assert` in place of a diagnostic. An assert is for a compiler bug; a
  diagnostic is for a user mistake.

## Comments

- English. Explain **why**, never **what**. If the code needs a comment to say
  what it does, rename something instead.
- Public API is documented in the header, above the declaration. The `.c` gets
  implementation notes only.
- No file banner blocks, no divider bars, no ASCII art, no changelog comments,
  no commented-out code, no `TODO` without a name and a reason.
- `//` for everything. `/* */` only when disabling a block temporarily, which
  should not survive to a commit.

## Commits

```
<module>: <imperative summary in lower case>

Why the change was needed, when it is not obvious from the diff.
```

- Summary under 72 characters. `lexer: add interpolated string tokens`
- Module is a src module name, or `docs`, `build`, `examples`.
- One logical change per commit. A rename and a behaviour change are two.
- No attribution or co-author lines.

## Diagnostics

Diagnostics are a feature, not error handling. Rules that are not negotiable:

- **Never stop at the first error.** Recover and keep going. One run reports
  everything wrong with the file.
- Every diagnostic has a stable code (`K0102`), a span, and a message.
- Where a fix is knowable, suggest it. Unknown name reports the nearest match.
- When a diagnostic is about something deeper than the site it was raised at,
  report the path down to the body responsible, not just the entry point.
- A diagnostic about more than one place carries a note per place, each with
  its own line and caret. Prose naming a line number is not that.
- `--json` emits everything a command says as machine-readable JSON: the
  same diagnostics, and for `check` what the program holds.
- Codes are allocated by stage and never reused: `K01xx` lexer, `K02xx`
  parser, `K03xx` types and bodies, `K04xx` cost contracts, `K05xx` what the
  compiler cannot emit yet, `K06xx` what fails while running, `K07xx` what
  cannot be read.

## What we are not doing

No LLM or network primitives in the language core; that is a library. No
tolerant parsing of near-miss syntax. No test suite, benchmark harness, or
research programme until there is a language to point them at.
