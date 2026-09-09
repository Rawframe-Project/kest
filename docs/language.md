# The Kest Language

This is the language as it is, and everything it shows is held to that: every
`kest` block here is one this compiler reads, the programs among them are ones
it compiles, every message is one a run says, every command and option is one
the command line has, and every call into the standard library is one that
library has. `tools/check-docs.sh` holds those, so a block or a message that
got ahead of the compiler stops the gate rather than sitting here reading like
the rest of it. What is not held that way is prose, which is why the table
under `Where each rule is run` says which example runs each rule, and
`docs/worklog.md` says when each of them arrived, newest last.

## Shape

```kest
module world.quests

import std.io
import std.math

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
        if math.abs(p.x - e.x) < 1.0 {
            p.health = p.health - 10
            io.print("hit, health {p.health}")
        }
    }

    return p.health > 0
}
```

## One form

`kest fmt` prints a file the one way the language writes it. `-w` writes each
file it is given and names the ones it changed; `--check` names them without
writing and exits non-zero, which is the question "is this already right".
The names it prints are the files `-w` would rewrite, and a file that does not
parse is not one of them: it is refused on the standard error with what is
wrong with it, and the status says so either way.
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

Comments are kept, at the indent of what they are written above — and a comment
written at the end of a line, or inside something that is printed as one line,
is written above that thing rather than above whatever follows it. What is
inside a string is left exactly as written, and so is the spelling of a number
— except the code in a hole, which is code and is written the one way code is
written here. So a comment may not be written inside a hole: there would be
nothing to write it back into, and at the level of the file the whole string is
one token, so nothing that reads a file for its comments would ever see it. It
is refused where it is written, and the place it belongs is the line above.

A list that does not fit in eighty columns goes one item to a line, all of
them or none: half on one line and half on the next is the arrangement nobody
asked for. A chain of operators breaks the same way, all of them or none, after
the operator.

Where a line cannot hold what is on it and there is one place a break may go,
it goes there: after the arrow of a match arm, and before the `else` of an `if`
that gives a value. A line inside a string cannot break at all, so a long one
stays long, and neither can a name: a line holding one longer than eighty
columns stays that long, because the only other thing to do is break a name in
half and half a name is a different name. What can break still breaks — a line
that cannot fit is not a reason to stop arranging the rest of it.

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

`>` is the one operator a line may end after, because `ref<Npc>` and
`store<Job>` end in one and a field ends where its line does. So a comparison
whose right side is on the next line is refused where it is written, and a
comparison too long for the line stays on the line it is on: breaking after the
`>` gives two statements, and breaking before it ends the line on a value,
which is the same refusal from the other side.

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

A file that says nothing is a program to run and not a module to read. Its
names live under nothing, which is what somebody writing one file to answer one
question wants; importing it is `K0702`, because those names would land in the
importing file's own and where a name came from is written at every use of it.

A module's name is where its file is: `module examples.game.npc` lives at
`examples/game/npc.kest`, and the file the command names settles where the
package directories start by having its own name taken off its path.
Two files may import each other, and a file is read once however many ask for
it: what a name means is worked out after everything is read, so a type or a
function from either side of a cycle is reachable from the other. A file that
imports itself is refused, because its own names are its own already.

`import examples.game.npc` therefore reads the same file whoever writes it, and
a file that says one thing and sits somewhere else is refused where it is
imported:

```
error[K0703]: `mism/helper.kest` calls itself `mism.helpers`
3 | import mism.helper
  |        ^^^^^^^^^^^ an import is a path, so a file read by this one says
                       `module mism.helper`
```

Its names live under the last part of what it calls itself, so a file that imports
it writes `render.draw` and `render.Sprite`, and the file itself may write
`draw` and `Sprite`. Where a name came from is written at every use of it. Two modules whose names
end the same way would put their names under the same one, and that is refused
for the whole program rather than mixed. The table those names go in is the
program's — `math.min` is one entry however many modules end in `math` — so
two of them in one program share a namespace, and a file importing one would
find the other's names without asking for them. A program may not hold a
`math.kest` of its own beside `std.math`; what would make that a question about
one file is a table keyed by the whole of a module's name, which is a change to
every lookup in the compiler and is not made. When one of the two is the
library's, the message points at the other one, because `std` is the one name a
program cannot use and the library is not the reader's to rename.

There are no methods. A function takes what it works on like anything else, so
`len(t)` and `text.upper(t)` are how those are written, and `t.upper()` is told
what to write instead — the whole of the name, with the module in front of it
when that is where the function is.

There is no `print`. Saying something is the host's to do, and `std.io` is
where a program asks for it:

```kest
import std.io

io.print("hello")
```

A host provides `Io.write`. The command line provides eight more that no
module declares, because it is a host like any other and binds what the
programs it ships with ask for: `Io.read`, which is everything on the standard
input as one piece of text; `Engine.name`, which is what the host calls itself
— `kest`, from this one; `Engine.decide`, which `examples/embed.kest` asks for
and which this host answers with 1; `Host.sqrt`, `Host.write` and `Host.clock`,
which `examples/host.kest` declares to show what an `extern` is; and
`Host.samples` and `Host.sample`, which it declares to show a host lending a
run of numbers and handing them over one at a time. A program that wants one of
those declares it and runs under a host that has it:

```kest
extern fn Io.read() -> text
```

Which of them a program may promise `no.alloc` for is not a fact about the
names. What crossing back costs is what decides it: `Io.read`, `Engine.name`
and `Host.samples` hand over a piece of text or a run of numbers, and the
machine has to own that, so they reach its heap. The other six answer with a
number or take one, and reach nothing. A program that promises for one of the
first three is told at the call, by the machine, which measures rather than
believes:

```
error[K0631]: `Engine.name` promises `no.alloc` and this host took 5 bytes in it
```

It is not in `std.io`, and that is the rule rather than an oversight: a
declaration there is a thing every host of every program that imports it has to
provide, and an engine has no standard input. What a module declares is what
every host of it must have; what a host offers beyond that is between the host
and the program that asks.

 `std.math` declares seven functions the host must
provide: `Math.sqrt`,
`Math.floor`, `Math.ceil`, `Math.sin`, `Math.cos`, `Math.pow` and
`Math.atan2`. A program that imports it requires all seven, whether or not it
reaches them, because the host may call any function in the program and nothing
can be left out on the grounds that this program does not use it.

Everything else in that module is written out of those: `tan` is a sine over a
cosine, `asin` and `acos` are `atan2` and a square root, and `round`, `sign`
and `lerp` are arithmetic.

Everything there is written in both widths a program works in. A frame works in
`f32` and a number is written in `f64`, and the `f32` one goes through the
`f64` one and comes back, because widening by hand at every call is the module
not doing its half. The same goes the other way for whole numbers: what takes
an `i32` takes an `i64`. `sign` is the one that does not come in pairs — it
gives back one of three answers whatever it was handed, so it answers in the
width those three fit in, and there is none for a float: what is not a number
is on neither side of nought, and calling that nought is the answer for a
number that is exactly nought. `asin` and `acos` give nothing back for anything
outside -1 to 1, because that is a question with no answer rather than a number
to make up.

`sqrt` of a negative says the same thing the other way, with the value a float
already has for it: not a number. So does nought over nought, and a number too
big to hold comes back as infinity. Neither can be found by comparing — the one
that is not a number is not equal to itself, and infinity is equal to itself —
so `math.isNumber(x)` is the question, and it is true only of a number a
program can go on with. `text.real` asks it before handing a line's field back.

`abs` of the smallest `i32` is itself, because its distance from nought is one
past the largest and negating it wraps like all arithmetic at the end of a
width. A program that cannot have that answer keeps the value away from the
edge before asking for it.

A module whose name starts with `std.` comes from the standard library
wherever the program is, and no project may use that name. The library is Kest
source. It is looked for at `$KEST_LIB`, then beside the program, then beside the
program's own directory where an install puts it, then where the build was told
it would be installed, and the first one that is actually there wins. A tree
being installed has two of them and the order is the whole of the answer: what
a program reads is the library beside the command it was run with, which is the
one somebody just built.

A library has no version and nothing to mismatch: it is source, compiled with
the program every time, so a program read with a library that is not the one it
was written against asks for a name that is not there rather than calling into
something else. The message says which module it looked in and where that
module was read from, because that is the question a reader has.

```kest
import std.io
import std.text

if let health = text.number(field) {
    io.print("{health}")
}
```

`text.number` reads a whole number and `text.real` reads one with a point in
it; both give nothing back when what they were handed is something else, which
is what makes reading a field a question rather than a guess. A number too big
to hold is something else: `text.number("2147483648")` is nothing, because an
`i32` handed more than it holds wraps rather than refusing, and a field that
comes back as a different number is worse than a field that does not come back.
The same goes for one too big for an `f32`, which would otherwise read as
infinity. Neither takes an
exponent: `1e3` is not a number here, because a program that means that can say
it another way and a rule with one shape is a rule a reader keeps.

`text.right(subject, width)` and `text.left` push a piece of text to one side
of a column that wide, which is what a table wants: a number to the right of
its column and a name to the left of its own. A width is in bytes, because text
is its bytes, and text already that wide comes back as it is — losing the end
of something to fit a column is worse than a column that does not fit.

`text.fixed(value, places)` writes one back with that many places, which is
what a line of a file wants: `1.5` and `1.50` are the same number and not the
same line. A hole in a string writes the shortest spelling that reads back as
the same number, which is the other question and the one a log asks. Reads back
by whom is the point of it: the spelling is chosen by reading it back, so what
a host gets out of those digits with the reader it already has is the number
the program had. Half goes
away from nought, places outside nought to nine are held to that, and a number
that rounds to nothing is written without a sign in front of it.

What is there: `std.io` says something, `std.math` names the host's arithmetic
and writes what can be built out of it, `std.text` cuts and builds text,
`std.sort` is told what comes first — `sort.by(items, sort.ascending)` —
`std.table` is a hash table, made by `table.empty()`, whose pairs are walked over its `keys` and
`values`, which are packed and in step, `std.vec` is two and three components of
`f32`, and `std.random` gives numbers that look random out of a state the
program holds.

