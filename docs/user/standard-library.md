# The standard library

Every module is Kest source under `lib/std`, compiled with the program each time, so there is no library version to mismatch. `kest doc lib/std/<module>.kest` prints every declaration of a module with its comment, and the reference on the documentation site is generated the same way. Each function is called through its module (`text.trim(line)`) or on a value (`line.trim()`, see [Functions](functions.md#xfa)).

Importing a module compiles all of it and asks the host for every door it declares, whichever functions the program calls. An import nothing writes through is a warning (`K0511`).

## Modules

| Module | What it holds |
| --- | --- |
| `std.io` | `print` (text and a line end) and `write` (text alone), through the host door `Io.write` |
| `std.math` | `min`, `max`, `abs`, `clamp`, `sign`, `lerp`, `isNumber`, `sqrt`, `floor`, `ceil`, `round`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `pow` |
| `std.vec` | `dot`, `length`, `lengthSquared`, `distance`, `distanceSquared`, `direction`, `lerp`, `perpendicular`, `cross` for `vec2`, `vec3` and `vec4` |
| `std.text` | reading and writing numbers, searching, trimming, splitting, joining, case, characters; see [Text](text.md#stdtext) |
| `std.table` | `Table<K, V>`, a hash table: `empty`, `set`, `get`, `orElse`, `has`, `remove`, `count`, `keyAt`, `valueAt`, `keysOf`, `fit`, `refill`, `compact` |
| `std.sort` | `by`, `byWith`, `ascending`, `descending` |
| `std.random` | `Source`, a seeded generator: `from`, `next`, `number`, `below`, `between`, `fraction`, `chance`, `one`, `shuffle` |
| `std.hash` | `start` and `join`, to fold several hashes into one |
| `std.bytes` | numbers, truths and text written into and read out of `[u8]`, for saves and records |
| `std.os` | `read`, `write` and `exists` for files, `argCount`, `arg` and `args`, `now` and `millisBetween`, all through host doors |
| `std.fdlibm` | the algorithms `std.math` computes sines, cosines, arctangents and powers with |

Most functions promise `no.alloc no.host deterministic` where they can; `kest check lib/std/<module>.kest` prints each signature with its promises.

## Notes by module

**`std.math`.** Every function comes in both widths a program works in (`f32` and `f64`, `i32` and `i64`). `asin` and `acos` answer `none` outside -1 to 1. `sqrt` of a negative and `0.0 / 0.0` give `nan`, and a value too large to hold gives an infinity; `math.isNumber(x)` is the test for a value a program can go on with, since `nan` is not equal to itself. `abs` of the smallest `i32` is itself, because negating it wraps. `sqrt`, `floor` and `ceil` are host doors; the rest is Kest, which is what makes them [deterministic](determinism.md).

**`std.vec`.** `length`, `distance` and `direction` scale by the largest component before squaring, so they work for any vector whose answer fits an `f32`. `lengthSquared`, `distanceSquared` and `dot` are plain arithmetic and overflow where the squares do; they are for comparing. `direction` answers `none` for a zero vector.

**`std.table`.** Keys are anything that compares, structs included. `get` answers `V?`; `orElse(t, k, fallback)` is the lookup with a fallback. Pairs are walked by place, `keyAt(t, at)` and `valueAt(t, at)` for `at` below `count(t)`, without copying. `set` may grow, so a `no.alloc` frame uses `fit`, which writes only where the key already is, after `refill` made room before the frame. A table keeps the room it has held; `t = table.compact(t)` gives it back. A key that is not equal to itself (a `nan`, or a struct holding one) is not stored.

```kest
module stock

import std.io
import std.sort
import std.table

struct Item {
    name: text
    price: i32
}

fn cheaper(a: Item, b: Item) -> bool no.alloc no.host {
    return a.price < b.price
}

fn main() -> i32 {
    let counts: table.Table<text, i32> = table.empty()
    counts.set("apple", 3)
    counts.set("apple", counts.orElse("apple", 0) + 2)
    let items = [Item("rope", 5), Item("lamp", 2), Item("map", 9)]
    sort.by(items, cheaper)
    let prices = [4, 1, 3]
    sort.by(prices, sort.descending)
    io.print("{counts.orElse("apple", 0)} {counts.has("plum")} {items[0].name} {prices[0]}")
    return 0
}
```

```text
5 false lamp 4
```

**`std.sort`.** Sorts in place, allocating nothing, by a function that promises `no.alloc no.host`. `ascending` and `descending` are the usual orders for anything that `orders`; `byWith(xs, context, before)` passes something to the order. A struct has no order of its own, so a sort of structs is always told one.

**`std.random`.** A `Source` is a value the program carries: `source = random.next(source)` before each number. Nothing is global and nothing asks the host, so one seed gives one run on every machine. `shuffle` rearranges an array in place and answers the source it left off at; `one` picks an element or answers `none` for an empty array.

```kest
module dice

import std.io
import std.random

fn main() -> i32 {
    let source = random.from(42)
    let total = 0
    for i in 0..3 {
        source = random.next(source)
        total += random.below(source, 6) + 1
    }
    io.print("{total}")
    return 0
}
```

**`std.hash`.** `hash(x)` covers a whole value. To hash some fields of a struct, fold them: `hash.join(hash.join(hash.start, hash(item.id)), hash(item.price))`. Folding with `std.hash` makes two programs that fold the same fields agree.

**`std.bytes`.** Writes numbers low byte first at their own width, floats as their bits, and text with its length, so what is written reads back as exactly itself. A `Reader` reads in order; reading past the end answers zero and marks the reader `short`, so a load reads everything and checks once at the end.

**`std.os`.** Every function is a door the host binds, so a program that imports it runs only under a host that provides files, arguments and a clock; the command line does, and passes the words after `--` as the arguments. `read` answers `none` for a file that is missing and for one holding a zero byte, and empty text for an empty file. A file the heap has no room to hold as text is not `none`: the command line refuses the call with `K0662`, as a host does when a door cannot do what it was asked. `now` is microseconds from an unspecified start; the command line uses a monotonic clock where the platform has one.
