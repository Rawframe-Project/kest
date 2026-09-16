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
| E4 | a text slot is a `const char *`; length is `strlen`, so `len` is O(n), embedded noughts are unrepresentable, and there is no reusable buffer |
| E5 | `live_from` scans to the store's high-water mark, so walking a store is O(high-water) rather than O(live) |

## Phases

The mission's order. `/home/kest/mission/STATE.md` carries which one is open.

    0 baseline and reproduction    done
    1 semantic and embedding repair
    2 one resolved per-instance representation
    3 temporaries, text, buffer, store
    4 validation correction
    5 backend decision from that representation
    6 a determinism profile that is true
    7 identity, schema, reload
    8 host reality and portability
    9 machine-readable surface
    10 documentation and the v1 boundary
