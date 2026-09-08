# The Kest Language

This describes what is decided, not what is implemented. `docs/worklog.md` says
what runs today. Anything here without an entry there is a target.

## Shape

```kest
module world.quests

import math

const GRAVITY: f32 = -9.81

struct Player {
    x: f32
    y: f32
    velocity: f32
    health: i32
}

fn update(p: Player, dt: f32) -> bool {
    p.velocity = p.velocity + GRAVITY * dt
    p.y = p.y + p.velocity * dt

    if p.y < 0.0 {
        p.y = 0.0
        p.velocity = 0.0
    }

    for e in enemies {
        if math.distance(p, e) < 1.0 {
            p.health = p.health - 10
            print("hit, health {p.health}")
        }
    }

    return p.health > 0
}
```

## One form

`kest fmt` prints a file the one way the language writes it. `-w` writes each
file it is given and names the ones it changed; `--check` names them without
writing and exits non-zero, which is the question "is this already right".
It writes through a file beside the target and renames over it, so a program
that stops half way leaves the file rather than half of it: four spaces a
level, one space around a binary operator, none inside a bracket, and a
bracket only where taking it away would change what binds to what. It prints
from the tree rather than from the characters, which is the only way to tell
`ref<Npc>` from `a < b`.

A file that does not parse is not formatted, and the command says so beside
what is wrong with it. It is the one command that shows nothing after a
mistake, and it is the one whose answer is meant to go back over the file: a
form of half a program would delete the other half. `lex` shows the tokens and
`parse` shows the tree it made, because nothing is going to be written from
either.

Comments are kept, at the indent of what they are written above. What is
inside a string, including the expressions in its holes, is left exactly as
written, and so is the spelling of a number.

A list that does not fit in eighty columns goes one item to a line, all of
them or none: half on one line and half on the next is the arrangement nobody
asked for.

A line ends where a statement can end, and a type that takes types ends in `>`:
`giver: ref<Npc>` is a whole field. The price is that a comparison cannot be
split after its operator — `a >` and the rest on the next line is refused where
it is written.

```kest
let w = World(
    [
        Enemy(Vec2(0.0, 0.0), 30),
        Enemy(Vec2(1.0, 1.0), 40),
        Enemy(Vec2(2.0, 2.0), 5)
    ],
    0
)
```

A chain of operators too long for the line breaks after each operator, one
level in, or two when it is a condition, because a condition has a block
starting one level in right after it:

```kest
let total = alpha * 1000 +
    beta * 2000 +
    gamma * 3000

if alpha != 1 ||
        beta != 2 ||
        gamma != 3 {
    return 1
}
```

The operator ends the line rather than starting the next one, because that is
what the language allows: a line that ends in an operator continues and one
that ends in a value does not.

A long string cannot be broken.

## One name, two functions

Two functions may share a name when they take different things. Which is meant
is settled by what is passed:

```kest
import std.math

let health = math.max(hit, 0)
let height = math.min(y, 1.0)
```

There is no ranking and nothing converts, so exactly one can match or none can.
When none does, every function of that name is listed with what it takes.

A host asking for one by name is told the same thing in the same way. A name
that is several functions cannot be handed over as an index, so `kest_entry`
says so and names them — `add#i32,i32`, `add#f32,f32` — and those are the names
the program compiled them under rather than anything a file wrote.

A host does not have to know that spelling. `kest_entry_of` gives the one at a
position, so a host walks them and asks each what it takes:

```c
for (uint32_t at = 0; ; at++) {
    int32_t candidate = kest_entry_of(runtime, "lengthOf", at);
    if (candidate < 0) {
        break;
    }
}
```

Asking for the second one is also how a host finds out whether a name is
several functions without asking for an index that is not there.

## Modules

A file may say what it is called, and what it reads:

```kest
module game.world
import game.render
```

A module's name is where its file is: `module examples.game.npc` lives at
`examples/game/npc.kest`, and the file the command names settles where the
package directories start by having its own name taken off its path.
`import examples.game.npc` therefore reads the same file whoever writes it. Its
names live under the last part of what it calls itself, so a file that imports
it writes `render.draw` and `render.Sprite`, and the file itself may write
`draw` and `Sprite`. Where a name came from is written at every use of it. Two modules whose names
end the same way would put their names under the same one, and that is refused
rather than mixed.

There is no `print`. Saying something is the host's to do, and `std.io` is
where a program asks for it:

```kest
import std.io

io.print("hello")
```

A host provides `Io.write`. `std.math` declares six functions the host must
provide: `Math.sqrt`,
`Math.floor`, `Math.ceil`, `Math.sin`, `Math.cos` and `Math.pow`. A program
that imports it requires all six, whether or not it reaches them, because the
host may call any function in the program and nothing can be left out on the
grounds that this program does not use it.

A module whose name starts with `std.` comes from the standard library
wherever the program is, and no project may use that name. The library is Kest
source. It is looked for at `$KEST_LIB`, then beside the program, then where
the build was told it would be installed, and the first one that is actually
there wins.

```kest
import std.text

if let health = text.number(field) {
    print("{health}")
}
```

What is there: `std.io` says something, `std.math` names the host's arithmetic
and writes what can be built out of it, `std.text` cuts and builds text, `std.sort` is told what comes
first, `std.table` is a hash table whose pairs are walked over its `keys` and
`values`, which are packed and in step, `std.vec` is two and three components of
`f32`, and `std.random` gives numbers that look random out of a state the
program holds.

A source is a value like any other, so it is carried the way a count is:

```kest
let source = random.from(seed)
source = random.next(source)
let face = random.below(source, 6)
```

