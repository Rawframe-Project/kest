# Where this is, and what is known to be wrong

This is the engineering note. It is short on purpose and it is kept current:
`docs/worklog.md` is history, `docs/decisions.md` is why, `CHANGELOG.md` is what
a reader with a program has to do about it, and this is where the work is.

## Reproduced defects, all fixed

Each was reproduced against this tree with the smallest program or host that
asks the question, and each is fixed and held by something that runs. Evidence
is named; nothing here is a claim from a document. **None of these is open** --
the list stays because what caught a thing is worth knowing, and because a
reader who finds the same shape again should find the first one beside it.

| | what | evidence |
| --- | --- | --- |
| F1 | a lent run of one element type is accepted where another is wanted | ASan `global-buffer-overflow`, READ of size 4, `src/vm.c` in `unpack`: eight `Row` (8 bytes each) read as eight `Wide` (16 each) |
| F2 | a call wider than the stack writes before it refuses | ASan `use-after-poison` in `kest_call`, then `K0602` |
| F3 | a `for` binding observes a mutation made through an alias in a struct | the bound copy reads 99 where the language says 7 |
| F4 | `a[0] = grow(a)` loses the assignment | `elem.addr` is emitted before the call; the store goes to the abandoned buffer. Silent, and only when the array is not the last block on the heap |
| F5 | fuel is falsely exhausted across host reentry | budget 300, outer takes the whole slice, the reentrant call sees nought left after about fifty steps |
| F6 | a cancelled machine runs straight-line work | cancel is only seen at a jump or a call, and a body with neither runs to the end |
| F7 | `u64` of a float above `INT64_MAX` saturates at 2^63 | `u64(1e19)` is 9223372036854775808, folded and at runtime |
| F8 | `check` accepts what `emit` and `run` refuse | and the refusal is `K0405`, the compiler-fault code, for a mistake in the program. Both instantiation orders |
| F9 | a reference from one world resolves in another | two independent builds of one file: a `ref<Thing>` made in the first read an unrelated object in the second and answered its value. The world id that fixed it was itself sixteen bits of a count and came round, which is F11 |
| F11 | a world identity wraps into a live one | the 65,537th machine of a process was told it was the first, and the first was still standing: a reference made in a world holding 7, 8, 9 resolved in one holding 1000, 1001, 1002 and answered 1000. Fixed by taking the world out of a reference altogether (D1033); held by the gate's `identity` section, which reports 4,464 references handed out twice against the tree before it |
| F13 | a program cannot hold two modules ending in one word | `import a.math` beside `import b.math` was `K0328`, and the refusal was about the program rather than the file: `import a.math` alone compiled with `b/math.kest` unread in the tree. A name lives under the whole of its module now and a file's `math.` expands through that file's own imports, so `render.math` and `physics.math` coexist; one file reaching both is what is refused (D1039) |
| F12 | a world runs out of identity in half a second | one thing in and one thing out, live set of one, and `K0630` after 16,777,215 turns because the count was one machine's and twenty-four bits. `bench/agents.kest` had 20,394 rounds in it. The count is the process's and forty bits now (D1034) |
| F14 | two lines of ordinary Kest break a table from outside | `pop(t.keys)` leaves `keys` a pair shorter than `values` while `slots` still says where the second key was, so `table.get` reads past the end and the refusal is at `lib/std/table.kest`; `table.count` answers `len(keys)` and agreed with it. A struct field may be written `own` now, which means only the module that declared the shape may name it, and `std.table`'s four are (D1041) |
| F15 | a `match` on what is not an enum says what is wrong and not what to write | reproduced by writing the `switch` a reader coming from elsewhere writes. `match` itself is whole -- a four-state gameplay machine in block form, with side effects, bindings, a `return` out of an arm and exhaustiveness, runs -- so the criticism that statement `match` is missing is false. The refusal now says `if let` for an optional and `if` for anything else (D1042) |
| F16 | a generic body is not read until something copies it | `fn never<T>(x: T) -> i32 { return x.nope + notAFunction(x) }` checked clean, so a library could ship a generic that works for no type. A type name now says what it may be asked to do -- `T: compares`, `T: orders` -- and the body is checked once where it stands against that (D1043) |
| F17 | a name the arena had no room for is compared as though it were there | `span_string` answers with an empty name when the arena is out and says so; `declare_functions` then asked whether two parameters share a name with a `memcmp` of as many bytes as the span is long. Five bytes past a one-byte global, found by refusing allocation 4638 of 9057 in `kest check examples/inventory.kest` under the sanitised build. It asks through `kest_word_same` now |
| F18 | the front page said there was no collector | there has been a non-moving mark-and-sweep heap since D996, and the page still said *flat to the byte across a hundredfold, and no collector*. The reference now has a section saying what the collector is, what it walks from, when it walks, what a pause costs and why there is no incremental marking (D1045) |
| F19 | the front page said the C API was 97 doors and the header had 98 | and nothing counted either. The family of every door is a table in `check-tables.sh` now, the reference says the six counts and how many the smallest host calls, and both are read off the header and off `examples/least.c` rather than remembered (D1046) |
| F20 | an enum could not take types and a struct could | so *the thing or why not* could not be written once: an API lost the reason, took an array to push reasons into, or declared four lines per value type, and a generic function could not answer one at all. `enum Answer<T>` goes through the same door a generic struct does now (D1048) |
| F21 | a project's `source` lines did nothing | they were read, printed by `kest doctor` and `--json`, and looked at by no other line of the compiler — so multiple source roots, which is how a dependency is written here, did not resolve. The loader finds the project above the first file and resolves every import under the sources now, refusing a module under two of them (D1049) |
| F22 | `sort.by` was quadratic, and a table never gave its room back | four thousand numbers out of order cost 260 million instructions, most of a second, and a hundred thousand pairs taken down to a thousand kept every byte. The sort is gapped now — 247× cheaper on the worst shape and 3.7× dearer on the best — and `table.compact` is the cold path that gives the room back (D1052) |
| F23 | a `deterministic` function answered two things on two machines of one process | `hash(ref)` was a hash of the whole reference, and the number above the place is one the whole process hands out. The comment above those constants said a program cannot see that number and missed `hash`. A reference hashes by its place now (D1054) |
| F24 | the sharded model did not scale | a workload that mostly makes places in a world ran no faster on eight threads than on one, and slower on two: one atomic write per `add`, on a word every thread of the process shares. A machine claims a thousand at a time now — 4.9× on eight threads, and 37 to 47 per cent faster on one (D1053) |
| F25 | a field of one of a run, standing where an optional is wanted, read two slots | D809 taught the element read that the checker has already widened the expression and the tag goes on afterwards; the field read from an address never learned it, so `items[at].price` answering `i32?` took the price and the next item's `id`. The compiler caught itself with `K0505` rather than emitting it. Found by a program written about text (D1055) |
| F26 | the reference said only a `no.host` body can promise `deterministic`, and that the profile excludes `sqrt` | an `extern` may declare itself inside the profile and `std.math` does, so `math.sqrt` promises `deterministic` and is not `no.host` — true since the promise shipped, and said the other way in D941, D942 and the reference. Behind it: the four host doors inside the profile were the four the conformance corpus did not fold, so the one part resting on a host's word was the one part nothing tested. Corpus and profile version both moved (D1060) |
| F27 | the reference's list of what reaches the heap named `slice` and not `room` | prose beside a table with nothing holding the two together. A `no.alloc` body calling `slice` compiles and one calling `room` is refused, both in three lines. `check-tables.sh` holds the sentence to the proof's table now (D1060) |
| F28 | an enum another module declared could be matched and not made | the branch that knows a case is named after its enum asked whether what was in front of it was one word. For another module's enum it is two, so `npc.Mood.Calm` was `` `npc` has nothing called `Mood` `` and the inner name was checked as a value. Every enum in the tree was declared and used in one file, so nothing had ever written one down across the boundary. Found by the third file of section 35's vertical slice (D1062) |
| F29 | a write to a struct parameter was discarded quietly whenever the function answered anything | `K0346` is the warning for exactly this and its first line turned it off for every function with a result. A world is a struct holding a store, a table and a few counters: the handles in it work and the counters silently do not. What says a body can hand it back is the type it answers with (D1065) |
| F32 | starting a machine wrote the build's arena, so two starts on two threads raced | the reference has said since D952 that a host may start and free machines from any thread, and the `races` section started all four of its machines on the thread that made them. Four more, each started and freed on a thread of its own, found it on the first run: the machine's report came out of the build's arena, which is a bump pointer. The report is the machine's own memory now (D1071) |
| F33 | a layout's mark said nothing about which cases a tag can name | a tag is one piece of one kind at one offset with one name whatever the enum's cases are, so a case put in the middle of an enum gave the identical mark — and a tag is a number that is its case's place, so a saved world read back with every case after the inserted one meaning the case below it. The mark folds the case names now, and `reload` has two more edits (D1072) |
| F37 | and it said nothing about which bits a set of named ones has | a bit is one shifted by its place in the list, so a bit put in the middle doubles every bit after it and a saved world reads back with each of them meaning the one below. Worse than the tag: there is no door onto a set's bit names, so the mark is all a host has. Found by re-reading F33 for the shape of itself (D1076) |
| F34 | a `scratch { }` block refused a frame that looked a thing up in a table | the walk that proves nothing escapes a block did not have the branch an `if let` compiles to among the operations that pass a value through, so every name an `if let` bound inside a block was read as something the block made. A frame that walks a world inside a block and looks each thing up in a table could not be written (D1073) |
| F35 | a `scratch { }` block could grow what outlives it, one call away | growing something older than the block is refused where it is written and the walk refuses a call handed what the block *made* — an older array is not that. Fourteen lines grow an array inside a block, the block gives the memory back, and the release build reads the elements and answers; the sanitised build says `use-after-poison`. A call handed something that can grow now has to promise `no.alloc` (D1075) |
| F36 | two walks that decide whether a `for` binds its element by address were blind to a `scratch { }` block | both switched over statement kinds with a `default`, so a body that named the array inside a block was read as naming nothing. A loop element used as a whole value inside a block was also compiled as if the address were the value, which `K0505` caught as a compiler fault. Written out in full with no `default` (D1075) |
| F30 | `deterministic` was not written into the name of a function type | `room += 14` stood where the word should have been: a number grown after the memory it described had been handed out. `check`, `--json`, the message naming the shape to write, and the name a generic copy is compiled under all named a shape that was not the shape (D1064) |
| F31 | a table could not be written to inside a promise | `table.set` may refill, so a frame keeping a count per thing could not promise `no.alloc` at all. `table.fit` is `set` with the growth taken out (D1063) |
| F38 | a debugger writes the build's own program, and every machine of that build reads it | a breakpoint is the instruction that was there written over, which is what makes one cost a running machine nothing (D991) — but the bytes are `module->functions[entry]->code` and the module is the build's. A host of twenty lines starts two machines from one build, writes a breakpoint into the first, and the second reads 166 where it read 0: an instruction it never compiled. The sharing stays — the fix is the program again per machine, paid by every host that never debugs anything — and the sentences change: `kest_code_of`, `kest_start`, which said nothing writes to a program once it is compiled, and *Who owns a machine* all say to debug a build no other machine is standing on. `make check`'s `sharing` section holds it (D1077) |
| F39 | a walk asked for at a breakpoint gave the stopped machine's own memory back | every door that guards the heap asks whether a function the host bound is on the stack, and a stop sets that to nothing because a stop is not a crossing out. So a stopped machine looked idle: a walk read to where the slots had got to when the host last called in, which is the bottom of the stack, saw no roots, and swept what the stopped frames held. Forty lines hold 1024 bytes, collect to nought, carry on and read the elements — `heap-use-after-free` under the sanitiser. A stop is the middle of a call and all five doors refuse there now, held by `make check`'s `stopped` section, by `examples/embed`, and by a hole that takes the guard out (D1078) |
| F40 | a call the host made from inside a call left its frames behind when it failed | a bound function calls back in, the call it makes divides by nothing ten frames down, and the call it was made from answers 230 where 107 is right — no refusal, nothing in the report, `kest_call` answering true. A run entered from a host call starts its frames at the depth the crossing recorded and a refusal comes back from the middle of a body without unwinding them; a call from outside starts at the bottom and wrote over them, which is why nothing ever saw it. The run gives back what it took however it ends now, and a breakpoint in one of those runs is `K0708` rather than a stop nothing can carry on from (D1079) |
| F41 | a slot that holds where a value is was named to a host as though it held the value | a `for` binds its element by address where the body never writes it (D866), and the compiler wrote that down in the IR — but the name is written as the local is declared and the decision is made after, so the flag was always false, and nothing carried it into the chunk either way. `kest debug` printed `b   slot 6   94381350649872` where the body says `Body`. Found by sweeping for state this project writes and never reads: six, of which five were leftovers. `kest_frame_at_address` is the door now, and `check-tables.sh` says no such state is left (D1081) |
| F42 | a manifest line read and never asked for | `kest new` writes that `kest test` runs the programs under the `tests` line; the reader read it into the project; nothing ever looked at it. `kest test` inside a project with no file named ran nothing and answered nought, which is a gate passing for having done nothing. It runs them now, and a project that says where its tests are and has none there is refused (D1082) |
| F43 | this language's own reader could not read what this language writes | a hole in a string writes the shortest spelling that reads back as the same number, which for anything small or large has an exponent in it — and `text.real` refused `1e3` on purpose, with a rule saying a program that means that can say it another way. Not true of a program handed the text by this language: a save written by a program and read by the same program came back as nothing, for exactly the numbers a simulation has most of. Four thousand floats from a seed found it in the first hundred. And `text.fixed` wrote `0.00` for a value that is not a number. Both fixed, and held by `examples/ordering.kest` (D1084) |
| F44 | the formatter wrote a different program, and a block inside brackets ran its lines together | a giving `if` or `match` takes whatever follows it into its arm, so `(if c -> a else -> b) - 2` without brackets is an `else` arm of `b - 2` — and the printer bracketed an operand only when it was a binary of lower precedence. Four shapes came out meaning something else, silently where the result was stable under a second formatting. Fixing it made the formatter print a bracketed `match` over lines, which the lexer refused: the bracket count was never put aside at a brace, though the field beside it said it was. Both fixed, held by `examples/ordering.kest` and by the formatter check's own program (D1085) |
| F45 | the thread sanitiser's objects were not rebuilt when a header changed | every object has a dependency file beside it and the line that reads them back named the release objects, the sanitised objects and the hosts, not these. A header that changed left half of them holding the old shape of `KestProgram` and half the new, so a field written through one layout was read as another: `program->instances` was nought while the count said there were some, and the `races` section died inside `kest_check_bodies`, nowhere near anything about threads. Wrong since that build was added, quiet because nothing had changed a header between two runs of it (D1089) |
| F46 | a place inside a value inside a place was refused as a fault in the compiler | `who[at].cools[i] = n` -- an index through a field of an indexed element -- answered `K0505`, which is what this compiler says when its two halves disagree. An assignment holds a place inside an array apart (D931) and the flag covered the whole target, so the element read in the middle left the array and the index on the stack where the handle belonged. Found by writing the gameplay benchmark the other way round; both shapes now answer the same checksum (D1091) |
| F47 | an editor was answered about the file it opened last, not the file it asked about | `kest lsp` kept one document. Open A, open B, change A, and `K0306 unknown name` typed into A was published against B's uri while A was told it was clean; hover, definition, rename and formatting all read the same one text. And the loader's overlay held one path, so a file importing one the person had edited and not saved was checked against the saved copy. Driven through the protocol rather than read out of the source. The server keeps every open file and chooses by uri before it dispatches, and the overlay is a set (D1143) |
| F48 | the gate's own sweep said a check missed a hole the check catches, on one machine and not on CI | `check-commands.sh` held a small file to `K0658` under ceilings of 1000, 2000 and 4000 bytes. The hole it is for shows only under a ceiling inside a 1956-byte window, and where that window starts moves about five bytes for every character of the file's path. The gate nests its rooms, so under a plain `/tmp` the path is 56 characters and the window is 2020 to 3976, between the rungs; CI's room is longer and 4000 lands in it. Every replication by hand was one room deep and caught. Reproduced outside the sweep, the old check passing the hole at the 56-character path. The ceiling is walked a quarter of a kilobyte at a time from nothing to where the file fits (D1144) |
| F49 | a library named without a slash after it was looked for as `libstd` | `kest_build(path, "/x/lib", ...)` ran the library and a module's path together, while `KEST_LIB` has always had the slash put on. Every host in this tree wrote `"lib/"`; the first one outside it did not. The loader puts the separator on for every door, and `examples/embed.c` builds with `"lib"` (D1145) |
| F50 | a `match` arm written `Case -> {` was answered about `if`, then with a line about an arrow for every arm after it | nine diagnostics for one mistake made nine times, eight of them about something that was not wrong. The arm parser says `K0204` with the arm's own fix and reads the block as the arm (D1147) |
| F51 | two AI tasks held a rule their `ask.md` never said | run blind (D1148), all three Luau runs of `called` numbered things from one and all three of `frail` read a worth's limit off a `number`; the hidden tests wanted nought and 32 bits. The `ask.md`s say both now |
| F52 | three refusals said what was wrong and not what to write | a struct built by field name, a call by name, and a number where text is wanted -- the Kest mistakes that repeated across blind runs. Each is told the fix now, held by the refusal corpus (D1148) |
| F53 | a carried body read two parameters the caller kept apart as though they were side by side | `sum(x, z)` answered `x + y` once `sum` was carried (D1156). Runs of slots are held to landing as runs, and `examples/carried.kest` answers `1` on the tree before the fix (D1157) |
| F54 | a store made inside the machine could be given back by the walk the next one set off | the walk read to the frame a host called into and not to the machine's own, so `examples/frame.kest` read freed memory once every allocation walked. Told where the stack is, and the gate walks at every allocation over forty-one examples (D1163) |
| F55 | a set of named bits kept in a struct in an array read the bytes after it as its own | a `flags` field is laid out at its declared width and was moved as a whole slot: eight bytes read, so `back.state != State.Hurt` held with `hp` in the high bits, and eight written. In both engines, flat and beside a tag. The kinds that say a set's width move at that width, and `examples/flags.kest` answers 29 on the tree before (D1176) |
| F56 | the release engine could give back what a body called before it was compiled was holding | a call to a body lowered after its caller asked for no room for the callee's frame, so the reach the collector walks stopped below it: `examples/parse.kest` compiled answered 1 walking the heap before every allocation. The size is written once every body is lowered, and `check-c.sh` walks every example compiled at every allocation (D1179) |

