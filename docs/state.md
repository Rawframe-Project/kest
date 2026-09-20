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
| F30 | `deterministic` was not written into the name of a function type | `room += 14` stood where the word should have been: a number grown after the memory it described had been handed out. `check`, `--json`, the message naming the shape to write, and the name a generic copy is compiled under all named a shape that was not the shape (D1064) |
| F31 | a table could not be written to inside a promise | `table.set` may refill, so a frame keeping a count per thing could not promise `no.alloc` at all. `table.fit` is `set` with the growth taken out (D1063) |

## Read from the source rather than run

| | what |
| --- | --- |
| F10 | `no.alloc` is documented more broadly than what is measured: diagnostic and trap machinery allocate outside the program heap |
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
| what a frame step runs, an entity | 57 instructions (D958) | 38, after D961 took the two commonest pairs of pushes and D962 gave every constant the same door |

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
    15 the standard library               small and said to be
    16 project and dependencies           done  D982
    17 tooling                            done  D976, D977, D979, D991
    18 the editor                         done  D978
    19 packaging                          done  D986 (no amalgamation),
                                          D989, D1000: an archive on both
                                          platforms, unpacked, run and built
                                          against in CI
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
  (D1068).
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