Nothing about it is global and nothing asks the host, so two runs from one seed
lay a world out the same way — which is what a simulation needs to be worth
running twice. `shuffle` rearranges an array in place and gives back the source
it left off at; `one` picks an element, or nothing when there are none.

Every command takes more than one file, and there are two kinds.

`check`, `run`, `emit` and `tick` read a *program*: the files named and
everything they import. `kest check *.kest` checks a project as a project;
checking only the entry point checks only what it reaches, and a file nothing
imports is never looked at.

`kest call <file> <function> [argument]...` calls one function and prints what
it gives. The arguments are read the way the language reads a literal: any
width of the right family, then the width it would have had on its own, so
`min 3 7` is the `i32` one and `min 3.5 7.5` is the `f32` one. A parameter that
cannot be typed at a shell is refused with the signatures listed.

`fmt`, `parse` and `lex` read each file on its own and follow nothing, because
what a file is does not depend on what it imports. One that cannot be read is
reported and does not stop the rest.

## Rules

A newline ends a statement. There are no semicolons, and a `;` between two statements is a
syntax error; the only place one is written is between a type and how many of
it. A statement continues onto the next line while it is incomplete: inside
brackets, or after a binary operator.

Conditions take no parentheses. `if x < 3 { }` is the only spelling; `if (x <
3) { }` is refused, because `(x < 3)` is a redundant grouping the formatter
would strip and the strict parser does not accept two spellings of one thing.

Blocks are braces, always, including single-statement bodies.

`defer f(x)` runs `f(x)` when the block it is in ends, however it ends: off the
end, through a `return`, through a `break` or a `continue`. Several of them run
in the reverse of the order they were written, because what was taken last is
given back first:

```kest
fn measured(a: f64, b: f64) -> i32 {
    Host.write("[")
    defer Host.write("]")
    if a <= 0.0 {
        return 1
    }
    return 0
}
```

It takes a call and nothing else. What is deferred still runs, so it counts
against a `no.alloc` promise like anything else.

An `if` gives a value when its arms say so, with the `->` that means "gives"
in a signature and in a match arm:

```kest
fn max(a: i32, b: i32) -> i32 no.alloc {
    return if a > b -> a else -> b
}

let name = if let held = door -> describe(held) else -> "nothing"
```

Both arms are the same kind: `->` arms give values, block arms do things, and
mixing them is refused. An `if` that gives one needs an `else`, because a
value has to exist on both ways through. There is no ternary; `?` means
"optional" and means only that.

Keywords are English. Identifiers are UTF-8, so `let hız = 5` and
`fn oyuncuGüncelle()` are legal.

Comments are `//` to end of line. Nothing else.

Operators, tightest first:

```
~  -  !                 on one thing
*  /  %
+  -
<<  >>
&
^
|
<  <=  >  >=
==  !=
&&
||
```

The bitwise operators bind tighter than the comparisons, so
`state & MOVING == MOVING` asks what it looks like it asks. C puts them the
other way and that is the one place its table is known to be wrong.

`&`, `|`, `^` and `~` apply to integers and to nothing else; `bool` has `&&`,
`||` and `!`, which say what they mean about one bit. A shift takes a value
and a count, and the count is an integer of any width, the way an index is.
`>>` brings the sign in on a signed type and nought on an unsigned one. A left
shift wraps at the declared width like every other arithmetic, and a negative
count is a failure with a message.

A statement that is only an expression has to do something. A call does, and
what it gives back may be worth ignoring; an `if` or a `match` whose arms are
blocks does. Anything else works a value out and leaves it lying there, which
is refused: `a == b` written where `a = b` was meant is the usual way to write
one by accident, and a `match` whose arms give values is a value and wants a
`return` or a name in front of it.

A string may hold expressions in braces, and `\{` writes a brace:

```kest
print("{len(world)} left, and the escort reads \"{escortOf(world, guard)}\"")
```

There is no `+` on text. Building a string reaches the heap, so a function
promising `no.alloc` may hold a string and may not build one.

Text is its bytes. `len(t)` counts them and walks the string to do it, `t[i]`
reads one as a `u8`, and two pieces compare by them. `for b in t` walks them,
which is what to write when the positions are not the point: an index measures
the string every time it is used and a walk measures it once. There is no character
type: `"hız"` is four bytes, and a program that wants characters says what it
means by one.

`'a'` is one byte written the way it reads, and its type is `u8`. It is not a
character: `'ı'` is two bytes and is refused, and so is `'ab'`. The escapes are
the ones a string has, so a byte written in a string and a byte written on its
own are one spelling.

```kest
fn isSpace(byte: u8) -> bool no.alloc {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r'
}
```

`hash(x)` gives a `u64` standing for a value. It applies to exactly what `==`
applies to — integers, floats, `bool`, text, a set of bits, and an enum whose
cases carry those — because a type that compares has one and a type that does
not has neither. A struct combines what its fields decide:
`hash(a) * 31 ^ hash(b)`.

`find(t, needle)` gives where it is, or nothing, and reaches no heap — it reads
the string, which is what looking through one costs. `find(t, needle, from)`
starts looking at `from` and answers where it is in the whole of `t`, so a scan
for every place something appears is a loop rather than a slice per step:

```kest
fn count(t: text, needle: text) -> i32 no.alloc {
    let seen = 0
    let at = 0
    while let found = find(t, needle, at) {
        seen += 1
        at = found + len(needle)
    }
    return seen
}
```

Starting outside the string is a message rather than a read past it, and
starting at its length finds nothing, which is what a scan that has reached the
end asks.

`matches(t, at, needle)` says whether `needle` sits at `at` in `t`. It compares
where it is told rather than looking for it, so it costs the place it steps to
and the piece it compares, and copies nothing. `find` is for where something
is; this is for whether something is where you already think it is.