## Read from the source rather than run

| | what |
| --- | --- |
| F10 | MEASURED, and no longer read from the source. `no.alloc` is about the Kest program heap and the reference says so; `examples/embed` now refuses inside a body that promises it -- K0604 at the line that asked, which reads the file again and draws a caret -- and holds the program heap and what it has ever asked for to being unmoved either side: 912 bytes and 912. A build where the refusal path takes sixteen bytes of that heap does not fail the assertion, it dies, because taking heap runs the collector over the frames of a machine in the middle of refusing (D1132) |
| E3 | FIXED, and the sentence it was written under is no longer true. The library keeps one piece of process-global mutable state on purpose: the count a place is stamped from, an atomic that only goes up and is never handed out twice. It is what makes a reference say which machine, which store and which occupant in one number, and it replaced both the build's counter this entry was about and the count of worlds that replaced that. See D1033 and D1034 |
| E4 | FIXED. Text is two slots — the bytes and how many — since D964: `len` is a read, a cut reaches nothing and `slice` came off the list of builtins that allocate, and a host reads bytes and a length through `kest_text_bytes` without measuring. The reusable buffer was already answered (D940). A nought inside is still refused, which is what D955 wrote down; what is new is that a cut does not end in one and the reference says so |
| E5 | `live_from` scanned to the store's high-water mark. It reads a bit a slot and sixty-four at a time now (D954): four thousand walks of a store holding eight things out of two hundred thousand went from 0.22s to 0.02s |

