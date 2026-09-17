# Where this is, and what is known to be wrong

This is the engineering note for the v1 convergence mission. It is short on
purpose and it is kept current: `docs/worklog.md` is history and is not read for
what to do next.

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
| E4 | a text slot is a `const char *`; length is `strlen`, so `len` is O(n) and embedded noughts are unrepresentable. The reusable buffer is answered: `fit` and `std.text`'s `fitting` (D940). A length-carrying representation was built and put back, because a cut would allocate under it; D955 says what the two-slot one would take, and the policy about a nought is written down |
| E5 | `live_from` scanned to the store's high-water mark. It reads a bit a slot and sixty-four at a time now (D954): four thousand walks of a store holding eight things out of two hundred thousand went from 0.22s to 0.02s |

## What the measurements say today

The numbers a decision quotes were true the day it was written, and two of them
have moved since. This is where the current ones are, so that a reader comparing
a run with a decision knows which is which.

| | then | now |
| --- | --- | --- |
| the colony's steady state, at 200, 400 and 800 days | 2,370,304 bytes (D940) | 2,338,096 bytes, and still flat: a store keeps a bit a slot rather than a byte (D954) |
| a frame step an entity | 127 ns (D926), 122 after D931 | 115 to 125 ns depending on the run, `make time` on this machine |
| what a frame step runs, an entity | 57 instructions (D958) | 38, after D961 took the two commonest pairs of pushes and D962 gave every constant the same door |

## Phases

The mission's order. `/home/kest/mission/STATE.md` carries which one is open.

    0 baseline and reproduction    done
    1 semantic and embedding repair    done
    2 one resolved per-instance representation   done   D962
    3 temporaries, text, buffer, store   done but for `scratch { }`,
                                         which phase 2 has now opened:
                                         D940, D954, D955, D956, D957
    4 validation correction            done   D944
    5 backend decision                 done   D958 counted it, D961
                                              measured a push, and D963
                                              built the second backend and
                                              measured it: 1.35 times
                                              slower, so keep the stack one
    6 a determinism profile that is true  done   D941, D942, D943
    7 identity, schema, reload         done   D945 to D949
    8 host reality and portability     done   D949 to D953
    9 machine-readable surface         done   D947
    10 documentation and the v1 boundary

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