`rest(t, at)` is what is left of `t` from `at`, and copies nothing: a piece of
text ends where it ends, so the rest of one is a place inside it. Reading past
the end is a message rather than a read past it, and the rest from its length
is empty.

```kest
fn fields(line: text, separator: text) -> i32 no.alloc {
    let seen = 0
    let tail = line
    while len(tail) > 0 {
        seen += 1
        if let at = find(tail, separator) {
            tail = rest(tail, at + len(separator))
        } else {
            tail = ""
        }
    }
    return seen
}
```

That loop reads every byte once between all its turns. Walking the same text by
index would read it again for every step, because an index into a piece of text
costs what it steps over: text is its bytes and where they end is the only
thing that says how many there are.

`slice(t, from, count)` makes a new piece of text, which reaches the heap:

```kest
if let at = find(entry, "=") {
    let name = slice(entry, 0, at)
}
```

Text built a piece at a time is built as bytes. `text(bytes)` makes one piece
out of a `[u8]`, and it is the only way to make text from something that is
not a string with a hole in it:

```kest
let out: [u8] = array()
for byte in subject {
    push(out, byte)
}
return text(out)
```

Gathering the bytes reaches the heap, because the array grows; what it does not
do is copy what is already gathered every time something is added, and the
piece of text is paid for once at the end. That is why `std.text` writes `join`,
`repeat`, `upper` and `lower` this way rather than out of `slice`, which copies
the whole of what it is given at every step. A zero byte in the array is refused
at run time, because text ends at its first zero and one in the middle would
quietly cut the rest off.

## Keywords

```
break  const   continue  defer   else    extern  false   fn
for     if      import    in      let     module  return  struct
true    type    while
```

`flags` is not one of these: it declares a type only where a declaration
begins, and is a name everywhere else.

`type` is kept back and has no meaning yet. Using it as a name is refused, so
nothing has to be renamed the day it gets one.

## Types

Signatures declare types. Bodies infer them.

```kest
fn scale(v: Vec3, k: f32) -> Vec3 {
    let x = v.x * k        // inferred f32
    return vec3(x, v.y * k, v.z * k)
}
```

Primitives: `i8 i16 i32 i64`, `u8 u16 u32 u64`, `f32 f64`, `bool`, `text`.

Naming a number type makes one, the same way naming a struct does:

```kest
let average = total / f32(len(items))
let index = i32(position.x)
```

Nothing converts on its own. An integer going into a narrower integer wraps,
which is what C does; a float going into an integer is truncated toward zero
and stops at the end of the range rather than being undefined, which is what
C does not.

`f32` and `f64` are different types and different instructions. `f32`
arithmetic rounds to `f32`, because the engine on the other side of the
boundary does, and an answer that differs from that one is the wrong answer.
Nothing converts between them on its own.

A host boundary is always declared and never inferred:

```kest
extern fn Clock.now() -> u64 no.alloc
```

A `const` is a name for a value worked out where it is written: a number, a
truth or a piece of text, arithmetic on those and on other constants, a struct
built out of them, and that many of something written where it stands.

```kest
const WIDTH: i32 = 16
const CELLS: i32 = WIDTH * 9
const MASK: u8 = 1 << 3
const ORIGIN: Vec2 = Vec2(0.0, 0.0)
const WEIGHTS: [f32; 3] = [1.0, 0.5, 0.25]
```

A constant is a value like any other where it is used: `array(CELLS, 0)` counts
with it while running and `[i32; CELLS]` counts with it while compiling, and it
is the same number in both.

A piece of text with a hole in it is not a constant, because filling a hole is
what the machine does and a constant is worked out before there is a machine.

It costs one instruction to push wherever it is used, because the working out
happens once and at compile time. It wraps at its declared width the way the
same arithmetic wraps while running, so `const NARROW: i8 = 120 + 10` is -126
and says so. Dividing by nought and a constant made out of itself are refused
where the constant is used, each saying which of the two it was.

## Values and references

A `struct` is a value. It lives where its frame does. A temporary is moved
rather than copied. A value passed to a function that neither keeps it nor
writes through it is lent, and costs nothing.

`ref<T>` is a handle into managed or host storage. It can go stale, because
something else may delete the target, so reading through it is a lookup that
can fail rather than a dereference. The failure cannot be ignored.

This split is why `Vec3` returned from a helper costs nothing: see D006.

It is also the choice anything holding state has to make. A function is handed
a value, so writing a field of one it was given changes this frame's copy and
nothing the caller can see — which is a warning where it cannot be anything
else, in a function that gives nothing back. The two answers are to give the
changed one back, the way `std.random` does:

```kest
source = random.next(source)
```

or to hold what changes behind a handle, the way `std.table` does, where the
struct is three arrays and everything written goes through one of them. Which
to pick is whether the thing has an identity — a table is handed around and
stays the same table — or is a number that a program carries.

A `let` of a struct is a copy and writing its fields is how a changed one is
made, so nothing is said about that: `let moved = p` and then `moved.x = 0.0`
is the idiom rather than the mistake.

## References

`store<T>` owns values and hands out `ref<T>`. `add` puts one in, `remove`
takes it out, and `get` reads through a reference and returns `T?`, because
what it named may be gone:

```kest
let world: store<Npc> = store()
let guard = add(world, Npc("guard", none))
set(world, guard, Npc("guard", smith))

if let npc = get(world, guard) {
    print(npc.name)
}
```

Nothing is notified of a removal and nothing counts references, so two values
may point at each other and neither has to be told. `get`, `set` and `remove`
allocate nothing; `add` can grow the store and does.

`array(n, v)` makes an array of `n` of `v`, and `push` puts one more on the
end. What it holds comes from what it is filled with, so nothing is written
down twice:

```kest
let samples = array(4, Sample(0, 0.0))
push(samples, Sample(1, 0.5))
```

An empty one has nothing to read that off, so `array()` takes what it holds
from where it is going, the same way `store()` does:

```kest
let out: [u8] = array()
```

`pop(a)` takes the last one off and gives a `T?`, because an empty array has
none to give. `remove(a, i)` takes out the one at a position and gives it,
keeping the order of what is after it — and does not ask, because naming a
position is a claim that there is one there, the same claim `a[i]` makes. `clear(a)` empties one. None of them
reaches the heap, so a `no.alloc` function may shrink an array:

```kest
let task = remove(queue, i)
if let last = pop(queue) { }
clear(queue)
```

`remove` shifts, so it costs what the shift costs, and that is on the call
rather than hidden. When the order does not matter, an array is the wrong
container: a store hands out references that survive a removal, and removing
from one costs nothing.

Both reach the heap. An array the host lent cannot grow, because growing moves
the elements and the block is not Kest's to move; that is a failure with a
message rather than a write past the end of what was lent. It cannot shrink
either, because the length is the host's and so is the extent it lent.

`for i in from..to` counts instead of walking, from the first number up to
but not including the second:

```kest
for i in 0..len(a) {
    total += a[i]
}
```

Both ends are one type, a literal at one end takes the type of the other, and
the end is worked out once rather than every turn. A count is a way to write a
walk and not a value, so `0..n` is written where a walk is asked for and
nowhere else.

`[T; 16]` is that many where it stands, rather than a handle to that many
elsewhere. It is a value like a struct: copying one copies all of it, and a
struct holding one holds the whole thing.

```kest
struct Transform {
    m: [f32; 4]
    tag: i32
}
```

Those are the twenty bytes a C compiler gives `struct { float m[4]; int32_t
tag; }`, so a host lends an array of them and the program walks it in place.
It is indexed, counted and walked the same way an array is, and the count is
known, so `len` costs nothing. It cannot grow: `push` is for the other one.

The count is a number, or the name of a constant that is one:

```kest
const CORNERS: i32 = 4

struct Quad {
    at: [f32; CORNERS]
}
```

A constant is worked out where it is written, so this is as pinned down as the
number is, and what a host has to match is printed either way: `kest check`
says `[f32; 4]` and forty-eight bytes whichever spelling made it.

An index written down is worked out where it is written: `m[2]` is the same
instruction `a.z` is, and `m[5]` on four of them is refused rather than
checked while running. An index worked out while running is checked while
running.

A walk of one is over a copy of it, because that many is a value. Writing the
run inside the walk therefore does not change what the walk reads, which is
the same rule an array's walk keeps and the reason the copy is made.

`for` walks an array, that many of something, text, a store or a set of bits,
and nothing else.

`a[i].health = 0` writes that field and nothing else, and `a[i].health` reads
that field and nothing else: neither takes the whole element apart. That is
what a frame does most, and it is why an array holds the host's bytes rather
than handles.

`for one in a` costs the same as that for the same work. A body that only ever
reads fields of the walked name, and writes nothing that could be the array,
never copies the element — so the two spellings are two spellings and not two
prices. Any other body gets the element.

What the walked name means does not depend on that. It is the element as it
was when the turn began, so writing the element in the same turn does not
change what the name reads. Anything the compiler cannot be sure of, it copies.

`for i, x in a` asks for the position as well. The name is a copy of the
walk's own count, so assigning to it changes nothing and the compiler says so.

A walk is over what the array held when it began. Its length is taken once, so
pushing inside the body does not lengthen the walk it is inside — what was
pushed is there afterwards and is walked by the next walk. Removing inside the
body is the other way round and is not silent: the walk reaches for what is no
longer there and says so, at the `for`, in the same words any read past the end
gets. A loop whose length its own body decides is a loop with no bound, and
this language is for programs with a frame to fit in.

`for` walks text and gives its bytes, one `u8` at a time, and `for i, b in t`
gives the position with them. There is no character type and this does not
invent one: `"hız"` is four bytes and a walk of it takes four turns.

```kest
fn spaces(t: text) -> i32 no.alloc {
    let seen = 0
    for b in t {
        if b == ' ' {
            seen += 1
        }
    }
    return seen
}
```

`for` walks a store and gives a reference, because a reference is what
removing and writing take. Removing while walking is allowed: the slot goes
dead behind the cursor and the walk does not go back to it. It is the one walk
that does not count to a limit — it looks for the next live slot instead, which
is why removing inside it is safe and why pushing inside an array's walk is
not.

```kest
for r in world {
    if let npc = get(world, r) {
        if npc.health <= 0 {
            remove(world, r)
        }
    }
}
```

## A thing that is one of several

```kest
enum Door {
    Shut
    Locked(i32)
    Open(f32)
}
```

`Door.Locked(7)` builds one. What a case carries is written by position, and
named where it is used:

```kest
match door {
    Shut {
        return "shut"
    }
    Locked(key) {
        return "locked with {key}"
    }
    Open(width) {
        return "open {width} wide"
    }
}
```

A `match` that leaves a case out is refused, and the message points at the case
it did not answer. `else` answers whatever is left, and is a written decision
rather than a silent one.

An arm written `Case -> expression` gives a value, and a match whose arms all
give one is a value:

```kest
return match door {
    Shut -> "shut"
    Locked(key) -> "locked with {key}"
    Open(width) -> "open {width} wide"
}
```

Every arm is the same kind. Mixing `->` arms with block arms is refused, so
whether a match is a value is written in the arms rather than worked out from
where it appears.