## What the measurements say today

The numbers a decision quotes were true the day it was written, and two of them
have moved since. This is where the current ones are, so that a reader comparing
a run with a decision knows which is which.

| | then | now |
| --- | --- | --- |
| the colony's steady state, at 200, 400 and 800 days | 2,370,304 bytes (D940) | 2,338,096 bytes, and still flat: a store keeps a bit a slot rather than a byte (D954) |
| a frame step an entity | 127 ns (D926), 122 after D931 | 107 to 110 ns, `make time` on this machine, and the number D979 would not pay a third of |
| what an array of text costs a frame, an entity | 25 bytes (D915) | 51, and read as what a frame was handed rather than what it still holds: a piece of text in one is sixteen bytes rather than eight (D964), and a step takes a place off a heap that gives places back, which is as wide as the step above what was asked for (D996) |
| what a frame step runs, an entity | 57 instructions (D958) | 30, after D961 took the two commonest pairs of pushes, D1154, D1155, D1165, D1167 and D1168 made more pairs one instruction, and D1156 and D1157 carried the two helpers to where they are called |

## Phases

The completion mission's sections, and which are answered. `/home/kest/mission/`
carries the mission itself; this is what the tree has to show for it.

    0  baseline and regression truth      done: fast, the focused
                                          regressions, the sanitisers, and
                                          three new defects found by driving
                                          rather than reading -- D972, D985,
                                          D991
    1  the semantics this ships           done: the reference is the
                                          normative one and says so, D994
    2  one resolved representation        done  D962
    3  persistent memory, decided         done  D992, on the trial in
                                          D972, and D996: the heap gives
                                          places back, so a world replaced in
                                          place settles instead of climbing
    4  text, bytes and buffer             done  D964, D971, D993
    5  temporary memory and scratch       done  D966, D972
    6  `store` and `ref` placed           done  D975
    7  the execution backend              done  D963
    8  shipping: VM-only or generated C   done  D987, measured against
                                          Daslang and a frame budget
    9  effects and contracts              done  D853, D976 for what the
                                          proof found
    10 the deterministic profile          done  D974 for the version, and
                                          three platforms held to one trace
    11 the C ABI                          done  D974, D981, D983
    12 reload and identity                done  D985
    13 concurrency                        done  D988
    14 the sandbox claim                  done  D981
    15 the standard library               small and said to be; D1171
                                          adds bytes for saves, and D1183
                                          found nothing else a program
                                          needed
    16 project and dependencies           done  D982
    17 tooling                            done  D976, D977, D979, D991,
                                          and D1182: the debugger in the
                                          editor
    18 the editor                         done  D978
    19 packaging                          done  D986 (no amalgamation),
                                          D989, D1000: an archive on both
                                          platforms, unpacked, run and built
                                          against in CI, and D1172: one
                                          binary that carries its program
    20 platforms and CI                   done: linux, windows, macos
    21 the benchmark suite                done  D980
    23 fuzz and sanitisers                done  D984, D997: six boundaries
                                          rather than one
    24 validation cleanup                 done  D990
    25 documentation                      done  D994
    26 versioning and release policy      done  D983, D998: four numbers and
                                          a rule for each. The version was
                                          1.0.0 for a day and is 0.0.1
                                          (D1035)
    27 the evaluation package             done: an hour's worth on the front
                                          page, and a release archive

