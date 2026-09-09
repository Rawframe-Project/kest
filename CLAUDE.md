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
                   `check-fmt.sh` holds the formatter to what it has to be —
                   and `fmt` holds itself to the first of these, reading back
                   what it wrote before handing it over and refusing rather
                   than printing a file the next command cannot read:
                   its output parses, is what the one form of the same file
                   written badly is — a line ended wherever one may end and
                   carry on, which is asked of a run rather than kept in a
                   list here, on every line including the ones holding a
                   comment, because what is written inside something that
                   comes out on one line was written about that thing, every line at another indent, a space left at
                   the end of each and every blank line doubled, none of which
                   is part of a program, because every file here is
                   already in the one form and formatting one otherwise
                   compares it with itself — means the same, keeps every
                   comment
                   somebody wrote — read twice, by the compiler and by the
                   check's own reading of what a comment is, with both lists
                   compared before and after and held to each other word for
                   word, because two counts agreeing says nothing about two
                   lists being the same list, and each one held to still being
                   above the thing it was written about, because the same
                   words above different things are the same list saying
                   something else — tried in every place a file offers rather
                   than in the places somebody thought of: one variant of a
                   file per line with a comment at the end of it and one with
                   a comment above it, which is how every closing brace in the
                   language turned out to be a place a comment was moved from,
                   and that file held to using every keyword the lexer has
                   and every kind of declaration a file can hold — which is
                   not the same list, since `flags` is a word rather than a
                   keyword, and is asked of a run rather than read out of the
                   source, being the list a reader is given when a file holds
                   something else — so which places there are is decided by
                   the language rather than by what somebody remembered — formats to itself, leaves a file it cannot
                   read exactly as it found it, breaks a line only where a line
                   may be broken — it breaks a long chain after its operator,
                   and `>` is the one operator a line may end after, so a
                   comparison holding one stays on the line it is on however
                   long that is, and what says which is the lexer's own answer
                   rather than a list kept beside it — and gives back the one form for
                   a file whose lines end with two characters. What says it
                   means the same is the tree the `parse` command prints, of
                   what went in and of what came out, so that comparison is
                   worth what the tree can tell apart: pairs of programs
                   differing in one thing each — a promise, a type, a name, an
                   order, how a number was spelled, which operator, the shape
                   of what runs — are held to having two trees, because a tree
                   that leaves a thing out is a formatter free to drop it with
                   every file here still called faithful. And over lines
                   longer than the one form allows, of every kind it can break
                   — no file here has one, and what a check reads is the files
                   there are. Over the tree, and over
                   a file nobody has formatted, which no file here is. And
                   the tree to being written in that form already, because a
                   language with one form is written in it.
                   `check-tables.sh` holds every list that has to name
                   everything of its kind, and every name in the Python a
                   check is written in — a heredoc or a string handed to
                   `python3 -c`, which is two thirds of it — to standing for
                   one thing wherever it is written, a `def` counted as a kind
                   of its own, because which of two things a line gets is
                   whichever was written above it — and what a name is made
                   of is followed through another name, since `out = pieces`
                   says what `out` is as plainly as `out = []` does — a counter
                   given a name a set further down the same file already had
                   ran the whole check and then refused with a `TypeError`
                   from Python, which says nothing about what the check was
                   for — and the library's widths, which are
                   a list of the same shape: a module that declares one name
                   in two widths is written in widths, and every function in
                   it that takes one takes both — a frame works in `f32` and a
                   number is written in `f64`, so a module written for numbers
                   is written twice over, and a half nobody wrote is named by
                   nobody and so is invisible to the check that holds every
                   function to being reached. Which modules it is asked of
                   comes from the library rather than from a name written
                   there, and what a host declares `extern` is left out, being
                   the host's and provided in the one width. The lists: the
                   token names, the instruction names, the keywords, the
                   builtins, and the pipeline above against the modules in
                   `src`. Every list it reads out of the
                   source goes through one door that refuses an empty one,
                   because a pattern that stops matching finds nothing and
                   nothing agrees with everything.
                   `check-header.sh` holds the public header to standing on
                   its own: a host that includes it and nothing else links
                   against the library and libc.
                   `check-dead.sh` holds every header to declaring what is
                   there and nothing that nothing calls, and the library to
                   making nothing a header does not declare — which is the
                   half `nm` answers rather than a pattern, and where a
                   declaration nothing can read looks the same as one nobody
                   wrote. An internal function wearing the public prefix is
                   said here too, because a reader cannot tell one from a
                   declaration that went missing, the public one
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
                   and the decisions to being syntax this language has, to
                   being written in the one form, since a document showing a
                   form the formatter would rewrite is one a reader cannot
                   copy out of, to checking and compiling where the block stands on its own
                   — a fragment names what the paragraph around it declared,
                   and what the checker says after an unknown name is whatever
                   it made of an error, so those are left alone and which
                   those are is what the checker says rather than a list —
                   and every one of them that declares a `main` to compiling,
                   running, answering nought and writing what is written under
                   it — which is the block below it fenced `text`, a fence of
                   its own because what a program wrote is not Kest and is not
                   nothing either, and a program with nothing written under it
                   is one whose answer nobody wrote down — a fragment leans on the prose around it and a program
                   carries what it uses — and every block fenced as nothing to
                   not being Kest, because a fence with nothing after it is
                   what a message or a signature is written in and nothing
                   reads one, and every file of this tree they name to being
                   there, the way `check-tables.sh` holds the ones this file
                   names, every
                   diagnostic they print to being a message a run of this
                   compiler says, and every name in a `json` block to being
                   one a run writes and every name a run writes to being one
                   a block shows. And the lines the reference quotes for the rules a
                   host has to keep for itself to being lines a run of the
                   engine says, because what the machine cannot refuse is held
                   by a host doing it wrong on purpose and saying what
                   happened. And every call a `kest` block makes into the
                   standard library to being a function that library has, for
                   a block that imports it — a block only has to parse, and a
                   call to something that is not there parses like any other.
                   And every `kest <command>` and every option
                   the documents write to being one the command line answers
                   to: `help` is held to what `main` compares against and the
                   reference writes the same things in its own words, so a
                   command renamed in one of them would otherwise leave two
                   documents disagreeing with nothing to say which of them is
                   the program. The worklog is not held to it: it
                   records what went wrong, so it holds code the parser
                   refuses on purpose. Every sweep it makes over a document
                   refuses to find nothing, and `check.sh` asks it about a
                   document with nothing in it to see it refuse: a check that
                   reads with patterns passes when the patterns stop matching.
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
                   to run, and so holds a path nothing here runs. Everything it
                   reads with a pattern refuses to find nothing, because a
                   check that asked about none of it says the costs are fine.
                   `check.sh` holds every `.kest` file in the tree to saying
                   nothing about itself: a project that warns everybody else
                   about a name nothing reaches and carries one is a project
                   nobody should believe.
                   `check-ceilings.sh` reaches every number a program can run
                   into: the three refusals that say it has as much of
                   something as it can be told it has, the two a machine has
                   rather than a program — how deep calls may nest and how much
                   stack there is, which a program reaches in a moment — the
                   heap a host says a program may have, reached by a host
                   written there because a command line has no such number to
                   give, the same heap filled by the host itself rather than by
                   the program — a lend costs a header and a place in a list,
                   and a host that lends every frame and ends nothing pays for
                   every one of them — and the same heap with nobody's ceiling on it, reached
                   by a program that grows on a machine given less than it
                   wants — what a reader does about running out depends on
                   whether the program wants a gigabyte or the machine has a
                   megabyte left, so both numbers are in the message, the numbers a host picks that a machine cannot have,
                   asked for under a limit on what a run may take, how many
                   places in stores a machine can tell apart, lowered in the
                   same copy, the memory the machine this runs on has, walked
                   as a ladder of `ulimit -v` from a level the program runs in
                   down to the level the C library cannot be mapped in, every
                   rung of which either runs or refuses in words — a diagnostic
                   is written into the arena that has just refused, so a run
                   with none left is the one that has nothing to say it with —
                   and every row of the
                   reference's table of what there is a most of, each by a
                   program with one too many in it. A row nothing runs into is
                   a message nobody has seen, which it says.
                   Two of the three are a minute and four gigabytes away and
                   the third is thirty-two gigabytes away, so it lowers the
                   ceiling in a copy of the tree and reaches all three in a
                   hundred lines of work each; the copy carries the objects it
                   builds and no others.
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
                   less to a tool than to a reader, about a walk that reads a
                   byte past what it measured, about a fault that says what it
                   is in its own words, about a promise refused where none was
                   wanted, about a shape that takes a different number of
                   things, about a shape that gives back something else,
                   about a shape that takes something else,
                   about a shape that holds something else, about a fixed
                   shape that holds a different number, about a value that
                   becomes an optional it does not fit, about a refusal a file
                   can meet that nothing asks for, about a refusal for a host
                   that says nothing, about a name asked the wrong way round
                   that answers anyway, about an escape nothing
                   names, about a builtin the promise's proof has no opinion
                   about, about a chunk that carries less than its declaration
                   promised, about a measurement of where a host is called from
                   that is short of what it turns out to be, about a frame that
                   agrees with whatever a host says is in it, about a word read
                   as a number whatever it says, about a handle another machine
                   made, about a lend the host took back and can still be read,
                   about a lend that leaves its header on the heap, about a
                   host's own string taken as the program's text, about a
                   machine that says it still has what it threw away, about an
                   arena whose blocks fall outside what it says they do, about
                   an allocation that arrives holding what was there before,
                   about a total of what was handed out that is not the sum of
                   it, about a refusal that does not say what it refused,
                   about a heap that ran out without saying what was growing,
                   about a heap that ran out without saying what was being
                   made, about a message that says an `i64` through a `%u`,
                   about a kind of type nothing says how to write, about a
                   kind of token nothing says a line may end after, about an
                   instruction the promise's second proof does not know, about
                   a scalar, a token kind and an instruction with no name of
                   its own, about a table a check reads with a pattern that
                   stops matching, about a host whose binds a check can no
                   longer read, about a fix the words show and the JSON
                   leaves out, about a frame that cost one thing in words and
                   another in JSON, about a peak that is under what the heap
                   ended holding, about a tick that does not say what it was
                   lent, about a run that answers nought whatever was said,
                   about a value the command line writes its own way, about
                   a type the checker can write and the machine cannot, about
                   a number that does not read back as itself, about a number a
                   host cannot read whole, about a lend taken back from one
                   handle only, about a lend taken back from one address only,
                   about a lend of more than a host has, about a lend at an
                   address the type may not sit at, about a file that calls
                   itself something else, about a package rooted at the file
                   rather than at its name, about a library looked for beside
                   the caller, about an installed library looked for in the
                   wrong place, about a build told one place and installed to
                   another, about two libraries and the wrong one read, about
                   a name a module does not have said without which module,
                   about a module written nearly right and not named back,
                   about two names equally near said as one, about a name of
                   two letters left unanswered, about two letters the other way
                   round counted as two, about a name too long to be near
                   anything, about a list left flat because its line could not
                   fit, about a file of nothing but a comment written as
                   nothing, about a tick of a file with nothing to tick, about
                   every module written out rather than the one asked about,
                   about a program of two files said by halves to a tool,
                   about a program that did not check written out anyway,
                   about a tool given something on the stream it does not read,
                   about a failure written before what a program printed,
                   about bytes with a nought among them taken as text, about a
                   write into a lend that goes somewhere else, about a
                   reference followed whatever it names, about two stores that
                   stamp their places alike, about two machines that stamp
                   their places alike,
                   about text made out of a lend that points at the lend, about a function no header declares, about a
                   check taken out of the middle of the gate, about a check
                   that runs before the build, about calls that nest deeper
                   than they may, about a heap ceiling nothing is held to,
                   about a machine that cannot be made and says nothing, about
                   a command line with no memory that says nothing,
                   about a build that leaves
                   something behind, about an install that leaves a file
                   behind, about a check that writes to a name another run has
                   too,
                   about a machine that keeps the host it was started with,
                   about a machine started from the host that started the
                   first one, about a check whose second trap replaces its
                   first, about a run with no memory left that says nothing,
                   about a machine with no memory left that says only that,
                   about an arena refused a block that says nothing about
                   what for, about a machine with nothing left that answers as
                   a ceiling, about a ceiling kept that answers as the machine
                   underneath, about a heap thrown away from inside a call,
                   about a machine freed from inside a call, about a machine
                   that says it was freed and was not, about a build freed out
                   from under its machines, about a build that never counts
                   a machine it made, about a machine that points into the list
                   it was started from, about a build that counts a machine
                   that never started, about a program asking for more names
                   than a call can name, about a ceiling on names raised past
                   what names them, about a name declared and not put where
                   names are looked up, about an index that names a place
                   there is no name at, about an index rebuilt with the
                   names in the other order, about a file that spells the
                   sanitiser's own name, about an option nothing tells a
                   reader about, about a version that says nothing, about a
                   heap between events that nothing throws away, about a
                   name `help` marks out that nothing walks, and about a
                   command the documents write and nothing answers to, about
                   a library the documents call and the library has not got,
                   about a block calling a print this language has not got,
                   about a documented program that does not compile,
                   about a program fenced as though it were not Kest, about
                   a file the documents name that is not there, about a fix no
                   diagnostic carries in either form, about a note with
                   nowhere to point at, about a note that points where its
                   own words are not, about a note that names its line and
                   not its file, about a qualified name put under the
                   module it was typed at, about a number at a command line
                   that settles nothing, about a call that answers where the
                   program is writing, about a frame's cost written into
                   by the program, and about a run that wrote nothing and said
                   it had worked, about a read that failed and was handed
                   over as nothing, about a host that calls itself something
                   the reference does not, and about a name the command line
                   provides and nothing says so, and about a promise about a
                   host that nothing measures, about a lend that costs what
                   it is lent, about a list of spare headers a reset left
                   behind, about a lend refused for want of room that says
                   nothing, about a header a lend does not give back, about
                   a lend that stays in the list after it ends, about a
                   reset that hands back what was written before it, about
                   a host's own rule the engine stopped showing, about a
                   host that lends what it has given back, about a lend at
                   no address that is given anyway, about a lend longer
                   than `len` can count, and about an address a type may not be
                   read at, about a byte read out of a lend as though it
                   were signed, and about a byte written into a lend out of the
                   wrong end, about a byte of text read as though it were
                   signed, about a character counted as many as its
                   bytes, about a cut the promise does not count, about a
                   character read past the end of what was read, about a
                   character that swallows the one after it, and about a walk
                   back that lands inside a character, about a piece per
                   character that grows with the text, about a cut that
                   copies what was already ending, about a documented block
                   that does not check, about a documented block that checks
                   and does not compile, about a program that says something
                   other than what is under it, about a name in a check that
                   stands for two things, about a name that stands for two
                   things in a quoted Python, about a name that is a function
                   and a value, about a name that is a run and a piece of
                   text, about a name whose kind comes through another name, about a line broken where a
                   line may end, about an arm that gains a blank line every
                   time it is formatted, about a function written for
                   one width and not the other, about a cut refused
                   without saying how long the text was, about a byte read
                   past the end that says nothing about how long the text was,
                   about a number too big to hold read as something else, and
                   about a number too big for an `f32` read as infinity.
                   A hole whose catch is a build that stops says so, because
                   what holds some of this is the compiler and a net it cannot
                   be seen catching anything is no net.
                   A net nobody has seen catch anything is indistinguishable
                   from no net. What went wrong is said before the list of what
                   was caught, because a miss thirty lines down is a miss
                   nobody reads. A hole whose catch is a build that stops
                   names the one object that has to refuse to compile rather
                   than the whole thing, so what stopped the build is the file
                   the hole is about and not whatever came first.
                   A hole is caught when the check says the
                   words, refuses with a number, and says what is wrong before
                   anything else: a check that complains and comes back nought
                   is a complaint printed as though it were what the check had
                   to say for itself. The holes are put out of order at once rather
                   than one after another — none of them reads what another
                   writes — and reported in the order they are written, because
                   a list that reports itself in whatever order finished first
                   is a list nobody can read twice.
                   A copy takes what a hole needs and nothing else: the
                   sanitised objects only where a sanitised build is asked for,
                   and the two hosts never, because they are built into the
                   copy. What a build does not write into is the same bytes
                   under another name where the machine allows it, which is why
                   a broken file is written by making a new one rather than by
                   cutting the old one short.
                   `check-lends.sh` holds what a host says when it lends: a
                   lend is an address, a count and a name, and a name that
                   means two types is a lend of whichever was found first
                   unless something refuses it. No program here has two of a
                   name — every example is one module — so it writes the
                   program and the ten-line host that lends to it.
                   `check-commands.sh` holds every command to producing
                   something, because one that prints nothing looks the same
                   as one that works, and holds the two forms of `check`,
                   `emit` and `lex` to saying the same thing, and a diagnostic
                   said both ways to being one diagnostic — the same message,
                   the same place, the same fix and the same notes in the same
                   order, asked of every command that says one, because a
                   diagnostic is the same thing whichever command it came out
                   of, and one that carries all four of what a diagnostic
                   carries — the code, the place, the fix, and the notes with
                   places of their own — to carrying them in both, because two
                   forms that agree are two forms that lost the same thing, and
                   every note that names something to pointing at a line that
                   has it, in the file the note says it is in rather than the
                   one the diagnostic is about — a promise in one module broken
                   in another is one diagnostic about two files — because a
                   note under the wrong line reads exactly like one under the
                   right line, and what `tick` says a frame cost — the crossings, what
                   came back and what the heap did — to being the same numbers
                   in both and to meaning what they say: as many crossings as
                   there were events, a peak that is at least what the heap
                   ended holding, and what it was run over said the same way in
                   both, because two runs of the same shape over different
                   events are two measurements, and `run` to answering with
                   what the program answered, which is the whole of what it
                   says when it works, and `call` to writing a value the way
                   the language writes one — what a program prints for a value
                   and what a command line prints for the same value are the
                   same words, and a number written down to reading back as
                   the number it was written from, which is what the writer
                   promises and what digits quoted in an example cannot say:
                   one is read by a
                   person and the other by a tool, and a kind of shape added to
                   one and not the other is a type nothing machine-readable can
                   see. It also holds what a chunk carries to what the
                   declaration promised, which is two commands rather than two
                   forms of one: at the call the promise's second proof cannot
                   see through, what the machine reads is the chunk.
                   Over every file in the tree, over a file
                   that holds nothing, over one asking the host for a name it
                   has not got, over a path that is not a file at all, and over
                   the two ways an import may not resolve — a file that calls
                   itself something else and a file that is not there — and
                   over a package four directories down, which is where the
                   rule about where a root is is either true or not, and from
                   another directory by the command's whole name, which is how
                   anybody who has installed this runs it. None of which
                   anything here is.
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
names under `/tmp` — `check-tables.sh` holds every one of them to that, and to
the rest of what a check is: something to run, saying what runs it, stopping on
a name nobody set, and taking away what it made. One room each, and one `trap`
each: a second `trap ... EXIT` replaces the first rather than adding to it, so
a check that reads as though it hands back both of its rooms hands back one.
That is not a thing anybody sees until the machine fills up, which is what
happened — nine hundred directories from one check, and a gate that stopped at
`No space left on device`. So the gate hands the whole run one place to work,
under `TMPDIR`, and looks at it afterwards: `room` is the gate saying what it
left behind, which is nothing. A check that refuses in the middle hands its
room back on the way out, because refusing in the middle is what a check is
for and those are the runs there are most of. A tenth check copies the shape
of whichever it was written beside, so the shape is written down — and none of
them writes anything another reads — `fmt -w`
is tried on a copy rather than on the file, because everything here reads these
files. That is what lets `check.sh` ask all nine at once and read what they say
back in the order they are written, and it is what two runs writing to one file
cost: a check that failed one time in six for no reason anybody could see.