Two values of an enum are equal when they are the same case carrying the same
things, so `door == Door.Locked(7)` asks what it looks like it asks. An enum
whose cases carry something that does not compare does not compare either, and
the refusal names what it was. `hash` covers the same ground, over the same
parts, so the two cannot disagree.

Two enums are answered together in one `match` rather than one inside
another. An arm answers a case for each subject, and `else` in a position
answers any case there:

```kest
return match door, move {
    Shut, Push -> Door.Open(1.0)
    Locked(key), Unlock(with) -> if with == key -> Door.Shut else -> door
    Open(width), Pull -> Door.Shut
    Open(width), else -> Door.Open(width)
    else -> door
}
```

Every combination has to be answered, so the nine here are covered by five
arms and the checker names any that is not. Arms are tried in order, so a
later one catching what an earlier one left is the point; what is refused is
an arm nothing can reach. An `else` on its own stands for every position, and
is the one way to leave a combination out.

An optional in a hole is `none`, or what it holds written the way it is
written on its own — both of which are what a program writes, which is the
whole of the rule.

A case in a hole is written the way it is built:

```kest
io.print("{Door.Locked(7)}")        // Door.Locked(7)
io.print("{Door.Named("gate")}")    // Door.Named("gate")
```

Text inside one is written as text, quotes and all, because what is being
written is the source and not the content. An enum has text exactly when
everything its cases carry has text, and when one does not, the refusal names
what it was.

The tag is a four byte integer at offset zero and the payload starts after it
at its own alignment, which is what a C tagged union is. A host may therefore
lend an array of them and the program walks it in place, reading and writing
the host's memory rather than a copy of it. `examples/embed.c` does exactly
that beside `examples/embed.kest`, which declares the same shape as an enum.

## A set of named bits

`flags` names a set and names each bit in it. Which bit a name stands for is
where it was written, so there are no powers of two to get wrong:

```kest
flags State: u8 {
    Moving
    Airborne
    Hurt
    Armed
}

let state = State.Moving | State.Armed
if state & State.Hurt == State() { }
```

The width is written rather than counted off the names, because it is what a
host sees: a ninth flag over a `u8` is refused, with the fix being to widen
the type rather than to change the layout underneath a host that was reading
it. It must be unsigned.

`&`, `|`, `^` and `~` combine two of one set and give that set. `==` and `!=`
compare. Nothing else applies: a set is not a number, so arithmetic on one is
refused, and two different sets cannot be mixed. `State()` is the empty one,
the way `array()` and `store()` are. `u8(state)` gives the bits and
`State(bits)` takes them back, both only at the declared width.

A `match` does not apply, and says so. Every combination of the bits is a
value, so nothing exhausts a set the way the cases of an enum exhaust it.

`for` walks the flags that are there, in the order they were declared, one
value of the set at a time:

```kest
let rebuilt = State()
for flag in state {
    rebuilt = rebuilt | flag
}
```

There is no position to walk by, the way there is none for a store: the flag
is what names the bit.

A set in a hole is written the way it is built:

```kest
io.print("{State.Moving | State.Armed}")   // State.Moving | State.Armed
io.print("{State()}")                      // State()
```

The name is the one a program writes where the set was declared, so a set
printed from another module reads without that module's name in front of it.

`flags` is a word rather than a keyword: it declares a type only where a
declaration begins, so a field called `flags` and a module called `flags`
both keep working.

## When there might be nothing

`T?` holds a `T` or nothing. A value standing where one is wanted becomes one,
which is the only conversion the language does:

```kest
fn find(items: [Item], id: i32) -> Item? {
    for item in items {
        if item.id == id {
            return item
        }
    }
    return none
}
```

`if let` is the only way to open it, and the name exists only inside the arm
where the value did:

```kest
if let item = find(stock, 7) {
    print("in stock")
} else {
    print("not carried")
}
```

`while let` is the same question asked every turn: the loop runs while there
is something and the name holds it.

```kest
while let task = newest(queue) {
    spent += task.cost
}
```

There is no operator that opens one without asking, because the whole point of
the type is that the question was asked.

## A function as a value

A function can be handed to another function. Its type is written the way its
declaration is, and what it promises is part of it:

```kest
fn ascending(a: text, b: text) -> bool no.alloc {
    return a < b
}

fn sort(items: [text], before: fn(text, text) -> bool no.alloc) no.alloc {
    for i in 1..len(items) {
        let j = i
        while j > 0 && before(items[j], items[j - 1]) {
            let held = items[j]
            items[j] = items[j - 1]
            items[j - 1] = held
            j -= 1
        }
    }
}

sort(words, ascending)
```

A value that promises `no.alloc` fits where one that does not is wanted, and
not the other way round. That is what keeps a cost contract provable through a
call whose body is not known: the promise is read off the type rather than off
the body.

A name that is several functions takes the shape of the place it is going, the
way a literal does. An extern is called and not named: which function the host
bound is settled when the program starts, so there is no value to hand around.
Two function values do not compare, and one has no text.

## One body, many types

A function may take types as well as values. A copy is compiled for each set
it is called with:

```kest
fn firstOf<T>(items: [T], fallback: T) -> T no.alloc {
    for one in items {
        return one
    }
    return fallback
}
```

What each name stands for is worked out from what was passed, so a call is
written the way any other call is. A function passed as an argument is settled
after the others, so `sort(words, ascending)` picks the `ascending` that
matches what `words` made `T`.

Nothing is boxed and nothing carries a tag: a copy over `[Vec]` was compiled
knowing a `Vec` is two `f32`. The cost is the copies, and a program that calls
one function with six types has six bodies.

A struct takes types the same way:

```kest
struct Table<K, V> {
    keys: [K]
    values: [V]
}

let ages: Table<text, i32> = table()
```

