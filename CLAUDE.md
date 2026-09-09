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
| `docs/decisions.md` | Decisions and why. Append-only; supersede, do not delete, and say `supersedes` in that word so the list at the top can be held to it. What a later one replaced is listed there, because nothing here is edited and an entry that is no longer what this project does reads exactly like one that is. |
| `docs/language.md` | Syntax and semantics reference. |
| `docs/worklog.md` | What was built, in order. Newest last. An entry is a heading, what was done and what it turned up, a `**Runs:**` line saying what was run to believe it, and — on the last one — a `**Next:**` line, which is what the next turn reads. `check-docs.sh` holds the two lines. |

## Layout

```
include/kest.h     Public embedding API. The only header a host includes.
libkest.a          The language. `kest` is one host of it and
                   `examples/embed.c` is another. `make embed-debug` builds
                   that one under the sanitisers, which is the only thing
                   that crosses the public boundary in both directions.
src/               Implementation. One module per .c/.h pair.
docs/              The four documents above.
lib/std/           The standard library, written in Kest and held to the
                   same rules as a program.
                   A file's `module` line has to match where the file is:
                   an import is a path, so one that does not is a file
                   nothing can import. `check.sh` holds every `.kest` in the
                   tree to it.
examples/          .kest programs that must keep working. Each one checks
                   itself and answers with which check failed, so a number is
                   a place in a file; `check.sh` prints the `return` that
                   matches. One that only resolves earns that only while
                   nothing can run it (D222). A `main` that gives nothing back is a shape the
                   language has and no example is written that way, so
                   `check.sh` runs one of its own.
tools/             Build and development scripts. `make check` runs all of
                   them and everything else, and is what "it passes" means.
                   `frame.kest` is the one measurement, run by `make time`.
                   Kest under `tools` is an instrument: held to resolving and
                   to formatting, not to running.
                   `check-fmt.sh` holds the formatter to what it has to be:
                   its output parses, means the same, keeps every comment
                   somebody wrote, formats to itself, leaves a file it cannot
                   read exactly as it found it, and gives back the one form for
                   a file whose lines end with two characters. Over the tree, and over
                   a file nobody has formatted, which no file here is. And
                   the tree to being written in that form already, because a
                   language with one form is written in it.
                   `check-tables.sh` holds every list that has to name
                   everything of its kind: the token names, the instruction
                   names, the keywords, the builtins, and the pipeline above
                   against the modules in `src`.
                   `check-header.sh` holds the public header to standing on
                   its own: a host that includes it and nothing else links
                   against the library and libc.
                   `check-dead.sh` holds every header to declaring what is
                   there and nothing that nothing calls, the public one
                   through the two hosts in this tree — every function it
                   declares is called by one of them, which is what makes the
                   header's own sentence about where to look for an example
                   true — and the host `check.sh`
                   writes to calling only what those two call — it is compiled
                   and thrown away, so nothing here would hold a name it was
                   the only user of, and the library written
                   in Kest to being named: a function, a constant or a shape
                   nothing anywhere names is one nothing has ever run. What counts as naming it is what
                   `check --json` says, which is the checker's answer and not a
                   reader's: per function rather than per name, so one of four
                   called `min` is the one that was meant.
                   `check-docs.sh` holds every decision a comment names to
                   being one that was written, and every example to being named
                   where a reader looks for one — the reference says what each runs,
                   and a file in the tree and not on that list, or on it and
                   not in the tree, is a check that fails. It also holds every
                   `kest` block in the reference
                   and the decisions to being syntax this language has, every
                   diagnostic they print to being a message a run of this
                   compiler says, and every name in a `json` block to being
                   one a run writes and every name a run writes to being one
                   a block shows. The worklog is not held to it: it
                   records what went wrong, so it holds code the parser
                   refuses on purpose.
                   `check-costs.sh` asks the library what twice as much
                   costs: every function that makes text, one size against
                   another, and every module that can reach the heap driven in
                   a loop. What to ask comes from the library rather than from
                   a list; what a command line cannot hand over — anything
                   taking an array — has to be asked by the host that can; and
                   a module every one of whose functions promises `no.alloc` is
                   not asked at all, because the compiler has already proved
                   the answer. What it weighs is memory, which is what a run
                   can be asked for without timing it. It also reads the two
                   hosts in this tree for the promises they were made to keep:
                   a bound function under an `extern ... no.alloc` may not make
                   text or lend an array, which is a thing to read rather than
                   to run, and so holds a path nothing here runs.
                   `check.sh` holds every `.kest` file in the tree to saying
                   nothing about itself: a project that warns everybody else
                   about a name nothing reaches and carries one is a project
                   nobody should believe.
                   `check-ceilings.sh` reaches every number a program can run
                   into: the three refusals that say it has as much of
                   something as it can be told it has, and every row of the
                   reference's table of what there is a most of, each by a
                   program with one too many in it. A row nothing runs into is
                   a message nobody has seen, which it says.
                   Two of them are a minute and four gigabytes away and the
                   third is thirty-two gigabytes away, so it lowers the ceiling
                   in a copy of the tree and reaches all three in a hundred
                   lines of work each.
                   `check-backstops.sh` puts each check this project makes
                   about its own work out of order, in a copy of the tree, and
                   requires it to be caught. Every check named here has a hole
                   of its own, which is the only thing that says a check does
                   what its sentence says. The holes: the compiler's three about what
                   it emitted and one about what the checker let through, the
                   machine's two — the call it cannot see through, and a
                   handle used as something it is not — the compiler's two
                   about two functions under one name and two copies of a
                   shape that are one type, the formatter's three
                   about the file it cannot read, the words it has to keep and
                   the file it was only asked about, the other host's own
                   about where a type's pieces are, the reference's own about
                   a message it quotes and about a number a program can run
                   into, the ceilings' own about the one refusal nothing else
                   reaches, the sanitised build's three about memory used
                   after it was given back or past the end of it, one a host's
                   own array, one a block the arena handed out and one a heap
                   that ran out, the other host's own about a call that keeps a
                   byte of the heap and about a library function that copies
                   everything every time, the machine's own about a host that
                   allocates under a promise made for it — the only holes
                   here caught by a build rather than by a message, since a
                   release build answers both with a number and an exit status
                   of nought — and this list's
                   own about what a header declares, about a library
                   name nothing has ever reached, and about a walk that says
                   less to a tool than to a reader, about an escape nothing
                   names, about a builtin the promise's proof has no opinion
                   about, about a chunk that carries less than its declaration
                   promised, about a measurement of where a host is called from
                   that is short of what it turns out to be, about a frame that
                   agrees with whatever a host says is in it, about a word read
                   as a number whatever it says, about a handle another machine
                   made, and about a machine that keeps the host it was started
                   with.
                   A net nobody has seen catch anything is indistinguishable
                   from no net. What went wrong is said before the list of what
                   was caught, because a miss thirty lines down is a miss
                   nobody reads. The holes are put out of order at once rather
                   than one after another — none of them reads what another
                   writes — and reported in the order they are written, because
                   a list that reports itself in whatever order finished first
                   is a list nobody can read twice.
                   `check-commands.sh` holds every command to producing
                   something, because one that prints nothing looks the same
                   as one that works, and holds the two forms of `check`,
                   `emit` and `lex` to saying the same thing: one is read by a
                   person and the other by a tool, and a kind of shape added to
                   one and not the other is a type nothing machine-readable can
                   see. It also holds what a chunk carries to what the
                   declaration promised, which is two commands rather than two
                   forms of one: at the call the promise's second proof cannot
                   see through, what the machine reads is the chunk.
                   Over every file in the tree, over a file
                   that holds nothing, over one asking the host for a name it
                   has not got, and over a path that is not a file at all —
                   none of which anything here is.
```