`std.vec` answers for every vector whose answer an `f32` holds, which is not
the same as every vector whose square it holds. `length`, `distance` and
`direction` divide by the largest component before anything is squared, so the
length of a vector of 1e20s is 1.41e20 rather than infinity and the direction
of a vector of 1e-21s is the direction of the same vector at any size. What is
squared and handed back as a square — `lengthSquared`, `distanceSquared` and
`dot` — is arithmetic and runs off the end of the width where arithmetic does:
the square of 1e20 is infinity because that is what the square is. Those are
for comparing, which is what they are cheap for, and comparing works up to the
size where the squares stop fitting.

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

What `check` writes out in full is what the first file named declares, and
every other module is a line saying how much it holds. A program that did not
check is not written out at all: what a reader asked is what is wrong with it,
and a listing of a program that is half worked out is a list of things that may
not be there. `--json` says both, because a tool reading a file somebody is
still writing wants what has been worked out so far.

What is wrong with a program is written where a shell keeps errors and what a
program holds is written where it keeps answers, so `kest check x.kest > held`
writes the answer to a file and shows the mistakes. In JSON everything is on
the one stream: a tool reads one thing, and an object split over two streams is
neither.

A shell may put the two back together, and then the order is what a reader is
reading for: what a program printed is written before what went wrong, whatever
either stream is going to. The first file is the
one somebody is asking about and the rest are what it stands on, so naming the
same files in another order is a different question and gets a different
answer.

`kest call <file> <function> [argument]...` calls one function and prints what
it gives. The function is any of the program's, which is the file named and
everything it imports: a bare name is one of the named file's own, and a name
with a dot in it is written the way `check` prints it — `shapes.doubled` is the
one in the module the file imported. The arguments are read the way the
language reads a literal: any
width of the right family, then the width it would have had on its own, so
`min 3 7` is the `i32` one and `min 3.5 7.5` is the `f32` one. A parameter that
cannot be typed at a shell is refused with the signatures listed, which is most
of what the standard library holds and none of what it holds of numbers and
text: `kest call x.kest text.number abc` says `none`, because an optional that
is nothing is a thing to print rather than a thing to fail at.

`fmt`, `parse` and `lex` read each file on its own and follow nothing, because
what a file is does not depend on what it imports. One that cannot be read is
reported and does not stop the rest.

A line ends with one character in the one form. A file written where they end
with two is read — the extra one is space, and space between tokens is not
part of what a program says — and what `fmt` gives back ends its lines the way
every other file here does. That is a file that differs everywhere, once.

Where a message points counts the same way: a line ends at a line feed, and at
a carriage return with no line feed after it, so a pair ends one line and not
two and a file written with returns alone has lines at all. A caret under a
column nobody can count to is a message about the wrong place.

A comment ends where the line does, and both characters end it. On a file
written with two that means the return was never part of what somebody wrote in
the comment; on a file written with the return alone it means the file is read
at all, rather than as one comment from the first `//` to the end of it.

`lex` does not parse either. Its answer is the tokens, so what it says about a
file is what the lexer found: a byte that starts no character, a string with no
end. A file whose tokens are fine and whose shape is not lexes without
complaint and is refused by `parse` and by `check`, which are the commands that
ask that question.

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
against a `no.alloc` promise like anything else — and what counts is what the
call does, not the `defer`: a promise that defers something which reaches the
heap is refused where the reaching is, with the promise and the `defer` named
on the way.

A block is where it runs, so what it names is still there: a `defer` written
inside an `if` runs at the end of that `if`, and a `return` from inside runs
every block's on the way out, innermost first. There is no way to write one
that reads a name which has gone.

What it is given is what its names hold where the block ends, not where the
`defer` is written: nothing is copied and put aside, because a copy per `defer`
is memory nobody asked for and this language does not spend that quietly. So a
name that changes after the `defer` changes what runs, which
`examples/borrow.kest` writes down — and the shape `defer` is for, giving back
what was just taken, is the shape where the two readings agree.

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

The `else` may sit on the next line, which is the one place a line break falls
inside an expression:

```kest
let rounded = if scaled >= 0.0 -> i64(scaled + 0.5)
    else -> i64(scaled - 0.5)
```

Nothing else can follow a value with `else`, so nothing is taken from anybody
by looking past the break for one. It is where the formatter breaks such an
`if` when the line will not hold it.

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

A count at or past the width is where C stops having an answer and this one has
it: everything is shifted out, so `1 << 64` is nought and `-8 >> 64` is -1,
which is what the sign says and what a shift of sixty-three then one more would
have given. It is the declared width and not the slot's: a `u8` of 200 shifted
nine either way is nought, and an `i8` of -8 shifted right nine is -1. D018 is the rule — match C where C has an answer, and answer where
it has none.

A statement that is only an expression has to do something. A call does, and
what it gives back may be worth ignoring; an `if` or a `match` whose arms are
blocks does. Anything else works a value out and leaves it lying there, which
is refused: `a == b` written where `a = b` was meant is the usual way to write
one by accident, and a `match` whose arms give values is a value and wants a
`return` or a name in front of it.

A string may hold expressions in braces, and `\{` writes a brace:

```kest
io.print("{len(world)} left, and the escort reads \"{escortOf(world, guard)}\"")
```

There is no `+` on text. Building a string reaches the heap, so a function
promising `no.alloc` may hold a string and may not build one.

A number in a hole is written the shortest way that reads back as the same
number, counted in characters. An `f32` needing eight digits gets eight and one
needing nine gets nine, and a whole number keeps its point, because `3` and
`3.0` are not the same value here. Shortest is in characters rather than in
digits because `%g` reaches for an exponent when its digits run out, and
`123456792.0` says more than `1.2345679e+08` in less.

Text is its bytes. `len(t)` counts them and walks the string to do it, `t[i]`
reads one as a `u8`, and two pieces compare by them. `for b in t` walks them,
which is what to write when the positions are not the point: an index measures
the string every time it is used and a walk measures it once. There is no character
type: `"hız"` is four bytes, and a program that wants characters says what it
means by one. `std.text` means what UTF-8 does and says so with three
functions: `charBytes(b)` is how many bytes the character starting with that
byte is written in — nought for a byte in the middle of one — `chars(t)` counts
them, and `charAt(t, i)` is the one at that place, as text of its own, or
nothing when there is no such place. A character comes back as text because a
character is not a number here: what a byte means is a program's to say, and
what a character means is Unicode's.

```kest
import std.text

// `hız` is four bytes and three characters, and the second is two of them.
let how_many = text.chars("hız")
let second = text.charAt("hız", 1)
```

Text that is not UTF-8 is still text, so a byte that begins no character counts
as one: a count that stops at the first of those is a count nobody can use. A
character whose bytes run out is the bytes that are there — text read a piece
at a time ends in the middle of one, and a line that stops the program at its
last character is a line nobody can read. So is one whose bytes disagree with
it: what a first byte says is three bytes wide is three bytes only when the two
after it are the middles of one, and a byte that begins a character of its own
ends the one before it. `charWidth(t)` is the width of the character at the
front of a piece of text, which is what to walk by; `charBytes(b)` is the
question about one byte on its own.

`charBack(t, at)` walks the other way: where the character before that place
begins. Walking back is what UTF-8 is for — the middle of a character says so
in every one of its bytes, so it is at most three steps and needs nothing kept
from the walk forwards. The two agree about every place in any text at all:
what the walk back lands on is where the walk forwards started, and bytes that
disagree with each other are a byte on its own to both of them.

There is no `for` over characters, and there will not be one: what the cheap
walk yields is places rather than values, and sugar that yielded values would
be a piece of text made for every character of every line anybody walked. The
walk is a `while` that keeps what is left:

```kest
import std.text

fn characters(line: text) -> i32 no.alloc {
    let count = 0
    let tail = line
    while tail != "" {
        tail = rest(tail, text.charWidth(tail))
        count += 1
    }
    return count
}
```

`rest` costs the bytes it steps over, so a walk written that way reads the text
once. A walk that keeps an index instead reads from the front of the text on
every step — `len(t)` walks to the nought and so does `t[at]` — and a line
walked that way is read once per character of it. That is why the width is
asked about a piece of text rather than about a place in one: a place is a
question somebody has to walk to, and the piece is the walk already done. A program that does want them all asks
for them all — `charsOf(t)` is one walk and a piece of text each, where
`charAt` in a loop is the whole of the text walked once per character, because
`charAt` counts from the start every time it is asked.

`chars` and `charBytes` promise `no.alloc`; `charAt` cuts, and cutting reaches
the heap. So a walk that asks for every character in turn is one piece of text
per character, and the same walk written with `charBytes` and an index is
nothing at all — which is what to write when the characters are being counted
or measured rather than kept. The promise says which one a function is: a body
that walks with `charAt` cannot keep `no.alloc`, and the refusal names the line
in the library that cuts. `examples/words.kest` walks both ways.

`'a'` is one byte written the way it reads, and its type is `u8`. It is not a
character: `'ı'` is two bytes and is refused, and so is `'ab'`. The escapes are
the ones a string has — `\n`, `\t`, `\r`, `\\`, `\"`, `\{`, `\}`, `\0` — so a byte
written in a string and a byte written on its own are one spelling.

One of them is not allowed inside text at all. Text ends at its first zero
byte, so a piece of it with one in the middle says less than it holds:

```
error[K0110]: a zero byte inside text, and text ends at a zero byte
      hold bytes in a `[u8]` when one of them is nought; `'\0'` is that byte on its own
```

which is the same refusal the machine makes for a zero byte arriving from an
array or from a host, said where it is written instead. `'\0'` on its own is a
`u8` of nought and is a byte like any other.

One of them is not allowed as itself. A carriage return inside text, written
as the byte rather than as `\r`, is refused:

```
error[K0109]: a carriage return inside text, written as itself
      write `\r`, which is the same byte and can be read
```

