# Tooling

Diagnostics, JSON, and the tools built on the same build: the language server, the debugger and its adapter, `kest doc` and the command line.

## Diagnostics

[`src/diag.c`](../../src/diag.c) is how anything is said. A diagnostic is a severity, a code, a span (a half-open byte range in a file; a zero length means the file as a whole), a message, an optional suggestion shown under the caret, and up to `KEST_MOST_PLACES` (8) further places, each a span with a sentence. A diagnostic that had more places says how many it left out (`leftOut`). Which eight is decided by relevance, not declaration order: the near misses of an overload first, and a note added to a finished diagnostic (such as which copy of a generic it is about) takes the last place rather than being dropped.

- **One shape, two renderings.** Text for people, JSON for tools, from the same records. Nothing is in one and not the other.
- **Line and column** come from a per-file table of line starts built once when the file is read (4 bytes a line, kept for the life of the build), so rendering a caret costs nothing at report time.
- **Near names.** Suggestions of the form "did you mean" use one distance function over what the scope can actually reach. A suggestion is never the word that was written, and a name from an unimported module is said as "is in this program, and this file does not import it", not offered as a spelling.
- **Faults in the compiler** are said through one function (`kest_diags_fault`) in one sentence, so a reader can tell the compiler's mistake from the program's. `check-tables.sh` holds every such sentence to coming from that function.
- **Running out while reporting.** A diagnostic is written into the arena that may have just run out, so a run records one bit when that happens and reports `K0639`, "not enough memory to finish, or to say more about it", after whatever it managed to say.
- **A machine's report** keeps the last 16 diagnostics nobody has read (`KEST_MOST_UNREAD`) and counts the rest (`notKept`). Compiling has no such cap: a program with five hundred errors is told all five hundred.

Every diagnostic code must be asked for by a check that makes it happen and reads what it said; `check-tables.sh` lists the codes no check asks for and requires each to be a fault the compiler owns up to or a host mistake written down as one.

## JSON

Every object starts with `schema` (`KEST_JSON_SCHEMA`), which moves when a field changes meaning, is removed, or is added to a list a reader was told was complete, and not when a field is added beside others. The compiler's own costs are counted before the JSON is written, so asking for more detail never changes the numbers. Names are always written whole with their module beside them (`module` per declaration), because a name can contain dots of its own and a tool splitting at a dot would invent modules. Absent values are `null`, never left out, so a tool can rely on every field.

Identity is four separate questions with four answers: a declaration is its module, name and place (`check`); a `signature` is a hash of what a caller can see (qualified name, parameter and result types, promises) that survives comments, moves, renamed locals and renamed type parameters; a `body` is a hash of a chunk's instructions and constants (`emit`); and a running object is a reference.

## Language server

[`src/lsp.c`](../../src/lsp.c) is the compiler answering an editor, not a second reader. Every answer comes from a normal build over the open buffers, with the checker's name index turned on (`index_names`), so hover, definition, references, rename and workspace symbols read what the checker resolved. A second parser in an editor would be a second meaning.

- It keeps every buffer the client opened, and every message names the one it is about; a file that imports an edited buffer is checked against the buffer, not the disk.
- It takes whole documents on every change, never incremental edits: applying an edit twice is the one way a server can disagree with the client about what a file says.
- Rename stays within a file, because a rename that reached another file would be done halfway rather than refused.

[`src/wire.c`](../../src/wire.c) is what the language server and the debug adapter share: length-framed messages, a small JSON reader into an arena thrown away per message, and a JSON writer, with no library underneath. Reading a message comes to one of three things, a tree, a message that is not JSON, or a message there was no memory to hold or read, and the last two are told apart because a client is told which. The language server answers both with an error under no id (`-32700` and `-32603`), and a request whose answer could not be written with `-32603` under its own id. These are written from fixed text, because they are needed exactly when there may be no memory to write anything else. The adapter cannot answer a message it could not read, since a reply names the request it answers, so it says so in the debug console and answers every request it read.

## Debugger and adapter

[`src/debug.c`](../../src/debug.c) is the debugger once; `kest debug` (words on two streams) and `kest dap` ([`src/dap.c`](../../src/dap.c), the Debug Adapter Protocol) are two ways of asking it. Breakpoints are bytes written over and put back (see [Machine](machine.md#breakpoints)). A breakpoint at a line is a breakpoint at the first instruction of that line in every body, found by walking each body's source origins. `step` stops at the next instruction anywhere, `next` steps over calls, and step out runs until the starting body returns. `locals` names slots by what the body called them, and asks the verifier's walk whether a slot is written and whether it is text before reading it (see [Verifier](verifier.md#for-the-debugger)). The adapter runs the program on the thread that answers the editor, which is why it cannot pause a running program; the program's output is read back from files and sent as output events.

## `kest doc`

[`src/doc.c`](../../src/doc.c) prints what a file declares for someone calling it. A comment on the lines directly above a declaration is about it; a blank line between them means it is not; a comment at the end of a line is about that line; comments before the first declaration that belong to no declaration are about the file. A function is printed up to where its body starts and anything else whole. Nothing is invented: a declaration without a comment is printed with nothing under it. The file is built first, so only a program that checks is documented. The generated API reference and the `llms.txt` digest on the documentation site are built from `kest doc` over `lib/std` and from the header.

## The command line

[`src/main.c`](../../src/main.c) is a host like any other. It binds the doors `std.io` and `std.os` declare plus a few that the examples ask for, sizes every machine from what the program says it needs, and asks `kest_needs_of` for the function it is about to call. Errors go to standard error and answers to standard output; with `--json` the object is on standard output and the program's own writing moves to standard error. `check-tables.sh` holds the commands `main` answers to and the options it reads to what `help` prints, and `check-commands.sh` runs every command over every file in the tree.