## The performance work after 1.0.0, and what is open

A second mission ran after the tag and was reopened when its own completion
turned out to be premature. What the tree has to show for it:

    the movement baseline       D1023: nine byte counters, held to the
                                instruction histogram by the gate
    the optimizer layer         D1024: verify, optimize, verify, lower;
                                `KEST_NOOPT` turns it off; `KEST_IRSAY` says
                                what it found
    copy propagation            D1025: 3.3 % of the bytes `agents` moves and
                                4.3 % of `rules`, and three candidates closed
                                on their counts
    compiling, by stage         D1026: nine stages, `KEST_SPENT=1`
    what a collection costs     D1027: `kest_collected`, and the pause
                                distribution the report had been printing a
                                per-call total in place of
    the runtime, measured       D1028: no layout or cache problem, and a
                                third of `rules` in the element move path,
                                changed -- 19 % fewer instructions
    the boundary                D1029: what each way of crossing a frame
                                crossed, and the round trip out and back
    text, arrays and stores     D1030: closed on this language's own profile
    the bounds checks           D1031: three quarters provable, which is
                                three quarters of the nothing D1015 measured
    what the heap is asked for  D1032: by width and by kind, and the
                                crossing nothing counted

**One thing is open and named as open.** It is measured and not done, and it
says why. The other was bulk text append and it is done.