because a reader cannot see it, two pieces of text that differ look the same,
and a file that has crossed machines carries one without anybody having written
it. A line feed inside text is refused already, by the string not being
terminated.

```kest
fn isSpace(byte: u8) -> bool no.alloc {
    return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r'
}
```

`hash(x)` gives a `u64` standing for a value. It applies to exactly what `==`
applies to — integers, floats, `bool`, text, a set of bits, and an enum whose
cases carry those — because a type that compares has one and a type that does
not has neither. Neither applies to a struct, because which of its
fields decide is the program's to say: a program that wants one writes
`hash(a) * 31 ^ hash(b)` out of the fields it means.

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
    while tail != "" {
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

Forwards is the cheap direction, and it is the only one. `rest` steps over what
it passes and `t[0]` is the byte it is standing on, so a walk that keeps what
is left reads the text once through. Everything asked about the far end costs
the walk to it — `len(t)`, and `t[len(t) - 1]`, and a loop coming in from the
right a byte at a time reads the whole text for every byte it takes off. A
function that wants both ends walks forwards once and remembers where the last
thing it wanted ended, which is what `trim` does.

Whether there is anything left is `t != ""` rather than `len(t) > 0`: comparing
text stops at the first byte that differs, so it is one byte, where the length
is the walk to the nought for an answer the first byte already had.

`slice(t, from, count)` makes a new piece of text, which reaches the heap —
unless it ends where the text already ends. Text ends at a nought, so a piece
that reaches the end of what it was cut from is a place inside it and the
nought after it is the one that was already there: `slice(t, i, len(t) - i)` is
the rest of it however it is spelled, and costs what `rest` costs, which is
nothing. A cut that stops sooner needs a nought of its own and pays for the
piece. The promise is the same either way — a `no.alloc` body may not cut at
all, because which of the two a cut is is not known until it runs:

```kest
if let at = find(entry, "=") {
    let name = slice(entry, 0, at)
}
```

Both of its numbers are nought or more, and so is every other place this
language takes: an index, the `at` of `matches` and `rest`, where `find` starts
looking, the position `remove` takes out. Where one is written down — a number,
a constant, or arithmetic on them — it is read where it is written: `K0351` for
a count below nought and `K0352` for a place below it. Where it is not, the
machine answers with `K0604`, which is the same rule at the only moment it can
be asked.

Bytes that are not text live in a `[u8]`. That is the type for what a file
holds, what a host lends, and anything with a nought in it: text ends at its
first nought and a run of bytes does not, so the two are different things and
this language says which it means. `std.text` has `bytes(t)` for taking a piece
of text apart and `text(a)` puts one back together, refusing an array with a
nought in it — where it is written when it is written down, and where it runs
when it is gathered:

```
error[K0604]: byte 1 is zero, and text ends at a zero byte
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
piece of text is paid for once at the end. `examples/embed.c` says what that is
worth: six hundred bytes of text built a piece at a time takes 180900 bytes of
heap, and gathered as bytes takes 1680. `call --json` says `heap` for the one
call it makes, which is how `tools/check-costs.sh` asks every library function
that makes text what twice as much costs. That is why `std.text` writes `join`,
`repeat`, `upper` and `lower` this way rather than out of `slice`, which copies
the whole of what it is given at every step. A zero byte in the array is refused
at run time, because text ends at its first zero and one in the middle would
quietly cut the rest off.

## Keywords

```
break   const   continue  defer   else    enum    extern
false   fn      for       if      import  in      let
match   module  none      return  struct  true    while
```

`flags` is not one of these: it declares a type only where a declaration
begins, and is a name everywhere else. A flag set says how wide it is, and one
that does not is `K0212` rather than a file that holds something a file cannot
hold — the width is what a host sees, so it is written rather than counted off
the names.

`type` is not one either, and is a name like any other. A word kept back for a
feature nobody has designed is a promise, and `flags` is how this language
takes a word back when it needs one: where a declaration begins it declares,
and everywhere else it is what somebody called their field.

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
C does not. Something that is not a number has no order, so it lands on nought
rather than on either end.

The least whole number divided by minus one is the other place C has no
answer. There is one number it cannot be — the answer is one past the top of
the width — so it wraps to itself, the way every other arithmetic at the end of
a width wraps, and the remainder beside it is nought. That is D018's second
half: answer where C has none, and answer the way the rest of the language
already does.

Dividing by nought is two different things. A whole number has no answer, so it
is `K0601` and the program stops; a float has one and it is the one C has, an
infinity with a sign, or not a number when nought is divided by nought. That is
D018 again: match C where C has an answer.

A whole number written inside a conversion is a number of that type when it
fits — `i64(9223372036854775807)` is that number, not an `i32` too small to
hold it — and a narrowing when it does not: `i8(300)` is 44, which is what
D018 says about a value that will not fit.

Those are two questions that look like one. `let x: u8 = 300` says this number
is a `u8`, and it is not, so it is refused. `u8(300)` says make me a `u8` out
of this number, and what that keeps is what a `u8` has room for. The first is
about what something is; the second is a thing being done to it, and the
answer to both is written where somebody can read it.

A hole in a string writes those as `inf`, `-inf` and `nan`. They are the one
thing this language prints that it cannot read back, because there is no way to
write them: a program that wants one divides. Not a number has one spelling
whatever a divide left in its sign bit, because that says something about the
bits and nothing about the value. `examples/numbers.kest` checks every edge of both
rules, because a program that counts on them should be able to see them run.

`f32` and `f64` are different types and different instructions. `f32`
arithmetic rounds to `f32`, because the engine on the other side of the
boundary does, and an answer that differs from that one is the wrong answer.
Nothing converts between them on its own.

A host boundary is always declared and never inferred:

```kest
extern fn Clock.now() -> u64 no.alloc
```

`no.alloc` on one of these is a promise about the host, made by whoever wrote
the declaration: this function does not take from the program's heap. A body
that promised the same may then call it, which is how `io.write` is written and
why a frame that promises nothing will be allocated may still say something.
The machine holds the host to it, because the host is the one thing here that
this project does not compile:

```
error[K0631]: `Io.write` promises `no.alloc` and this host took 6 bytes in it
```

What the host does with its own memory is its own business. What it may not do
is make text or arrays out of the program's.

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

Stale stays stale. A reference is a slot and a stamp, so one to something
removed reads nothing even after the slot has been taken back by something
added later — a slot map without the second number would answer with whoever
moved in. Writing through it and removing through it say no for the same
reason, and `examples/quests.kest` checks all three.

The stamp comes from the build and not from the store or the machine, so no two
slots in any two stores of any two machines made from one program are ever
stamped the same. A reference therefore says which store it
came from without carrying one: handed to another store of the same shape, it
names a slot stamped by something else and reads nothing. What runs out is how
many slots a machine has handed out altogether — a store of one, emptied and
filled four thousand million times — and running out is a message rather than a
stamp handed out twice.

A walk over a store steps over its dead slots, so what it costs is how far the
store has ever reached rather than how much is in it — with one exception: a
store with nothing left in it goes back to reaching nothing, because everything
a walk would step over is dead. What each slot has counted is kept, so filling
it again hands back the same slots with new counts and every reference from
before is as stale as it was.

A slot counts how many times it has been taken back, in thirty-two bits beside
a thirty-two bit index, which is what makes a reference one value. Four
thousand million removals of one slot and the count would come round to where
it started, and a reference from the first occupant would read as the newest
one. A slot that has used all of its counts is not handed out again: what that
costs is one slot in a store that has been removed from four thousand million
times, and what it buys is that stale stays stale for as long as the program
runs.

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
    io.print(npc.name)
}
```

Nothing is notified of a removal and nothing counts references, so two values
may point at each other and neither has to be told. `get`, `set` and `remove`
allocate nothing; `add` can grow the store and does.

`store(n)` makes one with room for `n` before anything is in it. `len` of it is
still nought, because room is not what it holds; what it buys is which frame
pays. A store grows by doubling, so the `add` that fills the last slot pays for
the next eight, and a host with a frame budget is set by that frame and not by
the others. Saying how many there will be moves the growth to the line that
said so. A count below nought is refused where it is written when it is written
down, and where it runs when it is not:

```
error[K0351]: a store cannot have room for -8
```

The room of something removed is handed out again, so a store that is added to
and removed from forever is work and not growth — which is most of what a
simulation does with one. A store that only grows still only grows. Nothing
gives memory back while a program runs (D012), so what a host has is the whole
heap at once, thrown away between frames if it wants it.

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

`n` is nought or more, and nought is an array with nothing in it. Below that is
`K0351` when the count can be worked out where it is written — a number, or a
constant, or arithmetic on them — and `K0604` while running when it cannot.
Indexing goes the same way: `a[-1]` is `K0352` where it is written, and an
index into a `[T; N]` is measured against `N` there as well, because that one
is written down too:

```
error[K0351]: an array cannot have -2 elements
 --> pool.kest:4:19
  |
4 |     let d = array(-2, 5)
  |                   ^^ a count is nought or more, and nought is an array with nothing in it
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

`clear` keeps the room it took, so `array(n, v)` and `clear` are together what
`store(n)` is on its own: room for `n` and nothing in it. An array grows by
doubling, and it grows where it stands when it is the last thing the heap
handed out — which is what a loop filling one array is. Then the steps up cost
nothing and what asking for room saves is the overshoot, since a thousand
pushed without asking ends up with room for 1024. When something else was
handed out in between, a growth takes a new block and copies, and the block it
came from stays where it is until the heap is thrown away, because nothing is
freed while a program runs (D012). An array big enough to have a block of its
own is the other way round: the block is made bigger and the old one goes back
to the host, so a big array holds itself rather than twice itself. `examples/embed.c` prints both numbers.
There is no third spelling for asking, because two lines already say it:

```kest
let seen: [i32] = array(1000, 0)
clear(seen)
```

A fill of nought is not written, because the memory an array is made from is
already nought. So those two lines cost the room and nothing else, which is
what `store(n)` costs, and a fill of anything else costs the writing as it
always did.

Both reach the heap. An array the host lent cannot grow, because growing moves
the elements and the block is not Kest's to move; that is a failure with a
message rather than a write past the end of what was lent. It cannot shrink
either, because the length is the host's and so is the extent it lent. That is
`push`, `pop`, `remove` and `clear` — the four that change how many there are —
and `examples/embed.c` asks a lent run for every one of them and is refused
four times.

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
known, so `len` of one is a number rather than a walk. Counting a name costs
nothing at all — nothing is loaded to be counted — while `len(corners())`
still calls `corners`, because a call is the point of the line as often as it
is not. It cannot grow: `push` is for the other one.

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

Adding while walking is the other half of that, and the answer is to gather and
add after. A store hands out the slot it last took back, or a new one at the
end when it is holding none — so something added inside a walk lands where the
walk has already been as often as where it has not, and whether this walk
reaches it depends on what died before it. Both halves of that are written
down and neither is worth relying on: the walk is a scan over live slots in
slot order, and where a new one goes is the store's business.

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
    io.print("in stock")
} else {
    io.print("not carried")
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

A function value is reached the way any other value is. Handed to a function,
named by a `let`, held in a field, in an array, in a store — and called from
wherever it is:

```kest
let rules: [fn(text) -> bool no.alloc] = array()
push(rules, long)
rules[0]("herald")
```

A name can be bound to one as well as handed to one, and the two are different
things. `let keep = long` is a name for `long`: the checker knows which body
that is and a cost is proved through it, so nothing else can be put in it.
Holding any function of a shape is written with the shape on the `let`, and
then what it promises is read off the shape:

```kest
let named = long
let which: fn(text) -> bool no.alloc = long
which = short
```

Assigning to the first is refused with `K0350`, because the alternative is a
promise proved through the function a name was bound to and broken by the one
assigned to it afterwards.

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

One that takes types may be handed over as well as called, and which copy it is
comes from where it is going:

```kest
fn ascending<T>(a: T, b: T) -> bool no.alloc {
    return a < b
}

fn sorted(items: [i32], before: fn(i32, i32) -> bool no.alloc) -> i32 no.alloc {
    return len(items)
}

let held: fn(text, text) -> bool no.alloc = ascending
```

The parameter of `sorted` says `fn(i32, i32) -> bool`, and there is one copy of
`ascending` that fits; the `let` says `text`, and there is one that fits that.
This is what lets a library say "the usual order" once rather than once per
type: `std.sort` has one `ascending` and one `descending`, and a shape with no
order of its own is refused in the copy that asked for it.

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
meant. It is still recognised where it is written, rather than being read as
two comparisons and refused at the `)`:

```
error[K0211]: `store` is not given its types where it is called
 --> nodes.kest:8:17
  |
