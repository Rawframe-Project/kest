# Glossary

The words this project uses with a fixed meaning, in code, comments, diagnostics and documentation.

| Word | Meaning |
| --- | --- |
| **build** | a compiled program and everything it was compiled from (`KestBuild`). Read-only once built; machines are started from it |
| **machine** | a running instance of a build (`KestRuntime`): its own stack, heap and report |
| **host** | the C program that embeds Kest. The command line is one |
| **door** | a function the host provides, declared in Kest as `extern fn`. Also called an extern |
| **capability** | the part of a door's name before the dot (`Io` in `Io.write`); denying one is not binding it |
| **binding** | the host's table of doors (`KestHost`), copied by a machine at start |
| **opened door** | a door the host also marked as safe for untrusted code with `kest_host_open` |
| **slot** | one `KestValue`, eight bytes: the unit a frame and the operand stack are made of |
| **frame** | the slots a call passes its arguments in and gets its answer back in |
| **layout** | how a type sits in memory: size, alignment and a piece per scalar. A value has a frame layout (one slot per scalar, an `f32` as a double) and a memory layout (C's) |
| **piece** | one scalar of a layout, with its offset, kind (`KEST_L_*`) and field path |
| **mark** | a 64-bit FNV-1a fold that says whether two things are the same: a file's bytes, a program's code, a layout's shape |
| **handle** | a value that names something on the heap: an array, a store, text's bytes. A pointer with no stamp |
| **reference** | a `ref<T>`: a place in a store and the stamp it was handed out with |
| **stamp** | a 40-bit handout number from one process-wide counter, never handed out twice |
| **store** | a slot map with stamps, live bits and a free list (`store<T>`) |
| **lend** | host memory the program sees as an array (`kest_borrow`), with a header on the machine's heap |
| **heap** | the machine's memory for values (`ground`), collected by mark and sweep |
| **arena** | a bump allocator for what compiling makes (`mem`), freed all at once |
| **plot** | a 16 KB region of the heap that hands out places of one width |
| **scratch** | a block of working memory: `scratch { }` in a program, or a host's mark and rewind |
| **promise** | `no.alloc`, `no.host` or `deterministic`, written after a function's result and proved |
| **profile** | the named, versioned set of rules `deterministic` promises (`kest-det 3`) |
| **edition** | the version of the language a program is read in (`2026`) |
| **chunk** | one compiled function in bytecode, with its constants, slot count and source origins |
| **body** | one concrete function in IR; a generic has one body per set of types |
| **copy** | a generic function, struct or enum compiled for one set of types, named like `pick#i32,i32` |
| **carried body** | a small function written into its caller instead of called |
| **fused instruction** | one instruction doing what a common pair or triple did |
| **fuel** | the step budget: a step is a backward jump or a call, plus weight for bulk work |
| **work** | the count compiling does (a token, a node, an instruction laid down or proved), bounded by `--work` |
| **room** | the memory ceiling for a whole command or build, bounded by `--room` |
| **release engine** | the C backend (`emitc`) and the binary `kest build --release` makes |
| **natives** | bodies the C backend compiled, bound to a machine with `kest_natives_here` |
| **fault** | a failure while running: the call answers false and the report says why. Not an exception |
| **refusal** | any diagnostic that stops a build or a call |
| **report** | the diagnostics a machine or build has said since a host last asked |
| **held machine** | a machine kept across frames with its world, by the `kest_held_*` doors |
| **world** | what a held machine keeps between calls: whatever the program's begin function gave back |
| **hole** | an expression in braces inside a string |
| **the one form** | the layout `kest fmt` writes; a file is either in it or not |
| **the gate** | `tools/check.sh`, everything that has to pass |
