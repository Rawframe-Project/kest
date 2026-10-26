# Versions and editions

`kest --version` prints five numbers, and each moves for a different reason:

```
kest 0.0.1, abi 4, json 4, profile kest-det 3, edition 2026
```

| Number | What it versions | Read by |
| --- | --- | --- |
| `kest 0.0.1` | the release, by Semantic Versioning | people |
| `abi 4` | the shape of the C API in `include/kest.h` (`KEST_ABI_VERSION`, `kest_abi_version()`) | hosts |
| `json 4` | the shape of the JSON every command writes (`schema` in each object, `KEST_JSON_SCHEMA`) | tools |
| `profile kest-det 3` | what `deterministic` promises (`kest_profile()`), see [Determinism](determinism.md) | hosts keeping replays and saves |
| `edition 2026` | the language a program is read in (`KEST_EDITION`) | programs |

## What a program is promised

**A program is read in the edition it names.** A project names its edition with an `edition` line in `kest.project`, and a project that names none is read in `2026`, the first. A change that would stop a program compiling, or make it mean something else, is only made in a new edition, and a later compiler reads a program in the edition it names the way it was read before.

**A warning comes first.** Anything a new edition refuses is warned about in the edition before it, with its own code and what to write instead. A name that was retired says what replaced it: `vec.add` is refused with `a + b` beside it.

**Every edition gets what cannot break a program:** syntax where no program could have written anything before, library functions beside the existing ones, and new warnings. Two kinds of fix apply to every edition because no program has a right to the old behaviour: a behaviour that contradicted the documentation, and a hole in what [Running untrusted code](untrusted-code.md) promises. Both are named as what they are in the changelog.

**Diagnostic codes are stable.** `K0342` keeps meaning what it means; the words under it may be improved.

## What hosts and tools are promised

Before 1.0.0, nothing a host or a tool reads is frozen. The C API, the JSON and the shape of the standard library may change between any two releases, and each change says in the changelog what a host or tool has to do about it. The numbers still tell a host which shape it has:

- A host compares `KEST_ABI_VERSION` with `kest_abi_version()` before calling anything and stops when they differ. The ABI number goes up whenever anything a host can see changes: a function's arguments or answer, a struct's fields or their order, an enum's cases or their numbers, or what any of them mean. It does not go up for something added at the end, which an older host never sees.
- A tool reads `schema` first. It goes up when a field changes meaning, is removed, or is added to a list that was said to be complete, and not when a field is added beside the others.
- A host keeping a replay or a save writes the profile down beside it. Its number goes up only when what a program can observe about arithmetic changes.

From 1.0 on, these become the promises: the ABI frozen for all of 1.x except additions at the end, schema 4 objects readable by schema 4 tools for all of 1.x, and a program that checks under one 1.x checking under every later one.

## What is not promised

**The bytecode is not a format.** It is internal, unversioned and not stable between any two builds of the compiler, even of the same commit on two machines. Nothing reads it back; a program ships as its source beside the machine, or as a [release binary](release-builds.md) that carries its source. Compiling is fast enough (a hundred thousand lines in about two hundred milliseconds) that a cache would buy nothing worth a format.

**A reference is not an identity outside its machine.** A saved world stores the program's own identifiers.

## How the language changes

A change to syntax, to what a program means, to the shape of the standard library, to a door of the C API, to a name in the JSON or to the deterministic profile starts as a GitHub issue labeled `feature` whose body carries the design: what changes, why, what it breaks and for whom, what else was considered, and which edition it lands in. The reasoning of a change that is made ends up in the documentation of the part it governs.