A copy is made the first time a set of types is written, and found again after
that. Which copy is being built comes from what it is built with, so
`Pair(1, "a")` is a `Pair<i32, text>` and nothing is written twice. A shape
written without its types is refused: `Pair` is not a type, `Pair<i32, text>`
is.

The types are written where the value is going and never at the call. There is
no `Pair<i32, text>(1, "a")`: inside an expression `<` is a comparison, and a
language that made it two things there would be guessing which one somebody
meant. A name written where a value is wanted says so, and says what to write
instead.

A generic function is called and not named: it is not one function, so there
is no value to hand around. A name that cannot be worked out from an argument
is refused, and so is a copy that would need two different things to be the
same name.

Each copy is checked against its own types, so `no.alloc` can hold for one and
not another, and a copy over a type that does not compare is refused where it
is made rather than everywhere.

A name may mean a builtin and a function a file declared, and which one is
settled by what is passed: `std.table` calls its own `remove` on a table and
the builtin one on the array inside it.

## Cost contracts

`no.alloc` on a function is a promise the compiler proves or refuses.

```kest
fn stepBody(p: Player, dt: f32) -> Player no.alloc {
    return integrate(p, dt)
}
```

The promise is written at entry points. Callees defined in the same unit are
judged by their bodies, transitively; only boundaries need a written promise.
A refusal names the path down to the body that allocates, not the function
that made the promise:

```
error[K0401]: this allocates, and `stepFrame` promises `no.alloc`
 --> frame.kest:5:17
  |
5 |     let trail = [n, n, n]
  |                 ^^^^^^^^^ reached through `second` -> `third` -> `leaf`
```

Building an array is the only thing in the language that reaches the heap.
Structs, optionals and calls do not. A foreign function is judged by what it
declares, because its body is not here to be read.

The promise is proved twice: once against the tree, where a refusal can name
the path, and once against the instructions that were emitted for it, where
there is nothing to miss because the machine's own list of what reaches the
heap is what is being asked. If the two ever disagree, the second one says so
and calls it a fault in the compiler.

## The host boundary

The default shape is one crossing carrying a borrowed view of contiguous host
storage. Per-value crossing stays expressible and is visible where it is
written, because it costs between four and ten times as much.

A crossing is about twenty nanoseconds, and it does not depend on how large
the program is. Calling by name would: a name is a search over everything the
program defines, so `kest_entry` does that once and a frame calls by what it
found.

Inward and outward are separate specifications. The event path is bulk-first:
the host hands Kest a batch of events to walk, rather than calling Kest once
per event.

What a program asks the host for is a list the host can read. `kest_start`
refuses a program whose externs are not all bound and says which by name, and
`kest_build_extern` is the same list before the refusal, walked from zero until
it answers NULL:

```c
for (uint32_t i = 0; kest_build_extern(build, i) != NULL; i++) {
}
```

It is what the program *declares*, not what it calls: a file importing
`std.math` for one function asks for all of them, because that is what the
import brought and what starting will hold the host to. A host embedding a
program it did not write would otherwise learn the names one failed start at a
time.

A name the host provides is bound once. `kest_host_bind` refuses a name that
is already bound rather than replacing it, because a machine takes what the
host held when it started and keeps it: a second binding would change the
table and not the machine, and reporting that it had worked would be true
before `kest_start` and a lie after it. A host that wants to swap a function
binds one that decides, which is a line of its own C.

A host lends by naming the type and saying what it thinks one is:

```c
frame[0] = kest_borrow(runtime, events, 4, "Event", sizeof(Event));
```

The stride is the program's own, so it cannot be wrong. The size is there to
be disagreed with: a host whose struct has come apart from the program's type
gets a message and a value whose `object` is NULL, rather than reading the
block as something it is not.

What it will be disagreed with is readable before the lend. `kest_build_layout`
answers how many types of a name the program has and, when that is one, what it
lays the type out as: the bytes, the alignment, and one piece per slot saying
where each scalar sits. Nought is a name the program does not hold in an array
and more than one is a name that needs its module written in front of it, which
are the two things a lend refuses for besides the size.

```c
const KestLayout *layout = NULL;
if (kest_build_layout(build, "Point", &layout) != 1 ||
    layout->size != sizeof(Point)) {
}
```

A host lending in a loop checks once. It is the same lookup the lend does, so
the two cannot come apart about what a name means.

The lend compares the size, because the size is what it is given. Two types of
the same size with their fields in a different order are the same size, so a
host that cares compares where the fields are: the layout says one piece a
slot, each a byte offset and what is there, and `offsetof` says the same thing
on the host's side. `examples/embed.c` does exactly that before it starts, and
a `Cell` written the other way round is refused there rather than read wrongly
later. A tagged union has no one piece a slot — which type a payload slot holds
depends on the tag — so the layout says `tagged` and there is nothing to walk.

A refused lend points at the declaration it is about. Two types of one name
carry a note at each, and the fix names the one that can be asked for, since a
name with its module in front of it is the only one of the two a host can say.
A size that disagrees carries the program's side of the disagreement at the
declaration:

```
error[K0610]: the program lays `other.Point` out in 8 bytes and this host has 12
      the two declarations have come apart
 --> other.kest:3:8
  |
3 | struct Point {
  |        ^^^^^ `x: f32` at 0, `y: f32` at 4
```

Which field moved is the host's half to work out, because the library never
sees the host's struct. What it can show is what it has, at the place it was
written.

A name it does not know is answered the way every other unknown name in the
language is, with the nearest one — measured the same way, and only over what
can be lent, because a name the program has and cannot lend fails the same way
the first one did:

