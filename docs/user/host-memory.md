# Host memory

What a host may hold of the program's memory, how it lends the program its own, how a world lives across frames, and how a running game reloads its rules. Read [Embedding Kest](embedding.md) first.

## Contents

- [The rule underneath](#the-rule-underneath)
- [Text](#text)
- [Lending memory](#lending-memory)
- [Keeping values between calls](#keeping-values-between-calls)
- [Giving memory back](#giving-memory-back)
- [The collector](#the-collector)
- [Saving and reloading](#saving-and-reloading)
- [What the machine cannot check](#what-the-machine-cannot-check)

## The rule underneath

A machine's heap belongs to the machine. Text, arrays and stores the program makes live there, and a host holds them as handles in `KestValue` slots. A handle is a pointer and carries no stamp, so the machine cannot tell a handle that is still good from one into memory it has since reused. Most of this page is the consequences of that one fact. A reference into a store is the exception: it is a place and a stamp, so a stale one is always caught.

The build that checks itself (`make kest-debug`, or a library built with the sanitizers) is told where every host block begins and ends, and catches several host mistakes that a release library cannot see. A host is worth running against it once.

## Text

`kest_text(runtime, bytes, length, &frame[i])` copies the host's bytes onto the machine's heap and writes the two slots a piece of text takes. The copy is the point: the program may keep text as long as it likes, which no host can promise for its own buffer. Bytes that are not UTF-8, or contain a zero, are refused (`K0611`). Handing the machine a pointer of the host's own as text is refused at the call, because it did not come from this machine.

`kest_text_bytes(&slot, &length)` reads text back. It is not always a C string: text cut from the middle of other text is a place inside it and has no terminating zero, so a host passes the bytes and the length on together, or copies them.

## Lending memory

A host can lend the program an array of its own memory instead of copying it in:

```c
KestValue events = kest_borrow(runtime, event_array, count, "Event", sizeof(Event));
```

The program sees an ordinary `[Event]` and reads and writes the host's bytes in place. The element type is named, and the machine checks the size against its own layout of that type and the alignment of the address; a mismatch is refused with the program's layout printed at the declaration (`K0610`). `kest_build_layout(build, "Event", &layout)` gives the size, alignment and each piece's offset, kind and field name before lending, so a host can compare with `offsetof` once.

What the program cannot do with a lent array:

- grow or shrink it: `push`, `pop`, `remove` and `clear` are refused, because the length and the block are the host's;
- keep it past `kest_lend_ends(runtime, lent)`, after which a read is refused (`K0637`);
- lend a type holding text, an array, a store, a reference or a function value (`K0647`), because those would be pointers the machine did not put there.

A lend costs a small header on the heap whatever its size. Headers of ended lends are reused, so a host that lends and ends a batch every frame pays for its widest frame once. `text(raw)` of a lent `[u8]` is the one place a lend copies: the text is the program's, the bytes stay the host's.

A host whose data is a byte buffer at an arbitrary address (a packet) cannot lend it as a wider type, because the address may not be aligned for it. It either copies the batch into an array of the type, which its compiler aligns, or lends the bytes as `[u8]` and lets the program read numbers out of them with explicit byte order.

A store cannot be lent: it is a slot map, not a run of elements. A host that wants one asks the program to make it.

## Keeping values between calls

The collector finds what is live by walking the machine's own slots. It cannot read the host's variables, so a handle a host keeps in its own memory across a call that allocates may be collected under it. There are two ways to be right:

- **Hand it back in.** A handle passed in the frame of every call is in the walk. `examples/engine.c` passes its world into each call.
- **Say so.** `kest_keeps(runtime, value)` keeps it alive until `kest_lets_go(runtime, value)` or a heap reset. It is not a count: keeping one value twice keeps it once.

`kest_still_holds(runtime, value)` answers whether the machine still has the memory a value is in, and `kest_kept_where` says where it lives: `KEST_KEPT_HEAP` (gone at a reset), `KEST_KEPT_PROGRAM` (text the source wrote, which lasts as long as the build), `KEST_KEPT_LENT` (a lend's header in front of the host's block) or `KEST_KEPT_NOWHERE`. A host asks before keeping a value, not after.

A host must never keep a pointer into what a handle holds: an array that grows moves its elements, and only the handle knows where they went.

`kest_held_new` and the `kest_held_*` functions do all of this for the common case of one world kept across frames: `kest_held_begin` calls a function and keeps what it gives back as the world, and `kest_held_call` calls with the world in front of the other arguments. `kest_held_run(held, "ground.tiles", &count, &stride)` reads a run inside the world where it lies, so drawing a map copies nothing.

## Giving memory back

**`kest_heap_reset`** throws the whole heap away between calls. Every handle the host holds is then dead, until the machine makes something new in the same place, at which point the old handle reads the new data. That is safe only for a host that keeps nothing across the reset.

**`kest_scratch_mark` and `kest_scratch_rewind`** do the same for part of the heap: a host marks before calling a query that makes text or arrays, copies out what it wants (a number, or text with `kest_gave_text` into its own buffer), and rewinds. Anything the program made after the mark is gone, including anything a step pushed into the world, so a mark goes around a query, never around a step that keeps something. Eight marks may be open at once, and a rewind to an outer mark closes the inner ones.

```c
uint32_t mark = kest_scratch_mark(runtime);
kest_call(runtime, query, frame, room);
kest_gave_text(runtime, query, frame, mine, sizeof(mine));
kest_scratch_rewind(runtime, mark);
```

Inside the program, a `scratch { }` block does the same with a proof that nothing escapes; see [Promises](promises.md#scratch-blocks).

Both resets and marks are refused while the program is running (from inside a door) and while anything is lent.

## The collector

The heap is a non-moving mark and sweep collector. Nothing moves, because slots carry no type tags: a walk treats every eight bytes as a possible address. A number that happens to look like an address keeps that memory alive, which costs memory and never correctness.

It walks on its own when the program has been handed as much as it was holding at the last walk (with a floor of 256 KB). `kest_collect_after` changes the multiplier, and `kest_collect` walks now, which a host with a frame budget calls between frames to move the pause there. A function that promises `no.alloc` is never interrupted by a walk, because a walk happens inside an allocation.

A pause is linear in what is still reachable, about 0.7 ms per megabyte on a desktop machine, so a world of about 20 MB reachable fits a walk in a 60 fps frame. There is no incremental collector; slots without tags cannot support the write barrier one needs. `kest_collected` reports each pause as it finishes, `kest_telemetry` fills counters for the heap's work, and `kest_clock` gives the machine the host's clock to time its pauses with.

## Saving and reloading

A reload replaces the rules of a running game without losing its world. The protocol is two functions the program declares: `save`, which takes the world and answers bytes, and `restore`, which takes the bytes and answers a world. `kest_held_reload(held, path, said, room)` builds the new program, asks the running machine to `save`, asks a machine of the new build to `restore`, checks that every door the host has called still exists and takes what it took, and only then replaces the running machine. If any step fails, the running machine is exactly where it was, and `said` says which step stopped it. `kest_held_changed` answers whether any file the program was built from has changed.

```kest
module keeper

import std.bytes

struct World {
    day: i32
    gold: i32
}

fn begin() -> World {
    return World(0, 10)
}

fn step(w: World) -> World {
    return World(w.day + 1, w.gold + 2)
}

fn save(w: World) -> [u8] no.host deterministic {
    let out: [u8] = array()
    bytes.putI32(out, w.day)
    bytes.putI32(out, w.gold)
    return out
}

fn restore(saved: [u8]) -> World no.host deterministic {
    let r = bytes.reader(saved)
    return World(bytes.readI32(r), bytes.readI32(r))
}
```

A host saving a world by its own means compares layouts: every layout has a mark (`kest_layout_mark`) folded from its size, alignment and every piece's offset, kind and name, and for an enum or a set of flags which cases or bits it has in what order. The same mark means the same shape; a different one means a field moved, changed width or was renamed, or a case or flag was added in the middle, which renumbers the ones after it. A reference is not an identity across machines, so a saved world stores the program's own identifiers and remaps them on the way back in.

`kest_build_code_mark` changes when what the machine will run changes and not for a comment; `kest_build_mark` changes when any byte of any file read changes. A host caching builds asks the first; a host watching files asks the second, or `kest_held_changed`.

## What the machine cannot check

Everything else a host can get wrong is refused where it is done. These are the few that are not, because a pointer carries no stamp:

- A handle to a lend that has ended names whatever is lent next.
- A value kept across `kest_heap_reset` reads whatever the machine puts there next.
- A block lent must outlive the lend, and a lend may not claim more elements than the block holds. The checking build catches both; a release build does not.
- The `context` pointer given to `kest_host_bind` must outlive every machine started with that host.
- A bound function must read the slots its declaration says and no more; `kest_extern_takes` and `kest_extern_layout` are how a host checks before binding.
- A machine may be sized smaller than the program needs, deliberately, to keep a program out of the rest of a frame; the machine cannot tell that from a mistake, and refuses at the line that runs out.