- Bulk text append is **closed**. It was eighteen per cent of
  `bench/words.kest` measured on the whole process and is more than half of it
  measured on the work: eighty per cent of the instructions that workload ran
  were one byte going on a run at a time. `push` and `fit` take a whole piece
  of text for a run of bytes now, which is no new name and two instructions,
  and `words` went from 4.9 to 3.2 times a `g++ -O2` baseline on the work
  (D1068). What was left after that was a number written through `snprintf`
  and a heap stepping through its bitmaps a bit at a time, and without them
  `words` is 0.81 times Luau's interpreter in instructions (D1173).
  Measured again as one run with the floor (D1184), and again on a quiet
  machine (D1190): 0.79, 0.94, 0.84, 0.74 and 1.04 of Luau's interpreter by
  processor time on `kernel`, `control`, `graph`, `words` and `rules` -- the
  load had made `rules` look a quarter ahead -- and a fuzz campaign of 24,000
  runs found nothing; `control` retires 3.6% fewer since an element of one slot is
  written from a local in one instruction (D1185), and `rules` compiled 7%
  fewer since the release engine reads a run's length inline (D1186);
  an element a walk counts through is proved inside its array and read with
  no guard compiled, `kernel` a third fewer and `rules` a tenth (D1187), and
  a walk calling bodies that keep their arrays is proved too (D1188); a
  walk to any limit asks once where it begins whether its arrays are long
  enough, and `control` compiled retires 16% fewer (D1189; the hole its
  second copy of the guard hid is repointed in D1190). The loop built
  without a landing mark on every label retires 1.5 to 4.4% fewer (D1191). `control` is 0.98 once its
  rule is carried (D1175), and `rules` 0.99 once
  a value is moved a run of its pieces at a time (D1177) and an element is
  weighed against a constant in one instruction (D1178): every workload here
  runs fewer instructions than Luau's interpreter. Threaded dispatch, which
  D1047 refused, took a tenth of the cycles off every workload (D1181).
