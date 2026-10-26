# Memory

Two allocators with different jobs: arenas for what compiling makes, which is freed all at once, and the heap a running program stands on, which gives places back one at a time. The host-facing rules are in [Host memory](../user/host-memory.md); this page is how they are built.

## Arenas

[`src/mem.c`](../../src/mem.c) is a bump allocator for tokens, trees, types, diagnostics and the compiled module. Nothing in an arena is freed individually.

- **One ceiling for a build.** A build's arena may be given a ceiling (`--room`, `kest_build`'s `room`), and every arena taken under it (the backends' working bodies, the verifier's tables, the token and tree arenas) is counted against that ceiling for as long as it holds anything. So every stage of compiling is bounded by one number a host chose.
- **Stages give back what is dead.** Tokens are dead once the tree is made (a node holds a span, never a token), and the trees are dead once the last generic copy is compiled and the promises are proved over the emitted code. Each lives in an arena of its own given back where its last reader ends, which is about half of what compiling asks for.
- **Three numbers.** `cost` is the most the arena and everything under it held at once (what a ceiling must allow for the same work to fit again), `held` is what remains at the end, and `working` is what stages held in memory of their own and gave back. A ceiling refuses against `cost`.
- **Growing lists** grow by a quarter in place where they can, never past what the remaining bytes could produce (a token is at least one byte), so an arena does not keep every size a list passed through.
- **Refusals for testing.** `KEST_REFUSE_ARENA` and `KEST_REFUSE_AT` make an arena refuse at a chosen request, which is how the checks walk every out-of-memory path and require each to end in a diagnostic (`K0639`, "not enough memory to finish, or to say more about it", is the one bit recorded when there is no room to record anything else).

## The heap

[`src/ground.c`](../../src/ground.c) holds a machine's text, arrays, stores and lend headers.

**Plots and widths.** Memory is taken from the host in plots. A plot is 16 KB aligned, so masking any address finds the plot it is in, and every plot starts with a magic number so a walk that meets a pointer into the middle of one can tell it from memory that belongs to someone else. Each plot hands out places of one width from a ladder: 16, 32, 48, 64, 96, 128 and so on, close together where values are small and doubling above, up to 128 KB. A value wider than the widest gets a plot of its own. A plot holds eight places of its width, capped at 256 KB, so a program that makes one wide thing does not pay a megabyte for it. A bitmap per plot says which places are live.

**Growth.** Arrays and stores double. When the place an array is in has room for the larger size (a 128-byte run in a 256-byte place) it grows where it stands and nothing moves; otherwise it takes a new place and copies, and the old place is given back at the next walk. New memory arrives as zeros, so a fill of zero is never written.

**Mark and sweep, conservative and non-moving.** Slots carry no tags, so the collector cannot know which slots are addresses. It reads every eight bytes as a possible address and keeps the place it points into, if it points into a live place of a plot. A number that looks like an address keeps memory alive; that costs memory and never correctness. Nothing moves, which is what lets a program hold an interior address (an element of an array, a piece of text cut from another) and lets a walk find what an interior pointer is inside of. Reference counting was rejected for the same reason and because it would be paid by every program on every copy.

**Roots.** The machine's slots up to the top of the stack, the values a host said it keeps (`kest_keeps`), every live lend, and the spare lend headers. The host's own memory is never read, which is why a walk is refused while the program is running inside a door: the host's C locals may hold handles the walk cannot see.

**When.** A walk happens inside an allocation, when the machine has been handed as much as it was holding at the last walk, with a floor of 256 KB so that a program making almost nothing does not walk over nothing. `kest_collect_after` changes the multiplier and `kest_collect` walks now. An open `scratch` block defers a walk, since the block is about to give its memory back anyway. A body that promises `no.alloc` cannot be interrupted, because there is no allocation for a walk to happen in.

**Cost.** A pause is linear in what is reachable: about 0.6 ms per megabyte to mark and 0.15 to sweep on the machine the benchmarks were taken on. Roots cost almost nothing. An incremental collector was rejected because it needs a write barrier on every store that could write an address, and with untagged slots and structs copied with `memcpy` every program would pay it on every copy.

**Fragmentation.** A plot goes back to the host only when every place in it is free. `kest_collected` reports, per pause, how much memory the host gave is in places nothing uses.

## Text

A piece of text is two slots, the address of its bytes and its length, and sixteen bytes where it sits in memory. The length makes `len` a read and lets a cut be a place inside another piece and a length, so `slice` and `rest` copy nothing and do not allocate. Text the machine makes (`text()`, a hole, a join) has a zero byte after it that is not part of it; a cut does not, since the byte after it belongs to what it was cut from. Text is held to UTF-8 where it enters: the source file when read, `text(bytes)` in the machine and `kest_text` at the boundary. Everything made from text that was already valid is valid, so nothing else walks it.

## Lends

A lend is a header on the heap in front of a block that is the host's. The header has the element layout and count; the machine keeps a list of live lends. A host taking a lend back (`kest_lend_ends`) marks the header ended, so a read through it is refused; ending either of two lends over the same bytes, or over overlapping tails, ends both. Ended headers are reused for the next lend, so a host that lends and ends a batch every frame pays for its widest frame once. A heap reset takes the headers and the list with it.

## Scratch

A `scratch` block in a program, and `kest_scratch_mark` and `kest_scratch_rewind` from a host, both record where the heap's allocations have reached and put everything made after that back. Eight may be open at once per body (nested lexically) and 64 per machine. Inside the language the compiler proves nothing escapes (see [Checking](checking.md)); from the host it is the host's rule, because the machine cannot tell a kept pointer from a dropped one. Every way out of a block puts the heap back, including a fault, a spent budget and a cancel.

## Reset

`kest_heap_reset` empties the same heap rather than making a new one, so it asks the host for nothing. Every handle from before it is invalid until the machine makes something new at the same address, at which point the stale handle reads the new data: a pointer carries no stamp. `kest_still_holds` and `kest_kept_where` let a host ask before keeping a value.
