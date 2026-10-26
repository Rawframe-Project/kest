# Promises

A function may promise three things about itself after its result: `no.alloc`, `no.host` and `deterministic`. The compiler proves each one over every body the function reaches, or refuses the program at the line that breaks it.

## Contents

- [The three promises](#the-three-promises)
- [What reaches the heap](#what-reaches-the-heap)
- [How a refusal reads](#how-a-refusal-reads)
- [Promises through function values](#promises-through-function-values)
- [Promises on host doors](#promises-on-host-doors)
- [Filling a buffer without allocating](#filling-a-buffer-without-allocating)
- [`scratch` blocks](#scratch-blocks)
- [What the compiler proved anyway](#what-the-compiler-proved-anyway)

## The three promises

| Promise | Means | What it is for |
| --- | --- | --- |
| `no.alloc` | the body takes nothing from the program's heap | a frame step that must not pay for allocation or be interrupted by the collector |
| `no.host` | the body calls nothing the host provides | a function that is pure computation, safe to call while the host holds its own locks |
| `deterministic` | the body answers the same bits on every machine that keeps the [simulation profile](determinism.md) | lockstep networking, replays, rollback |

```kest
module step

fn cooled(left: [i32]) no.alloc no.host deterministic {
    for i in 0..len(left) {
        if left[i] > 0 {
            left[i] -= 1
        }
    }
}
```

The promises are written in any order, each once. They are proved where they are written: a helper called from a promising function is judged by its body, transitively, and does not have to repeat the promise. A host can ask whether a function made each promise before calling it (`kest_entry_promises`).

`no.host` and `deterministic` differ: a door the host declares `deterministic`, such as `Math.sqrt`, may be called from a `deterministic` body but never from a `no.host` one.

## What reaches the heap

- an array literal, `array()`, `store()`, and `text(bytes)`;
- a string with a hole in it;
- `push`, `add` and `room`, which may grow what they are given;
- a lend handed back from a host, whose header is on the heap;
- a call to a host door that does not declare `no.alloc`.

Everything else does not: `fit`, `pop`, `remove`, `clear`, `get`, `set`, `find`, `matches`, `rest`, `slice`, `len`, `hash`, structs, optionals, fixed runs built in place, and calls to functions whose bodies do not allocate. A `defer`red call counts like any other call. `no.alloc` is about the program's heap only: what the compiler, a diagnostic or a host's own C code allocates is outside it.

## How a refusal reads

A refusal names the line that breaks the promise and then every call between it and the promise:

```
error[K0401]: this allocates, and `chain.stepFrame` promises `no.alloc`
  --> chain.kest:5:17
   |
 5 |     let trail = [n, n, n]
   |                 ^^^^^^^^^ a run that can grow is one on the heap
  --> chain.kest:17:4
   |
17 | fn stepFrame(n: i32) -> i32 no.alloc {
   |    ^^^^^^^^^ `chain.stepFrame` promises it here
  --> chain.kest:18:12
   |
18 |     return second(n)
   |            ^^^^^^^^^ which calls `chain.second`
```

The promise is proved twice: once over the source, which is where the path above comes from, and again over the instructions the compiler emitted. If the two ever disagree the second calls it a fault in the compiler. A misspelt promise such as `no.allocate` is an error that lists the three.

## Promises through function values

A promise is part of a function type, so a call through a function value is judged by the type. A type that promises nothing is not a body that allocates; it is one nothing is known about, and calling it from a promising function is `K0402`, with the fix of writing the promise into the type: `fn(i32) -> i32 no.alloc`. Only a function that keeps the promise fits that type. A host that writes a function index into a frame is checked by the machine at the call (`K0623`).

## Promises on host doors

An `extern` declares its own promises, because its body is C the compiler cannot read: `extern fn Clock.now() -> u64 no.alloc`. The machine checks the part it can see: after a call to a door that promised `no.alloc`, the program's heap must be where it was, or the call fails with `K0631`. What the host's C code does with its own memory is the host's business.

## Filling a buffer without allocating

`push` may grow, so a `no.alloc` body cannot call it. `fit` is `push` that never grows: it writes where there is room and answers `false` where there is not. Room is made before the frame with `array(n, v)` followed by `clear`, or with `room(xs, n)`; `clear` keeps the room.

```kest
module gather

import std.io

fn gather(into: [i32], many: i32) -> i32 no.alloc {
    clear(into)
    let put = 0
    for i in 0..many {
        if fit(into, i) {
            put += 1
        }
    }
    return put
}

fn main() -> i32 {
    let buffer: [i32] = array()
    room(buffer, 4)
    io.print("{gather(buffer, 6)} {len(buffer)}")
    return 0
}
```

```text
4 4
```

`array()` makes an array with no room, and the compiler warns (`K0347`) when a body fills one with `fit` without giving it room first. `table.fit` and `table.refill` do the same for a table, and `text.fitting` for bytes of text.

## `scratch` blocks

A `scratch { }` block marks the heap when it is entered and puts it back when it is left, so a block that builds text to print costs the same every time round a loop.

```kest
module frames

import std.io

struct Npc {
    name: text
    hp: i32
}

fn tick(world: [Npc]) -> i32 no.alloc {
    let alive = 0
    for i in 0..len(world) {
        if world[i].hp > 0 {
            world[i].hp -= 1
            alive += 1
        }
    }
    return alive
}

fn main() -> i32 {
    let world = [Npc("ann", 2), Npc("bo", 0)]
    for round in 0..3 {
        scratch {
            let line = "round {round}: {tick(world)} alive"
            io.print(line)
        }
    }
    return 0
}
```

```text
round 0: 1 alive
round 1: 1 alive
round 2: 0 alive
```

What is made inside may not outlive the block, and the compiler proves it: giving it back from the function, storing it in a name declared outside, or putting it into an array or store from outside is an error (`K0507`). Growing anything older than the block is also an error, so inside a block a call that is handed something older can only be made if it promises `no.alloc`. Copying out is fine: a number is a number afterwards, and `text.fitting(out, piece)` copies bytes into a buffer from outside.

A host may be handed what the block made, so `io.print` works inside one, but a host that keeps it past the call keeps a pointer into memory that is about to be reused. Every way out of a block puts the heap back, including a fault or a budget running out. Blocks nest up to eight deep in one body.

## What the compiler proved anyway

The proof runs whether or not a function promises anything. `kest check --cost` prints, for each function, whether it reaches the heap, whether it reaches the host, whether anything in it is outside the deterministic profile, and which promises it could make. `kest check --json` carries the same facts under `proved`.
