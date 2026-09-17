# Where this is, and what is known to be wrong

This is the engineering note. It is short on purpose and it is kept current:
`docs/worklog.md` is history, `docs/decisions.md` is why, `CHANGELOG.md` is what
a reader with a program has to do about it, and this is where the work is.

## Reproduced defects

Each was reproduced against this tree with the smallest program or host that
asks the question. Evidence is named; nothing here is a claim from a document.

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
| F9 | a reference from one world resolves in another | two independent builds of one file: a `ref<Thing>` made in the first read an unrelated object in the second and answered its value |

## Read from the source rather than run

| | what |
| --- | --- |
| F10 | `no.alloc` is documented more broadly than what is measured: diagnostic and trap machinery allocate outside the program heap |
| E3 | the library has no process-global mutable state; what is shared is the build's stamp counter, which two runtimes of one build write without synchronisation. That is what F9 rests on |
| E4 | FIXED. Text is two slots — the bytes and how many — since D964: `len` is a read, a cut reaches nothing and `slice` came off the list of builtins that allocate, and a host reads bytes and a length through `kest_text_bytes` without measuring. The reusable buffer was already answered (D940). A nought inside is still refused, which is what D955 wrote down; what is new is that a cut does not end in one and the reference says so |
| E5 | `live_from` scanned to the store's high-water mark. It reads a bit a slot and sixty-four at a time now (D954): four thousand walks of a store holding eight things out of two hundred thousand went from 0.22s to 0.02s |

## What the measurements say today

The numbers a decision quotes were true the day it was written, and two of them
have moved since. This is where the current ones are, so that a reader comparing
a run with a decision knows which is which.

| | then | now |
| --- | --- | --- |
| the colony's steady state, at 200, 400 and 800 days | 2,370,304 bytes (D940) | 2,338,096 bytes, and still flat: a store keeps a bit a slot rather than a byte (D954) |
| a frame step an entity | 127 ns (D926), 122 after D931 | 115 to 125 ns depending on the run, `make time` on this machine |
| what an array of text costs a frame, an entity | 25 bytes (D915) | 38, because a piece of text in one is sixteen bytes rather than eight (D964) |
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
    3  persistent memory, decided         done  D992, on the trial in D972
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
    19 packaging                          done  D986 (no amalgamation), D989
    20 platforms and CI                   done: linux, windows, macos
    21 the benchmark suite                done  D980
    23 fuzz and sanitisers                done  D984
    24 validation cleanup                 done  D990
    25 documentation                      done  D994
    26 versioning and release policy      done  D983
    27 the evaluation package             done: an hour's worth on the front
                                          page, and a release archive

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

## Phase 2, which is in the tree now

D962 is the change D959 wrote down and did not make. `src/ir.h` is one body per
concrete function: values each read by an operation after it, places that say
what reaches a thing rather than an address already worked out, typed
three-address operations, and branches naming what they land on. `compile`
writes a body and no instruction; `lower` writes this machine's bytecode from
one and decides nothing about what a program means; `build` calls them in order.

What D959 said it would take turned out to be what it took: the walk kept its
shape, and what changed was the layer under it. What D959 said the half about
names was worth also turned out to be right — this language refuses shadowing,
so reading a resolved name buys nothing a scan does not already give. What the
body is for is the second backend and the lexical `scratch { }`, and both are
open.

What says the change means the same is a disassembly of every `.kest` file in
the tree before and after: fourteen files byte for byte, and the rest differing
in three ways that are each the backend doing in one place what it did in
three. D962 lists them.