- The dispatch loop, and what is left of it is smaller than it was written
  down as. The 48 to 92 per cent was the share of cycles *inside* the
  interpreter's loop, which is everything a program does and says nothing about
  dispatch. What a dispatch costs against what it dispatches is about one to
  one, measured by turning the fusions off in the same binary: a quarter to two
  fifths fewer dispatches buys 16 to 31 per cent of cycles. Mispredicting the
  indirect branch costs 1 to 4 per cent, which is what threaded dispatch could
  win and what a GNU extension in the hottest loop of an ISO C11 library would
  cost. See D1047.

**And the aggregate copy is closed.** `let one = world[at]` … `world[at] = one`
is 29 % of `bench/kernel.kest`'s cycles and writing the same program in place
is **2.15 times slower**, so the copy form is the faster of the two spellings
the language already has rather than a prison (D1038). The six `elem.addr` a
body that D1038 named as the real opportunity were measured and are not one:
holding the address saves a bounds check and a multiply and no instructions at
all, against a gap of thirty-six million. What the gap is, is that reading the
element once into slots makes every field operation after it a slot operation.
What was real beside it is fixed: reading a field of an element cost two
dispatches and is one now, which is 16.9 % of the place form's instructions and
nothing at all to the benchmarks, none of which is written that way (D1044).