```
error[K0610]: the program has no array of `Smaple` to lend to
      the nearest one that can be lent to is `Sample`
```

What is offered is what a host can write and get: the plain name when it means
one type, and the module in front of it when it does not.

What is lent is named, so it has to be a type the program declared. A run, an
optional or a reference is spelled out of other types and has no name of its
own; a host lending an array of one wraps it in a struct, which is a line in
the program and a name both sides can agree on.

What can be lent is what the program's declarations say it takes: a signature
mentioning `[Point]` is enough, whether or not any body ever reaches into one.
A type the program never holds in an array cannot be lent, and says so.

A store cannot be lent at all. It is a slot map with generations, live flags
and a free list rather than a run of elements, so nothing a host has is one; a
host that wants one asks the program to make it and holds what came back.

A handle says what it is, so a store handed where an array was wanted is a
message rather than a wrong read. That is the one thing about a handle the
machine does check, and it is checked because the boundary cannot: `kest_call`
knows how wide a frame must be and not what is in it.

Calling in is the same shape. The arguments go into a frame and the result
comes back over them, so the host says how wide the frame is and the program
says how wide it has to be:

```c
int32_t spawn = kest_entry(runtime, "spawn");
uint32_t needed = kest_frame_slots(runtime, spawn);
kest_call(runtime, spawn, frame, 4);
```

The name is the one the file writes; that a file saying `module game.world`
registered its `spawn` as `world.spawn` is not the host's business.

What goes in the frame is laid out the way a value sits on the stack, which is
not the way it sits in memory: one slot a scalar, in the order the fields are
declared, and a float is a double in a slot even where it is an `f32` in an
array. So a `Vec2` is two slots and `away(a: Vec2, b: Vec2)` is four, which is
what `kest_frame_slots` says. That is D016's two layouts, and this is the other
one: lending shares the host's bytes, calling copies scalars into slots.

Where each argument starts is asked rather than counted. `kest_frame_takes`
says how many there are and `kest_frame_at` says where the one at a position
begins, so a host writes the second `Vec2` at what the program says rather than
at what the first one's fields add up to:

```c
uint32_t second = kest_frame_at(runtime, between, 1);
```

What the argument is comes back as the layout `kest_build_layout` gives for a
type by name, so an argument is checked the way anything lent is: the bytes,
and where each piece of it sits. A frame of the right width with the wrong
things in it is the mistake that catches. `kest_frame_gives` says the same
about what comes back over them, and nothing when the function gives nothing —
which is how a host knows that reading `frame[0].real` is reading what the
program wrote there.

A name nothing knows is -1 and nothing else, because asking whether a program
defines something is what this is for. Two names are there and still cannot be
handed over, and those say why: a generic is compiled once for each set of
types it is used with, so `pick` is several functions and the host has to say
which one — `kest_report` names them, and those names are the program's own
rather than anything a file wrote — and an extern crosses the other way, so a
host asking for a function it provides itself is told so at the declaration
that asked for it.

A frame too narrow for what a function takes, or for what it gives back, is a
message before the call rather than a read or a write past the end of the
host's array. Both widths are the declaration's and are known before anything
runs, so a program refused for a frame is a program that has not done whatever
it does and had its answer thrown away. No
frame at all is a frame of no slots, which is what to pass for a function that
takes nothing and gives nothing, and is refused for anything else. A frame
wider than a function needs is nothing to say anything about: what is passed is
what the function takes and the rest is the host's array being larger than this
call.

The width of a function that takes nothing and gives nothing is zero, and so is
the width of an index that is no function. The number cannot tell those apart,
so the second one says so: a host that passed a -1 straight through without
looking at it finds that in the report rather than finding a call that did
nothing.

A machine is given a stack and a depth, and the program says what it needs:

```c
KestLimits limits = {0, 0, 0};
KestReason why = {KEST_REACH_UNASKED, NULL};
if (kest_needs(build, &limits, &why)) { }
```

That is enough for every function a host could call, worked out from what the
program calls. There is no answer for a program that can reach itself or that
calls through a function value, and then a host picks a number, which is what
every host did before there was anything to ask.

Which of those two it was, and the function it was found in, is what `why`
holds. They are not the same news: a run of calls that comes back round is a
shape, and a host that did not know its program had one can go and look at the
function named, while a call through a value is what the language is for and
leaves a host nothing to do but pick. The name is the program's own, so a
function that takes something carries what it takes — `down#i32` — which is how
one copy of a generic is told from another.

A bound function may call back in. What it starts stands above what is already
running, so the frame that called it is still there when it returns, and the
room for it is the host's to ask for: `kest_needs` answers for one call in and
a host that calls in from inside one adds what that needs. Running out of room
is a message rather than a wrong read.

What it may not do from there is take away what the program is standing on.
Throwing the heap away and freeing the machine are both refused while the
program is running, and said rather than done; lending is not, because it puts
something on the heap rather than taking the heap.

`kest_report` writes what the program has said since it was last asked, which
is how a host finds out why a lend or a call did not work. It is asked of the
runtime: while a program is running, that is the only thing a host holds. The
build compiles and starts, and what failed to compile went to `kest_build`.

Both of those take the form to write in, and the two forms carry the same set:
prose for a person, and JSON for whatever reads it after — an editor, a build,
a model repairing what it wrote. This is the `--json` the commands have, at the
boundary rather than at a command line, and it is asked for at each of the two
places output is written rather than set once somewhere else:

```c
KestBuild *build = kest_build(path, NULL, stderr, KEST_FORM_JSON);
kest_report(runtime, stderr, KEST_FORM_JSON);
```

A machine that has said nothing since it was last asked writes nothing, in
either form. JSON is one object per call for the run of diagnostics that call
is about, and the count in it is of what that object holds rather than of
everything the machine has ever said.

