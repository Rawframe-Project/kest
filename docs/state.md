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
    26 versioning and release policy      done  D983, D998: this is 1.0.0,
                                          and what its four numbers promise
                                          is in the reference
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

**Two things are open and named as open.** They are measured and not done, and
each says why.

- Bulk text append. Eighteen per cent of `bench/words.kest` is `std.text`'s
  `append` copying a byte at a time. A bulk copy needs a builtin, which is
  language surface, and D1030 says why that was not added on this evidence.
- The dispatch loop. The largest cost in every workload — 48 to 92 per cent of
  cycles — and a quarter of its own cycles are the front end on its indirect
  branch. D979 measured from the other side what touching it costs.

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

## What shipped, and what it rests on

1.0.0 is tagged and published. What the closeout changed, in the order it was
done: the heap became a thing a program can be given pieces of back (D996), the
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
