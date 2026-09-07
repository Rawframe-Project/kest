# Worklog

What was built, in order, newest last. One entry per session. An entry says
what runs, not what is planned.

## 2026-09-07, repository set up

Read the predecessor repository at `Rawframe-Project/kest-research` and took
four results from it: the measured host boundary shapes, the measured cost of
having only managed references, the measured inference boundary for the
allocation contract, and the two `.kest` sketches, whose syntax this project
adopts.

Wrote `CLAUDE.md`, `docs/decisions.md` with D001 to D009, and
`docs/language.md`. Repository builds an empty `kest` binary that reports its
version.

**Runs:** `make`, `./kest --version`.
**Next:** lexer.

## 2026-09-07, lexer

Three modules. `mem` is a block-chained arena, because nothing the compiler
produces before the program runs outlives compilation. `diag` holds spans,
source line lookup, and both renderings; column counts characters rather than
bytes so a caret lands under the right glyph in a UTF-8 identifier. `lexer`
turns source into tokens.

Two things in the lexer are decisions rather than mechanics. A line break is a
statement terminator only when the previous token could have ended a statement
and no bracket is open, so a line ending in `*` or `,` continues without any
rule the author has to remember. And every byte above ASCII starts an
identifier, which makes `let hız = 5` legal without carrying Unicode tables.

`;` and `/* */` are refused with their own codes rather than falling through to
"unexpected character", because D004 makes the parser strict and D008 makes the
message carry the cost of that.

**Runs:** `kest lex <file>`, `--errors=json`. Six errors in one broken file
report in one pass.
**Next:** ast and parser.

## 2026-09-07, parser

`ast` defines the tree and prints it; `parser` is recursive descent with
precedence climbing over six levels. Literals and names keep only their span,
so the tree stays small and the text is read from the source when it is needed.

The whole of `examples/player.kest` and `examples/frame.kest` parses:
`ref<Npc>?`, `[ref<Quest>]`, `extern fn Clock.now() -> u64 no.alloc`,
`while`, `for ... in`, compound assignment, and correct precedence and
associativity.

Recovery has two levels and the difference matters. A broken statement
recovers to the next line, so the rest of a body still reports its own errors.
A broken *signature* recovers to the next declaration, because a body measured
against a signature nobody has produces only noise; the first version of this
reported a spurious "expected a declaration" at the first `let` of the
abandoned body. Three real errors in a four-error file, no cascades.

`no.alloc` is spelled with a dot rather than as a keyword so the namespace can
hold further contracts without spending more keywords on them.

**Runs:** `kest parse <file>`, and `--errors=json` on both commands. Clean
under ASan and UBSan on every example.
**Next:** types.