`include/kest.h` is the only header a host includes and `libkest.a` needs libc
and nothing beyond it.

## Running

`kest run` calls `main`. A `main` that returns `i32` supplies the process exit
status, and one that returns nothing exits zero.

```kest
fn main() -> i32 {
    print("hello")
    return 0
}
```

`main` is the one function with a shape the language holds it to, because it
is the one nothing in the file calls: it takes nothing, because `run` hands it
nothing, and it gives `i32` or nothing, because what it gives is the status.
Anything else is a mistake where it is written rather than a surprise when
somebody runs it:

```
error[K0347]: `main` gives `bool`, and what `main` gives is the exit status
      give `i32`, which is a number from 0 to 255, or give nothing
```

Only the file that was named is held to this. A `main` in a file that one
imports is a function like any other, because nothing will call it.

An exit status carries a number from 0 to 255, and that is the whole of what
one can carry. A `main` that answers something else is not cut down to fit,
because cutting 256 down gives nought and nought is the answer that means
nothing went wrong:

```
error[K0618]: `main` answered 300, and an exit status carries 0 to 255
      answer inside that range, and print what does not fit
```

Which is why `run` has nothing to add to `--json`: the status is the answer,
and it is the answer in full or it is a diagnostic.

A host chooses how much the machine may use, through `KestLimits`: the stack,
the depth of calls, and the heap. The first two are what a program needs and
`kest_needs` answers them. The heap is the one that grows while a program runs,
so it is the one a host watching a frame budget puts a number on, and crossing
it is a message at the instruction that asked:

```
error[K0617]: the program has used the 65536 bytes it was given
 --> hungry.kest:7:9
  |
7 |         push(rows, i)
  |         ^
```

That is a different thing from the machine running out, which is `K0605`, and
only one of the two is anybody's mistake. Zero is no ceiling, which is what a
host with no opinion gets and what every host had before there was one.

A build makes as many machines as a host wants. Each has its own stack, heap
and diagnostics; what they share is the compiled program, and nothing writes to
that once it is compiled — a generic is copied per set of types while
compiling, not while running. What one machine says is not what another
reports.

What a machine is running with is asked of the machine. `kest_allowed` fills
the same struct `kest_start` was given, with the host's numbers where it gave
them and the built-in ones where it did not, which is otherwise not knowable
from outside:

```c
KestLimits allowed = {0, 0, 0};
kest_allowed(runtime, &allowed);
```

A host can ask how much a running program has allocated with `kest_heap_used`,
which is a number without a scale until the ceiling beside it is readable.
`kest_heap_reset` throws all of it away and starts again, which is safe
between calls because nothing of a program's survives one, and which
invalidates every handle the host is still holding.

A failure at runtime is reported in the same shape as a failure at compile
time, with the same codes, the same source location and the same `--json`
output. Nothing about repairing a program needs to know which of the two it is
reading.

## Diagnostics

Compilation reports every error it can find, not the first. Each has a stable
code, a span, and a suggestion where one is knowable.

```
error[K0104]: unknown function `printf`
  --> player.kest:14:9
   |
14 |         printf("hit")
   |         ^^^^^^ did you mean `print`?
```

A diagnostic about more than one place says both:

```
error[K0401]: this allocates, and `stepFrame` promises `no.alloc`
  --> chain.kest:5:17
   |
 5 |     let trail = [n, n, n]
   |                 ^^^^^^^^^
  --> chain.kest:17:4
   |
17 | fn stepFrame(n: i32) -> i32 no.alloc {
   |    ^^^^^^^^^ `stepFrame` promises it here
  --> chain.kest:18:12
   |
18 |     return second(n)
   |            ^^^^^^^^^ which calls `second`
```

The same run with `--json` emits the identical set, notes and all, for
tooling and for models repairing their own output. With `--json`, a program's own writing goes to standard error, so what is left
on standard output is the JSON. `kest check --json` adds what the program
holds beside what is wrong with it: every type with its
layout and every function with what it takes, what it returns, whether it
promises `no.alloc`, and where it was declared.

`kest emit --json` adds the instructions: what is laid out, what the host must
provide, and every function with its code as an offset, a name and the numbers
after it. What the text form decorates — the value behind a constant, where a
jump lands — is left as the numbers there, because a reader that wanted prose
would not have asked for JSON. `kest fmt --json` says whether each file is
already in the one form and does not print it, because a stream that is an
object and a file's contents at once is neither. `kest call --json` puts what
the function gave back in the object, written the way the language writes it;
a function that gives nothing back has no `result`, and so has a call that was
refused before it ran. `kest tick --json` puts the crossings, what they gave
back, the peak between calls and what the heap holds at the end in the object,
and a program that takes no events has neither key. A handler that gives
nothing has `gave` as null rather than nought, because nothing and nought are
two answers.

`tick` calls `onEvents(events: [i32])` once and `onEvent(event: i32)` once per
event, and reads what comes back as a whole number. A handler that gives
something else is told so and not called, since a `text` handed back is a
pointer and a total of pointers measures nothing:

```
error[K0620]: `onEvents` gives `text`, and tick reads what comes back as a whole number
 --> handler.kest:3:4
  |
3 | fn onEvents(events: [i32]) -> text {
  |    ^^^^^^^^ give an integer, or give nothing
```

What it takes is `K0619`, a generic handler is `K0622` — nothing calls one from
inside the file, so there is no copy to run — and a file with neither handler
is `K0621`, which has no span because what is wrong with it is that it is not
there.

A `tick` that drove nothing says why and exits non-zero. There is one of those
for each way it can happen, which is what makes the status worth reading: it is
1 when something was said and 0 when the run happened.