Pipeline, in dependency order. Each module depends only on those above it:

```
kest     the public API: what a host sees, and the host itself
mem      arena allocator, growable buffers
diag     diagnostics, source spans, how near two words are, JSON output
lexer    source -> tokens
ast      syntax tree node definitions
parser   tokens -> ast
loader   follows imports and parses every file reachable
types    type representation, declarations, name lookup
check    function bodies against those declarations
contract proves the `no.alloc` promises
value    runtime values, the instruction set, the disassembler
fmt      ast -> the one form the language has
compile  ast -> bytecode
vm       bytecode execution
build    the stages as one thing, which is what a host has
main     CLI
```

`check-tables.sh` holds this list to the tree: every module is named once, in
an order where a module includes only what is above it.

## Checking

The sweeps that ask one thing of many files — every command over every file,
and the same under the sanitisers — do eight at a time. A script has no job
control, so `jobs` says nothing in one: what holds the number down is counting
them, eight started and waited for and then eight more. What each says is kept
and read back in the order the files were given.

Under the sanitisers, the three commands that read each file on its own —
`lex`, `parse`, `fmt` — are asked about every file in one run, because a run
under the sanitiser pays for its shadow memory before it reads a byte. What
that loses is which file, so a run that says anything is asked again file by
file, and the slow way happens only when something is wrong.

Every check makes a scratch directory of its own rather than writing to fixed
names under `/tmp`, and none of them writes anything another reads — `fmt -w`
is tried on a copy rather than on the file, because everything here reads these
files. That is what lets `check.sh` ask all nine at once and read what they say
back in the order they are written, and it is what two runs writing to one file
cost: a check that failed one time in six for no reason anybody could see.