8 |     let nodes = store<Node>()
  |                 ^^^^^^^^^^^ write `store()`, and the type on the binding it goes to
```

A call with something passed to it is told the other half of the rule, that
the copy is made from what is passed. A name written where a value is wanted
says so as well, and says what to write instead.

A generic function is called and not named: it is not one function, so there
is no value to hand around. A name that cannot be worked out from an argument
is refused, and so is a copy that would need two different things to be the
same name.

Each copy is checked against its own types, so `no.alloc` can hold for one and
not another, and a copy over a type that does not compare is refused where it
is made rather than everywhere. What is said about a copy is said at the line
in the body that cannot be compiled and carries a note at the call that asked
for the copy, because a body reads the same for every set of types and the call
is what tells them apart:

```
error[K0314]: `>` does not apply to `Pair`
 --> largest.kest:15:12
  |
15 |         if one > best {
  |            ^^^^^^^^^^
 --> largest.kest:30:20
  |
30 |     if let worst = largest(ps) {
  |                    ^^^^^^^^^^^ this copy was asked for here, with `T` as `Pair`
```

The note says what the type names stand for, because two calls on one line are
two copies and the line alone does not say which.

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
error[K0401]: this allocates, and `chain.stepFrame` promises `no.alloc`
  --> chain.kest:5:17
   |
 5 |     let trail = [n, n, n]
   |                 ^^^^^^^^^ a run that can grow is one on the heap
```

followed by a note per hop, from the promise down to the line that breaks it.

What reaches the heap is a run that can grow, text with a hole in it, and the
builtins that grow or copy: `array()`, `store()`, `push`, `add`, `slice`, and
`text` from bytes. Each says which of those it was. A run of a written length
does not: it is laid out where it stands, so `[f32; 3]` built inside a promise
is the struct's own bytes. Structs, optionals and calls do not either. A
foreign function is judged by what it declares, because its body is not here to
be read.

A call through a function value is judged by its shape, and a shape that
promises nothing is not a body that allocates — it is one nobody has said
anything about. That is `K0402`, and the fix is in the shape:

```
error[K0402]: nothing promises about what this calls, and `apply` promises `no.alloc`
 --> apply.kest:4:12
  |
4 |     return f(n)
  |            ^^^^ write the promise into the shape: `fn(i32) -> i32 no.alloc`
```

The promise is proved twice: once against the tree, where a refusal can name
the path, and once against the instructions that were emitted for it, where
there is nothing to miss because the machine's own list of what reaches the
heap is what is being asked: that list names every instruction there is, so an
instruction added to the language stops the build until somebody says whether
it allocates. If the two ever disagree, the second one says so
and calls it a fault in the compiler.

The first proof has an opinion about every name the language answers to on
its own, and not only about the ones that reach the heap: a builtin it had
never heard of would be one it said nothing about, and the promise would then
be broken with nothing to name the line it was broken on.

The second proof follows a call to a named function and stops at a call
through a value, because which body that enters is not known until it runs. It
is known while it runs, and a compiled function carries what it promised, so
the machine checks that one call as it makes it and refuses with `K0623`. That
is the same fault said in the same words, at the only place it can be seen.

## What there is a most of

A few numbers are what they are because an instruction holds them in two bytes
or a frame counts them in one, and one is what `len` can count to. Every one of
them is a message with the number in it — `K0502` for how many of something,
`K0503` for how far, `K0630` for more than a program can be told it has — and
none of them is a wrap or a quiet truncation:

| At most | What |
| --- | --- |
| 256 | names in a function, counting its parameters |
| 16 | loops one inside another |
| 32 | `break`s in one loop, and 32 `continue`s |
| 32 | `defer`s in a function |
| 65535 | bytes of code a jump reaches, or a loop reaches back |
| 8 | things one `match` chooses between at once |
| 256 | combinations one `match` answers, before it needs an `else` |
| 65535 | elements a `[T; N]` holds, and at least one |
| 65536 | names a program asks the host for |
| 2147483647 | elements an array or a store holds, and bytes in text |

```
error[K0503]: this loop is 156012 bytes of code, and a loop reaches back 65535
error[K0503]: this jumps 156012 bytes of code, and a jump reaches 65535
error[K0502]: a function holds at most 256 names
error[K0502]: a loop holds at most 32 continues
```

All but the last are the compiler's, found before a program runs. The last is
the machine's, because how many a program has is not a thing the compiler can
see coming, and it is one number rather than three: an array, a store and text
are counted by the same `len`, which gives back an `i32`.

A program that runs into one of these is a program that would be worth reading
again anyway. They are here because a number a program can run into belongs
where somebody can read it, rather than only where it is enforced.

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

It is what the program can call, which is not the same as what it uses: a file
importing `std.math` for one function asks for all seven, because every
function of that module is compiled and each of them calls one. A host
embedding a program it did not write would otherwise learn the names one failed
start at a time.

An `extern` that nothing calls is not on the list, because nothing can ever
reach it, and saying so is `K0506`:

```
warning[K0506]: nothing calls `Host.never`, so no host is asked for it
```

A warning rather than a refusal, because a declaration nobody uses is not
wrong. It is worth saying because a host writer reading that file would bind
it, and binding it is work with nothing on the other end.

`check` says it, and so does everything else that reads the file, because what
settles it is the checker: it resolves every name, so it knows which were never
resolved to. `K0508` and `K0509` beside it are the same sentence about a
constant and a shape of the program's own.

Beside the name is what the program expects to cross: `kest_extern_takes` how
many arguments, `kest_extern_layout` what each of them is, and
`kest_extern_gives` what comes back, or NULL for one that gives nothing. They
are the layouts `kest_frame_layout` gives for a function the host calls,
because a crossing is the same shape whichever way it goes. Nothing else checks
that a bound function and the declaration agree: one bound to a name that takes
one thing and written to read two reads whatever is beside it, and
`examples/embed.c` is a host that says what it believes and compares.

A name the host provides is bound once. `kest_host_bind` refuses a name that
is already bound rather than replacing it, because a machine takes what the
host held when it started and keeps it: a second binding would change the
table and not the machine, and reporting that it had worked would be true
before `kest_start` and a lie after it. A host that wants to swap a function
binds one that decides, which is a line of its own C. `examples/embed.c` binds
one of its names twice on purpose and refuses to carry on if that is allowed,
because a refusal nobody asks for is a refusal nobody has seen — and it binds
one function that decides with a thing it can change, which is the other half
of the same sentence: from its third frame on it stops asking the program and
answers for itself, and what the world does says so without anything having
been rebound.

What a host asks about itself is asked for the same way anything else is: an
`extern` the program declares and the host binds. `examples/embed.kest` has
`Engine.name` and asks it what it is running under, and the host beside it
answers with what it is doing as well as what it is called — the same one
function that decides, saying which of its two minds it is in.

Two hosts in one process share nothing. A `KestHost` is a list of bindings its
caller owns, and a machine reads the list it was started from and keeps its own
copy, so the same name bound in two hosts to two contexts is two answers and
neither can be reached through the other. There is no call that would let one
host at another's machines, and none that would let a program ask which host
started the machine beside it: what is not there is what makes this true, which
is why it is written here rather than refused with a code. `examples/embed.c`
starts a machine from a second host, asks it the same question it asked the
first, and stops if the two answers are the same.

### What a host has to keep

Most of what a host can get wrong is refused where it is done: a name bound
twice, a lend of a type the program has not got, a size that disagrees with the
layout, a machine freed while a program is running, a build freed under its
machines. What is left is what the machine cannot see, and all of it is the
same sentence — what a host is handed is a pointer, and a pointer carries no
stamp:

- A handle to a lend that has ended is dead. It stays dead until the next
  lend, which gets its header, and then it names that one instead. See D352.
- What a host kept across `kest_heap_reset` is gone. It stays gone until the
  machine makes something, which goes where it was, and then the same pointer
  is live and reads what is written there now. See D353.
- The block a host lends stays the host's, and has to outlive the lend: the
  machine holds an address and a count and cannot know when the block went.
  The build that checks itself does know — it is told where every block a host
  has ends — so a host lending what it has given back is refused there and
  nowhere else:

  ```
  error[K0610]: this host lent 4 `u8` and does not own that many
  ```

  A host is worth running against that build once for exactly this.
- What a host bound a context with is the host's own memory. The machine keeps
  the pointer and not what it points at, so it has to outlive every machine
  started with that list. See D325.
- A function bound to a name takes what the declaration says. Nothing checks
  the two against each other: one written to read two things where the program
  passes one reads whatever is beside it. `kest_extern_takes` is how a host
  asks before it binds.

`examples/embed.c` is a host that does each of them wrong on purpose where it
can, and prints what happened:

```
and a handle to a lend that ended names whatever was lent next
and text kept across a heap being thrown away reads what the machine made next
```

A host hands text over with `kest_text`, which copies it into the machine's
heap:

```c
frame[0] = kest_text(runtime, "embed", 5);
```

The copy is the point. Text is the machine's for as long as the program holds
it, and a host that handed a pointer of its own would be undertaking to keep it
that long — which it cannot know. A zero byte inside the length is a mistake
rather than a cut, because text ends at its first zero, and what comes back
then is empty and said.

A host lends by naming the type and saying what it thinks one is:

```c
frame[0] = kest_borrow(runtime, events, 4, "Event", sizeof(Event));
```

What that costs the machine's heap is a header, and a header is one size
whatever it stands in front of: lending four bytes and lending forty thousand
cost the same, which is the whole reason a host lends rather than hands over a
copy. The header a lend gives back is the header the next lend gets, so what a host
pays for lending is the most it has lent at once rather than how many times it
has lent: a batch lent and ended every frame costs what one frame of it costs,
for ever. A host that ends nothing pays for every one until the heap goes.

A handle to a lend that has ended is dead the moment it ends, and stays dead
only until the next lend: the header it points at is the header that lend gets,
so an old handle then names the new block. The machine cannot tell them apart,
because a lend handle is a pointer and carries no stamp — a reference into a
store is a number that carries one, which is why that case is caught and this
one is a rule. Ending a lend is where a host drops the handle. When that heap is one the host
gave, the host is the one that filled it and is told so:

```
error[K0643]: this host lent something and the heap it gave has 8 of its 65536 bytes left
``` It is also why a function that hands
one back cannot promise `no.alloc`: the header is an allocation, even though
the block is the host's own.

A lend is an address and a count, and a host with nothing to lend has a count
of nought rather than an address of nothing. That is the one bad address the
machine can tell from a good one, and it says so rather than handing the
program a run of bytes at nowhere:

```
error[K0644]: this host lent 4 `u8` and gave no address to find them at
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
on the host's side. It says what the whole is aligned to as well, which is not
the size and is not in the pieces: it is where a host may put one, and
`_Alignof` says it there. A host lending an array of something the program
reads eight bytes at a time has to have put it where an eight byte read is
allowed. `examples/embed.c` does exactly that before it starts, and
a `Cell` written the other way round is refused there rather than read wrongly
later. A tagged union is walked the same way, with one thing more to know:
which type a payload slot holds depends on the tag, so the layout says `tagged`
and the pieces past the tag say `payload` rather than naming a type that is
only one of the answers. Where they sit is not one of the answers either — it
is where the widest case put them, and it is the same for every case — so a
host compares those offsets like any other, and `examples/embed.c` does. A
struct is `tagged` when anything in it is, and its own fields are still pieces
that say what they are; the word says some piece is a payload, not that there
is nothing to walk. A host reads the tag and knows what a payload holds; what
it must not do is take one for the machine word a handle is, which is what
those pieces were called before they had a name of their own.

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

Where the array sits is refused the same way. The size says how far apart two
of them are and the pieces say what is inside one; neither says the address is
one the program may read a field from, and a payload read across a word
boundary is something the C standard has no answer for:

```
error[K0610]: the program aligns `Event` to 8 bytes and this host lent one 4 past a multiple of that
      lend an array of the type itself, which the host's own compiler aligns; a byte buffer read as one is not aligned by anything
 --> world.kest:18:6
  |
18 | enum Event {
   |      ^^^^^ this is the type it is about
```

Nothing a program can be written to do reaches this one: the address is the
host's alone, which is why `examples/embed.c` asks for the refusal on purpose
rather than leaving it a thing nobody has seen.

What a host does about it, when what it has is bytes, is copy. A packet read
off a socket is a run of bytes at whatever address the reading put it, and the
type the program wants is read wider than a byte at a time — so the lend is
refused, and refusing it is the whole of what the machine can do about somebody
else's address. The way through is to copy the batch into an array of the type
itself, which the host's own compiler aligns, and lend that: one copy for the
batch rather than one for each thing in it, and after it every read and write
is the host's own memory again. `examples/embed.c` does that with a packet a
byte out of alignment, and the program reads the batch out of the copy.

The other way to write it is to lend the bytes as `[u8]` and let the program
read what it wants out of them. What the machine charges is the same either
way — a lend is a header whatever it stands in front of — so there is no number
here to choose by: the difference is a copy of the batch on the host's side
against a loop over bytes on the program's, and neither of those is the
machine's. A host that already has the type copies; a host whose wire form is
bytes anyway lends them.

Taking a run of bytes apart is the program's half of that, and the language has
one way to do it: read a `u8`, widen it, and shift it into place.

```kest
fn recordAt(raw: [u8], at: i32) -> i32 no.alloc {
    return i32(raw[at]) |
        (i32(raw[at + 1]) << 8) |
        (i32(raw[at + 2]) << 16) |
        (i32(raw[at + 3]) << 24)
}
```

Which end the bytes start at is the program's to say, because a wire form says
it and a machine does not: a program that reads a number out of bytes without
writing down the order is a program that works on one computer.
`examples/embed.kest` reads a batch that way out of a buffer the host lent
without copying, beside the batch it reads out of a copy.

Writing one is the same crossing the other way, and the same rule about which
end the bytes start at:

```kest
fn putRecord(raw: [u8], at: i32, value: i32) no.alloc {
    raw[at] = u8(value & 255)
    raw[at + 1] = u8(value >> 8 & 255)
    raw[at + 2] = u8(value >> 16 & 255)
    raw[at + 3] = u8(value >> 24 & 255)
}
```

What a program writes into a lend is the host's own memory, so a program
answering in a wire form puts the bytes where the host will read them and
nothing is copied in either direction. The same eight bytes carry the question
and the answer in `examples/embed.c`, which reads them back the way it would
read anything off a wire.

Words go the same way, and it is the one answer that has nowhere else to go: a
piece of text handed back as a value is the machine's own memory, and what a
host keeps of that is gone when the heap goes. Written into a lend it is the
host's memory instead, and text is its bytes — `len` counts them and `what[i]`
is one — so it is the same loop as a number:

```kest
fn sayInto(raw: [u8], at: i32, what: text) -> i32 no.alloc {
    let n = len(what)
    if at < 0 || at + n > len(raw) {
        return 0
    }
    for i in 0..n {
        raw[at + i] = what[i]
    }
    return n
}
```

`examples/embed.c` asks for a word that way, throws the heap away, and reads
the bytes afterwards — which is the whole of why a host would ask for one like
this rather than keeping the value.

A lend copies nothing, and there is one place that promise ends: making text of
a lent run. Text is the program's and the bytes are the host's, so `text(raw)`
is a copy of every byte — five for four of them, which is the run and the
nought after it, and `examples/embed.c` prints that number where it makes one.
Everything else a program does with a lent array reads and writes the host's
own memory. Which is the same memory the host has, so who may write to it is
whoever is running: a call holds the machine until it comes back, and between
calls the host has it. What either of them wrote is what the other reads —
there is no copy anywhere to go stale, and no moment when both are writing,
because a host function called from inside a call is the only thing running
while it runs.

What is in the memory is not compared at all, and a run of bytes is where that
shows: a host may lend a `[u8]` with anything in it, including a nought, and
nothing about the lend is wrong. What refuses a nought is `text`, when the
program asks for one — `examples/embed.c` lends four letters and then the same
four with a nought among them, and the second is refused at the asking rather
than at the lend.

How many there are is the host's word and nothing weighs it: the memory is the
host's and where it ends is written down nowhere the library can read. What can
be said is what the program is able to count to, since `len` gives back an
`i32`, and a lend longer than that is one whose end the program cannot see:

```
error[K0610]: this host lent 2147483648 `Event` and the program counts them with an `i32`
      lend 2147483647 at a time at the most; `len` is where the program reads the end from
```

So of the four things a lend is — a name, a size, an address and a count —
three are compared against something and the fourth is held to what the
program can do with it.

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

A lend lasts as long as the host says it does. `kest_lend_ends` is the host
saying the block is not its to lend any more: nothing is freed, because the
block was the host's throughout, and what changes is that the program cannot
read it. A host lending what it owns for the length of a frame ends the lend at
the end of the frame, and a program still holding it is told so where it reads:

```
error[K0637]: the host has taken this lend back
```

Without that, the header says nothing about how long the block was good for,
and a program's copy of the handle outlives whatever the host did next. What
the program keeps of a lend is what it copied out of one.

What a program makes out of a lend is the program's own. `text` of a lent run
of bytes copies them onto the heap where everything else the program holds
lives, so it outlives the lend and says the same thing after the block has been
taken back and written over. That is the one place a lend stops being free, and
it is where it should be: the bytes are the host's and the text is the
program's.

A block lent twice is two handles over one run of bytes, and it is the bytes a
host takes back: ending either of them ends both, because a handle still
reading memory its owner has moved on from is the thing ending a lend is for.
The tail of a block lent on its own is the same thing said differently — two
runs that share their ends — and ending either of those ends both as well.
What a host lends is memory, and what it takes back is all of it.

Ending one is also what makes lending free to repeat. Nothing of the block is
on the machine's heap, but the header is, so a host lending a batch every frame
would leave one there every frame; a header the host has given back is the one
the next lend is made out of. A thousand frames of lending and ending cost what
one does.

Where a lend starts is the host's word as well, and less of it can be weighed.
An address a value of that type may not sit at is refused, because that is
arithmetic: a field read across a word boundary is a read the C standard has no
answer for. Everything else about where is the host's to be right about — a
lend from the middle of a row is aligned, is inside the block, holds as many as
it says, and is not what the host meant. What a run of bytes means is the one
thing this crossing never asks.

How many there are is the host's word. A build that ships cannot weigh it —
the block is the host's and its end is written down nowhere this library can
read — so a lend of four out of two is taken and the program walks off the end
of somebody else's memory. Under the sanitisers it is weighed, because that
build is told where every block ends, and a lend longer than what is there is
refused where it is made rather than found where it is read.

A store cannot be lent at all. It is a slot map with generations, live flags
and a free list rather than a run of elements, so nothing a host has is one; a
host that wants one asks the program to make it and holds what came back.

A handle says what it is, so a store handed where an array was wanted is a
message rather than a wrong read. That is the one thing about a handle the
machine does check, and it is checked because the boundary cannot: `kest_call`
knows how wide a frame must be and not what is in it.

Four bytes at the front say what a handle is, and any four bytes can be those
four. So a call in asks the heap about every handle it is handed as well: one
that did not come out of this machine is refused before anything reads it, and
a handle that another machine made is the ordinary way a host has one —
two worlds side by side share the program they were compiled from and nothing
else.

Text is asked the same question, in the two places a program's text can live:
the heap, where anything made while running goes, and the arena the program was
compiled into, where the text a file wrote lives. A string of the host's own is
in neither, so handing one over is refused rather than held — `kest_text`
copies the bytes and answers what to hand over instead. A host that says the
same name every frame keeps what it was given and hands that back, because
saying it again copies it again.

```
error[K0636]: `spawn` takes a handle in slot 0 and this one did not come from this machine
```

A walk of the heap's blocks answers it, which is why it is asked once at a call
and not at every instruction that uses a handle. Inside a call the tag is the
whole of it: what got in has already been asked where it came from.

It is what a host holding a handle from before `kest_heap_reset` is told by as
well. A reset gives back what the heap had handed out, so a handle from before
one is a pointer into memory this heap has not given anybody — until it hands
that memory out again, and a stale handle into whatever is there then is beyond
what anything here can see.

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

A host with nothing but words hands those over instead, written the way a
program writes them, and the machine lays them out:

```c
const char *given[2] = {"3.0", "4.0"};
kest_takes_text(runtime, entry, frame, wide, given, 2);
```

One word an argument, not one a slot. What cannot be written as a word — a
struct, an array, a store, a reference — is refused rather than guessed at, and
so is a word that is not what the declaration says:

```
error[K0635]: `wide` is not a number, and `lengthOf` takes it
```

That is what the command line does with what was typed at it, through the same
door, and it is the half of `kest_gave_text` that goes the other way: one says
what a frame holds without a host reading a slot, the other fills one without a
host writing any.

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
things in it is the mistake that catches. The other way round is a host saying
what it is about to write, one kind a slot, in the order the arguments are laid
out:

```c
const uint8_t writing[3] = {KEST_L_F32, KEST_L_F32, KEST_L_F32};
kest_frame_fills(runtime, entry, writing, 3);
```

which is the same disagreement `kest_borrow` is told about, at the other
crossing. A slot holds whatever was put in it and carries nothing that says
what that is, so a host that means to write a number where the program reads a
float finds out here or not at all:

```
error[K0634]: `lengthOf` takes `f32` in slot 1 and this host says `i64`
```

Saying what some of the slots hold is not checking the rest, and a host that
stops short is told that rather than told nothing. `kest_frame_gives` says the
same about what comes back over them, and nothing when the function gives
nothing — which is how a host knows that reading `frame[0].real` is reading
what the program wrote there. `kest_frame_reads` is the same saying in that
direction, over what comes back:

```
error[K0634]: `lengthOf` gives back `f32` in slot 0 and this host says `i64`
```

A function that gives nothing back has nothing to read, so a host saying it
reads a slot out of one is told the width rather than the kind.

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

A machine is given a stack and a depth, and both are taken before anything
runs, so a host that asks for more than the machine it is on can give is told
so rather than handed nothing:

```
error[K0638]: this host asked for 4000000000 slots of stack and this machine cannot have that much
```

A build is the same shape earlier: `kest_build` answers nothing when the
machine it is on has nothing to read a program with, and says which of the two
kinds of nothing that was rather than leaving a host to guess:

```
error[K0705]: there is not enough memory to read a program
```

`kest_host_new` answers nothing for the same reason and has nowhere to say so,
which is why binding into nothing is refused rather than written through.

Between those two is everything else that can run out, and what a diagnostic is
written into is the arena that has just refused. A run with nothing left could
therefore say nothing at all: it recorded nothing, counted nothing, and came
back the way a run that worked comes back. So a run records one bit when that
happens, which is the one thing it can record without room to record anything,
and says it wherever it reports:

```
error[K0639]: there was not enough memory to finish, or to say more about it
```

It comes after whatever the run managed to say, because it is about what is
missing from that. A command line that could not have the memory it wanted for
itself — before there is a build to record anything in — says the same line,
and in JSON says the same object, because what happened is the same thing.

The program says what it needs:

```c
KestLimits limits = {0, 0, 0};
KestReason why = {KEST_REACH_UNASKED, NULL};
if (kest_needs(build, &limits, &why)) { }
```

That is enough for every function a host could call, worked out from what the
program calls. There is no answer for a program that can reach itself or that
calls through a function value, and then a host picks a number, which is what
every host did before there was anything to ask.

A host that knows which functions it calls can ask about one of them instead:

```c
KestLimits stepping = {0, 0, 0};
kest_needs_of(build, "step", &stepping, NULL);
```

That is the least for that function and what it reaches, which is smaller than
the least for everything whenever the program holds something deeper that this
host will never call — a library it imported for one function, most often. A
host that calls several asks about each and takes the largest, because which of
them it will call is the host's to know.

The command line is a host like any other and does this: `run` asks about
`main`, `call` asks about the function it was given, `tick` asks about both
handlers and takes the larger of the ones the file has, and each gets what it
asked for or the usual numbers when there is no answer. A chain of calls a
thousand deep runs because the program said it was one, and a program that can
reach itself gets `KEST_STACK_SLOTS` and `KEST_CALL_DEPTH` and finds out, which
is what it got before.

`kest emit` prints both, so a host writer can read them without writing a
program to ask:

```
needs 902 slots and 301 frames
     2 and 1 for `onEvent` on its own
needs a number a host picks: `shapes.kept#...` calls through a value
```

The second line is there for each of `main`, `onEvents` and `onEvent` the file
has, when what it needs is less than the whole — those three because they are
the ones a command line calls. A host with its own names asks `kest_needs_of`
about those.

Which of those two it was, and the function it was found in, is what `why`
holds. They are not the same news: a run of calls that comes back round is a
shape, and a host that did not know its program had one can go and look at the
function named, while a call through a value is what the language is for and
leaves a host nothing to do but pick. The name is the program's own, so a
function that takes something carries what it takes — `down#i32` — which is how
one copy of a generic is told from another.

A bound function may call back in. What it starts stands above what is already
running, so the frame that called it is still there when it returns, and the
room for it is the host's to ask for. What it stands on is a number rather than
a guess:

```c
KestLimits inside = {0, 0, 0};
kest_needs_from(build, NULL, &inside, NULL);
```

That is where the machine already is at the deepest place the program calls
into the host, and what a host that calls back in needs is that plus what the
function it calls needs on its own — `kest_needs_of` for that one, added to
this. Both are nought when nothing the program does reaches a host function,
and then there is nowhere to call back in from. Naming a function asks about
that one and what it reaches, the same as `kest_needs_of`. Running out of room
is a message rather than a wrong read.

The machine holds itself to that number where it is used. Every call into the
host is checked against what was measured, because a host builds a stack out of
it and would find out otherwise by running out of room somewhere it was told it
would not:

```
error[K0633]: this calls into the host 3 slots and 2 frames in, where 2 and 0 were measured
```

Like every other message that names this project rather than a program, it
cannot be caused by anything a program does.

What a host may keep of what it was handed is the value itself, for as long as
the heap it is on lasts. A handle stays where it is even when what it holds
grows, and text is never written over; what ends either of them is the heap
being thrown away, which only the host that threw it away knows about. So the
machine answers that one:

```c
if (kest_still_holds(runtime, kept)) { }
```

True while the machine still has the memory it handed out, false after a reset,
and false for anything it never gave. What a host must not keep is a pointer
into what a handle holds: an array that grows moves its elements, and the
handle is what knows where they went.

What it may not do from there is take away what the program is standing on.
Throwing the heap away and freeing the machine are both refused while the
program is running, and said rather than done; lending is not, because it puts
something on the heap rather than taking the heap.

`kest_report` writes what the program has said since it was last asked, which
is how a host finds out why a lend or a call did not work. It is asked of the
runtime: while a program is running, that is the only thing a host holds. The
build compiles and starts, and what failed to compile went to `kest_build`.

`kest_build_report` is the same question asked of the build, and it is the one
to ask when `kest_start` gives back nothing: a program that asks the host for a
name the host has not got is refused before it runs, so there is no machine to
ask why. Nothing is written twice, so a host may ask after every start.

All three of those take the form to write in, and the two forms carry the same set:
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
import std.io

fn main() -> i32 {
    io.print("hello")
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

A name may be several functions in this language and `main` is the one where it
may not, because what `run` calls is a name and not a shape:

```
error[K0355]: `main` is the name `kest run` calls, and this file declares more than one
      one of them is where the program starts; the rest want names of their own
```

The first is the one held to the shape above, and the others are told they are
one too many rather than told what `run` would have wanted of them, which is a
message about a line that is not the entry.

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
it is a message at the instruction that asked. It says what the program has and
what it wanted, because a program that missed by eight bytes and one that
missed by a megabyte are the same problem otherwise:

```
error[K0617]: the program has used 65472 of the 65536 bytes it was given, and this asked for 96 more
 --> hungry.kest:7:9
  |
7 |         push(rows, i)
  |         ^ it was an array holding 8192 of 4 bytes each, growing to 16384
```

What it was growing is there because what a host raises a ceiling by is not
what the last allocation asked for: something that doubles asks for the double
again at the next one. Where nothing is growing the message says what was being
made instead — an array of a million, a piece of text of so many bytes, a store
with room for so many — because a program that asks for everything at once and
a program that arrives there a bit at a time are not the same thing to do
anything about.

That is a different thing from the machine running out, which is `K0605`, and
only one of the two is anybody's mistake. It says the same two numbers, because
what a reader does about running out depends on which of the two it is — a
program that wants a gigabyte, or a machine with a megabyte left — and `out of
memory` on its own is a sentence they already knew:

```
error[K0605]: the program has used 8421426 bytes and this asked for 16777217 more, which this machine has not got
```

There is no third number here, because there is no ceiling: what this machine
has is whatever the one it is running on had left, which nobody wrote down.

One ceiling is not the host's to choose. `len` gives back an `i32`, so an array
or a store holds 2147483647 at the most, and the one that would have been next
is refused where it is put in:

```
error[K0630]: this array holds 2147483647, which is all `len` can count
```

A count the program cannot read is not worth carrying on with, and what
happened instead was worse than a wrong count: the capacity doubled around the
end of the number it is kept in, nought bytes were asked for, and two thousand
million were copied into them.

Text is counted by the same `len` and reaches the same ceiling by a shorter
road, since joining two makes one as long as both and thirty doublings is
thirty lines:

```
error[K0630]: this text would hold 2147483648, which is more than `len` can count
```

It is refused where it would be built rather than where it is counted. A text
that exists and cannot be measured is a number the program reads as an `i32`
and no `i32` holds, which is the one thing D018 says never happens.

A store says the same thing about itself, at the `add` that asked:

```
error[K0630]: this store holds 2147483647, which is all `len` can count
```

Nobody has reached that one on a machine of the usual size — a slot is sixteen
bytes before the three arrays beside it, so the count runs out somewhere past
thirty gigabytes — which is why `tools/check-ceilings.sh` lowers the ceiling in
a copy of the tree and reaches all three in a hundred lines of work each. A
message nobody has seen is the same as no message.

Every failure while running says how it got there: a note per call under the
one that failed, outermost first, so the line and the way in are read together.

```
error[K0601]: division by zero
 --> game.kest:4:12
  |
4 |     return 10 / n
  |            ^
 --> game.kest:12:12
  |
12 |     return middle(0)
  |            ^ `middle` was called here
```

Eight of them is what a message holds, and a run of calls deeper than that says
how many were left out — a number is what a reader of a deep one wants, and the
middle of it is not. Zero is no ceiling, which is what a
host with no opinion gets and what every host had before there was one.

What has to outlive what is one sentence: the build outlives the machine.
Starting reads what the host bound and keeps its own copy of it, so the list of
names may go as soon as a machine has started, and the layouts a build lent are
the build's and go with it.

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
`kest_heap_wanted` is what the allocation that was refused was asking for, and
`kest_heap_refused_by` is which of the two refused it:

```c
switch (kest_heap_refused_by(runtime)) {
case KEST_REFUSED_CEILING: break;
case KEST_REFUSED_MACHINE: break;
case KEST_REFUSED_NOTHING: break;
}
```

The number is the same number either way and a host does something different
about each: a ceiling it set is a number it can raise, and a machine with
nothing left is not — raising a ceiling there is a host raising it forever.
`KEST_REFUSED_NOTHING` is what a machine says until something has been refused,
which is why the two are read together rather than one instead of the other.

`kest_heap_reset` throws all of it away and starts again, which is safe
between calls because nothing of a program's survives one, and which
invalidates every handle the host is still holding. Gone lasts until the
machine makes something: the first thing on an emptied heap goes where the last
one was, so a pointer a host kept is live again and reads what is written there
now. That is the rule for text and for lends alike, and it is the same reason —
a pointer carries no stamp, where a reference into a store carries one. It is the same heap
emptied rather than a new one, so it asks the host for nothing and the only
way it answers false is a host asking for it from inside a call:

```
error[K0613]: the heap cannot be thrown away while the program is running
```

Freeing the machine there is refused the same way and for the same reason: the
stack the program is standing on goes with it. Both are asked for by
`examples/embed.c` from inside the function the program calls it back through,
which is the only place either of them is wrong, and it counts what it was
told.

The host list is the one of these that is never refused, because after
`kest_start` nothing points into it: what a machine keeps is the function and
the context, copied, and the names it was found by are the program's own. So
`kest_host_free` may be called as soon as every machine that wants that list
has started, which is what `examples/embed.c` does — both of its lists are gone
before anything runs. What the context points at is a different question: that
is the host's own memory, handed over at binding, and it has to outlive every
machine that was started with it, because the machine keeps the pointer and not
what it points at.

A machine started with no host at all is the same thing said loudest: every
extern the program declares is unbound, and what comes back says which.

The build under them is the same shape one step out. `kest_build_free` answers
whether there is no build now: true when it freed one, true when there was
none, and false when a machine is still standing on it. What the machines run
is on the build — the program, the layouts, and the text every diagnostic
points at — so freeing it under them is not something they survive, and it is
refused where it is asked for:

```
error[K0640]: this build cannot be freed while 2 machines are standing on it
```

Free the machines, then the build. That is the only order there is, and the
count is in the message because a host that lost one of four machines is
looking for which.

`kest_runtime_free` answers whether there is no machine now: true when it freed
one and true when there was none, false when it was refused. What a host does
about a false is come back and ask again when the call returns, because that is
what makes the refusal stop — there is nothing else to wait for and nothing to
retry inside the call. Nothing takes the machine away by force, so a host that
asks in a loop and never returns from the bound function keeps the machine, the
heap under it and everything the program put there. That is a leak, and it is a
host's to avoid rather than a thing this library will do behind it: the
alternative is freeing what a running program is standing on, which is worse
than a leak in every way that matters.

A failure at runtime is reported in the same shape as a failure at compile
time, with the same codes, the same source location and the same `--json`
output. Nothing about repairing a program needs to know which of the two it is
reading.

## Where each rule is run

Every example is a program that checks itself and answers with which of its own
checks failed, so what a rule does is a thing to run rather than a paragraph to
believe. This is the list of them, held to being complete by
`tools/check-docs.sh`: a file here and not in the tree, or in the tree and not
here, is a check that fails.

| Example | What it runs |
| --- | --- |
| `ants.kest` | a frame that walks an array of value structs and moves each one |
| `borrow.kest` | what has to be given back on every way out of a function |
| `camera.kest` | `std.vec` and `std.math` where a camera follows something |
| `chance.kest` | numbers that look random, and two runs from one seed |
| `embed.kest` | the program the engine beside it runs, frame by frame |
| `events.kest` | the host calling in, one crossing for a batch |
| `flags.kest` | bits, which is what a `u8` of state is |
| `frame.kest` | two structs that name each other, and a reference that may be nothing |
| `game.kest` | where a package's directories start, from a module name |
| `grow.kest` | an array whose size nobody wrote down |
| `host.kest` | what an `extern` declares and what crosses at one |
| `inline.kest` | `[f32; 4]` where it stands, rather than a handle to four elsewhere |
| `inventory.kest` | a container written in Kest rather than built into the language |
| `lines.kest` | a program that reads, and a host that has to provide the reading |
| `lookup.kest` | a lookup that finds nothing, which is a value and not a crash |
| `math.kest` | a loop, a chain of `if`, and a function that answers with text |
| `numbers.kest` | what a number does at the end of its range, at every width |
| `parse.kest` | reading a line of fields out of the standard library |
| `physics.kest` | helpers that take and return vectors, called from a hot path |
| `pieces.kest` | text built a piece at a time, which is built as bytes |
| `player.kest` | a struct is a value, so a function changes its own copy |
| `quests.kest` | characters that point at each other, cycles, and deletion |
| `queue.kest` | what a shift costs, said on the call that shifts |
| `rows.kest` | a struct holding a run of structs, which is what a host lends |
| `scan.kest` | the same line `parse` reads, read without reaching the heap |
| `shapes.kest` | one body, one copy per set of types it is called with |
| `state.kest` | a thing that is one of several, and a `match` that leaves none out |
| `tree.kest` | an enum whose case holds the type it belongs to |
| `words.kest` | text as its bytes, with no character type anywhere |
| `world.kest` | what a struct is and what an array is, in one program |

## Diagnostics

Compilation reports every error it can find, not the first. Each has a stable
code, a span, and a suggestion where one is knowable.

```
error[K0307]: `player.Player` has no field `healt`
 --> player.kest:9:18
  |
9 |     let left = p.healt - amount
  |                  ^^^^^ did you mean `health`?
```

A diagnostic about more than one place says both:

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

The same run with `--json` emits the identical set, notes and all, for
tooling and for models repairing their own output, which is this:

```json
{
  "diagnostics": [
    {
      "severity": "error",
      "code": "K0309",
      "file": "bad.kest",
      "line": 8,
      "column": 12,
      "offset": 109,
      "length": 7,
      "message": "`doc.hurt` takes 2 arguments, found 1",
      "notes": [
        {
          "file": "bad.kest",
          "line": 3,
          "column": 19,
          "message": "this one was not written"
        }
      ]
    }
  ],
  "errors": 1
}
```

A diagnostic about a whole file rather than a place in it carries `file` and
nothing else of where: no line was chosen, and one written down would be a
place a tool would draw. One about the whole program carries no `file` either.

Two more fields turn up where there is something to say. `suggestion` is the
line a person is shown under the caret, and `leftOut` is how many places there
were no room for — a diagnostic holds eight notes, and one that stops at eight
says how many it did not show:

```json
{
  "code": "K0624",
  "message": "no `doc.take` takes nothing",
  "suggestion": "nothing was written after the name",
  "leftOut": 1
}
```

A program's own writing goes to standard error whenever what the command
answers with is something else: with `--json`, so what is left on standard
output is the JSON; for `call`, so what is left is the value; and for `tick`,
so what is left is what the frame cost. `run` is the one whose answer is what
the program said, and that stays where a reader looks. Whether it arrived is
asked before anything else is said: `Io.write` gives nothing back, so a program
cannot be told that its writing failed, and a run into a disk with nothing left
would otherwise write nothing and answer nought.

```
error[K0641]: what the program said could not be written
```

`Io.read` is the same shape the other way. It gives back text, so a stream that
would not be read hands over an empty piece and a program cannot tell that from
an empty input — a closed stream and a directory both read as nothing. The host
finds out and says so, and what is read short for want of memory is nothing
rather than a piece of the input:

```
error[K0642]: what the program asked to read could not be read
``` So
`kest call x.kest math.min 3 7` in a shell is `3` and nothing else, whatever
the program says on its way there. `kest check --json` adds what the program
holds beside what is wrong with it: every type with its
layout and every function with what it takes, what it returns, whether it
promises `no.alloc`, whether the host has to provide it, and where it was
declared. The file that was named is given in full, with a function the host
has to provide marked as one, and a line for each module it imported.

```json
{
  "types": [
    {
      "name": "doc.Point",
      "kind": "struct",
      "slots": 2,
      "bytes": 8,
      "align": 4,
      "file": "doc.kest",
      "line": 3,
      "column": 8,
      "fields": [
        {"name": "x", "type": "i32", "slot": 0, "byte": 0},
        {"name": "y", "type": "i32", "slot": 1, "byte": 4}
      ]
    }
  ],
  "functions": [
    {
      "name": "doc.hurt",
      "parameters": ["doc.Point", "i32"],
      "result": "i32",
      "noAlloc": false,
      "foreign": false,
      "named": true,
      "file": "doc.kest",
      "line": 10,
      "column": 4
    }
  ],
  "constants": [
    {"name": "doc.LIMIT", "type": "i32", "file": "doc.kest", "line": 8,
     "column": 7}
  ]
}
```

A file with a `main` in it is a program, and a name in it that nothing reaches
is one that will never be used. A constant nothing reads:

```
warning[K0508]: nothing in this program reads `SPARE`
      take it out: a constant is a name for a value, and one nothing reads is a value nobody asked for
```

Counting with one is reading it, so `[i32; CELLS]` and `array(CELLS, 0)` both
name `CELLS`.

And a shape nothing names:

```
warning[K0509]: nothing in this program names `Spare`
      take it out, or hold one: a shape nothing names is laid out and never reached
```

A host cannot ask for one either, because what a host may lend is a type the
program holds in an array and holding it in one is naming it. There is no
warning about a function nobody in the program calls, because a host asks for
one by name and every function is an entry until a host says otherwise (D224). A shape that
names itself — a list whose next is one of its own — is named by that, so this
is quiet about those and catches the ones nobody mentions at all.

Both are said about the file that was named and not about what it imported,
since a library is named by whoever imports it and would light up from end to
end. They are warnings rather than refusals because a declaration nobody uses
is not wrong.

`named` is whether anything in this program named that function: called it, or
handed it around as a value. A shape and a constant say it too, where naming
one is holding one and reading one. It is the checker's own answer rather than a
reader's count of mentions, and it is per function rather than per name, so one
of four called `min` is the one that was meant. `check-dead.sh` reads it over
every example and every file of the library, and a library function no run
names is a function nothing has ever run.

An enum is a type like any other and says what its cases are, with the tag each
one is written as and what it carries:

```json
{
  "types": [
    {
      "name": "doc.Shape",
      "kind": "enum",
      "cases": [
        {"name": "Dot", "tag": 0, "named": true, "carries": []},
        {"name": "Line", "tag": 1, "named": true,
         "carries": [{"type": "i32", "slot": 1, "byte": 4}]}
      ]
    }
  ]
}
```

A set of bits is a type like those, with the width it is kept in and which bit
each name stands for:

```json
{
  "types": [
    {
      "name": "doc.State",
      "kind": "flags",
      "over": "u8",
      "bits": [
        {"name": "Moving", "bit": 0, "named": true},
        {"name": "Hurt", "bit": 1, "named": false}
      ]
    }
  ]
}
```

Neither form says anything about a shape that takes types, or about a copy of
one made with a name still standing for itself: a shape is not a type and has
no layout, and `0 bytes` is a number nobody can use. A copy made with real
types is a type like any other and is in both. `check-commands.sh` holds the
printed form and this one to naming the same declarations of the file they were
asked about.

A bit or a case says `named` the way a function does, and nothing warns about
one that is not: a set of bits and an enum are shapes a host lends, so a name
the program never writes is still a name the boundary uses (D225). What is
here is the answer, for whoever wants to ask it.

beside the `diagnostics` and `errors` every command has. Without `--json` the
same list is printed for a person:

```
fn ants.main() -> i32
random  1 type, 9 functions
io  3 functions, 1 the host provides
math  37 functions, 6 the host provides
```

A reader came for the file in front of them, and `kest check` on one of those
modules is how to read that one. `--json` holds all of it either way, because
what a tool wants is everything and what a person wants is the part they asked
about.


`kest emit --json` adds the instructions: what is laid out, what the host must
provide, what the machine needs before any of it runs, and every function with
its code as an offset, a name and the numbers after it. What it needs is the
two numbers `kest_needs` answers with, and they are null when there is no
answer — a run of calls that comes back round has no deepest frame, and a call
through a value reaches what is not known until it runs, so `why` says which it
was and `where` says in which function. Beside them is `entries`, the same two
numbers for each of `main`, `onEvents` and `onEvent` the file has, in the same
shape:

```json
{
  "layouts": [
    {"bytes": 8, "align": 4, "tagged": false,
     "pieces": [{"byte": 0, "is": "i32"}, {"byte": 4, "is": "i32"}]},
    {"bytes": 8, "align": 4, "tagged": true,
     "pieces": [{"byte": 0, "is": "i32"}, {"byte": 4, "is": "payload"}]}
  ],
  "hosts": ["Host.sqrt", "Host.write"],
  "needs": {
    "slots": 8,
    "frames": 2,
    "entries": [{"name": "main", "slots": 8, "frames": 2}]
  },
  "functions": [
    {
      "name": "doc.onEvent#i32",
      "parameterSlots": 1,
      "slots": 1,
      "deep": 2,
      "noAlloc": false,
      "code": [
        {"at": 0, "op": "load", "operands": [0]},
        {"at": 3, "op": "const", "operands": [0]},
        {"at": 6, "op": "add.i", "operands": []}
      ]
    }
  ]
}
```

Every one it has is there whether or not it differs from the whole,
because a tool looks one up by name; the text form leaves out the ones that are
the same, because a reader would be reading them twice. What the text form decorates — the value behind a constant, where a
jump lands — is left as the numbers there, because a reader that wanted prose
would not have asked for JSON.

`kest lex --json` says the token stream, which is the whole of what that
command answers:

```json
{"tokens": [{"kind": "identifier", "line": 1, "column": 4, "text": "main"}]}
```

and where every comment in the file is beside it. A comment is not a token —
the lexer steps over one — so the stream is not where they are:

```json
{"comments": [{"line": 3, "column": 1, "text": "// above the struct"}]}
```

which is what a thing that folds them, or gathers them, or checks that a
formatter kept them, would otherwise have to find by reading the file itself
and getting the two slashes inside a string wrong.

`kest fmt --json` says whether each file is
already in the one form and does not print it, because a stream that is an
object and a file's contents at once is neither. `formed` is null for a file
that did not parse, because whether a program is in the one form is not a
question about a file that is not a program, and the two are told apart by
whatever is deciding which files to write:

```json
{"file": "examples/math.kest", "formed": true}
```
 `kest call --json` puts what
the function gave back in the object, written the way the language writes it;
a function that gives nothing back has no `result`, and so has a call that was
refused before it ran. Beside it is `needs`, in the shape `emit` uses, for the
function that was called: `emit` answers about the three names a command line
might call and this is the command that always knows exactly which one it is. `kest tick --json` puts the crossings, what they gave
back, the peak between calls and what the heap holds at the end in the object,
and a program that takes no events has neither key. A handler that gives
nothing has `gave` as null rather than nought, because nothing and nought are
two answers:

```json
{
  "onEvents": {"crossings": 1, "gave": 174933},
  "onEvent": {"crossings": 1024, "gave": 174933, "peak": 24},
  "events": {"count": 1024, "lent": null},
  "heap": 24,
  "thrown": 0
}
```

`events` is what it was run over. `lent` is the numbers a caller wrote down, or
null when they were counted up from nought, which is a thing to say rather than
a thousand numbers to write out: a run of `0,1,2` and a run of `4,5,6` are two
measurements of the same shape, and a program whose answer depends on which it
was is one nobody can read without knowing.

`thrown` is how many times the heap was thrown away between events, which is
nought unless `--reset` says otherwise. Without it a run that allocated nothing
and a run that threw everything away say the same thing, and the words say the
same: `none of it freed` where a reset freed all of it, `thrown away 3 times`
where one did.

`tick` calls `onEvents(events: [i32])` once and `onEvent(event: i32)` once per
event, and reads what comes back as a whole number. The events are counted up
from nought, and `kest tick file 4,5,6` lends those three instead: a program
whose answer depends on what it was given is measured against what it was
given, rather than against a run nobody chose. A handler that gives
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

