# Front end

From bytes on disk to one tree per file: `lexer`, `ast`, `parser`, `fmt`, `project` and `loader`.

## Lexer

[`src/lexer.c`](../../src/lexer.c) turns a file into tokens. Comments are not tokens; they are collected beside the stream for the formatter and `kest lex --json`.

- **Statement ends.** A newline ends a statement after a token a value can end with, and continues it after anything else (an operator, an opening bracket, a comma). `kest_lexer_ends_statement` lists every token kind explicitly, with no `default`, because a kind added without a decision would read as an unfinished line and swallow the next one; that has happened twice. `>` ends a statement because a type like `ref<Npc>` ends in it, which is why a comparison cannot be split after its operator.
- **Keywords.** `KEYWORDS` in `lexer.c` is searched in alphabetical order, and `check-tables.sh` holds it sorted. A misspelt keyword is a name to the lexer; `kest_nearest_keyword` lets the parser suggest the keyword it was near.
- **Line endings.** A line ends at `\n`, and at `\r` with no `\n` after it, so files from any platform give the same line and column numbers. A carriage return inside a string, written as the byte, is refused: it is invisible and cannot be told from one that is not there.
- **UTF-8.** Source is held to UTF-8. Identifiers may hold any byte above ASCII. Invisible and direction-changing characters inside strings must be written as `\u{...}`, so a file cannot look like one program and be another.

## Syntax tree and parser

[`src/ast.h`](../../src/ast.h) nodes keep spans, never copies of text: the text is `source[span]` and values are produced later, so the tree stays small and every node can say where it came from. Expression, statement and declaration nodes are 48 to 88 bytes on a 64-bit machine.

[`src/parser.c`](../../src/parser.c) is recursive descent with recovery. After an error it discards tokens to the next place a statement could begin, and when the failed line opened a block it skips to that block's closing brace so the braces after it stay in step. So one run reports every error in a file, and a run with syntax errors still yields a tree for the rest.

The parser refuses second spellings at the place they are written, with the rule beside the caret: brackets around a whole condition, `%=` and the other compound operators, a struct literal with field names, types written at a call (`store<Node>()`), `-> void`. Those refusals are the language's one-spelling rule enforced where a reader sees it.

Lists are read into a fixed buffer of 16 on the C stack and copied into the arena at their exact length, because the arena never gives anything back and a list that grew in it would leave every size it passed through behind.

`kest_parse_block_again` re-reads one block's source into a fresh tree; a walk over a struct's fields is checked once per field, and each copy needs a tree of its own to record types in.

## Formatter

[`src/fmt.c`](../../src/fmt.c) prints from the tree, not from the tokens, because only the tree can tell `ref<Npc>` from `a < b`. It returns the text rather than writing it, so the caller can leave a file alone when it is already in form.

- A file that does not parse is never formatted: writing back half a program would delete the other half.
- Comments are kept above what they precede, at its indent. A comment at the end of a line, or inside something printed as one line, moves above that thing. A comment inside a hole is refused by the lexer, because the hole's code is reformatted and there is nowhere to put it back.
- String contents and number spellings are kept as written, except code inside holes.
- A list that does not fit in 80 columns goes one item to a line, all or none; a chain of operators breaks the same way, after each operator. A name or a string longer than the line stays long, and the rest of the line is still arranged.
- Brackets are kept only where removing them changes what binds to what. The formatter asks the parser's precedence table (`kest_binary_precedence`) rather than keeping its own, because a formatter that disagreed about `|` and `+` would drop brackets that matter.
- `fmt -w` writes through a file beside the target and renames over it, so an interrupted write leaves the original.

## Projects

[`src/project.c`](../../src/project.c) reads `kest.project`: lines of `name value`, `#` comments, nothing else. An unknown name is refused, because a misspelt line reads exactly like a missing one. `kest_project_from` reads a manifest handed over as text, which is how a release binary is a project with no files around it.

## Loader

[`src/loader.c`](../../src/loader.c) reads the named file and every file it imports, each once, and parses each.

- **A module's name is its path.** `module game.npc` must be `game/npc.kest`. For a file on its own the root is the file's directory with the module path taken off; inside a project the `source` directories are the whole search list, in order. A module found under two sources is refused, since an import line cannot say which.
- **Names live under the whole module name.** The table is keyed by `game.render.draw`, so `render.math` and `physics.math` coexist in a program. What `math.` means is settled per file by its imports, and one file reaching two modules ending in `math` is the refusal.
- **Cycles are allowed.** Names are resolved after every file is read, so two modules may import each other. A file importing itself is refused.
- **A file with no `module` line** is a program and cannot be imported (`K0702`): its names live under nothing and would land in the importing file's own.
- **The standard library** is found at `$KEST_LIB`, then `lib/` beside the program, then `../lib/kest/` beside it, then the install directory compiled in (`KEST_LIB_DIR`). The first that exists wins, so a source tree uses its own library rather than an installed one. `std` is reserved.
- **Files handed over.** `kest_build_from` gives the loader a list of files to read instead of a disk; every path a read asks for is one of those or is missing.

Every file read is recorded with its size and an FNV-1a mark of its bytes, which is what `kest_build_read` lists, what a reload watches and what `check --json` reports as `read`.