**And the native question is open again, and is being answered by building
it.** D1017 and D1067 closed it twice on the reading that what a generated-C
backend takes away is the dispatch and that the dispatch is not the gap. A
gameplay-shaped workload measured against Luau's own native tier reopened it
(D1090), and D1092 decided two engines: the machine for developing and C for
shipping. That backend is in the tree — `kest emit --c`, every one of
this tree's 2,088 bodies, held to answering what the machine answers by
`tools/check-c.sh` — and on two scalar workloads it runs sixteen and
twenty-three times fewer machine instructions than the machine does (D1093).
The hybrid binary is there too (D1094): a chunk may carry a C function, a call
enters it instead of the instructions, and a refusal inside it is reported at
the same line with the same words. On `bench/control.kest` -- a real program
with one writable body -- an actor-round costs 1,333 machine instructions
interpreted and 1,144 with that body compiled. Elements followed (D1095), then tagged
values and text (D1096, D1097), then bodies that reach the heap and the doors
they need (D1098, D1099), then the world a game keeps — stores and the
references into them (D1102) — and then text: put in order (D1103), and read, made and built in a
buffer (D1104), after which `std.text` compiles whole. A body this backend
cannot write no longer takes the chain above it with it: it is handed to the
machine through a call of its own, and the machine writes its frame over the
one the call already made (D1105); a call through a function value is written
too (D1107), and so is the crossing into the host (D1108) and one of a
fixed run of slots (D1109) -- which leaves seven bodies of two thousand and no
family among them. **And the development loop is measured** (D1127): a reload costs 0.48 ms, of
which 0.44 is building the program and 0.05 is everything else -- starting a
machine, putting the world back, asking the doors, swapping. The reload path is
a compile, and a compile has not moved across the backend era: 112,647 lines
checked in 286 ms against D1088's 288, a million in 4,644 against 4,910.
Timing it found the host asking for its doors after it had published, so an
arity change ran the new program while saying the world was the one it was.

