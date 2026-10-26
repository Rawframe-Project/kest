# Host boundary

How the C API in [`include/kest.h`](../../include/kest.h) is designed, and the rules a new door has to follow. How a host uses it is in [Embedding Kest](../user/embedding.md) and [Host memory](../user/host-memory.md); the reference is generated from the header.

## Rules for every door

**One header, nothing beneath it.** A host includes `kest.h` and nothing else, and `libkest.a` needs only libc. `tools/check-header.sh` compiles a file that includes only the header and calls every function it declares, and links it against the library alone. The header is the common subset of C11 a host compiler accepts.

**Every refusal says why, in the report.** A door that cannot do what it was asked answers its "no" value (false, NULL, -1, 0) and writes a diagnostic into the machine's report, or the build's for a door asked of a build, with the same code and shape a program's errors have. Two answers are deliberately silent:

- a name a program does not define is -1 from `kest_entry`, because asking whether a program defines something is a question, not a mistake;
- the end of a walk (`kest_build_extern`, `kest_entry_name`, `kest_entry_of` past the last) is NULL or -1 and says nothing, because reading a list to the end is how it is read.

And `kest_host_bind` refusing a name already bound has no report to write into, since a host list is not a machine.

**An answer that means nothing is not an answer a real function can give silently.** Where a value a real function could answer (zero arguments, an empty piece of text) is also what an invalid index answers, the report says which it was. A question about the program (how many functions, what a name is) is said once per machine, since the program does not change; a refusal about a call is said every time.

**Answers that are lists are closed enums.** Where a door answers one of several things (`KestKept`, `KestSlot`, `KestReach`, `KestRefusal`), the enum has nothing else in it, so a host that reads it with a `switch` and no `default` is told by its own compiler when a case is added. Both hosts in the tree read every such enum that way.

**Nothing is taken away from a running program.** Freeing a machine, throwing its heap away, moving its heap ceiling, collecting, and marking or rewinding scratch are refused while the program is running (from inside a door) and while it is stopped at a breakpoint, because the program is standing on the heap and the stack. Lending is allowed, because it adds to the heap rather than taking it.

**What outlives what is one rule:** the build outlives its machines (`kest_build_free` refuses while machines stand on it and says how many), a machine copies its host list at start, and a door's `context` is the host's memory and must outlive every machine using it.

## Two layouts at one boundary

A value crosses the boundary in one of two shapes, and the API says which everywhere:

- **Frame layout**, for calls in and out: one slot per scalar, in field order, an `f32` as a double, text as two slots (bytes and length), an enum as its tag then its widest case's slots. `kest_frame_layout`, `kest_frame_gives`, `kest_extern_layout` and `kest_extern_gives` describe it piece by piece, and `kest_slot_of` says which `KestValue` member each piece is read through. `kest_frame_fills` and `kest_frame_reads` let a host state what it is about to write or read, so a frame of the right width with the wrong things in it is caught.
- **Memory layout**, for lends and for arrays a host reads: the C layout of the type. `kest_build_layout` gives size, alignment and each piece's offset, kind and field path (`where.x`, `cells[2].at`), so a host compares against `offsetof` and `_Alignof`.

Kinds that mean more than their width have kinds of their own: `KEST_L_TAG` (an enum tag), `KEST_L_HELD` (the byte that says whether an optional holds something), `KEST_L_BOOL`, `KEST_L_REF` (a reference, read through `integer`), `KEST_L_TEXT`, and `KEST_L_PAYLOAD` (a slot whose type depends on the tag; `kest_case_of` says what a case carries). Each was split out after two different values had the same run of pieces and a host could pass one for the other without being told. The machine reads a tag where it crosses (`K0636` going in, `K0650` coming back) and where a program reads a lend (`K0651`), since a lend's bytes are the host's and can change after the lend was made.

## Layout marks and reloads

Every layout has a mark: an FNV-1a fold of its size, alignment, whether it holds a tag, and every piece's offset, kind and name, plus for a tag which cases it can name in what order, and for a flag set which bits it has in what order. The last two were added because inserting a case in the middle of an enum, or a flag in the middle of a set, renumbers everything after it while leaving size, alignment and every offset unchanged; a world saved before such an edit read back with each case meaning the one below it. `kest_held_reload` compares marks and door shapes before replacing a running machine.

## The held doors are built from the public ones

`kest_held_*` is written only with doors any host could call (`src/held.c`), because every host that kept a world across frames had written the same few hundred lines: the world in front of every call, every handle in it kept, doors found again after a reload and held to taking what they took, files watched by their marks. A host that wants another protocol writes it the same way.

## Versioning

`KEST_ABI_VERSION` and `kest_abi_version()` must match before a host calls anything else. The number goes up when anything a host can see changes (arguments, answers, struct fields or their order, enum cases or their numbers, or their meaning), and not for an addition at the end. `KEST_VERSION_*`, `KEST_JSON_SCHEMA`, `KEST_PROFILE_*` and `KEST_EDITION` are the other numbers, each moved for its own reason; see [Versions and editions](../user/editions.md). `kest_version()` and `kest --version` read the same string so the library and the command line cannot disagree.

## Threads

A build is read-only once built, with two exceptions: a failed start writes the build's report (so failed starts are serialized by the host), and a debugger writes breakpoints into the build's code. The count of machines standing on a build is an atomic. A machine's heap, stack, stamps and report are its own, so machines of one build run on separate threads freely; two threads in one machine is undefined and nothing locks. `kest_cancel` is a single store, safe from any thread or a signal handler. The gate runs four starts at once and two machines of one build on two threads under the thread sanitizer.

## Two hosts share nothing

A `KestHost` is a list its caller owns; a machine copies the list it started from. No door lets one host reach another's machines or lets a program learn which host started the machine beside it, which is why isolation between hosts in one process is a property of what is absent rather than a check.

## The checking build

`kest_checked()` says whether the library was built with its own checks and the sanitizers. That build is told where every host allocation ends, so it catches what a release build cannot: a lend longer than its block (`K0610`), a lend of memory the host has freed, a door context freed after binding (`K0654`), a handle used after its machine was freed. A host is worth running against it once.
