#!/bin/sh
# The other backend, held to answering what the first one does. `kest emit --c`
# writes the same bodies as C for the compiler a release is built with, and
# what says the two are one program is that a program compiled both ways
# answers the same thing. `make check` runs this. See D1093.
#
# Two halves. Every program it is given is written as C and handed to the
# host's compiler, because C that will not compile is the one kind of wrongness
# this backend can have that reading the file does not show, and the programs
# in this tree are a corpus of a thousand bodies nobody had to write for it.
# And then the programs below, written here and run both ways, because a file
# that compiles says nothing about what it does. They are written here rather
# than kept in the tree for the reason `check-lends.sh` writes its own: what
# they are for is this check, and a program in `examples` is for a reader.
set -u
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ $# -eq 0 ]; then
    echo "nothing was given to write as C, which is every sweep below passing"
    echo "without reading a file"
    exit 1
fi
if [ ! -x ./kest ] || [ ! -f libkest.a ]; then
    echo "the compiler and the library are not built"
    exit 1
fi

compiled=0
written=0
bodies=0
wrong=0
# What is wrong, kept rather than printed as it is found: what reads a check
# that refuses reads the first line, so the sentence saying how much is wrong
# comes before the list of it.
said="$work"/said
: >"$said"
for file in "$@"; do
    if ! ./kest emit --c "$file" >"$work"/wrote.c 2>"$work"/why; then
        continue
    fi
    # How much of the file it wrote, read out of the file itself. A count of
    # bodies rather than of files: a module of generic functions nothing has
    # used yet has no bodies at all, and a file with none of them written and
    # a file with nothing in it read the same from outside.
    counted=$(sed -n 's,^// \([0-9]*\) of \([0-9]*\) bodies written$,\1 \2,p' \
              "$work"/wrote.c)
    if [ -z "$counted" ]; then
        echo "    the C written for $file does not say how much of it was \
written" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    written=$((written + ${counted% *}))
    bodies=$((bodies + ${counted#* }))
    if $cc -O1 -Iinclude -c -o "$work"/wrote.o "$work"/wrote.c \
            2>"$work"/why; then
        compiled=$((compiled + 1))
        continue
    fi
    {
        echo "    the C written for $file will not compile:"
        sed 's/^/        /' "$work"/why | head -5
    } >>"$said"
    wrong=$((wrong + 1))
done

# And that the sweep above was about anything. A backend that wrote nothing
# would hand the host's compiler a file of comments and pass. It used to be
# wrong to write everything as well -- there was no C for a crossing into the
# host, so a file that claimed the whole of a program was a file claiming what
# it could not do. There is now (D1108), and a program every body of which is
# written is the ordinary case rather than a fault; what says a body left out
# is left out honestly is the reason beside its name and the run that follows.
if [ "$compiled" -eq 0 ] || [ "$written" -eq 0 ] ||
        [ "$written" -gt "$bodies" ]; then
    echo "    $written of $bodies body(s) written over $compiled file(s), \
which is not a backend that wrote anything" >>"$said"
    wrong=$((wrong + 1))
fi

mkdir "$work"/programs
cat >"$work"/programs/numbers.kest <<'PROGRAM'
module numbers

fn mix(a: i64, b: i64) -> i64 no.alloc no.host deterministic {
    let c = a * 3 - b / 2
    let d = c % 7
    return c + d * 2
}

fn widths(i: i32) -> i64 no.alloc {
    let a: u8 = u8(i % 256)
    let b: i16 = i16(0 - i)
    let c: u32 = u32(i) * 3000000
    let d: i8 = i8(i % 128)
    let e: u16 = u16(c % 65536)
    let f: u64 = u64(c) << 3
    let g: i64 = i64(b) >> 2
    return i64(a) + i64(b) + i64(c) + i64(d) + i64(e) + i64(f % 1000) + g
}

fn main() -> i32 {
    let total: i64 = 0
    let i: i64 = 1
    while i < 200 {
        total = (total + mix(i, i * 2 + 1) + widths(i32(i))) % 1000003
        i += 1
    }
    return i32((total % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/floats.kest <<'PROGRAM'
module floats

fn main() -> i32 {
    let sum: f64 = 0.0
    for i in 0..500 {
        let x = f64(i) * 0.5
        sum += x * x - x / 3.0 + x % 7.0
    }
    let narrow: f32 = f32(sum) * 2.0
    if narrow > 0.0 {
        sum = sum + f64(narrow)
    }
    return i32((i64(sum) % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/shapes.kest <<'PROGRAM'
module shapes

struct Point {
    x: f64
    y: f64
    tag: i32
}

enum State {
    Idle
    Hunting(i32)
    Fleeing(i32, i32)
}

fn moved(p: Point, by: f64) -> Point no.alloc {
    return Point(p.x + by, p.y - by, p.tag + 1)
}

fn worth(state: State) -> i32 no.alloc {
    return match state {
        Idle -> 1
        Hunting(at) -> at * 2
        Fleeing(from, to) -> to - from
    }
}

fn pick(i: i32) -> State no.alloc {
    if i % 3 == 0 {
        return State.Idle
    }
    if i % 3 == 1 {
        return State.Hunting(i)
    }
    return State.Fleeing(i, i * 2)
}

fn main() -> i32 {
    let p = Point(1.0, 2.0, 0)
    let total: i32 = 0
    for i in 0..300 {
        p = moved(p, f64(i) * 0.25)
        total = (total + worth(pick(i))) % 100003
    }
    return i32((i64(p.x + p.y) + i64(total)) % 251) + p.tag % 7
}
PROGRAM
cat >"$work"/programs/control.kest <<'PROGRAM'
module control

fn fib(n: i32) -> i64 no.alloc {
    if n < 2 {
        return i64(n)
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() -> i32 {
    let total: i64 = fib(18)
    let i: i32 = 1
    while i < 400 {
        if i % 17 == 0 {
            i += 1
            continue
        }
        for k in 0..4 {
            total += i64(k * i)
        }
        total = total % 1000003
        if total < 0 {
            break
        }
        i += 1
    }
    return i32(total % 251)
}
PROGRAM
cat >"$work"/programs/elements.kest <<'PROGRAM'
module elements

struct Body {
    x: f64
    y: f64
    tag: i32
    small: u8
}

// A field of an element rather than the whole of it, which is the read a
// frame does most and the one that carries a byte offset into the element.
fn tagsOf(world: [Body]) -> i32 no.alloc no.host deterministic {
    let sum = 0
    for at in 0..len(world) {
        sum += world[at].tag + i32(world[at].small)
    }
    return sum
}

fn spread(world: [Body]) -> f64 no.alloc no.host deterministic {
    let sum: f64 = 0.0
    for at in 0..len(world) {
        sum += world[at].y
    }
    return sum
}

fn step(world: [Body], rounds: i32) -> f64 no.alloc no.host deterministic {
    let sum: f64 = 0.0
    let i = 0
    while i < rounds {
        for at in 0..len(world) {
            let one = world[at]
            one.x += one.y
            one.tag = (one.tag + 1) % 7
            one.small = u8((i32(one.small) + 3) % 256)
            world[at] = one
        }
        i += 1
    }
    for one in world {
        sum += one.x + one.y + f64(one.tag) + f64(one.small)
    }
    return sum
}

fn main() -> i32 {
    let world: [Body] = array()
    for i in 0..64 {
        push(world, Body(f64(i), f64(i) * 0.5, i % 5, u8(i % 256)))
    }
    let total = i64(step(world, 20)) + i64(tagsOf(world)) +
        i64(spread(world))
    return i32((total % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/world.kest <<'PROGRAM'
module world

struct Thing {
    name: text
    worth: i32
    next: ref<Thing>?
}

fn build(many: i32) -> store<Thing> {
    let all: store<Thing> = store(many)
    let made: [ref<Thing>] = array()
    for i in 0..many {
        push(made, add(all, Thing("thing", i, none)))
    }
    for i in 0..len(made) {
        if let one = get(all, made[i]) {
            let linked = one
            linked.next = made[(i + 1) % len(made)]
            set(all, made[i], linked)
        }
    }
    return all
}

fn worth(all: store<Thing>) -> i64 no.host {
    let total: i64 = 0
    for r in all {
        if let one = get(all, r) {
            total += i64(one.worth)
            if let onward = one.next {
                if let there = get(all, onward) {
                    total += i64(there.worth % 3)
                }
            }
        }
    }
    return total
}

fn thin(all: store<Thing>, every: i32) -> i32 no.host {
    let gone = 0
    for r in all {
        if let one = get(all, r) {
            if one.worth % every == 0 {
                remove(all, r)
                gone += 1
            }
        }
    }
    return gone
}

fn main() -> i32 {
    let all = build(200)
    let sum: i64 = 0
    for round in 0..8 {
        sum += worth(all)
        if round == 4 {
            sum += i64(thin(all, 7))
        }
    }
    return i32((sum % 251 + 251) % 251) + len(all) % 7
}
PROGRAM
cat >"$work"/programs/growing.kest <<'PROGRAM'
module growing

struct Item {
    name: text
    many: i32
}

fn fill(how: i32) -> [Item] {
    let bag: [Item] = array()
    for i in 0..how {
        push(bag, Item("thing", i))
    }
    return bag
}

fn drain(bag: [Item]) -> i32 no.host {
    let total = 0
    while len(bag) > 0 {
        let one = remove(bag, 0)
        total += one.many + i32(len(one.name))
    }
    return total
}

fn counted(how: i32, what: i32) -> i32 no.host {
    let counts: [i32] = array(how, what)
    let sum = 0
    for at in 0..len(counts) {
        sum += counts[at]
    }
    return sum
}

fn main() -> i32 {
    let bag = fill(24)
    let total = drain(bag) + counted(6, 7)
    if len(bag) != 0 {
        return 1
    }
    return i32((total % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/stacked.kest <<'PROGRAM'
module stacked

struct Point {
    x: f64
    y: i32
}

fn bump(n: i32) -> i32 {
    return n + 1
}

fn walk(world: [Point], rounds: i32) -> i64 {
    let sum: i64 = 0
    let i = 0
    while i < rounds {
        for at in 0..len(world) {
            let one = world[at]
            one.y = bump(one.y) % 11
            one.x += f64(one.y)
            world[at] = one
            sum += i64(one.y)
        }
        i = bump(i)
    }
    return sum
}

fn main() -> i32 {
    let world: [Point] = array()
    for i in 0..32 {
        push(world, Point(f64(i), i % 7))
    }
    return i32((walk(world, 20) % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/deep.kest <<'PROGRAM'
module deep

fn down(n: i32) -> i64 no.alloc no.host deterministic {
    if n <= 0 {
        return 0
    }
    return 1 + down(n - 1)
}

fn main() -> i32 {
    return i32(down(100000) % 251)
}
PROGRAM
cat >"$work"/programs/words.kest <<'PROGRAM'
module words

struct Tag {
    name: text
    rank: i32
}

fn same(a: Tag, b: Tag) -> bool no.alloc no.host deterministic {
    return a == b
}

fn weigh(t: Tag) -> i64 no.alloc no.host deterministic {
    let mixed = hash(t)
    let named = hash(t.name)
    let ranked = hash(t.rank)
    let odd = hash(f64(t.rank) * 0.5)
    return i64(mixed % 1000) + i64(named % 1000) + i64(ranked % 100) +
        i64(odd % 10)
}

fn label(which: i32) -> text no.alloc no.host deterministic {
    if which == 0 {
        return "bread"
    }
    if which == 1 {
        return "hammer"
    }
    return "coin"
}

fn main() -> i32 {
    // The same words made again somewhere else hash the same, which is what
    // a hash of text is. Asked outright rather than left to the total below,
    // where a hash of where the text is rather than what it says comes out
    // different by an amount that is whatever the addresses were -- and one
    // run in 251 it came out the same. See D1164.
    if hash(label(0)) != hash("{label(0)}") {
        return 200
    }
    let total: i64 = 0
    for i in 0..30 {
        let one = Tag(label(i % 3), i % 7)
        let two = Tag(label((i + 1) % 3), i % 7)
        total += weigh(one)
        if same(one, two) {
            total += 3
        }
        total += i64(len(label(i % 3)))
    }
    return i32((total % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/tagged.kest <<'PROGRAM'
module tagged

enum Kind {
    Food(i32)
    Tool(i32)
    Coin
    Note(i32)
}

struct Item {
    name: text
    kind: Kind
    many: i32
    spare: f32?
}

// Two cases carrying something in the same place and not the same kind of
// thing: a byte and a float are read as what their case says, not as the
// other one. See D1232.
enum Mark {
    Scored(u8)
    Weighed(f32)
    Blank
}

fn marks(ms: [Mark]) -> i32 no.alloc no.host deterministic {
    let total = 0
    for at in 0..len(ms) {
        total += match ms[at] {
            Scored(n) -> i32(n)
            Weighed(w) -> i32(w * 4.0)
            Blank -> 7
        }
        ms[at] = if at % 2 == 0 -> Mark.Weighed(f32(at) + 0.5)
            else -> Mark.Scored(u8(at * 3))
    }
    return total
}

fn worth(items: [Item], round: i32) -> i32 no.alloc no.host deterministic {
    let total = 0
    for at in 0..len(items) {
        let one = items[at]
        total += match one.kind {
            Food(fills) -> fills * one.many
            Tool(power) -> power * 3
            Coin -> one.many
            Note(which) -> if which == 0 -> 0 else -> 1
        }
        if let held = one.spare {
            total += i32(held)
        }
        one.many = (one.many + round) % 5
        one.kind = Kind.Tool(at % 3)
        items[at] = one
    }
    return total
}

fn main() -> i32 {
    let items: [Item] = array()
    for i in 0..16 {
        let spare: f32? = none
        if i % 3 == 0 {
            spare = f32(i)
        }
        push(items, Item("a", Kind.Food(i % 4), i, spare))
    }
    let total = 0
    for round in 0..5 {
        total += worth(items, round)
    }
    let ms: [Mark] = array()
    for i in 0..9 {
        push(
            ms,
            if i % 3 == 0 -> Mark.Blank
                else -> if i % 3 == 1 -> Mark.Scored(u8(i))
                else -> Mark.Weighed(f32(i) * 0.25)
        )
    }
    for round in 0..3 {
        total += marks(ms)
    }
    return i32((total % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/ordered.kest <<'PROGRAM'
module ordered

fn pick(i: i32) -> text no.alloc no.host deterministic {
    if i % 5 == 0 { return "apple" }
    if i % 5 == 1 { return "apricot" }
    if i % 5 == 2 { return "ap" }
    if i % 5 == 3 { return "" }
    return "banana"
}

fn least(names: [text]) -> text no.host deterministic {
    let best = names[0]
    for i in 1..len(names) {
        if names[i] < best {
            best = names[i]
        }
    }
    return best
}

fn weigh(a: text, b: text) -> i64 no.alloc no.host deterministic {
    let n: i64 = 0
    if a == b { n += 1 }
    if a != b { n += 2 }
    if a < b { n += 4 }
    if a <= b { n += 8 }
    if a > b { n += 16 }
    if a >= b { n += 32 }
    return n
}

fn main() -> i32 {
    let names: [text] = array()
    for i in 0..10 {
        push(names, pick(i))
    }
    let total: i64 = 0
    if least(names) == "" {
        total += 7
    }
    for i in 0..16 {
        total += weigh(pick(i), pick(i / 4))
    }
    return i32(total % 251)
}
PROGRAM
cat >"$work"/programs/texting.kest <<'PROGRAM'
module texting

struct Tag {
    name: text
    rank: i32
}

fn parts(line: text, sep: text) -> i32 no.alloc no.host deterministic {
    let many = 1
    let at = 0
    while true {
        if let found = find(line, sep, at) {
            many += 1
            at = found + len(sep)
        } else {
            break
        }
    }
    return many
}

fn headed(line: text) -> text no.alloc no.host deterministic {
    if matches(line, 0, "## ") {
        return rest(line, 3)
    }
    return slice(line, 0, 2)
}

fn spaces(line: text) -> i32 no.alloc no.host deterministic {
    let n = 0
    for b in line {
        if b == 32 {
            n += 1
        }
    }
    return n + i32(line[0])
}

fn spelled(t: Tag, n: i32, f: f64, g: f32, b: bool, u: u32) -> text {
    return "{t} {n} {f} {g} {b} {u}"
}

fn bytes(what: text, room: i32) -> i32 {
    let out: [u8] = array()
    room(out, room)
    let fitted = 0
    for i in 0..4 {
        if fit(out, what) {
            fitted += 1
        }
        if fit(out, 46) {
            fitted += 1
        }
    }
    push(out, "-tail")
    let whole = text(out)
    clear(out)
    return len(whole) + fitted + len(out)
}

fn drained(all: [i32]) -> i32 no.alloc no.host deterministic {
    let sum = 0
    while true {
        if let one = pop(all) {
            sum += one
        } else {
            break
        }
    }
    return sum
}

fn main() -> i32 {
    let line = "## a line of words, with a comma"
    let total = 0
    total += parts(line, ", ")
    total += len(headed(line))
    total += len(headed("a line with no heading on it"))
    total += spaces(line)
    total += len(spelled(Tag("thing", 4), -7, 0.5, 1.25, true, 9))
    total += bytes("ab", 4)
    total += bytes("ab", 64)
    total += drained([1, 2, 3, 4])
    return total % 251
}
PROGRAM
cat >"$work"/programs/across.kest <<'PROGRAM'
module across

// A run written into the program rather than made while it runs, read at an
// index.
const TIERS: [i32; 4] = [0, 90, 250, 1200]

// And one holding a number with no spelling C reads back, which is the one
// thing this backend will not write: the bits an infinity is made of cannot
// go in an initialiser. A body holding this is a body the machine runs, and
// what is written for it hands the call to the machine.
const FAR: [f64; 2] = [1.0 / 0.0, 0.0 - 1.0 / 0.0]

fn through(f: fn(i32, i32) -> i32 no.alloc no.host deterministic, a: i32,
           b: i32) -> i32 no.alloc no.host deterministic {
    return f(a, b) + 1
}

fn apart(a: i32, b: i32) -> i32 no.alloc no.host deterministic {
    return a * 2 + b
}

fn stretched(which: i32, by: i32) -> i32 no.alloc no.host deterministic {
    if FAR[which % 2] > 0.0 {
        return by * 3
    }
    return by
}

fn tier(i: i32) -> i32 no.alloc no.host deterministic {
    return TIERS[i]
}

fn down(n: i32, by: i32) -> i32 no.alloc no.host deterministic {
    if n <= 0 {
        return 0
    }
    return 1 + through(down, n - by, by)
}

fn main() -> i32 {
    let total = 0
    for i in 0..20 {
        total += through(apart, i, i % 3)
    }
    total += down(50, 1)
    for i in 0..4 {
        total += tier(i)
    }
    total += stretched(0, 5)
    total += stretched(1, 7)
    return total % 251
}
PROGRAM
cat >"$work"/programs/crossed.kest <<'PROGRAM'
module crossed

// A run holding a number with no spelling C reads back, which is the one
// thing this backend will not write. `through` holds it, so `through` is a
// body the machine runs -- and it is in the middle of a run of calls that
// nests until the machine refuses it, so one half of the chain is C and the
// other half is the machine, and both halves count.
const FAR: [f64; 2] = [1.0 / 0.0, 0.0 - 1.0 / 0.0]

fn through(n: i32) -> i32 no.alloc no.host deterministic {
    if FAR[n % 2] != 0.0 {
        return down(n) + 1
    }
    return 0
}

fn down(n: i32) -> i32 no.alloc no.host deterministic {
    if n <= 0 {
        return 0
    }
    return 1 + through(n - 1)
}

fn main() -> i32 {
    return down(100000) % 251
}
PROGRAM
cat >"$work"/programs/crossing.kest <<'PROGRAM'
module crossing

import std.io

struct Row {
    name: text
    worth: i32
}

fn said(r: Row) -> i32 {
    io.print("{r.name} is worth {r.worth}")
    return r.worth
}

fn main() -> i32 {
    let total = 0
    for i in 0..4 {
        total += said(Row("row {i}", i * 3))
    }
    io.print("total {total}")
    return total % 251
}
PROGRAM
cat >"$work"/programs/fixedrun.kest <<'PROGRAM'
module fixedrun

struct Cell {
    worth: i32
    weight: f32
}

struct Board {
    cells: [Cell; 4]
    turn: i32
}

fn made() -> Board no.alloc no.host deterministic {
    return Board([Cell(3, 0.5), Cell(1, 1.5), Cell(4, 2.5), Cell(1, 3.5)], 0)
}

fn walked(b: Board) -> i32 no.alloc no.host deterministic {
    let sum = 0
    for one in b.cells {
        sum += one.worth + i32(one.weight)
    }
    return sum
}

fn at(b: Board, i: i32) -> i32 no.alloc no.host deterministic {
    return b.cells[i].worth + i32(b.cells[i].weight * 2.0)
}

fn turned(b: Board, i: i32, to: i32) -> Board no.alloc no.host deterministic {
    let one = b
    one.cells[i] = Cell(to, f32(to) + 0.5)
    one.turn += 1
    return one
}

fn main() -> i32 {
    let b = made()
    let total = walked(b)
    for i in 0..4 {
        total += at(b, i) * (i + 1)
    }
    let after = turned(turned(b, 0, 7), 3, 2)
    total += walked(after) + after.turn
    return total % 251
}
PROGRAM
cat >"$work"/programs/runoff.kest <<'PROGRAM'
module runoff

struct Board {
    cells: [i32; 4]
    turn: i32
}

fn at(b: Board, i: i32) -> i32 no.alloc no.host deterministic {
    return b.cells[i]
}

fn main() -> i32 {
    let b = Board([3, 1, 4, 1], 0)
    let sum = 0
    for i in 0..6 {
        sum += at(b, i)
    }
    return sum
}
PROGRAM
cat >"$work"/programs/held.kest <<'PROGRAM'
module held

struct Cell {
    at: i32
    weight: f32
}

struct Row {
    cells: [Cell; 3]
    tag: i32
}

const TIERS: [i32; 4] = [0, 90, 250, 1200]
const NAMES: [text; 3] = ["bronze", "silver", "gold"]

fn tier(i: i32) -> i32 no.alloc no.host deterministic {
    return TIERS[i]
}

fn named(i: i32) -> text no.alloc no.host deterministic {
    return NAMES[i]
}

// A number with no spelling C reads back, which is what the bits are for.
fn far(by: f64) -> f64 no.alloc no.host deterministic {
    let big: f64 = 1.0 / 0.0
    if by > 0.0 {
        return big
    }
    return 0.0 - big
}

fn rowOf(tag: i32) -> Row no.alloc no.host deterministic {
    return Row([Cell(1, 0.5), Cell(2, 0.75), Cell(3, 1.0)], tag)
}

// One of a fixed run inside memory the program holds an address into, at an
// index worked out while it runs.
fn cellAt(rows: [Row], which: i32,
          cell: i32) -> i32 no.alloc no.host deterministic {
    return rows[which].cells[cell].at
}

fn scratched(rounds: i32) -> i32 no.host deterministic {
    let sum = 0
    for i in 0..rounds {
        scratch {
            let made: [i32] = array(4, i)
            sum += len(made) + made[0]
        }
    }
    return sum
}

fn main() -> i32 {
    let total = 0
    for i in 0..4 {
        total += tier(i)
    }
    for i in 0..3 {
        total += len(named(i))
    }
    if far(1.0) > 0.0 {
        total += 1
    }
    if far(-1.0) < 0.0 {
        total += 2
    }
    let rows: [Row] = array()
    push(rows, rowOf(1))
    push(rows, rowOf(2))
    for w in 0..2 {
        for c in 0..3 {
            total += cellAt(rows, w, c)
        }
    }
    total += scratched(3)
    return total % 251
}
PROGRAM
# Two walks whose element is not inside the array by the time it is read,
# because the body made it shorter: once through a body it called and once
# by taking from it where it stands. A walk the compiler proves inside its
# array is read with nothing asked (D1187), so these are what says the proof
# looks at the body: both engines have to refuse at the same place.
cat >"$work"/programs/shrunk.kest <<'PROGRAM'
module shrunk

fn drop(xs: [i32]) {
    remove(xs, len(xs) - 1)
}

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..6 {
        push(xs, i)
    }
    let total = 0
    for x in xs {
        total += x
        drop(xs)
    }
    return total % 251
}
PROGRAM
cat >"$work"/programs/taken.kest <<'PROGRAM'
module taken

fn main() -> i32 {
    let ys: [i32] = array()
    for i in 0..6 {
        push(ys, i)
    }
    let total = 0
    for at in 0..len(ys) {
        total += ys[at]
        remove(ys, 0)
    }
    return total % 251
}
PROGRAM
# And a walk that calls a body which only calls another: a call is proved
# harmless when what it calls keeps runs, and this one calls a body that
# takes from the array, so the walk proves nothing. See D1188.
cat >"$work"/programs/handed.kest <<'PROGRAM'
module handed

fn shorten(xs: [i32]) {
    remove(xs, 0)
}

fn through(xs: [i32]) {
    shorten(xs)
}

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..6 {
        push(xs, i)
    }
    let total = 0
    for x in xs {
        total += x
        through(xs)
    }
    return total % 251
}
PROGRAM
# And a walk that counts past the end of an array it indexes: the question
# asked where a walk begins says the array is shorter than the limit, and
# every element goes back to being asked about, so both engines refuse at
# the sixth. See D1189.
cat >"$work"/programs/shorter.kest <<'PROGRAM'
module shorter

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..5 {
        push(xs, i)
    }
    let total = 0
    for at in 0..10 {
        total += xs[at]
    }
    return total % 251
}
PROGRAM
# And a walk that puts a shorter array in the slot it is asking about: the
# question was asked of the array that was there, so an array stored there in
# the walk makes the walk prove nothing. See D1189.
cat >"$work"/programs/swapped.kest <<'PROGRAM'
module swapped

fn main() -> i32 {
    let xs: [i32] = array()
    let short: [i32] = array()
    for i in 0..6 {
        push(xs, i)
    }
    push(short, 7)
    let total = 0
    for at in 0..6 {
        total += xs[at]
        xs = short
    }
    return total % 251
}
PROGRAM
# A body every way out of which is a `return` inside a `match`: the jump each
# arm would have made past the others follows its `return` and is never
# taken, and what it would have landed on is reached by nothing. The backend
# refused a label only those land on, and handed `colony.work` and every body
# of this to the machine. See D1206.
cat >"$work"/programs/returns.kest <<'PROGRAM'
module returns

enum Job {
    Idle
    Chop(i32)
    Sow(i32)
}

fn work(job: Job, worked: i32) -> i32 {
    match job {
        Idle {
            return 0
        }
        Chop(c) {
            if worked < 3 {
                return 1
            }
            return c
        }
        Sow(c) {
            return c + worked
        }
    }
}

fn main() -> i32 {
    match Job.Chop(7) {
        Idle {
            return 1
        }
        Chop(c) {
            if work(Job.Chop(c), 5) != 7 || work(Job.Sow(2), 3) != 5 ||
                    work(Job.Idle, 9) != 0 {
                return 2
            }
            return 0
        }
        Sow(c) {
            return 3 + c
        }
    }
}
PROGRAM
# A walk to an array's length is proved inside the array only where nothing in
# it makes the array shorter or starts below nought, and each way of doing
# either is a program here: a `pop` and a `clear` in the walk, a call through
# a function value that pops, and a walk from minus one. Each stops at the
# first element past what is there, run by the machine; with the proof made
# where it may not be, the compiled walk reads what the bytes still hold.
# Taken out one at a time, each condition was not refused by anything else.
# See D1214.
cat >"$work"/programs/popped.kest <<'PROGRAM'
module popped

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..6 {
        push(xs, i)
    }
    let total = 0
    for at in 0..len(xs) {
        total += xs[at]
        pop(xs)
    }
    return total % 251
}
PROGRAM
cat >"$work"/programs/cleared.kest <<'PROGRAM'
module cleared

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..6 {
        push(xs, i)
    }
    let total = 0
    for at in 0..len(xs) {
        total += xs[at]
        clear(xs)
    }
    return total % 251
}
PROGRAM
cat >"$work"/programs/throughvalue.kest <<'PROGRAM'
module throughvalue

fn dropLast(xs: [i32]) {
    pop(xs)
}

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..6 {
        push(xs, i)
    }
    let shorten: fn([i32]) = dropLast
    let total = 0
    for at in 0..len(xs) {
        total += xs[at]
        shorten(xs)
    }
    return total % 251
}
PROGRAM
cat >"$work"/programs/below.kest <<'PROGRAM'
module below

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..6 {
        push(xs, i)
    }
    let from = 0 - 1
    let total = 0
    for at in from..len(xs) {
        total += xs[at]
    }
    return total % 251
}
PROGRAM
# And the question D1189 asks where a walk begins, on its edge: a limit one
# past the array's length. `shorter.kest` walks to ten over five, which a flag
# off by one answers the same as the right one. See D1215.
cat >"$work"/programs/onepast.kest <<'PROGRAM'
module onepast

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..5 {
        push(xs, i + 1)
    }
    let total = 0
    for at in 0..6 {
        total += xs[at]
    }
    return total % 251
}
PROGRAM
cat >"$work"/programs/outside.kest <<'PROGRAM'
module outside

fn look(world: [i32], at: i32) -> i32 no.alloc no.host deterministic {
    return world[at]
}

fn main() -> i32 {
    let world: [i32] = array()
    for i in 0..4 {
        push(world, i)
    }
    // One past the end rather than far past it, because one past is the
    // index a bounds test written with the wrong comparison lets through and
    // nine is the index any test at all refuses.
    return look(world, 4)
}
PROGRAM
cat >"$work"/programs/shifted.kest <<'PROGRAM'
module shifted

fn main() -> i32 {
    let by: i32 = 0
    for i in 0..3 {
        by -= i
    }
    return 1 << by
}
PROGRAM
cat >"$work"/programs/stopped.kest <<'PROGRAM'
module stopped

fn main() -> i32 {
    let n: i32 = 0
    for i in 0..3 {
        n += i * 0
    }
    return 10 / n
}
PROGRAM

both=0
for file in "$work"/programs/*.kest; do
    name=$(basename "$file")
    ./kest emit --c "$file" >"$work"/one.c 2>"$work"/why
    # Every one of these is written whole on purpose: a program half of which
    # the machine runs would be a differential test of the half that is left.
    if ! grep -q '^int main' "$work"/one.c; then
        {
            echo "    $name has no \`main\` written for it:"
            grep '^// not written' "$work"/one.c | head -3 | sed 's/^/        /'
        } >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    # And the whole of it: a `main` of its own over bodies the machine runs
    # is the machine run twice. Two are written to be halves -- a number with
    # no spelling in C, handed to the machine on purpose, which is what they
    # hold the crossing to. See D1206.
    whole=$(sed -n 's,^// \([0-9]*\) of \([0-9]*\) bodies written$,\1 \2,p' \
            "$work"/one.c)
    case "$name" in
    across.kest | crossed.kest) whole="halves" ;;
    esac
    if [ "$whole" != "halves" ] && [ "${whole% *}" != "${whole#* }" ]; then
        {
            echo "    $name was not written whole: $whole"
            grep '^// not written' "$work"/one.c | head -3 | sed 's/^/        /'
        } >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    if ! $cc -O2 -Iinclude -o "$work"/one "$work"/one.c libkest.a -lm \
            2>"$work"/why; then
        {
            echo "    the C written for $name will not compile:"
            sed 's/^/        /' "$work"/why | head -5
        } >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    # Both streams, because what a program writes and what a refusal says are
    # both what it said: the program that stops while it runs is here to hold
    # a compiled body's refusal to being the machine's refusal, word for word
    # and with the same line under it.
    machine_said=$(./kest run "$file" 2>&1 </dev/null)
    machine_was=$?
    c_said=$(KEST_LIB=lib/ "$work"/one "$file" 2>&1 </dev/null)
    c_was=$?
    if [ "$machine_was" -ne "$c_was" ]; then
        echo "    $name answers $machine_was run by the machine and $c_was \
compiled as C" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    if [ "$machine_said" != "$c_said" ]; then
        echo "    $name writes something else compiled as C" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    # And that any of it ran. Two engines that answer alike answer alike when
    # one of them never started, so what this reads is the run's own count of
    # which bodies it entered: a backend whose C is never called is a backend
    # nothing here would notice. Every program above is written whole, so at
    # least one of its bodies has to have been the C's.
    ran=$(KEST_LIB=lib/ KEST_NATIVES=1 "$work"/one "$file" 2>&1 >/dev/null |
          sed -n 's/^natives: [0-9]* written, \([0-9]*\) entered.*$/\1/p')
    if [ -z "$ran" ] || [ "$ran" -eq 0 ]; then
        echo "    nothing written for $name was entered, so what ran was the \
machine" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    both=$((both + 1))
done

# And a multiply and an add built where the host's compiler may fuse them.
# The machine rounds twice; a compiler that fuses rounds once, and where the
# target has FMA -- every arm64, and x86 told `-mfma` -- GCC fuses by default,
# so a `deterministic` body answered other bits compiled than run. Clang fuses
# across statements only when told to, and where it is told to heed the file's
# pragmas that is the build that holds its half of the guard. A probe asks
# whether fusing across statements is live here at all, the way the generated
# C writes a multiply and an add, because a build that cannot fuse answers
# alike for the wrong reason, and says so rather than passing. See D1226.
fused_said="none built where the host's compiler fuses, which it does not here"
cat >"$work"/probe.c <<'PROBE'
#include <stdio.h>
int main(void) {
    volatile double x = 0.1, y = 10.0, z = -1.0;
    double product = x * y;
    double sum = product + z;
    return sum != 0.0 ? 0 : 1;
}
PROBE
fusing=""
for flags in "-mfma" "-mfma -ffp-contract=fast-honor-pragmas" "" \
        "-ffp-contract=fast-honor-pragmas"; do
    # shellcheck disable=SC2086
    if $cc -O2 $flags -o "$work"/probe "$work"/probe.c 2>/dev/null &&
            "$work"/probe 2>/dev/null; then
        fusing="-O2 $flags"
        break
    fi
done
if [ -n "$fusing" ]; then
    cat >"$work"/fused.kest <<'PROGRAM'
module fused

import std.io

fn muladd(x: f64, y: f64, z: f64) -> f64 no.alloc deterministic {
    return x * y + z
}

fn main() -> i32 {
    let xs: [f64] = array()
    push(xs, 0.1)
    push(xs, 10.0)
    push(xs, 0.0 - 1.0)
    io.print("{bits(muladd(xs[0], xs[1], xs[2]))}")
    return 0
}
PROGRAM
    ./kest emit --c "$work"/fused.kest >"$work"/fused.c 2>"$work"/why
    # shellcheck disable=SC2086
    if ! $cc $fusing -Iinclude -o "$work"/fused "$work"/fused.c libkest.a \
            -lm 2>"$work"/why; then
        {
            echo "    fused.kest will not compile with $fusing:"
            sed 's/^/        /' "$work"/why | head -5
        } >>"$said"
        wrong=$((wrong + 1))
    else
        machine_said=$(./kest run "$work"/fused.kest 2>&1 </dev/null)
        c_said=$(KEST_LIB=lib/ "$work"/fused 2>&1 </dev/null)
        if [ "$machine_said" != "$c_said" ]; then
            echo "    fused.kest answers $machine_said run by the machine and \
$c_said compiled as C with $fusing, where the compiler fuses a multiply and \
an add" >>"$said"
            wrong=$((wrong + 1))
        fi
        fused_said="one built with ${fusing% } where the host's compiler \
fuses"
    fi
fi

# And one program run both ways inside one process, by a host of its own. It
# is the only thing here that holds the seam in the direction a game meets it:
# a host that calls back into the program while a body the host's compiler
# compiled is the frame underneath. The two hosts in this tree that call back
# in drive the machine, and the little host at the bottom of a generated file
# only writes -- so this one is written here, links the generated file with
# `-DKEST_NO_MAIN`, and calls `kest_natives_here` for one of its two runs and
# not for the other. Same answer, same words, one binary. See D1111.
inside_said="no host of its own was built"
cat >"$work"/reentry.kest <<'PROGRAM'
module reentry

import std.io

extern fn Host.askedBack(n: i32) -> i32

fn doubled(n: i32) -> i32 {
    return n * 2 + 1
}

fn worked(n: i32) -> i32 {
    return Host.askedBack(n) + doubled(n)
}

fn main() -> i32 {
    let total = 0
    for i in 0..5 {
        total += worked(i)
    }
    io.print("reentry {total}")
    return total % 251
}
PROGRAM
cat >"$work"/twice.c <<'HOST'
/* One program run twice in one process: once with the bodies this backend
   wrote bound to their chunks and once with none of them bound. The door it
   binds calls back into the program, which is the path nothing else here
   runs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kest.h"

bool kest_natives_here(KestRuntime *rt);

static void wrote_it(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(frame, &length);
    if (bytes != NULL && length > 0) {
        fwrite(bytes, 1, length, stdout);
    }
}

static void asked_back(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    int32_t which = kest_entry(runtime, "reentry.doubled");
    if (which < 0) {
        kest_native_failed(runtime, "there is no `doubled`");
        return;
    }
    KestValue handing[8];
    memset(handing, 0, sizeof handing);
    handing[0] = frame[0];
    if (!kest_call(runtime, which, handing,
                   sizeof handing / sizeof handing[0])) {
        kest_native_failed(runtime, "the call back in did not run");
        return;
    }
    frame[0] = handing[0];
}

// Once with the C, once without, and once with the C linked in and the machine
// started for code nobody trusts, which runs what the verifier proved and not
// what the host's compiler made of it: what says so is how many instructions
// the machine itself ran, which the C does not add to. See D1246.
static int ran(const char *path, bool compiled, bool untrusted,
               unsigned long long *stepped) {
    KestBuild *build = kest_build(path, getenv("KEST_LIB"), stderr,
                                  KEST_FORM_TEXT, 0);
    if (build == NULL) {
        fprintf(stderr, "no program\n");
        return -1;
    }
    KestHost *host = kest_host_new();
    if (host == NULL ||
        !kest_host_bind(host, "Io.write", wrote_it, NULL) ||
        !kest_host_bind(host, "Host.askedBack", asked_back, NULL) ||
        !kest_host_open(host, "Io.write") ||
        !kest_host_open(host, "Host.askedBack")) {
        fprintf(stderr, "no host\n");
        return -1;
    }
    KestLimits bounded = {0, 0, (size_t)1 << 22, (uint64_t)1 << 32};
    KestRuntime *rt = untrusted ? kest_start_untrusted(build, host, &bounded)
                                : kest_start(build, host, NULL);
    kest_host_free(host);
    if (rt == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        kest_build_free(build);
        return -1;
    }
    if (compiled && !kest_natives_here(rt)) {
        fprintf(stderr, "this C was written from another program\n");
        kest_runtime_free(rt);
        kest_build_free(build);
        return -1;
    }
    int32_t which = kest_entry(rt, "reentry.main");
    KestValue answer[8];
    memset(answer, 0, sizeof answer);
    kest_count(rt, true);
    bool went = which >= 0 &&
                kest_call(rt, which, answer,
                          sizeof answer / sizeof answer[0]);
    if (!went) {
        kest_report(rt, stderr, KEST_FORM_TEXT);
    }
    KestCounted counted;
    memset(&counted, 0, sizeof counted);
    kest_counted(rt, &counted);
    *stepped = (unsigned long long)counted.steps;
    int said = went ? (int)answer[0].integer : -1;
    kest_runtime_free(rt);
    kest_build_free(build);
    return said;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: twice <program>\n");
        return 2;
    }
    unsigned long long compiled_steps = 0;
    unsigned long long machine_steps = 0;
    unsigned long long untrusted_steps = 0;
    int with = ran(argv[1], true, false, &compiled_steps);
    int without = ran(argv[1], false, false, &machine_steps);
    int distrusted = ran(argv[1], true, true, &untrusted_steps);
    printf("compiled %d machine %d untrusted %d\n", with, without, distrusted);
    printf("the machine ran %llu instruction(s) beside the C, %llu without it "
           "and %llu for code nobody trusts with the C linked in\n",
           compiled_steps, machine_steps, untrusted_steps);
    return with == without && with == distrusted && with >= 0 &&
                   untrusted_steps == machine_steps &&
                   compiled_steps < untrusted_steps
               ? 0
               : 1;
}
HOST
if ! ./kest emit --c "$work"/reentry.kest >"$work"/reentry.c 2>"$work"/why ||
        ! $cc -O1 -Iinclude -DKEST_NO_MAIN -c -o "$work"/reentry.o \
            "$work"/reentry.c 2>>"$work"/why ||
        ! $cc -O1 -Iinclude -o "$work"/twice "$work"/twice.c "$work"/reentry.o \
            libkest.a -lm 2>>"$work"/why; then
    {
        echo "    a host of its own will not build against the C this wrote:"
        sed 's/^/        /' "$work"/why | head -5
    } >>"$said"
    wrong=$((wrong + 1))
else
    inside=$(KEST_LIB=lib/ "$work"/twice "$work"/reentry.kest 2>&1 </dev/null)
    inside_was=$?
    inside_said="one run both ways inside one process by a host of its own, \
which calls back into the program from a body this backend wrote, and a third \
for code nobody trusts, which runs none of it"
    case "$inside" in
    *"reentry 50"*"reentry 50"*"reentry 50"*"compiled 50 machine 50 untrusted 50"*)
        if [ "$inside_was" -ne 0 ]; then
            echo "    a host running one program both ways said the right \
thing and came back $inside_was" >>"$said"
            wrong=$((wrong + 1))
        fi
        ;;
    *)
        echo "    a host running one program both ways, with a door that \
calls back in, said \`$inside\` and came back $inside_was" >>"$said"
        wrong=$((wrong + 1))
        ;;
    esac
fi

# And a walk that hands its array to the host, whose door calls back into the
# program and shortens it. The walk proofs refuse a walk that calls the host,
# because a host handed an array can do exactly this, and only a host can
# show that the refusal is needed: taken out, the release engine read past
# the end and answered 21 where the machine stops at index three. Both runs
# have to stop, alike. See D1221.
cat >"$work"/hostwalk.kest <<'PROGRAM'
module hostwalk

extern fn Host.shorten(xs: [i32])

fn dropLast(xs: [i32]) {
    pop(xs)
}

fn main() -> i32 {
    let xs: [i32] = array()
    for i in 0..6 {
        push(xs, i + 1)
    }
    let total = 0
    for at in 0..len(xs) {
        total += xs[at]
        Host.shorten(xs)
    }
    return total % 251
}
PROGRAM
cat >"$work"/walked.c <<'HOST'
/* A walk that hands its array to the host, which calls back into the
   program to shorten it: run once with the bodies this backend wrote and
   once without, and the two held to stopping alike. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kest.h"

bool kest_natives_here(KestRuntime *rt);

static void shorten(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    int32_t which = kest_entry(runtime, "hostwalk.dropLast");
    if (which < 0) {
        kest_native_failed(runtime, "there is no `dropLast`");
        return;
    }
    KestValue handing[8];
    memset(handing, 0, sizeof handing);
    handing[0] = frame[0];
    if (!kest_call(runtime, which, handing,
                   sizeof handing / sizeof handing[0])) {
        kest_native_failed(runtime, "the call back in did not run");
    }
}

static int ran(const char *path, bool compiled) {
    KestBuild *build = kest_build(path, getenv("KEST_LIB"), stderr,
                                  KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return -2;
    }
    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Host.shorten", shorten, NULL)) {
        return -2;
    }
    KestRuntime *rt = kest_start(build, host, NULL);
    kest_host_free(host);
    if (rt == NULL || (compiled && !kest_natives_here(rt))) {
        return -2;
    }
    int32_t which = kest_entry(rt, "hostwalk.main");
    KestValue answer[8];
    memset(answer, 0, sizeof answer);
    bool went = which >= 0 &&
                kest_call(rt, which, answer, sizeof answer / sizeof answer[0]);
    if (!went) {
        kest_report(rt, stdout, KEST_FORM_TEXT);
    }
    int said = went ? (int)answer[0].integer : -1;
    kest_runtime_free(rt);
    kest_build_free(build);
    return said;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return 2;
    }
    int with = ran(argv[1], true);
    int without = ran(argv[1], false);
    printf("compiled %d machine %d\n", with, without);
    return 0;
}
HOST
if ! ./kest emit --c "$work"/hostwalk.kest >"$work"/hostwalk.c 2>"$work"/why ||
        ! $cc -O1 -Iinclude -DKEST_NO_MAIN -c -o "$work"/hostwalk.o \
            "$work"/hostwalk.c 2>>"$work"/why ||
        ! $cc -O1 -Iinclude -o "$work"/walked "$work"/walked.c \
            "$work"/hostwalk.o libkest.a -lm 2>>"$work"/why; then
    {
        echo "    a host walking an array it shortens will not build:"
        sed 's/^/        /' "$work"/why | head -5
    } >>"$said"
    wrong=$((wrong + 1))
else
    walked_out=$(KEST_LIB=lib/ "$work"/walked "$work"/hostwalk.kest 2>&1 \
                     </dev/null)
    case "$walked_out" in
    *"outside an array"*"outside an array"*"compiled -1 machine -1"*) ;;
    *)
        echo "    a walk whose array the host shortens does not stop alike \
both ways: $(printf '%s' "$walked_out" | tail -1)" >>"$said"
        wrong=$((wrong + 1))
        ;;
    esac
fi

# And every program in this tree that runs, run both ways. The ones above are
# written for this check and are what it can write; these are what somebody
# wrote for another reason, which is where a fixture's blind spot shows. A
# program that asks the host for something a file this backend wrote does not
# provide -- a clock, a file, whatever an engine offers -- cannot be run this
# way, and is counted rather than passed over quietly.
alike=0
wants_a_host=0
walked=0
not_walked=0
# And each again built under the sanitisers, where the host's compiler has
# them: what this backend writes is C, and C's undefined arithmetic is a
# wrapped number in a build that does not look and a report in one that does.
# The count of an unsigned walk was one more than the top of `int64_t` in the
# generated C and answered right for as long as nothing looked. See D1218.
sanitised=0
sanitisers=""
printf 'int main(void) { return 0; }\n' >"$work"/probe.c
if $cc -fsanitize=undefined,address -fno-sanitize-recover=undefined \
        -o "$work"/probe "$work"/probe.c 2>/dev/null &&
        "$work"/probe 2>/dev/null; then
    sanitisers="-fsanitize=undefined,address -fno-sanitize-recover=undefined"
fi
for file in "$@"; do
    if ! grep -q '^fn main(' "$file"; then
        continue
    fi
    if ! ./kest emit --c "$file" >"$work"/one.c 2>/dev/null; then
        continue
    fi
    if ! grep -q '^int main' "$work"/one.c; then
        continue
    fi
    if ! $cc -O2 -Iinclude -o "$work"/one "$work"/one.c libkest.a -lm \
            2>"$work"/why; then
        {
            echo "    the C written for $file will not link:"
            sed 's/^/        /' "$work"/why | head -5
        } >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    machine_said=$(./kest run "$file" 2>&1 </dev/null)
    machine_was=$?
    c_said=$(KEST_LIB=lib/ "$work"/one "$file" 2>&1 </dev/null)
    c_was=$?
    case "$c_said" in
    *"the host does not provide"*)
        wants_a_host=$((wants_a_host + 1))
        continue
        ;;
    esac
    if [ "$machine_was" -ne "$c_was" ] || [ "$machine_said" != "$c_said" ]; then
        echo "    $file answers $machine_was run by the machine and $c_was \
with what was written as C, or says something else" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    # And the C again walking the heap before every allocation, which is what
    # finds a handle the collector's reach stops short of: a call to a body
    # written after its caller once asked no room for the callee's frame, and
    # what that frame held was given back at the next walk. The two examples
    # that grow worlds are left out and counted, for the reason the gate's own
    # walk leaves them out. The workloads under `bench` are worlds grown to be
    # measured, which a walk at every allocation reads whole each time, and
    # are not walked for the same reason. See D1179.
    case "$file" in
    examples/churn.kest | examples/holding.kest)
        not_walked=$((not_walked + 1))
        ;;
    examples/*)
        walked_said=$(KEST_LIB=lib/ KEST_WALK_EVERY=1 "$work"/one "$file" \
                          2>&1 </dev/null)
        walked_was=$?
        if [ "$walked_was" -ne "$c_was" ] || [ "$walked_said" != "$c_said" ]; then
            echo "    $file answers $c_was written as C and $walked_was \
walking the heap before every allocation, or says something else" >>"$said"
            wrong=$((wrong + 1))
            continue
        fi
        walked=$((walked + 1))
        ;;
    esac
    # shellcheck disable=SC2086
    if [ -n "$sanitisers" ] &&
            $cc -O1 -g $sanitisers -Iinclude -o "$work"/one-sane "$work"/one.c \
                libkest.a -lm 2>/dev/null; then
        sane_said=$(KEST_LIB=lib/ "$work"/one-sane "$file" 2>&1 </dev/null)
        sane_was=$?
        if [ "$sane_was" -ne "$c_was" ] || [ "$sane_said" != "$c_said" ]; then
            {
                echo "    $file answers $c_was written as C and $sane_was \
under the sanitisers, or says something else:"
                printf '%s\n' "$sane_said" |
                    grep 'runtime error\|ERROR: AddressSanitizer' | head -2 |
                    sed 's/^/        /'
            } >>"$said"
            wrong=$((wrong + 1))
            continue
        fi
        sanitised=$((sanitised + 1))
    fi
    alike=$((alike + 1))
done

# Four of them are programs that stop while they are running, and both halves
# have to stop: a backend that wrote a division by nought as one the host's machine
# traps on, or as one it quietly answers, would be a program that means
# something else. Counted rather than assumed, because a program that stops is
# one whose answer is the same either way for the wrong reason.
for stopping in stopped shifted outside deep crossed runoff shrunk taken handed shorter swapped popped cleared throughvalue below onepast; do
    stops=$(./kest run "$work"/programs/$stopping.kest 2>/dev/null </dev/null
            echo $?)
    if [ "$stops" -eq 0 ]; then
        echo "    $stopping.kest is written to stop while it runs and does \
not stop" >>"$said"
        wrong=$((wrong + 1))
    fi
done

# A walk before every allocation that walked nothing is a pass nobody took.
if [ "$alike" -gt 0 ] && [ "$walked" -eq 0 ]; then
    echo "    no program written as C was run walking the heap before every \
allocation" >>"$said"
    wrong=$((wrong + 1))
fi

if [ "$wrong" -ne 0 ]; then
    echo "$wrong thing(s) wrong with the C this backend wrote"
    cat "$said"
    exit 1
fi
echo "$written of $bodies body(s) over $compiled program(s) written as C the \
host compiler takes, $both program(s) written here and $alike of this tree's \
own run both ways for the same answer and the same words, $walked of those \
again walking the heap before every allocation with $not_walked left out for \
growing worlds, $sanitised again under the sanitisers, $fused_said, \
$inside_said, and \
$wants_a_host that ask the host for what a file this wrote does not provide"