`make check` is the whole of it: both builds, both hosts, every example run or
resolved, every command against every file under the sanitisers, every tool
named above, and a handful of files written on the spot for what no file in the
tree is: one that holds nothing, one whose lines end the way another machine
ends them, one whose `main` gives nothing back, and a third host of ten lines
that asks what came back before anything came back. There is no count of them here, because a count is a thing
that goes stale; `check-tables.sh` holds the three lists that say which they
are — the files in `tools`, the ones named above, and the ones `check.sh` runs
— to each other. It takes no list of files, because a list is the thing that
goes stale. Nothing is finished until it passes.

The sanitised build is told what the arena handed out: a block is poisoned
when it is taken and each allocation is opened to its own size, with a gap
after it that stays poisoned. A read one element past the end of something is
then a report rather than whatever was next, which is what it is in a release
build and what it was here in both. The release build includes nothing but ISO
C; the header this uses is the sanitiser's, in a build already standing on it.

`make time` prints one number and is not part of `check`, because a duration
is not a pass or a fail. There is one measurement and there is nowhere it is
written down. If a second one is ever wanted, that is a decision, not a file.

## Lists that have to be complete

Some lists have to name everything of their kind, and every one of them has
been wrong at least once. None is held by a comment.

| The list | Where | Held to it by |
| --- | --- | --- |
| What a value can be written as | `types.c` and `vm.c` | no `default`: a new type tag stops the build in both |
| What a line may end after | `lexer.c` | no `default`: a new token kind stops the build |
| The token names | `lexer.c` | `_Static_assert` on how many, `check-tables.sh` on which |
| The instruction names | `value.c` | the same two |
| Which instructions reach the heap | `value.c` | no `default`: a new instruction stops the build, in the proof that reads what was emitted |
| What a piece of a layout can be | `kest.h` and `value.c` | a `_Static_assert` on how many, and a name in `SCALARS` for each |
| The keywords | `lexer.c` | `check-tables.sh`, against the list the reference prints |
| The builtin names | `check.c`, `compile.c` and `contract.c` | `check-tables.sh`, holding what the checker asks about, what the compiler emits for, what a message suggests from, and what the promise's proof knows each of them does to the heap |
| What a builtin calls what it takes | `check.c` | `check-tables.sh`, against the signatures the reference prints |
| The names the command line calls | `main.c` | one `#define` each, and every list built from them; `main` is the language's and is in `kest.h` |
| The commands the command line has | `main.c` | `check-tables.sh`, holding what `main` answers to against what `help` prints |
| What a comment is | `lexer.c` | `check-fmt.sh`, holding its own reading of a file against the compiler's |
| The escapes and what each means | `lexer.c` | one table, read by what accepts them, what turns them into bytes and what names them; `check-tables.sh` asks a run which it takes and holds that to the reference |
| The numbers a program can run into | `compile.c`, `check.c`, `types.c`, `vm.c` | `check-tables.sh`, against the table the reference prints; `check-ceilings.sh`, against a program with one too many in it, which has to be told the number the table says |
| The modules and what they may include | this file's pipeline | `check-tables.sh`, against `src` and against every `#include` |
| The checks this project makes | this file's layout | `check-tables.sh`, against `tools` and against what `check.sh` runs; `check-backstops.sh`, against the holes, so every check has been seen catching something |
| The files this file names | this file's layout | `check-tables.sh`, against the tree: a name that has moved describes something that is not there |

A `default` in a switch over one of these is how a thing gets added without
anybody deciding about it. Where a switch cannot say it — a table indexed by an
enum — the count is asserted while building and the spelling is checked by a
tool.

## Words

A word is a keyword only when a program that used it as a name would be
ambiguous where it stands. Everything else is a word: `flags` declares a type
where a declaration begins and is a name everywhere else, and so may the next
one. The cost of a keyword is paid by every program that wanted the name, and
it is paid every day, so it is worth being sure.

No word is kept back for a feature that does not exist. "Reserved for later" is
a promise, and a language that makes one it is not keeping takes a name from
somebody today for something nobody has designed. `type` was that until D179.

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
  compiler cannot emit, `K06xx` what fails while running and what the command
  line asked a program for and could not have, `K07xx` what cannot be read.
- The marks round a thing go round the whole of it and never inside it:
  `(i32, i32)` is one thing a function takes and `` `i32`, `i32` `` is two
  things that are not what it takes. A list of things is a list of marked
  things, each whole.
- A message says whose mistake it is. What a program can be written to avoid is
  a diagnostic about the program; what only this project can cause says so, in
  the words `K0405` and `K0505` use. "Not yet" is a promise, and the compiler
  should not make one it is not keeping.

## What we are not doing

No LLM or network primitives in the language core; that is a library. No
tolerant parsing of near-miss syntax. No test suite, benchmark harness, or
research programme until there is a language to point them at.