What was asked is written down where it is asked and held against what was
heard: a check whose run never started leaves no answer, and an answer nobody
left reads exactly like a check with nothing to say. What a check says it did
is its last line, so a check that says nothing and one that says more after it
are both said about rather than read as whatever came last.

The guards the gate makes about itself have no holes, because what would catch
one missing is itself. Each was watched working in a copy of the tree when it
was written, and what stays in the tree is the guard.

What the gate does itself, beside the checks in `tools` that it asks:

```
build        both builds and both hosts
asking       a host asking what came back before anything came back
returns      files written on the spot: line endings, noughts inside text,
             and a promise around a `defer`
warnings     every file holding its tongue about itself
modules      every file where its `module` line says it is
project      `lib/std` read as one project rather than as files
examples     every example run or resolved under both builds, answering the
             same under each, and a `main` that gives nothing
instruments  every Kest under `tools` resolved
host         both hosts, sanitised and not
sanitisers   every command over every file under the sanitisers, and the
             two builds asked which of them checks itself
nothing      a document with nothing in it, and checks handed no files
room         every check handing back the room it took
```

`check-tables.sh` holds that list to what `check.sh` says: a line deleted from
the middle of the gate is a check that no longer happens, and the run reads the
same as it did the day before. It holds the order too, which is the one thing
about the gate that is not a list: nothing reaches for what was built before
the build runs, because a probe that passes when a command fails passes when
there is no command.