**And the vertical slice is measured, both ways** (D1124): sixty calls of
`examples/slice`, 1.613 ms in the middle by the machine against 0.640
compiled, with the heap doing the same thing under both to the byte and the
collector's longest pause 0.18 ms. Two and a half times, where a frame of
arithmetic is nearly six -- an integrated program spends its time in the
runtime.
**And what a frame costs is measured, both ways** (D1123): twenty thousand
bodies a frame, five hundred frames, the release engine at 306 µs in the
middle and 403 at its worst against the machine's 1812 and 8168, with
hand-written C at 63 and 97. The worst frame the release engine had is below
the median frame the machine had, and over those five hundred frames there was
one allocation and no walks — a frame that promised `no.alloc` cannot be
interrupted by the collector.
A call from one compiled body to another is written out rather than made
through a door, which is 14% of the gameplay workload and 20% of the one that
is mostly branches (D1122); what a call keeps is in the public header for
that, held to the machine's own shape while it builds.
On `bench/kernel.kest` the release engine runs a
body-step in 65.5 machine instructions against the machine's 527 and Luau's
native code generation at 158, with `g++ -O2` at 23 — the five times that
workload cost against `g++` is 2.8 times since a run of elements stopped being
read through a call (D1112); on `bench/rules.kest`,
the gameplay workload, it runs the whole process in 1.244 G instructions
against the machine's 6.254 G, Luau's native tier at 1.294 G and `g++ -O2` at
0.429 G. **D1092's re-evaluation trigger was three times `bench/rules.cpp` and
this is 2.9**, so the two-engine decision stands on a measurement rather than
on an argument.

**And the edit loop is measured** (D1100): `kest check` from a project's entry,
from cold, is 7 ms for a real small project, 366 ms for 112,647 lines in 1,892
files and 4.4 s for a million lines. A mistake costs no more than no mistake,
one file on its own is milliseconds at any scale, and what `check` prints
costs nothing measurable. So there is no daemon, no persistent session and no
incremental state, and the number that would earn one is written down.

## What the closeout shipped, and what it rests on

1.0.0 was tagged and published on 2026-09-18 and withdrawn on 2026-09-19
(D1035). The tag and the release are gone and the history is not. What the
closeout changed, in the order it was done: the heap became a thing a program can be given pieces of back (D996), the
fuzzer grew from one boundary to the six somebody else's bytes arrive through
(D997), the version became 1.0.0 with four numbers and a policy for each
(D998), the gate learned to name what a compiler made and nobody meant to keep
(D999), and both platforms got an archive that is unpacked, run and built
against on every commit (D1000). Two defects the gate found on the way are
D1001 and D1002, and the second is one a machine that is not this one found.

## What is left, and it is not engineering

Nobody outside this project has written a program in Kest. That is the one
thing in the completion mission's section 33 that a repository cannot do for
itself, and it is not a thing to fake. The evaluation package is the answer to
the part that *is* this repository's: a clean checkout, eight commands, and
numbers beside three other languages.

## What was found by driving rather than reading

Three defects this year were in things that were true when they were written,
stayed written, and stopped being checked because nothing asked. Each was found
the same way: by making the thing do what it says.

- A `store` grown inside a `scratch { }` block was emptied by the end of it,
  taking with it what it held before the block opened. No refusal and no
  message. Found by running the fixed-live-set trial section 9 asks for
  (D972).
- A reload accepted a program whose entry took one more argument than it did,
  because the host checked the shape of the *world* and nothing checked the
  shape of a *call*. Found by driving seven edits through the host that does
  the whole protocol (D985).
- Everything that reads the code walks it an instruction at a time, and a walk
  that met a breakpoint read a one-byte instruction where a three-byte one is.
  The debugger reported the wrong line, and a wrong line looks exactly like a
  right one. Found by stepping (D991).
- The language server answered about the file opened last rather than the file
  a message named. Measured carefully and never widened: D1131 timed it at
  three depths on two project sizes and opened one file every time. Found by
  opening two (D1143).
- A check held three ceilings that belonged to one depth of path, and the gate
  runs it at another. Every replication by hand was one room deep and caught
  the hole; the gate was two deep and missed it. Found by scanning every
  ceiling a byte at a time and asking where the window was (D1144).

Two more were found by building for a second and third platform: a test that
relied on where an array happened to land, and a line end that made two
platforms write different bytes. Both were in the tests rather than the
language, which is the useful half of what a second platform is for.

## Where to read about a piece of it

This note says where the work is and what went wrong. Why a thing is the way it
is belongs in `docs/decisions.md`, which is append-only and says what each
decision rests on; what a reader with a program has to do about a change belongs
in `CHANGELOG.md`; and what a program means belongs in `docs/language.md`, which
is the normative one (D994).

The one thing to read first, for somebody picking this up: the completion
mission's sections above, each with the decision that answered it. Every one of
those decisions names what it was measured or argued on.