The `Makefile` is held too: every target this file tells a reader to type is a
target it has, everything the gate builds is something `clean` removes, and
every file an install puts on a machine is one an uninstall takes away, and
where a build says the library will be is where an install puts it. And it
is run: `check-commands.sh` installs into somewhere of its own, runs what it
put there on a program that imports the library, and takes it away again — the
lines being right is one thing and the files arriving is another.

`make check` is the whole of it: both builds, both hosts, every example run or
resolved, every command against every file under the sanitisers, every tool
named above, and a handful of files written on the spot for what no file in the
tree is: one that holds nothing, one that holds a comment and nothing else,
one whose lines end the way another machine ends them, one whose `main` gives
nothing back, a document with nothing in it,
an empty list handed to the two checks that read what they are given, and a
third host of ten lines that asks what came back before anything came back. There is no count of them here, because a count is a thing
that goes stale; `check-tables.sh` holds the three lists that say which they
are — the files in `tools`, the ones named above, and the ones `check.sh` runs
— to each other. It takes no list of files, because a list is the thing that
goes stale. Nothing is finished until it passes.

The sanitised build is where a host's word about its own memory is weighed: a
lend says how many there are and nothing in a build that ships can know whether
there are that many, and this one is told where every block ends.

The sanitised build is told what the arena handed out: a block is poisoned
when it is taken and each allocation is opened to its own size, with a gap
after it that stays poisoned. It is also where the arena is held to what it
keeps rather than works out — the block it started with, the block that
answered last, and what all of them sit between — because a program behaves
the same whether those are true or not, and what reads them is a crossing and
a reset. And to what it hands out: every allocation is memory that is nought,
which three separate pieces of this file are what make true and none of them
is the whole of. A read one element past the end of something is
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
| The refusals a file can meet before it runs | `lexer.c`, `parser.c`, `check.c` and `types.c` | `check-tables.sh`, holding every one of them to being asked for by a check — a message nobody has ever seen is a message nobody knows is there — with five written down there that nothing has been able to reach, and a count of what is left, which is what a program meets while it runs or while a host holds it — the other host asks for those, by reading the code back out of a report after asking for the refusal |
| What a fault says it is | `diag.c` | `check-tables.sh`, holding the words to one place: `kest_diags_fault` is the door, and a fault written out in its own words in any other file is a reader met by the same news in two voices |
| What a message's words say about the numbers in it | `diag.h` | the words a message is written in, read by the compiler against what is handed to them: a `%u` given an `i64` stops the build |
| What a value can be written as text | `types.c` and `vm.c` | no `default` in either, and `check-tables.sh` holding the two to each other: what the checker says can go in a hole is what the machine writes |
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
- Where a fix is knowable, suggest it. Unknown name reports the nearest match,
  and both when two are exactly as near: knowing two and saying one is choosing
  for a reader. More than two and it says nothing, because a list of names is
  not a suggestion. Two letters is the shortest a name is answered for, and one
  is not answered for at all.
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
