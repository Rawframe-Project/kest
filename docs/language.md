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

Two kinds of number appear here and they are not read the same way. A count is
the program's — how many tokens a file is, how many types checking it made, how
many slots a value takes on the stack — and it is the same count on any machine
that reads the same file. A measurement is the machine's: what a stage cost in
bytes, what a shape takes in memory, where a run stops when memory runs out. All
of those were measured on the machine this was written on, and another machine
answers with its own. Where one of them is written here it is written as what it
was, not as what it must be.

Getting your own is `make check`, which runs everything this document shows and
says the numbers for the machine it ran on; the two checks that measure one, and
the line about what shapes take in memory, say so where they say them. Each
number here is written beside the command that answers it — `parse --json` for
what a tree is made of, `emit --json` for what a function was given,
`kest_runtime_cost` for what a machine is — so that a reader can ask again
rather than believe a table of which command says what, which would be one more
thing to keep in step.

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
let total = alphabetical * 1000 +
    betelgeuse * 2000 +
    gamekeeper * 3000 +
    delicatessen * 4000

if alphabetical != 1 ||
        betelgeuse != 2 ||
        gamekeeper != 3 ||
        delicatessen != 4 {
    return 1
}
```

The operator ends the line rather than starting the next one, because that is
what the language allows: a line that ends in an operator continues and one
that ends in a value does not.

`>` is the one operator a line may end after, because `ref<Npc>` and
`store<Job>` end in one and a field ends where its line does. So a comparison
whose right side is on the next line is refused where it is written, and a
comparison too long for the line stays on the line it is on. Breaking it after
the `>` gives two statements and breaking it before ends the line on a value,
which is the same refusal from the other side — and the compiler says so from
either, rather than leaving a reader to find this paragraph:

```
error[K0204]: expected an expression, found end of line
  |
4 |     if a >
  |            a line may end after `>` because a type may: `ref<Npc>` is a
                whole field. So a comparison stays on the line it is on
```

A long string cannot be broken.

## One name, two functions

Two functions may share a name when they take different things. Which is meant
is settled by what is passed:

```kest
import std.math

fn scored(hit: i32, height: f32) -> i32 {
    let health = math.max(hit, 0)
    let above = math.min(height, 1.0)
    return health + i32(above)
}
```

There is no ranking and nothing converts, so exactly one can match or none can.
When none does, every function of that name is listed with what it takes, and
what `these` are is said beside the caret in the same notation:

```
error[K0329]: no `one` takes these
  |
4 |     return one(true)
  |            ^^^^^^^^^ these are (bool)
```

A literal is said as what it is rather than as the type it would have taken on
its own — `one(1)` passes a whole number — because which width a literal would
have been is the question the call was asking.

Which functions are listed depends on which of the two sentences it is. Where
none takes what was passed, the near misses go first. Where more than one does,
the ones listed are the ones that do: a call of `1` against seven widths of
whole number and a `text` is ambiguous between the seven, and the `text` is not
what a reader is looking at.

The two are found in two passes. A literal fits any width of its family, so the
first pass takes `1` for a `u8` as readily as for an `i32`; the second asks
exactly, which is what settles a call between a `u8` and an `i32` — a whole
number is an `i32` when nothing says otherwise, and a number with a fraction is
an `f32`. That is the same rule that gives a bare `let x = 1` its type, and it
is written in one place, so a call between widths cannot settle on one thing and
a literal on its own be another. The second is a tie-breaker and
not a second chance: what fits exactly fits the family, so it is asked only
where the first left more than one standing.

Two can match where one takes what the other takes inside an optional, and
`none` fits both. That is the same sentence the other way round, and the list
under it is the same list:

```
error[K0329]: more than one `f` takes these
```

A host asking for one by name is told the same thing in the same way. A name
that is several functions cannot be handed over as an index, so `kest_entry`
says so and names them — `add#i32,i32`, `add#f32,f32` — and those are the names
the program compiled them under rather than anything a file wrote. It points at
where they are written, and says every place there is: a generic compiled twice
is one declaration and is pointed at once, and two functions of a name are two
places, which is how the two are told apart from outside.

```
error[K0615]: `game.pick` is more than one function here: they take different things
```

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

A host that did not write the program has no names to ask for. What it has is
the list, which `kest_entry_name` walks — the other direction of `kest_entry`,
an index to what the program calls the thing at it:

```c
for (int32_t at = 0; ; at++) {
    const char *what = kest_entry_name(runtime, at);
    if (what == NULL) {
        break;
    }
}
```

The spelling is the one `kest_entry` takes back, copies of a generic included,
so a host reads the list, keeps the indexes it wants and never looks a name up
again. The walk ends by answering NULL and says nothing about it, because
reading a list to the end is not a mistake.

`kest_entry_wrote` answers the same function as somebody wrote it, without what
tells one copy of a generic from another. That is the spelling every message
uses, so a host reading a refusal and a host reading the list are looking at the
same word — and two indexes written the same are one function compiled twice,
which is how the list says so:

```c
const char *what = kest_entry_name(runtime, at);   // `game.pick#i32,i32,bool`
const char *wrote = kest_entry_wrote(runtime, at); // `game.pick`
```

What goes back into `kest_entry` is the first of them. The second is not always
a name that can: one that is several functions is refused, and that refusal is
what names the copies.

`kest_entry_promises` answers whether a function made a promise, which the
compiler proved against the code it emitted. Which promise is asked by naming
it — `KEST_PROMISE_NO_ALLOC`, `KEST_PROMISE_NO_HOST` or
`KEST_PROMISE_DETERMINISTIC` — because a promise is a thing to name rather than
a door to add, and the language has room for more of them than it has today.
They are the things about a function a host can act on before calling it: a
frame step that may reach the heap is one an engine puts somewhere other than a
frame, one that may call back in is one a host driving frames from inside its
own lock cannot install at all, and one that keeps the simulation profile is one
a host may replay from inputs or run on two machines and compare. What a program
costs in other ways — how many of its values were worked out where they stand,
what reading it cost — is in what `--json` prints, because a host cannot do
anything about those and a tool reading them can.

```c
if (!kest_entry_promises(runtime, at, KEST_PROMISE_NO_ALLOC)) {
    // not a frame step: somewhere else, or nowhere
}
if (!kest_entry_promises(runtime, at, KEST_PROMISE_NO_HOST)) {
    // it may call back in: not while this host holds its own lock
}
if (!kest_entry_promises(runtime, at, KEST_PROMISE_DETERMINISTIC)) {
    // two machines may answer differently: not a step to replay or to compare
}
```

`emit --json` says both for every function it lists, so a tool reading a listing
beside `check --json` joins them on a field rather than on a rule about where to
cut a name:

```json
{ "name": "game.pick#T,T,bool$i32", "wrote": "game.pick",
  "file": "game.kest", "line": 188, "column": 4 }
```

`name` is what the chunk is compiled under and `wrote` is the declaration it
came from. The listing written for a person says it as the front of the name,
which is where a person reads it.

The three after them are where that declaration is written, under the names
`check --json` lists a declaration's place under. Two chunks written the same
are either one generic compiled twice or two functions of one name, and the
place is what says which: copies of a generic share the declaration they were
made from. The listing written for a person says it where that is the question
— a written name that is more than one chunk — and nowhere else:

```text
fn game.pick#T,T,bool$i32  3 parameter slots, 3 slots, 1 deep
     declared at game.kest:188:4
```

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

Its names live under the whole of what it calls itself, and a file that imports
it writes the last part: a module `game.render` holds `game.render.draw`, the
file that imports it writes `render.draw` and `render.Sprite`, and the file
itself may write `draw` and `Sprite`. Which module a `render.` means is the
importing file's own question, so **two modules may end in the same word** --
`render.math` and `physics.math` are two modules and a program may hold both.
What is refused is one *file* reaching two of them, because there `math.` would
be either, and the refusal points at that file's two import lines. See D1039.

A body may give one of those names to something of its own:
a `let` or a parameter called `draw` is what `draw` means from there on, which is
refused between two locals — at any point in a body one name means one thing —
and allowed here, because a body's names are its own. Nothing is out of reach
when it happens. `render.draw` is how the file's one is written inside that body,
and a file that names no module has nothing to put in front of it, so there the
two names cannot both be written. Calling the name the body took says which is
which:

```
error[K0308]: `i32` is not a function
      write `game.size` for that one
   |
 3 | fn size() -> i32 no.alloc {
   |    ^^^^ this file calls something else by that name
```

A file may take one of the language's own names the same way. `len`, `get`,
`set`, `find`, `add` and `remove` are written without a module in front, so a
file that declares one of them is adding to what the name answers to rather than
covering it: what the name means is settled by what it is handed, the file's one
for the shapes it takes and the language's for the rest. `std.table` and
`std.vec` each do it, and so do two of the examples. A call that fits neither is
told what the language wanted and pointed at the other:

```
error[K0310]: `len` counts an array, a store or text, found `i32`
   |
 3 | fn len(xs: [i32]) -> i32 no.alloc {
   |    ^^^ this file calls something else by that name
```

Where a name came from is written at every use of it. The table those names go
in is keyed by the whole of a module's name — `a.math.min` and `b.math.min` are
two entries — so two modules ending in the same word are two modules and a
program may hold both. A program may have a `math.kest` of its own beside
`std.math`, and `render.math`, `physics.math` and `vendor.math` may all be in
one program at once.

What `math.` means is the question of the file that writes it, and it is
answered by that file's own imports and nothing else. So a file reaching two
modules that end in one word is what is refused, because there `math.` would be
either of them:

```
error[K0328]: this file reaches two modules called `math`
   |
 4 | import b.math
   |        ^^^^^^ a name written `math.` here would be either of them, so one of the two has to be called something else
   |
 3 | import a.math
   |        ^^^^^^ the other one
```

and the answer is to rename one of them, which no other file in the program has
to hear about. When one of the two is the library's, the other is the one to
rename, because `std` is the one name a program cannot use and the library is
not the reader's to rename.

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

A host provides `Io.write`. The command line provides twelve more that no
module declares, because it is a host like any other and binds what the
programs it ships with ask for: `Io.read`, which is everything on the standard
input as one piece of text; `Engine.name`, which is what the host calls itself
— `kest`, from this one; `Engine.decide`, which `examples/embed.kest` asks for
and which this host answers with 1; `Engine.rank`, which the same program hands
a `Point` and which answers with its three numbers added up, and which is there
because a crossing handed a shape is the one a host gets wrong by reading the
right number of bytes in the wrong order; `Engine.hurt`, which the same program
hands an `Event` and which answers with the tag, because a host that kept no
layout can read the one slot of a value with a tag in it whose kind the tag does
not decide and no others; `Engine.blame`, which the same program asks for an
`Event` and which answers with the tag every enum that has a case has, for the
same reason and in the other direction; `Engine.who`, which the same program
asks for a name and a number and which answers with the name this host calls
itself, made into text the machine owns; `Host.sqrt`, `Host.write` and `Host.clock`,
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
machine has to own that, so they reach its heap — and `Engine.who` does as well,
because the name in the shape it answers with is text like any other. The other
eight answer with a number or with a value that has a tag in it, or take one,
and reach nothing: neither is anything the machine owns. A program that promises for one of the
first three is told at the call, by the machine, which measures rather than
believes:

```
error[K0631]: `Engine.name` promises `no.alloc` and this host took 5 bytes in it
```

The machine measures one other thing a host does inside a call, for the same
reason: a crossing that answers a value with a tag in it is answered by a host
writing the tag, and every slot after a tag means whatever the tag says. A tag
the enum has no case for is a payload nobody wrote, and the program would read it
and have no way to doubt it — so it is read where it is answered, which is the
one moment anything can:

```
error[K0650]: `Engine.blame` answers with a tag in slot 0 and 4 is no case of it
```

Everything else a host can be wrong about at this boundary is settled before
anything runs. This one cannot be: the tag is decided inside the call.

A tag going the other way is read at the door. A frame is full before `kest_call`
runs anything, so a host that writes a tag into one is told there rather than at
the instruction that meets it, in the same walk that holds the text and the
handles it was handed:

```
error[K0636]: `damageOf` takes a tag in slot 0 and 4 is no case of it
```

Writing one back is the other half. A value with a tag in it written into memory
is the same bytes for the same value: what the case does not carry is written as
nought, so a narrow case put over a wide one leaves nothing of the wide one under
the new tag. A host reading by the tag never saw the difference; one comparing two
values, hashing them or writing them out saw two where the program had put one.

`KEST_L_REF` is a place in a store, which is not a machine word at all: a
reference is the slot it names and how many times that slot has been handed out,
packed into one whole number, and a host reads and writes it through `integer`.
It said `KEST_L_WORD` until it said this, and the answer that came with that —
read it through `text` or `object` — was a pointer made out of a number nobody
meant as one. A store and a place in one are eight bytes each, so
`healthOf(world: store<Npc>, who: ref<Npc>)` and a function taking two arrays
were two machine words either way, and a host handing two handles where a store
and a place were wanted said a frame that agreed with itself.

`KEST_L_HELD` is the byte an optional keeps after its value, saying whether the
value is there. One byte, read and written as a whole number, and the same reason
as the tag: `struct { at: i32?, n: i32 }` and a struct of a number, a `bool` and
a number are one run of pieces — same kinds, same offsets, same size — so a host
could lend either under the other's name and be told nothing. The middle piece is
what tells them apart:

```
error[K0634]: `marking` takes `held` in slot 1 and this host says `u8`
```

`KEST_L_BOOL` is the third of the same byte and was the last one hiding in
`KEST_L_U8`. A `u8` holds nought to 255 and a truth holds one of two, so a host
writing 7 into one wrote a value the program reads as true where it asks `if`
and as neither where it asks `== true` — which is a program told two different
things about one slot. A host writes 0 or 1, and one that writes anything else
is told at the call.

The other thing with a flag beside it is written the same way. An optional is a
value and a byte saying whether the value is there, and an empty one is that byte
set to nought with nought under it — so two empty ones of a type are two of the
same bytes, whatever the memory held before. The compiler is what makes that
true: `none` is as many slots of nothing as the value takes, and a flag that says
so.

That promise is about the value, not about the memory around it. The padding a C
compiler leaves between the fields of a struct is nobody's to read and nothing
writes it — it is bytes no field names, where a case's unused payload and an
empty optional's value are bytes another reading does.

What a crossing answers with is read the same way. A host writing back a piece of
text or a handle is writing something the program may keep, and what it keeps
outlives the call it came from — so text the machine did not make and a handle it
did not hand out are refused where they are answered:

```
error[K0652]: `Engine.name` answers with text in slot 0 that did not come from this machine
```

It is the same walk, saying what a crossing did rather than what a function
takes, so it reaches inside a shape at that end too: a crossing answering with
`Npc { name: text, health: i32 }` has the name in it read the way a name handed
over on its own is. `kest_text` is what makes a host's bytes the machine's, and
what it answers is what to write back. A `ref` needs none of this: it is a
number, and the stamp in it is read where it is used.

All of that is read by what an argument is rather than by what its first piece
is. A shape with a piece of text in a field is a word and whatever else it holds,
and the word is the machine's to own the same as one handed over on its own:

```
error[K0636]: `greets` takes text in slot 0 and this did not come from this machine
```

Whether an argument holds anything of the sort is a walk of its pieces, not of
its type: a word is a handle, text is bytes, a tag is a tag, and every other
kind is a number in a slot — which is whatever the host put there. An argument of numbers
costs the walk that says so and nothing else.

Which kind of handle a slot holds is read at the door as well. Both kinds begin
with what they are, so a store handed where an array was wanted is named where it
was handed rather than at the instruction that walks it:

```
error[K0636]: `worn` takes an array in slot 0 and this host handed a store
```

Nothing a correct program can write reaches the machine's own reading of those
four bytes now; what is left of it guards against a compiler that has agreed an
array is a store.

A lend is the other way a shape crosses, and there neither reading applies: the
bytes are the host's own and it goes on writing to them, so a tag held at the
lend is a promise about a moment that has passed. It is read where the program
reads it — the one place the bytes and the type are in one hand:

```
error[K0651]: `Event` here holds tag 7 and has no such case
```

at the line that read it. The payload slots beside such a tag come back as
nought, which is what makes the value readable at all and is not what makes it a
value of that type: a `match` over it has no arm to take, and taking none is not
the same as taking one. That is the whole of it — nothing walks a lend to look
for tags, because a walk of one says what was true when it ran.

Both readings are a walk of the tags in what crosses, wherever they are. A struct
with one inside it has its tag where the fields in front of it end, and that one
is read the same as the tag of a value that is an enum — `KEST_L_TAG` is what
makes the walk possible, and before it there was nothing to walk for. An argument
that holds no tag costs the comparison that says so.

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
`std.sort` sorts with gaps rather than by plain insertion — in place, allocating
nothing, recursing nowhere — because plain insertion is quadratic and four
thousand numbers out of order cost two hundred and sixty million instructions,
which is most of a second. It is 3.7 times dearer on a run that is already
nearly in order and 247 times cheaper on one that is reversed, and neither end
of that is a dropped frame. See D1052.

A table keeps room for what it has held: taking ninety-nine thousand pairs out
of a hundred thousand gives nothing back, because a table that shrank on every
removal would make a frame's cost depend on what that frame took out.
`table.compact(t)` is the cold path that gives it back, and it answers a new
table — `by = table.compact(by)` — because a table is a value and replacing a
field of it replaces the copy's handle. A table that held a hundred thousand
and now holds a thousand keeps 2,121,728 bytes a fresh one does not; compacted,
it keeps 8,192.

`std.table` is a hash table, made by `table.empty()`, whose pairs are walked by
their places: `table.keyAt(t, at)` and `table.valueAt(t, at)` for `at` under
`table.count(t)`, which copy nothing. What it holds them in is four arrays that
are its own, so nothing outside can be handed one — a sort that moved a key and
not the value beside it would leave a table answering about one key with
another key's value, and that used to be two lines of ordinary Kest away.
`table.keysOf` gives the keys copied, for a program that wants an order of its
own. `std.vec` is two and three components of
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

There is no name for the absence of a value. A function that gives nothing back
is written `fn f() { }`, with no `->`, and that is the only way to write it:
`fn f() -> void` is refused, because a second spelling of one thing is what this
language does not have. Nothing else can hold one either — a field, a parameter
and a binding all name something there is.

A `let` gives its value where it is written, and what it is given has to be a
value: a call that gives nothing back names nothing, and `let a = note(1)` is
refused rather than leaving a name that cannot be read. There is no declaring a
name now and filling it in later either, so no name is ever read before it
holds something and nothing has to be tracked to know that. The type is the one part that may be
left out, because the value says what it is: `let a = 0` and `let a: i32 = 0`
are the same binding, and `let a: i32` is refused.

Conditions take no parentheses. `if x < 3 { }` is the only spelling; `if (x <
3) { }` is refused, because `(x < 3)` is a redundant grouping the formatter
would strip and the strict parser does not accept two spellings of one thing.

Blocks are braces, always, including single-statement bodies. A block is also a
statement on its own, which is how a name is given a life shorter than the
function it is in — and how a `defer` is made to run before the end:

```kest
let total = 0
{
    let held = costOf(world)
    defer release(world)
    total += held
}

io.print("{total}")
```

`defer f(x)` runs `f(x)` when the block it is in ends, however the program ends
it: off the end, through a `return`, through a `break` or a `continue`. Several
of them run in the reverse of the order they were written, because what was
taken last is given back first:

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

**A fault is not an end, and this is not `finally`.** A machine that divides by
nought or reads past an array stops there: `kest_call` answers false, the host
is told what happened, and no `defer` runs. The reason is that a deferred call
can fault too, and running them during a fault needs a rule for a fault inside
a fault — which is the exception machinery this language does not have. What
the machine owns goes with the machine, so nothing of the program's leaks: the
heap is thrown away with it and a `scratch { }` block open at a fault goes with
it. What can be left unbalanced is a host's own state, taken through an
`extern` and given back through a deferred one — the `Host.write("[")` above
would leave its bracket open. A host closes what it paired when a call answers
false, the same way it would for any other refusal. See D1040.

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
`||` and `!`, which say what they mean about one bit:

```kest
let both = flags & wanted
let either = flags | wanted
let apart = flags ^ wanted
let rest = ~flags
let neither = !ready && !waiting
```

A shift takes a value
and a count, and the count is an integer of any width, the way an index is.
`>>` brings the sign in on a signed type and nought on an unsigned one. A left
shift wraps at the declared width like every other arithmetic, and a negative
count is a failure with a message.

A count at or past the width is where C stops having an answer and this one has
it: everything is shifted out, so `1 << 64` is nought and `-8 >> 64` is -1,
which is what the sign says and what a shift of sixty-three then one more would
have given. It is the declared width and not the slot's: a `u8` of 200 shifted
nine either way is nought, and an `i8` of -8 shifted right nine is -1. D018 is the rule — match C where C has an answer, and answer where
it has none. A constant is worked out with the same answers, and a count below
nought is refused where it is written, because that is where the machine stops.

A name, a field or an element may be assigned to, with `=` or with one of the
four that work the value out first. There is no `%=`, `&=` or the rest of them:
four are what a program written here reaches for, and a fifth that appears once
in a file is written out.

```kest
count = 1
count += 2
count -= 1
count *= 4
count /= 2
```

`x += y` is `x = x + y` and not a second arithmetic. They compile to the same
instructions and answer the same for the values at the ends of a width, which
is where two paths through a compiler part company if they are going to:
`x /= -1` on the least `i32` is the least `i32`, the same as `x = x / -1`.

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

What a hole holds is a value that can write itself, which is the same list that
compares: a number, a truth, text, a set of bits, an enum, a struct, `[T; N]`,
and an optional of any of those. A value laid out flat is written the way a
program writes one — the name and the fields for a struct, the elements in
brackets for a run, a text inside quoted because the bytes on their own do not
say where a field ends:

```text
Card("ace", 1, Vec2(0.5, 2.0))
[1, 2, 3]
Door.Named("side", 1.5)
```

An array, a reference, a store and a function value have none, and neither does
anything holding one: what a handle means as text is what is behind it, and
reaching through one is a different question. One of them in a hole is refused
with the type named:

```
error[K0324]: there is no text for `[i32]`
```

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
the ones a string has — `\n`, `\t`, `\r`, `\\`, `\"`, `\{`, `\}`, `\0`, `\u{...}` — so a byte
written in a string and a byte written on its own are one spelling. The last of
them stands for a character rather than for a byte, so it is a byte literal
only when the character is one: `'\u{41}'` is `A` and `'\u{a0}'` is two bytes and
is refused, which is the answer `'ı'` gets.

All of them are allowed inside text, `\0` with the rest. Text carries how many
bytes it is rather than ending at the first nought, so a nought in the middle
of a piece of text is a character in the middle of a piece of text and `len`
counts it. `'\0'` on its own is a `u8` of nought and is the same byte. See D971.

`\u{...}` is a character written by its number: one to six hexadecimal digits,
up to `U+10FFFF`, written out as the UTF-8 it is. It is there because a file
may not hold every character a program has to emit. A mark with no width, a
space that is not the space and a mark saying which way to read are refused
where they are written — a file that looks like one thing and is another is
where a reader and this compiler part company — and some of them are part of
how a language is spelled. The mark between the halves of a Persian verb is
one; the space French typography puts in front of a question mark is another.

```kest
import std.io

fn main() -> i32 {
    // A zero-width non-joiner, which is part of the word and is invisible.
    let verb = "\u{645}\u{6cc}\u{200c}\u{631}\u{648}\u{645}"
    io.print("{len(verb)} bytes")
    return 0
}
```

```text
13 bytes
```

A number that is not a character is refused:

```
error[K0110]: `U+D800` is not a character
      characters run up to `U+10FFFF`, and `U+D800` to `U+DFFF` are not among them
```

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
applies to, because a type that compares has one and a type that does not has
neither.

**A value laid out flat compares when everything in it compares.** That is
integers, floats, `bool`, text and a set of bits; a struct, when every field
does; `[T; N]`, when `T` does; and an enum, when everything its cases carry
does. An array, a store and a function value never compare, however they are
reached: two arrays are equal when they hold the same things, and comparing the
handles answers a different question — so a struct holding a `[T]` is refused,
and the refusal names the field's type rather than the struct's.

**A `ref<T>` compares, and it is the one handle that does.** It is an identity
rather than a way to reach something: a place and a stamp, and the stamp is the
build's own counter, so no two places in any two stores of any two machines from
one build are ever stamped alike. Two references are equal when they are the
same handout — the same entity, and still the entity it was. A reference to a
place that was removed is not equal to the reference the same place is handed
out under next, and one from another store is not equal to anything in this one.
That is exactly the question a program asks about a reference, and it is the
only one it can ask.

Reading both and comparing what they name is a different question and the wrong
answer to this one: two settlers with the same fields are one settler under that
reading, and one settler whose hunger went up is two. A reference hashes for the
same reason it compares, so a `Table<ref<T>, V>` is a way to hang something off
an entity without putting it in the entity. Writing one out follows the same line and
for the same reason: what a handle says as text is what is behind it, which is a
question about reaching through rather than about the value.

What that buys is that a struct is a key. `std.table` wants `hash` and `==` of
a key and nothing else, so `Table<At, text>` over a `struct At { x: i32  y: i32 }`
holds a world by its places: `At(1, 2)` is the key rather than a handle to one,
and two of them holding the same numbers are one key.

What a key has to be is *one* key, and a value that is not equal to itself is
not one: a float that is not a number, or a shape holding one, is a different
key every time it is handed over. A table given one would count a pair nothing
could ever find and could never take it out again, so it is not given one —
`table.set` with such a key leaves the table as it found it, and what a table
counts is what it can find. `math.isNumber` is the question to ask before it
gets there.

A program that wants some of the fields rather than all of them folds the ones
it means with `std.hash`:

```kest
import std.hash

struct Item {
    id: i32
    price: i32
}

fn mark(item: Item) -> u64 no.alloc {
    return hash.join(hash.join(hash.start, hash(item.id)), hash(item.price))
}
```

`hash.start` is where a fold begins and `hash.join(mark, one)` folds one number
into it, low byte first. It is the same arithmetic and the same order this
compiler folds a program made of several files with, which is what makes it the
one to use: two programs folding the same fields with it get the same number,
and two folding them each their own way get two answers to one question.

What it answers is the same number on every machine and in every version of this
compiler. That is a promise rather than an accident: a program that writes a hash
into a save file, a replay that compares one machine's numbers with another's,
and a table walked in the order its hashes put it in are all things that work
only while the number does not move. For text it is FNV-1a over the bytes, which
is the same arithmetic and the same number a build answers with for a file
holding exactly those bytes — so a host may compute one without running the
program. For the other types the arithmetic is this compiler's own and is not
written here, and it is as fixed as this one.

```kest
import std.io

enum Kind {
    Idle
    Moving(i32)
}

fn main() -> i32 {
    io.print("{hash("kest")}")
    io.print("{hash(1)}")
    io.print("{hash(Kind.Moving(3))}")
    return 0
}
```

```text
6357821359474220922
12994781566227106604
15983665745253778074
```

Those three numbers are run and held every time this project is checked, which
is what makes the promise above a thing rather than a sentence. A `bool` hashes
as the number it is, so `hash(true)` is `hash(1)`.

What the promise costs is that nothing is salted: somebody who knows a program's
keys are hashed this way can choose keys that land in one place and make a table
walk a list. A program taking keys from somebody it does not trust hashes them
with something of its own first — the language gives the number, and what to do
about an opponent is the program's.

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
holds, what a host lends, and anything at all: a run of bytes is whatever it
holds and text is UTF-8, so the two are different things and this language says
which it means. `std.text` has `bytes(t)` for taking a piece of text apart and
`text(a)` puts one back together, refusing an array that is not UTF-8 — which
is the one walk either way costs, and the one place it is paid for:

```
error[K0604]: byte 1 begins no character, and text is UTF-8
```

A nought goes through: it is a character, and text counts it like any other.

`fit(xs, value)` is `push` with the growth taken out: it writes where there is
room, answers `false` where there is not, and never reaches the heap. That is
what makes filling a buffer something a `no.alloc` body can do, and `push`
never can — a `push` may double the block, and a promise cannot be kept by
hoping it does not.

```kest
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
```

`clear` keeps the room it took, so a buffer made once with `array(n, v)` and
cleared each turn is a buffer that is filled for the rest of the program's life
without asking for anything. `std.text`'s `fitting` and `fittingNumber` write a
piece of text and a whole number into one, so a tick can build a line and
promise it reached no heap.

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

A walk over text is a walk over what the name held when the walk began: the
handle is taken and the length measured once, before the first turn, so a body
that writes the name walks on over what it was given. It is the same rule D053
wrote for an array, where a body that pushes cannot lengthen what it is
walking. The byte itself is read without asking whether the place is there,
which is the one read in this language that does not ask — and what makes that
right is the measurement the walk did before it started. A build that checks
itself asks anyway, and says `K0645` if a walk ever reads past what it
measured, because nothing else could: the byte after a piece of text is a byte
the arena handed out for something else.

Gathering the bytes reaches the heap, because the array grows; what it does not
do is copy what is already gathered every time something is added, and the
piece of text is paid for once at the end. `examples/embed.c` says what that is
worth: six hundred bytes of text built a piece at a time takes 180900 bytes of
heap, and gathered as bytes takes 1680. `call --json` says `heap` for the one
call it makes, which is how `tools/check-costs.sh` asks every library function
that makes text what twice as much costs. That is why `std.text` writes `join`,
`repeat`, `upper` and `lower` this way rather than out of `slice`, which copies
the whole of what it is given at every step. An array that is not UTF-8 is
refused at run time, because text is UTF-8 and a run of bytes is not.

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

`scratch` is not one: it opens a block of working memory in front of a brace
and is a name everywhere else. Nor is `own`, which marks a struct field in
front of a name and a colon — `own keys: [K]` — and is a name everywhere else,
so `own: i32` is still a field called `own`.

`type` is not one either, and is a name like any other. A word kept back for a
feature nobody has designed is a promise, and `flags` is how this language
takes a word back when it needs one: where a declaration begins it declares,
and everywhere else it is what somebody called their field.

## Types

Signatures declare types. Bodies infer them.

```kest
fn scale(v: Vec3, k: f32) -> Vec3 {
    // inferred f32
    let x = v.x * k
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

`/` and `%` go together: what a division leaves over is what `%` gives, and
the two answer the same way at every end of a width.

```kest
let over = total / many
let left = total % many
```

They go together for a float as well. What `7.5 % 2.0` leaves over is `1.5`,
and what is left over carries the sign of the number being divided rather than
the sign of what it is divided by, which is what a whole number already does:
`-7.5 % 2.0` is `-1.5`.

```kest
let wrapped = angle % 360.0
```

The least whole number divided by minus one is the other place C has no
answer. There is one number it cannot be — the answer is one past the top of
the width — so it wraps to itself, the way every other arithmetic at the end of
a width wraps, and the remainder beside it is nought. A constant is
worked out the same way, which is the one place in this arithmetic where C does
not merely have no answer but takes the program down for asking. That is D018's second
half: answer where C has none, and answer the way the rest of the language
already does.

Dividing by nought is two different things. A whole number has no answer, so it
is `K0601` and the program stops; a float has one and it is the one C has, an
infinity with a sign, or not a number when nought is divided by nought. That is
D018 again: match C where C has an answer. `%` by nought is the same two
things: `K0601` for a whole number, and not a number for a float, which is
again the answer C has.

The same two things where a constant is worked out. A whole number divided by
nought is `K0504` there, because nothing can be written down for it; a float is
worked out and is the answer above, because the machine would have given that
answer and not stopped. Two arithmetics, one promise.

A whole number written inside a conversion is a number of that type when it
fits — `i64(9223372036854775807)` is that number, not an `i32` too small to
hold it — and a narrowing when it does not: `i8(300)` is 44, which is what
D018 says about a value that will not fit.

A conversion to a type that already holds every value of what it is converted
from does nothing at all: `i32` of an `i16`, `u16` of a `u8`, `u8` of a `bool`.
There is nothing to cut, so no instruction is written for it. A conversion to
one that does not — `i16` of an `i32`, or `i32` of a `u32`, where the values are
in range for the width and not for the sign — is the cut above. Which of the two
a conversion is, is read off the two types and not off the value.

Those are two questions that look like one. `let x: u8 = 300` says this number
is a `u8`, and it is not, so it is refused. `u8(300)` says make me a `u8` out
of this number, and what that keeps is what a `u8` has room for. The first is
about what something is; the second is a thing being done to it, and the
answer to both is written where somebody can read it.

A hole in a string writes those as `inf`, `-inf` and `nan`. They are the one
thing this language prints that it cannot read back, because there is no way to
write them: a program that wants one divides, and a program that wants to name
one divides in a constant.

```kest
const FURTHEST: f64 = 1.0 / 0.0
```
 Not a number has one spelling
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
truth or a piece of text, arithmetic on those and on other constants, a
conversion of one, a struct built out of them, a case of an enum with what it
carries, that many of something written where it stands, and the two builtins
whose answers cannot be anything else — `len` of a run whose size the type says
and `hash` of a value that compares.

What it is not is a choice. A `match` or an `if` that picks between two values is
made while running, so a constant that wanted one is two constants and a program
that picks between them.

The refusal says which of two things happened, because they are not the same news
and a tool reading them acts on each differently. `K0504` is a constant nobody
could work out — made of itself, divided by nought — and is a mistake where it is
written. `K0510` is a constant that asked for something this language makes while
running: a choice, a call into a program, a piece of text with a hole in it. The
first is fixed where it stands and the second is written another way.

```kest
const WIDTH: i32 = 16
const CELLS: i32 = WIDTH * 9
const MASK: u8 = 1 << 3
const ORIGIN: Vec2 = Vec2(0.0, 0.0)
const WEIGHTS: [f32; 3] = [1.0, 0.5, 0.25]
```

Its type is written, unlike a `let`'s. A `const` is a name that crosses out of
the file it is in, and D005 declares at every boundary rather than inferring
across one: `const N = 1` is refused where `let n = 1` is not.

A constant is a value like any other where it is used: `array(CELLS, 0)` counts
with it while running and `[i32; CELLS]` counts with it while compiling, and it
is the same number in both. A count may name one from another module —
`[Npc; npc.PARTY]` — which is the same name with a dot in it that the type
beside it is. It is answered by finding the file that module is rather than by
looking the name up, because a type is resolved before the constants are
symbols.

A conversion is worked out there too: `const LOW: i32 = i32(WIDE)` cuts where it
is written, and `const THIRD: f32 = f32(1.0 / 3.0)` rounds there, each the same
way the machine would have. A case of an enum is a value like any other, so
`const SHUT: Door = Door.Shut` and `const LOCKED: Door = Door.Locked(7)` are
worked out there as well — the tag and what the case carries. So are the two
builtins whose answers cannot be anything else: `len` of a run whose size the
type says, and `hash` of any value that compares — which this language promises
does not move, so a program may have a table of them before it starts. What a constant
cannot hold is a call into a program: that is where working out stops.

A piece of text with a hole in it is not a constant, because filling a hole is
what the machine does and a constant is worked out before there is a machine.
A hole with nothing in it is refused where it is written, because there is
nothing to fill it with:

```
error[K0207]: this hole is empty
```

It costs one instruction to push wherever it is used, because the working out
happens once and at compile time. It wraps at its declared width the way the
same arithmetic wraps while running, so `const NARROW: i8 = 120 + 10` is -126
and says so. Dividing by nought and a constant made out of itself are refused
where the constant is used, each saying which of the two it was.

## Values and references

A `struct` is a value. It lives where its frame does. A temporary is moved
rather than copied. A value passed to a function that neither keeps it nor
writes through it is lent, and costs nothing.

What a struct holds is a value in the same way, except a handle: an array field
is a handle to what it names, and a struct copied field by field copies the
handle and not what is behind it. A shape that holds more than one of them holds
them in step — `keys[i]` beside `values[i]` — and a program that writes one of
them writes the shape, whichever copy of the struct it has: a handle handed out
is a handle written through.

**A field may be the module's own.** Written `own`, it can only be named from
inside the module that declared the shape — read, written, or given to the
shape being built — so nothing outside can be handed the handle in the first
place:

```kest
struct Table<K, V> {
    own keys: [K]
    own values: [V]
}
```

Reading is what is refused and not writing, because reading is the whole of it:
`pop(t.keys)` never writes the field, it reads it and changes what it names
through the handle it got. A shape whose every field is `own` is one only its
own module can build, which is what an opaque type is without a second word for
it. `own` is a word and not a keyword, so a field called `own` still works, and
none of it reaches the machine: a field is laid out where it was laid out and a
host reads the bytes it read.

```
error[K0365]: `keys` is `std.table`'s own
   |
10 |     let gone = pop(by.keys)
   |                       ^^^^ what reaches it is what `std.table` declares
```

A shape that keeps something in step and does not keep its fields says so where
it is declared.

**The mistake this makes easiest.** A function handed a struct is handed a
copy, so writing to a field of it writes into the copy:

```kest
struct Actor {
    hp: i32
    bag: [i32]
}

fn hurt(one: Actor) {
    one.hp -= 1
    push(one.bag, 1)
}
```

`one.hp -= 1` changes nothing the caller will ever see. `push(one.bag, 1)` does
— the array is a handle and there is one of it — so half of that function works
and half of it quietly does not. Nothing refuses it, because both lines are
exactly what they say.

What to write instead is the function answering with the value:

```kest
fn hurt(one: Actor) -> Actor {
    one.hp -= 1
    return one
}
```

and the caller writing it back. Three of the programs written to measure this
language were caught by the first shape, each time by a check of their own that
failed rather than by a wrong number, and it is the one thing a reader coming
from a language where an object is a reference should expect to get wrong once.

`ref<T>` is a handle into managed or host storage. It can go stale, because
something else may delete the target, so reading through it is a lookup that
can fail rather than a dereference. The failure cannot be ignored.

Stale stays stale. A reference is a slot and a stamp, so one to something
removed reads nothing even after the slot has been taken back by something
added later — a slot map without the second number would answer with whoever
moved in. Writing through it and removing through it say no for the same
reason, and `examples/quests.kest` checks all three.

The stamp comes from the process, and no two places in any two stores of any
two machines of it are ever stamped the same. A reference therefore says which
store it came from without carrying one: handed to another store of the same
shape, it names a place stamped by something else and reads nothing. The same
one number answers three questions at once — another machine's reference,
a reference from a machine that has been freed, and this store's own from
before the place was given back are all a number this place was never stamped
with. What runs out is how many places the process has handed out altogether,
which is 1099511627775, and running out is a message rather than a stamp handed
out twice.

A walk over a store steps over its dead slots, so what it costs is how far the
store has ever reached rather than how much is in it — with one exception: a
store with nothing left in it goes back to reaching nothing, because everything
a walk would step over is dead. What each slot has counted is kept, so filling
it again hands back the same slots with new counts and every reference from
before is as stale as it was.

A reference is one value: forty bits of handout number beside twenty-four bits
of place. The number is never handed out twice, so it cannot come round onto a
place that is still live — which is what the number before it did, in sixteen
bits of a count of machines, and what D1033 is about. What that costs is a
ceiling on handouts rather than a ceiling on any one place: a program that has
spent all of them is refused at the `add` that asked, with a message saying so,
and stale stays stale for as long as the process runs.

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

**An element of an array is a place**, so `world[at].x = 1.0` and
`world[at].x += world[at].dx` write where the element is rather than into a
copy. An element of a store is not: it is reached through `get`, which answers
`T?` because a reference can be stale, and put back with `set`. That asymmetry
is deliberate and it is measured. Reading a whole element into a local, changing
it and writing it back is the *faster* of the two shapes wherever more than one
field is touched — 65.4 ms against 83.6 ms over twenty thousand elements and
fifty rounds, changing four fields of seven, because each `world[at].field`
resolves the element again and pays its own bounds check while a copy amortises
one move over every field. So the store's missing place form would buy nothing
on the shape that wants it. See D1038.

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
if let last = pop(queue) {
}
clear(queue)
```

`remove` shifts, so it costs what the shift costs, and that is on the call
rather than hidden. When the order does not matter, an array is the wrong
container: a store hands out references that survive a removal, and removing
from one costs nothing.

`clear` keeps the room it took, so `array(n, v)` and `clear` are together what
`store(n)` is on its own: room for `n` and nothing in it. `room(xs, n)` is the
same thing said to one that already exists — room for `n` without changing what
is in it or how many there are — which is what a program calls when it has a
container it cannot replace. It takes either of the two things here that grow,
an array or a store, because `array(n, v)` and `store(n)` are the same sentence
said at the making and this is it said afterwards. A table's keys and its values are two of those, so
`table.refill` is written on it: a pair put into a table told how many are
coming costs nothing of the heap, and one put into a table that grows into them
costs fifty-one bytes.

```kest
let xs: [i32] = array()
room(xs, 1000)
```

Asking for less than it holds asks for nothing, and it is the capacity rather
than the length: `len` after one of these says what it said before. An array
grows by doubling, and it grows where it stands when the place it is in has
the room for it — a run of a hundred and twenty-eight bytes sits in a place of
two hundred and fifty-six, so the step up to two hundred and fifty-six costs
nothing and moves nothing. What asking for room saves is the overshoot, since
a thousand pushed without asking ends up with room for 1024. When it does not
fit, a growth takes a new run and copies, and the run it came from is given
back the next time the machine walks what it can still reach (D996).
`examples/embed.c` prints both numbers. There is no third spelling for asking,
because two lines already say it:

```kest
let seen: [i32] = array(1000, 0)
clear(seen)
```

What each of the three containers costs a frame is a number rather than a
sentence about doubling, and these are the numbers: what one more entity a step
is handed, measured by `check-costs.sh` over a tick and held there, so a
reading of this table that the program disagrees with is a gate that fails. It
is what a step was handed and not what it is holding afterwards — a frame that
makes a name and lets it go holds nothing at the end of it and paid for every
one, and paying is what a frame budget is about.

| a step that puts an entity into | told nothing | told how many |
| --- | --- | --- |
| a piece of text | 32 bytes an entity | — |
| an array | 51 bytes an entity | 0 bytes an entity |
| a table | 76 bytes an entity | 0 bytes an entity |
| a store | 115 bytes an entity | 0 bytes an entity |

These are bytes, and a byte count is this machine's as much as the program's: a
handle is a machine word and a header is made of them, so the table above is
what a container costs on the machine this was read on. What is the same count
anywhere is the instructions, which is why the frame step further down is held
to its count and these are held to what a tick on this machine says.

A frame that promises `no.alloc` is 0 bytes an entity, which is what the promise
means read from outside it. Being told is worth everything: the room is made
once, before the frame, and the frame pays for nothing. A store is the dearest
because it grows four runs at once — what it holds, what each has counted, which
are live and which are free — and a table is dearer than an array because it
grows three. Which are live is a bit a slot rather than a byte, which is why a
walk of one reads sixty-four slots at a time; what each place has been stamped
with is eight bytes rather than four, which is why a store is thirteen bytes an
entity dearer than it was, and D1033 is what those eight bytes bought.

Not everything about a cost here is a number this project measures. `remove`
from an array shifts what comes after it and `remove` from a store does not:
that is a difference in what one instruction does rather than in how many run or
how much is asked of the heap, so neither of the two things this gate can count
can see it. It is a claim about the algorithm, true by reading the code, and
nobody's measurement.

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

A loop is left with `break` and its turn is ended with `continue`, and both
belong to the loop they are written in:

```kest
for i in 0..len(a) {
    if a[i] == 0 {
        continue
    }
    if a[i] < 0 {
        break
    }
    total += a[i]
}
```

Both ends are one type, a literal at one end takes the type of the other, and
the end is worked out once rather than every turn. A count is a way to write a
walk and not a value, so `0..n` is written where a walk is asked for and
nowhere else.

Assigning to the name a walk binds is allowed and changes nothing that outlives
the turn — the walk keeps its count where nothing can name it, and warns that
the write is discarded. Where nothing in the body assigns to it, that name *is*
the count: the copy is a defence against a write that is not there, and a hop of
a `for` is one instruction rather than three. The same is true of the position
in `for i, x in a`. It is not something to write a program around — it is why
the cheapest loop this language has is the one that is written most.

`[T; 16]` is that many where it stands, rather than a handle to that many
elsewhere. It is a value like a struct: copying one copies all of it, and a
struct holding one holds the whole thing.

A struct with no fields is one slot wide, not none. It has a value the way
every other struct does — it is passed, stored, held in a constant and copied —
and a width of nothing would make it a value that is not there, which is a
different thing from a value with nothing in it.

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

An arm names a case. There are no arms for values: `1 ->` and `true ->` and
`"x" ->` are not written here, because what a `match` chooses between is the
cases of an enum and nothing else has a list of them to be exhausted. A number
has too many and text has more than that, and both are what `if` is for.

A `match` that leaves a case out is refused, and the message points at every
case it did not answer. `else` answers whatever is left, and is a written decision
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

**What a function says when it cannot do what it was asked.** There is no
exception here and there is no built-in `Result`. There are three shapes and
the difference between them is what a caller can do about it:

- `T?` when there is nothing more to say than that there is nothing. `find`,
  `get`, `os.read` and `text.number` all answer that way.
- `bool` when the answer is whether it worked and the caller does not branch on
  why. `room` answers that way.
- An enum when the caller decides differently for different reasons. It may
  take types — `enum Answer<T> { Held(T) Trouble(text) }` — so a program writes
  the shape once and a `match` makes the caller answer every case.

A function that chooses the first and has more to say is a function that threw
something away; one that chooses the third where there is one reason has made
a reader write an arm for nothing. What none of them does is stop the program:
a machine stops only where it met something it could not make sense of, and
that is a fault rather than a value.

Two values of an enum are equal when they are the same case carrying the same
things, so `door == Door.Locked(7)` asks what it looks like it asks. An enum
whose cases carry something that does not compare does not compare either, and
the refusal names what it was. It is the same rule a struct is held to and for
the same reason: both are values laid out flat, and what one is is what is in
it. `hash` covers the same ground, over the same parts, so the two cannot
disagree.

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
// Door.Locked(7)
io.print("{Door.Locked(7)}")
// Door.Named("gate")
io.print("{Door.Named("gate")}")
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
if state & State.Hurt == State() {
}
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
// State.Moving | State.Armed
io.print("{State.Moving | State.Armed}")
// State()
io.print("{State()}")
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

`if let` is how it is opened, and the name exists only inside the arm where the
value did:

```kest
if let item = find(stock, 7) {
    io.print("in stock")
} else {
    io.print("not carried")
}
```

`while let` is the same question asked every turn: the loop runs while there
is something and the name holds it.

A program that wants the answer and not the value asks for it:

```kest
if find(stock, 7) != none {
    io.print("in stock")
}
```

That is what a lookup in this library answers with, and it is the same answer
whatever is being looked in: the builtin `get` on a store gives what is there or
nothing, and `table.get` gives what is under a key or nothing. A lookup that
took a value to hand back *instead* would make a program build one to throw
away, and would make a miss read as a hit — `table.get(kinds, Kind.Tool, -1) !=
-1` is a program asking `!= none` in a number it hoped nothing else would use.

Asking what is there and saying what to use when there is nothing are two
questions, and the second is `table.orElse(counts, 7, 0)`. It is not a second
way to ask the first: one of them can answer *nothing* and the other cannot.

`== none` and `!= none` ask the byte beside the value and nothing else, which is
why they are the one comparison an optional answers: two optionals still do not
compare, because that is a question about what they hold and one of them may hold
nothing. `none` may be written on either side. Before this there was no way to
ask, so a program wrote `if let` and a name it never read — `text.contains` and
`table.has` in the library were four lines each and are one now.

An optional of an optional is two questions rather than one, and they stay
apart: the outer one is asked with `== none`, and what it holds is asked the
same way again. `Item??` is what a lookup that may not have been asked gives
back beside one that was asked and found nothing.

```kest
import std.io

fn main() -> i32 {
    let missing: i32? = none
    let holdsMissing: i32?? = missing
    let holdsNothing: i32?? = none

    io.print("{holdsMissing != none}")
    io.print("{holdsNothing != none}")
    if let held = holdsMissing {
        io.print("{held == none}")
    }
    return 0
}
```

which says

```text
true
false
true
```

A value becomes an optional where one is wanted, and it does that once: `1`
standing where a `i32??` is wanted is refused, because two conversions is the
sort of quiet reach this language does not make. `let outer: i32?? = inner`
is written with the `i32?` in hand.

What stands between `let` and `=` is a name and only a name. Neither of these
is a pattern: nothing is compared with what is held and nothing is taken apart,
so `if let 1 = door` and `if let Some(x) = door` are not written here. The
question either way is whether there is anything, and the answer is a name for
it. A `for` is the same: what it writes before `in` is a name for what comes
out, and a position before that.

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

**There are no closures, and what one would have captured is written beside
the function.** A predicate that needs a threshold takes the threshold; a
helper that takes a predicate takes what the predicate needs, which may be any
type:

```kest
fn countIf<T, C>(items: [T], with: C, keep: fn(T, C) -> bool no.alloc) -> i32 no.alloc {
    let seen = 0
    for one in items {
        if keep(one, with) {
            seen += 1
        }
    }
    return seen
}
```

and `sort.byWith(npcs, player, nearer)` is that in the library. What to run
later is a struct holding the function and what it needs, which is a closure
written out. The cost of not having them is a parameter; the cost of having
them is a capture mode, a lifetime, an environment on the heap that a walk has
to follow and a `no.alloc` body cannot make, a rule for capturing out of a
`scratch { }` block, a value with no name for a reload to match, and a shape
the host boundary has none of. See D1051.

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

**What a name may be asked to do is written, and there are two of them.** A
body that compares two of what it was given, or puts one in order, says so on
the name:

```kest
fn firstAt<T: compares>(items: [T], want: T) -> i32 no.alloc {
    for at, one in items {
        if one == want {
            return at
        }
    }
    return -1
}
```

`compares` is `==` and `!=`, and `orders` is `<`, `>`, `<=` and `>=`. There is
no third. `hash` stands for exactly what compares, so a word for it would be a
second spelling of the first, and everything else — arithmetic, a field, an
index — is not a thing a type name may be asked to do at all: a body that adds
two of what it was given is a body written for numbers and declared for
anything.

These are written for the same reason `no.alloc` is written. A generic's body
is checked once, where it stands, against exactly what the declaration says its
names can do — so a generic nothing has called yet is still a body that has
been read — and a call that asks for a copy is refused where the type has not
got what was asked for:

```
error[K0366]: `sort.ascending` wants a `T` that orders, and `Card` does not
   |
13 |     sort.by(cards, sort.ascending)
   |                    ^^^^^^^^^^^^^^ there are as many orders as fields, so write the one you mean: `fn(Card, Card) -> bool`, handed to what sorts
```

with a note at the `T: orders` that asked. And a requirement nothing in the
body uses is a warning, the way an import nothing writes through is: it refuses
types that would have worked.

`compares` and `orders` are words rather than keywords. They stand after a
colon in a list of type names and are names everywhere else.

One that takes types may be handed over as well as called, and which copy it is
comes from where it is going:

```kest
fn ascending<T: orders>(a: T, b: T) -> bool no.alloc {
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

A struct is one of those, and on purpose. Two of one struct are equal in exactly
one way, so `==` is the language's to answer; which of them comes *first* is a
choice, and a struct with three fields has three orders before anybody argues
about direction. So there is no order on a struct and no way to declare one —
what sorts is told what comes first, and being told is a function value:

```kest
fn nearer(a: vec.Vec2, b: vec.Vec2) -> bool no.alloc no.host {
    return a.x < b.x
}

sort.by(points, nearer)
```

Four lines for the choice and one for the call, and the refusal says so where
somebody asked for the other thing: *there are as many orders as fields, so
write the one you mean: `fn(Card, Card) -> bool`, handed to what sorts.*

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

And an enum:

```kest
enum Answer<T> {
    Held(T)
    Trouble(text)
}
```

which is the shape an answer that can fail has, and the reason there is no
built-in one: `T?` says whether there is a thing and this says what happened
instead, and which of the two an API wants is the API's to decide. A case that
carries none of the names — `Trouble` here — says nothing about which copy it
is, so which copy comes from where the value is going, the way `array()` and
`store()` already work. `examples/boxes.kest` runs both.

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
builtins that grow or copy: `add`, `array()`, `push`, `room`, `store()`, and
`text` from bytes. Each says which of those it was. `slice` and `rest` are not
among them and have not been since D964 made a cut a place inside what it was
cut from and how many bytes of it. A run of a written length
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

There are three promises, and a word written where one goes that is none of
them is answered for rather than left to be a body that never turned up:

```
error[K0216]: `no.allocate` is not a promise this language has
 --> promise.kest:1:15
  |
1 | fn f() -> i32 no.allocate {
  |               ^^^^^^^^^^^ this language has `no.alloc`, `no.host` and `deterministic`
```

The same is said of a promise written with its first half left off and of one
written twice, and it is said in a function's type as well as after its
signature, because they are the same promises read through the same door.

`no.host` is the second: a function that promises it calls nothing the host
provides. It is proved the way `no.alloc` is and on a walk of its own, and what
reaches the host is one thing rather than a list — nothing a body writes crosses
out of a program, and the only way out is a call to an `extern`, which is a call
like any other. A foreign function reaches the host whatever it promises about
the heap.

```
error[K0401]: this calls the host, and `tick` promises `no.host`
 --> tick.kest:4:12
  |
4 |     return Host.now()
  |            ^^^^^^^^^^ `Host.now` is the host's
```

`deterministic` is the third, and it is the one written as a word rather than as
a `no`: it says what a body does — answer the same on every machine that keeps
the simulation profile — rather than what it does not. What it refuses is a
reach outside that profile, which today is a call to a door the host provides:

```
error[K0401]: this reaches outside the simulation profile, and `drifts` promises `deterministic`
 --> drifts.kest:6:12
  |
6 |     return Host.now()
  |            ^^^^^^^^^^ `drifts.Host.now` is the host's
```

That is the same refusal `no.host` gives about the same line, said in the words
of the promise that was made. The two are not one promise: `no.host` is about
where control goes and `deterministic` is about what comes back.

**An `extern` may declare itself inside the profile, and that is how they part
company.** A foreign body is not here to be read, so what it declares is the
only thing there is to go on — which is already how `no.alloc` works on one. An
`extern` that writes `deterministic` may be called by a body that promises it;
one that does not, may not. `std.math` declares `Math.sqrt`, `Math.floor` and
`Math.ceil` that way and `Math.sin`, `Math.cos`, `Math.pow` and `Math.atan2`
not, because IEEE 754 requires a square root to be correctly rounded and a
floor and a ceiling to be exact, and requires nothing at all of a sine. So
`math.sqrt` is `deterministic` and is not `no.host`, and a body that reaches it
is the same.

That is the one place the promise rests on somebody's word rather than on a
proof, and it rests on it in the same direction `no.alloc` does. What a host
owes is under *What is the same everywhere*. See D941, D942 and D1060.

The three are written after one signature in any order — `no.alloc no.host
deterministic` is how the library writes them — and each is written once. Each
is asked about on its own where a value goes somewhere: a value promising more
may go where one promising less is wanted, and one that promises the heap and
not the host is neither above nor below one that promises the other way.

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

A program cannot reach that check: the promise is part of the type, so a value
that does not promise cannot go where one that does is wanted. What reaches it
is a host. A function value is one slot holding which function it is, and a
host filling that slot writes a number — a number carries no promise, and the
machine asking the chunk is the only thing between a host that read the wrong
index and a program running something it was told would not allocate.

The shape is asked there too. Every function index is the same kind of thing in
a frame, so what tells one from another is what it takes and what it gives, and
a call through a value carries both — a number that names a function of another
shape is `K0657` at the call rather than a body reading the slots below the ones
it was given.

## What a program may do

A program can do exactly what the host bound and nothing else. There is no
ambient anything: no filesystem, no clock, no environment, no process, no
network. `io.print` reaches the outside because `std.io` declares
`extern fn Io.write(...)` and something bound it; a host that binds nothing has
a program that computes and answers.

A **capability is the receiver of an extern**. `extern fn Io.write(...)` is
under `Io`, `extern fn Clock.now()` is under `Clock`, and a bare
`extern fn tick()` is under the empty one. There is no second mechanism and no
list to keep in step: what a program may do is what was bound, so denying a
capability is not binding it.

What a host reads before it decides is `kest_build_capability`, which walks the
receivers once each, and `kest_build_extern`, which walks every name. Both are
asked of the build, which is what a host has before there is a machine, so the
decision is made before anything runs rather than one failed start at a time.

```
the program asks for the capability `Engine`
the program asks for the capability `Io`
```

**What this is and is not.** It is a boundary for code you wrote or code you
trust to be cooperative, inside a host that decides what it may reach. It is
**not** a sandbox for code that is trying to get out. A budget stops a program
that will not stop and a heap ceiling stops one that grows, and neither of those
is a threat model: running code you do not trust in your own process is a
different product, and it is Wasm or another process rather than this. The
reference says so here so that nobody reads the ceilings as a security claim.

What the compiler itself is bounded by, for source somebody else wrote, is
`--room` for the memory reading and compiling may take and the table below for
everything a program can have too many of. Compile time is not bounded and is
not claimed to be. See D981.

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
| 128 | expressions one inside another |
| 32 | `break`s in one loop, and 32 `continue`s |
| 32 | `defer`s in a function |
| 65535 | bytes of code a jump reaches, or a loop reaches back |
| 8 | `scratch { }` blocks one inside another in one body |
| 64 | `scratch { }` blocks one machine holds open at once |
| 8 | things one `match` chooses between at once |
| 256 | combinations one `match` answers, before it needs an `else` |
| 65535 | elements a `[T; N]` holds, and at least one |
| 65536 | names a program asks the host for |
| 2147483647 | elements an array or a store holds, and bytes in text |
| 16777215 | places in one store |
| 1099511627775 | times a process hands out a place, counting the ones taken back |

```
error[K0503]: this loop is 156012 bytes of code, and a loop reaches back 65535
error[K0503]: this jumps 156012 bytes of code, and a jump reaches 65535
error[K0502]: a function holds at most 256 names
error[K0502]: a loop holds at most 32 continues
```

All but the last three are the compiler's, found before a program runs. The
last is the machine's, because how many a program has is not a thing the
compiler can see coming, and it is one number rather than three: an array, a
store and text are counted by the same `len`, which gives back an `i32`. So are
the blocks a machine holds open: nesting in one body is lexical and refused
where it is written, and a body with a block in it that calls itself opens one a
call deep, which is a number met while running.

A program that runs into one of these is a program that would be worth reading
again anyway. They are here because a number a program can run into belongs
where somebody can read it, rather than only where it is enforced.

## The host boundary

There are three hosts in this tree and they are for three different readers.
`kest` is the command line, in `src/main.c`. `examples/embed.c` asks every door
this language has, one after another, and is long because the list is — it is
where a door is held to what it says, and it is what to read when one of them
does not do what this document says. `examples/engine.c` is the shape a host
has rather than a list of doors: it builds a program, asks it for a world, keeps
that world between frames, lends its own memory a frame at a time, spends a
budget, is refused by a door of its own, saves the world as numbers and builds
the program again under it without losing what it had. It is five hundred lines
and it is meant to be read start to finish by somebody writing a host of their
own. See D949.

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

The object says nothing the same way everywhere: `null` is there is none.
`answered` is null for a `main` that gives nothing back, `result` is null for a
call to a function that answers nothing, `gave` is null for a handler that gives
nothing, `lent` is null for events counted up from nought, and `slots` and
`frames` are null when there is no answer to what a program needs. A field is
there either way, so a reader can rely on it and cannot add it up.

What `check --json` says a function gives back is not that: it is the type,
under `gives`, and the type a function that gives nothing back has is `nothing`
— the language's own word for it. That field is a name and the ones above are
values, which is why they are two names.

`fmt --json` says one object a file: whether it was already in the one form,
what was wrong with it if anything was, and — for the run that answers with a
file rather than with a question about one — the file:

```json
{"schema": 2, "diagnostics": [], "errors": 0, "file": "doc.kest",
 "formed": false,
 "text": "module doc\n\nfn twice(n: i32) -> i32 {\n    return n * 2\n}\n",
 "edit": {"offset": 12, "length": 34, "line": 3, "column": 1,
          "text": "fn twice(n: i32) -> i32 {\n    return n * "}}
```

`formed` is null for a file that did not parse, false for one that is not in the
form yet, and true for one that is. `text` is there for a plain run and not for
`-w`, which put it in the file, or `--check`, which was asked a question rather
than for a file. Beside it is `edit`: where the file and the one form of it
differ, as one replacement — an offset and a length into the file that was read,
the line and column they are at, and what goes there. Everything before it and
everything after it is the same in both, so a tool that formats on save replaces
that much and its reader keeps their cursor. It is null for a file already in the
one form. A stream that is JSON and a file at once is neither; a string
inside an object is not that, and it is what an editor asking for a formatted
file reads.

`run --json` says what the program answered:

```json
{"schema": 2, "diagnostics": [], "errors": 0, "cost": 24495, "answered": 7}
```

`answered` is null for a `main` that gives nothing back, because nothing and
nought are two answers and an exit status says the same thing for both. The
words do not say it — what `run` writes is what the program wrote, and a number
of the command's own in the middle of that is a line nobody asked for — so a
person reads the status and a tool reads the object. They are the same number.

`call` is the one command whose answer is a value, and a value is read by a
shell — so the value goes where a shell reads it in both forms, and everything
else goes beside it. A function that prints while it works out what to answer
says both: the value on its own, and the program's writing on the other stream,
in words and in an object alike. What a reader of `$(kest call ...)` gets is the
value and nothing to strip off it.

Under `--json` the object is the answer and what the program wrote is beside it:
the object goes where a tool reads it, the program's own writing goes to the
other stream, and a refusal goes in the object rather than into the middle of
what the program was saying. A tick that worked has an empty `diagnostics` and a
tick that ran into something has it there, with the program's writing untouched
either way. In words the two are one stream on purpose — `tick` writes its
measurement where a reader looks for an answer, so what the program prints goes
to the other one, where it cannot be read as a number.

A command that worked says nothing. What the command line writes where the
complaints go is what is wrong with what it was given, so a run with nothing
wrong leaves that stream empty and a shell reading the answer reads an answer —
which is what the two streams are for. A warning is the one thing said there by
a command that worked, and it is said about the program rather than about the
run: a file with nothing to say about itself makes no noise at all.

Walking the copies of a name ends in silence, the same way a walk of what the
program asks a host for does. `kest_entry_of` hands back -1 past the last of
them and writes nothing down: a host walks that list for every name it looks up,
and most names are one function, so a machine that explained at the end of a
walk would answer every reading of a list with a complaint about the reading.
Asking for the name itself is the door that speaks — `kest_entry` on a name that
is several functions is refused and names the copies — and it is the same pair
one crossing over.

What a machine says about a frame that worked is nothing, which is the answer
more often than any refusal is. A report is what was said since it was last
asked, so a machine that spoke on a path that works would hand what it said to
whoever asked next, and the frame it landed on would not be the frame it came
from. `examples/embed.c` reads that silence where a host would: after a lend
made and read, after a thousand of them ended, after a heap thrown away.

The end of that walk is the one question of the four that says nothing. A name
that is not there is how a walk ends, so `kest_build_extern` hands back NULL and
writes nothing down; the other three — how many a function takes, what it takes,
what it gives back — answer past the end the way they answer about a real
function that takes nothing and gives nothing back, and say into the report
which it was. `examples/embed.c` asks each of them on its own and reads the
report after each, because three questions and one reading is a check that
cannot tell which of the three spoke.

`kest_host_find` answers what a name is bound to, or NULL, which is how a host
asks what it has already said rather than keeping a second list beside the one
the library keeps.

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
- The block a host lends is the host's own. An address the machine handed out
  is not: text a program gave a host is a pointer into the heap or into the
  build, and a lend is memory a program may write into, so lending those bytes
  back as a run of numbers would make the one thing this language says cannot
  be written into a thing that can — and a literal rewritten that way stays
  rewritten for every machine the build starts. It is refused in every build,
  because whose memory an address is is a thing the machine knows:

  ```
  error[K0653]: this host lent 9 `u8` at an address this machine owns
  ```

- What a program reaches is two numbers and not three. `kest_needs` and the two
  beside it answer the slots and the frames and write nought for the heap: what
  a program allocates is what it is given to work on, and a loop over four
  events and a loop over four thousand are the same program. Nought is written
  rather than left as it was found, so a host that reuses one of these cannot
  carry an old cap into a machine and call it what the program asked for. A
  host's own cap goes on after asking, and a host that has not measured one gets
  a machine that says what it reached for when it runs out. See D724.
- What a machine is given is what it gets. A host may size one smaller than the
  program reaches, and nothing says so: a machine sized for one function is how
  an engine keeps a program out of the rest of its frame, and a machine cannot
  tell that from a host that got its arithmetic wrong. `kest_needs` says what the
  program reaches and `kest_allowed` says what a machine was given, so a host
  that wants them compared compares them; what a host that does neither gets is
  the refusal at the line that asks for what is not there. See D723.
- What a host bound a context with is the host's own memory. The machine keeps
  the pointer and not what it points at, so it has to outlive every machine
  started with that list. See D325. Nothing about a pointer says when it stops
  being one, so that cannot be asked at the binding — the build that checks
  itself reads a byte of it before handing it to the function it was bound to,
  and says so when that byte has gone:

  ```
  error[K0654]: `Engine.who` was bound with something this host has since given back
  ```
- A function bound to a name takes what the declaration says. Nothing checks
  the two against each other: one written to read two things where the program
  passes one reads whatever is beside it. `kest_extern_takes` is how a host
  asks before it binds.

`examples/embed.c` is a host that does each of them wrong on purpose where it
can, and prints what happened:

```
and a handle to a lend that ended names whatever was lent next
and text kept across a heap being thrown away is not this machine's any more
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
```

It is also why a function that hands one back cannot promise `no.alloc`: the
header is an allocation, even though the block is the host's own.

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
slot, each a byte offset, what is there and what the program calls it, and
`offsetof` says the first two on the host's side. It says what the whole is aligned to as well, which is not
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

**What a piece is called** is a path from the value being asked about rather
than a word: `x` for a field, `where.x` for a field of a field, `cells[2].at`
for one of a run laid out where it stands. It is null where a piece is nobody's
field — a scalar asked about on its own, the byte that says whether an optional
is there, a slot a case carries, which `kest_case_of` names instead. A host
doing schema work — a save format, a reloader, something matching a program
against bytes it already has — was reading names out of `check --json` and bytes
out of the layout, which is two doors for one question and two things to keep in
step. `examples/embed.c` reads the seven pieces of a `Row` by name before it
lends one.

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

What the type holds is settled before any of the memory is looked at. A lend is
the host's block, so a pointer sitting in it is one the machine did not put
there: it cannot say the text is text, and it cannot take it back when the lend
ends. A shape holding text, an array, a store, a reference or a function value
is refused at the lend:

```
error[K0647]: `Npc` holds `text`, which is the machine's own and cannot be lent
```

which is what keeps `text` of a lent run the one place a lend stops being free.
A host with names to hand over hands them over a frame, where `kest_text` makes
them the machine's.

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

How many things there are in an array — one the machine made or one the host
lent — is `kest_array_length`, which answers that and, where the host asks for
it, how much room there is. A host driving a world reads a run back and has to
know where it stops, and the alternative is being told by the program it is
checking. A value that is not an array of this machine's answers nought, which
is what an empty one answers too: a host that has to tell those apart asks
`kest_still_holds` first. There is no such door for a store, because how many
live places a store holds is a walk and `len` in the language is where that is
written.

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

More than one at a time is the same promise with a number in it. A lend costs a
header and a place in the machine's list of what is lent, and a host that lends
its entities, its tiles and its events lends three blocks a frame rather than
one. Every header it gives back is one the next frame lends out of, so what a
host pays for is its widest frame, once: eight lends a frame for a hundred
frames cost the heap what the first frame did, and the hundredth costs nothing.
A host wanting that number for its own frame reads `kest_heap_taken` on either
side of one — what a call cost is a difference of two readings of what the
machine has ever handed out, and `kest_heap_used` is what the program is
holding now, which goes down as well as up.

Once, that is, until the heap goes. The headers waiting to be used again are on
it and so is the list of what is lent, so a reset takes both: the first frame of
lending after one buys them again, and a handle from before it is not the
machine's to give back. `kest_still_holds` says so and `kest_lend_ends` refuses
it — which is not what a host is told about a piece of text kept across a
reset, because text is a pointer into the heap and nothing else. A lend has a
header the machine wrote and can be asked about; text has nowhere to keep the
answer, so what a host is told about one is whether that memory is the
machine's now and never what was written there.

Which is why there is a second question. `kest_still_holds` is a yes about two
places at once — the heap the program runs on, and the build the machine was
started from — and they do not last the same length of time. Text a program
made while running is on the heap and goes with it; text the file was written
with is in the build and outlasts every reset. `kest_kept_where` says which:
`KEST_KEPT_HEAP`, `KEST_KEPT_PROGRAM`, or `KEST_KEPT_NOWHERE` for a pointer
that is neither, which is what a host's own string is and what anything from a
heap that has gone becomes. A host keeping a value between frames asks that
before it keeps one rather than asking afterwards, because afterwards the
question is about memory that may already be somebody else's.

### What a host keeps across a call

What a program makes stands on memory the machine gives back when nothing can
reach it, and what it walks to decide that is its own: the slots of the program
that is running, and the worlds and runs those name. A handle in the host's own
memory is not in that walk — the machine cannot read the host's variables — so
a host that keeps one across a call that allocates is holding a pointer to
memory the machine has given away.

There are two ways to be right about this, and a host picks one:

- hand it back in. A handle in the frame of the call is one the walk reads, so
  a host that passes its world into every call it makes about that world needs
  nothing else. This is what `examples/engine.c` does.
- say so:

```c
kest_keeps(runtime, world);
```

  The machine then keeps it whatever the program can reach, until
  `kest_lets_go` or `kest_heap_reset`. It is not reference counting and a host
  does not have to balance it: one value said twice is kept once, and a heap
  thrown away forgets every one of them. `kest_lets_go` answers whether the
  machine was keeping it, so a host that let go twice is told rather than left
  to guess.

Both hosts in this tree keep a world in their own memory across calls that do
not hand it back, and both say so. See D996.

A lend is the fourth answer, `KEST_KEPT_LENT`, because it is two things at
once: a header of the machine's, on the heap, in front of a block that is the
host's own. The block outlasts anything the machine does and the handle goes
with the heap like everything else on one, so a host asking whether it may keep
a lend is asking about both halves and is told which of them it is holding. A
lend that has ended answers `KEST_KEPT_HEAP` — what is left is the header, and
nothing is in front of it any more — and the block on its own, asked about
without the handle, answers `KEST_KEPT_NOWHERE`, because the machine never had
it.

Every answer of that shape this header gives is a list with nothing else in it:
where a value is kept, which member of a slot a kind is written through, why
there is no deepest call, and which of the two said no when the heap ran out. A
host that reads one with a `switch` and no `default` is told by its own compiler
when an answer is added to it, which is the net this project keeps over its own
lists and the whole of what a host writes to be given the same one. Both hosts
here read every one of them that way; `examples/embed.c` turns each answer into
what it does about it in a single function, so a fifth is a build that stops
rather than a value that falls through to whatever the last reader assumed.

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
crossing.

The kinds in that array are the type's own widths — what a piece of it is where
memory is shared — and not the widths of the slots they go in. A `bool`
argument is `KEST_L_U8`, one byte, and the slot it is written into is eight of
them. Which member of a `KestValue` a slot is written and read through is the
other reading of the same kind, and `kest_slot_of` says which: `KEST_L_F32` and
`KEST_L_F64` are `real`, `KEST_L_WORD` is `object`, `KEST_L_TEXT` is `text`,
`KEST_L_PAYLOAD` is whatever the tag beside it says, and every other kind,
however narrow, is `integer`. Every kind names one member, which is what makes
the answer worth asking for. A host that reads a kind and writes the width it
names writes one byte of the eight, and the machine reads all eight.

`KEST_L_TEXT` is the last of the five this kind was hiding, after the tag, the
byte an optional keeps, a truth and a reference. It said `KEST_L_WORD` until
D896, which is the width and not what it is: `text`, `[u8]` and `store<T>` were
one kind, and the answer that came with it — `text` or `object`, whichever the
type is — sent a host to the declaration for the one thing a layout is for.
Reading a store's handle through `text` is a walk to a nought byte over the
machine's own memory.

`KEST_L_TAG` is the four bytes of a tag, written and read as a whole number like
any other. What it is for is not its width: it is the one piece of a layout that
says what it *means* rather than what it is, so a layout can say where the tags
in it are. An enum whose cases carry nothing is one slot of four bytes and so is
an `i32`, so a tag beside a number and a number beside a tag were one run of
pieces — same kinds, same offsets — until the tag said which it was:

```
error[K0634]: `footed` takes `tag` in slot 0 and this host says `i32`
```

`KEST_L_PAYLOAD` is the one kind that does not answer that question on its own,
because which type is in a payload slot is the tag's to say. An enum crosses a
frame as the tag and then what its case carries, and the host that wrote the tag
is the one that knows which case it meant:

```c
const KestPiece *carries = NULL;
uint16_t count = 0;
const char *named = kest_case_of(layout, piece, tag, &carries, &count);
```

where `piece` is the piece the tag is — `KEST_L_TAG` says which — and which
answers the case's name as the program wrote it, and one piece a slot over
the slots after the tag. `kest_slot_of` over those kinds says which member each
of them is, the same as anywhere else — so a host handing over `Moved(f32, f32)`
writes `real` into the two slots after the tag, and one handing over `Hit(i32)`
writes `integer` into one of them and leaves the other where it is. Nothing reads
what a case does not carry: a frame is as wide as the widest case.

One comes back the same way it goes in. A function that gives back an enum fills
the slots the arguments were in — the tag first and what the case carries after
it — and `kest_frame_gives` says `tagged` with `KEST_L_PAYLOAD` for those slots,
so a host reads the tag before it reads anything else and asks what the case it
names carries. `kest_frame_reads` is said over those kinds the same as any
others. A host that read every case the way the first one worked is right until
the day the program hands back another: two floats and a whole number sit in the
same slot and are not the same member of it.

The same door reads one at the other crossing. A host function the program calls
is handed the tag and the payload slots in its frame, and `kest_extern_layout`
says `KEST_L_PAYLOAD` there for the same reason — so a host reading one asks the
same question about the layout the crossing says it takes, with the tag it was
just handed. An enum crosses either way: `examples/embed.c` writes one into a
frame it fills and reads one out of a frame it is handed, and holds its own four
case names against the program's before it does either.

The name is there because a tag is a number the order of the declaration decides.
A host with its own names for the cases holds them against the program's by
walking the tags up from nought, which is how it finds out that a case added in
the middle of an enum moved the ones after it. Nothing comes back for a tag that
is no case and for a piece that is not a tag. A value that is an enum has its tag
at piece nought; one inside a shape has it wherever the fields in front of it
end, and a host walking the pieces of what it fills finds it there and asks the
same question about it. The bytes a case carries are the ones inside the value
the tag belongs to, which begins where the tag does — so a host laying its own
memory over the whole thing adds the tag piece's own offset.

A slot holds whatever was put in it and carries nothing that says
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

What comes back is read the way it is written: through `kest_slot_of` over the
kinds `kest_frame_gives` says the result is made of, rather than through what
the host remembers asking for. A result of one slot lets a host be right by
remembering — every result in this tree was a number, a handle or a piece of
text until one was not. A `Cell` is an `i32` and an `f32`, two slots that are
not the same member as each other, and a host that reads the second the way it
read the first reads the bits of a double as a whole number. `examples/embed.c`
walks a result of two kinds that way and compares it with its own walk of its
own memory.

What comes back is written over the arguments, so a result of more than one
slot lands where they were. `moved(p: Point, by: f32)` takes four slots and
gives back three: after the call the `Point` is at nought and what was handed
over is gone. `kest_frame_slots` is the wider of the two, which is what a frame
has to be for both.

Asked for as words, what came back is what a program writes in a hole — a
number, a `bool`, a case of an enum, a struct, a run of a written length, a
piece of text as what it holds. What holds a handle has no text, and neither
does a function that gives nothing back; asking for either says which it was
rather than answering minus one in silence:

```
error[K0646]: `moved` gives back `Point`, which has no text of its own
```

A host that wants a shape laid out its own way walks it with `kest_frame_gives`
and writes what it finds, which is what to do when the one the language writes
is not the one the host wants.

A name nothing knows is -1 and nothing else, because asking whether a program
defines something is what this is for. Two names are there and still cannot be
handed over, and those say why: a generic is compiled once for each set of
types it is used with, so `pick` is several functions and the host has to say
which one — `kest_report` names them, and those names are the program's own
rather than anything a file wrote — and an extern crosses the other way, so a
host asking for a function it provides itself is told so at the declaration
that asked for it.

Every other question at this crossing answers with a number or a pointer, and
for every one of them the answer that means nothing here is one a real function
can give: nought arguments, nought slots in, nothing past the last argument,
nothing given back, an empty piece of text. So the value cannot say which it
was and the report does — for an index that is no function, for a place past
the last thing the program asks the host for, and for text handed over with no
address to copy from:

```
error[K0634]: this program defines 12 functions and there is nothing at -1 to say what a frame holds
```

That one is said once, and so is everything a machine says about the program a
host is asking about: how many functions there are, what a name it cannot call
is, what a name that is several functions is, how wide a type is laid out. A
program does not change while a machine runs, so a host asking twice is asking
the same question twice, and the answer to the call itself — refused, NULL,
nought — is the same every time. What a refusal says about a call is said every
time, because the call is new.
`kest_entry_name` answers it without a word and without a byte, which is what to
ask when the question is whether an index is a function at all. A host that says
how many slots it is about to describe and hands nothing to read them from is
told that instead, every time, because it is about the call and not about the
program:

```
error[K0634]: this host says what 3 slots hold and handed nothing to read them from
```

Two answers mean no and say nothing anywhere, and both are written down. A name
nothing knows is one of them, above. The other is binding a name a host has
already bound, which is refused because either answer would surprise somebody —
and a host is not a machine, so there is no report to write the reason into.

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

A name that reaches a run of calls that comes back round has no least, and then
the same two readings that size a whole program are asked of that one name:

```c
KestLimits most = {0, 0, 0};
KestReason why = {KEST_REACH_UNASKED, NULL};
kest_bound_of(build, "step", 16, &most, &why);
```

`kest_bound_of` answers the least where there is one and a bound where there is
not, over what that name reaches rather than over the whole file, and `why` says
which of the two it gave. A bound is enough and may be more than enough; a least
is what the program wants.

When a machine sized from a bound runs out of frames, the refusal says the
shape of the bound over what the call the host made reaches: so much a frame,
and so much whatever the frames. That is what a host raising a ceiling does
arithmetic with, and it is about the call rather than about the file — a body
nothing the host calls can reach is not in it.

`kest_bound_from` is the same for the third door: where a host may be called
back in from, answered where it can be and bounded where it cannot. Its bound
counts only the bodies that reach a host function, because a body that never
reaches one cannot stand in a chain of frames that ends at a host call, above it
or below it. What a re-entrant host wants is that plus `kest_bound_of` for the
entry it calls, which is the same sum as for a program with a least.

It does not have to ask at all: `emit` says what a machine to call each function
takes, beside that function — `5 slots and 1 frame to call it`, and `least` in
the object — because the walk that answers for the whole program works both out
on the way. The two numbers on the function line above it are the frame that
function has of its own; this is everything it reaches. A host that calls one
function out of a library reads it there: for `examples/embed.kest` the whole
program wants 34 slots and three frames, and `step` alone wants 13 and one.

What comes back when the answer is no says which no it is. `KEST_REACH_ITSELF`
and `KEST_REACH_VALUE` are the two a host answers by picking a number.
`KEST_REACH_NO_NAME` is a name the program has not got, which is a string of the
host's own to fix and the same news `kest_entry` gives with -1 — the command
line reads exactly that to tell a function it will not call from one it cannot
answer for. `KEST_REACH_NO_ROOM` is the working out itself running out of
memory, which is not the same as there being no answer: a host that frees
something and asks again may be told one. `KEST_REACH_UNASKED` is a host that
handed over nothing, and a reason nobody has written into yet. A build that did
not compile is none of these, because it is not a thing a host holds:
`kest_build` frees one and answers NULL.

A host that asks nothing is asked for: leaving `stack_slots` and `call_depth`
at zero is what the program asked for, worked out by the machine as it starts.
It is the worst any function needs plus the worst call back into the program
from inside a host function, because a machine does not know which function a
host will call and a host that binds one may be called from inside it — a
program that wants sixteen slots gets a machine of a few hundred bytes where it
used to get half a megabyte. `KEST_STACK_SLOTS` and `KEST_CALL_DEPTH` are as
much as usual, said by name; they are what a machine takes when the program has
no answer to give.

The command line is a host like any other and does this: `run` asks about
`main`, `call` asks about the function it was given, `tick` asks about both
handlers and takes the larger of the ones the file has, and each gets what it
asked for or the usual numbers when there is no answer. A host that picks its own
numbers instead — which is what every host did before there was anything to ask,
and what one does when what it asks about is not what it calls — is told at the
refusal what it should have asked for: running out of stack or of frames says
how many the program needs beside how many it was given, or, when there is no
answer to give, which of the two reasons that is and where. The number is the
one that runs: a machine given it does not run out. And when the working out has
nowhere to happen — a machine that spent its heap and then ran off its stack has
run out of both at once — it says that instead, because the number is there and
this run cannot reach it, which is the machine's trouble and not the program's. The same answer comes with the refusal a host meets first, which is a call
in that will not fit at all. What the working out costs the program is nothing:
it is done on the program's own heap and handed back before the machine
answers, because a refusal is not the end of a run and a frame that goes wrong
twice a second would otherwise be a heap that shrinks twice a second. A chain of calls a
thousand deep runs because the program said it was one, and a program that can
reach itself is given the frames a host allows and a bound on what each of them
costs: there is no worst chain to add up, but a frame is at most the widest body
whatever the run of calls above it turned out to be, so the slots follow from
the frames. A host that says sixteen frames is asking for sixteen of those.

The chain is read the other way round as well. The frames that go round cost the
widest body that goes round; the bodies that do not go round can each stand in a
chain once, because twice would be a run of calls coming back round through
them, so the whole of them together is paid for once. A program whose loop is
its widest body is smaller by the first reading and one whose loop is narrow and
whose bodies are many is smaller by the second, so it is given the smaller of
the two. `KEST_STACK_SLOTS` is what it gets when even that is more.

A call through a value costs the worst of the functions this program ever turns
into one. Which function such a call enters is not known before it runs, but
which ones it could enter is: a function becomes a value in one place, so the
list of them is written down while the program is compiled and the call is
worked out against it, the same way a call that names one is worked out against
that one. A program that turns none of its own functions into a value and calls
through one anyway was handed it by a host, and then there is nothing to count.

A host that hands in a function of its own that needs more is told so at the
call, in the same words as any other machine asked for more than it was given:
under-asking is a program that stops and says what it wanted, which is the
whole shape of these numbers.

A function with no deepest call says so on its own line — `2 parameter slots, 7
slots, 2 deep, calls through a value` — and the object says the same under
`why`, null for a function whose stack can be worked out. Every function that
*reaches* one of those has no answer either, and says the same words with the
function they came from on the line under them — `in shapes.kept#…`, which is
the one to open. What a reader asks about a function is whether its own stack
can be worked out, and it cannot if anything it calls has no bottom. The object
says that as `where`, which is the function itself for the one that is the
reason. The line at the top names the first
one the walk met, which is the one the program is told about.

A call prints the name of what it reaches beside the number that reaches it:
the number is an index into the list of functions and the name is what makes it
readable, so what calls what is a thing to read rather than to count out.

`kest emit` prints both, so a host writer can read them without writing a
program to ask:

```
needs 902 slots and 301 frames
     2 and 1 for `onEvent` on its own
needs a number a host picks: `shapes.kept#...` calls through a value
```

A host that wants to know whether the library it is linked against checks
itself asks `kest_checked` rather than asking its own compiler: the two are not
the same question, and the compilers do not spell the question the same, so a
host that asks for one spelling gets the wrong answer under the other. What it
is for is deciding whether to run what only a checked build catches, and whether
to keep away from what a checked build catches first — `examples/embed.c` does
both.

`kest_bound` is the same for the first door: what this program can want
altogether, answered where there is a least and bounded where there is not. It
is what a machine given nothing is sized by, said before there is one, so a host
that wants to know what saying nothing will cost can ask rather than start one
and read it back.

The command line sizes what it runs from the names it calls, asking the least
where a name has one and a bound where it has not. A name with neither used to
leave it sizing from everything the file defines, which put the bodies nothing
it calls reaches into the number.

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
and then there is nowhere to call back in from.

The reason says which function the call is in, so a host is told what it is
paying for as well as how much:

```c
KestReason why = {KEST_REACH_UNASKED, NULL};
kest_needs_from(build, NULL, &inside, &why);
// why.where is `embed.step#store<embed.Npc>`, and NULL when nothing calls in
```

Asking costs the build one walk of the program, and asking again costs nothing:
the answer cannot change after the program is compiled, so it is worked out the
first time somebody asks and every machine the build starts is handed it. For
`examples/embed.kest` that walk is 1596 bytes against the 600 a machine is made
of, which is what a host making a machine a frame used to pay every frame.

The number is the whole chain from an entry down to that call; the name is the
end of it, which is the one function a host could shorten to make the number
smaller. Asking about the named function alone answers what it reaches the host
at by itself, which is less — and naming a function in the asking is the same
question about that one and what it reaches, as `kest_needs_of` is. Running out
of room is a message rather than a wrong read.

What to call something with can be handed over as words — `kest_takes_text`
lays each one out as the type the declaration says — which is what a command
line has and what a host reading a line of configuration has. A host holding
values of its own writes them into the frame and says what it wrote with
`kest_frame_fills`. A slot that takes text and holds no address is a frame
nobody filled, and so is one that takes a handle and holds nothing: both are
refused before anything runs, naming the slot. A handle that is something else
is found where it is used, because four bytes at the front of one is what says
what it is.

What a call answered is read the same way whatever it is: `kest_gave_text`
writes a number, a `bool`, a case of an enum, a struct, a run of a written
length or text as itself, and says how many bytes it needed. A store, a
reference and anything holding one has no text of its own and is `K0646` rather
than something written wrongly — that is where a host walks `kest_frame_gives`
and lays the slots out itself, which it may do for a struct too when the way
the language writes one is not the way it wants.

The C API is 98 doors in 6 families: 39 for running a program, 18 for reading
what one is made of, 14 for watching what it cost, 12 for stopping one, 10 for
its memory and 5 for steering it while it runs. A host that compiles, binds,
sizes and calls needs 22 of them, which is what `examples/least.c` is; the rest
are there for hosts that want more, and every one of them is called by one of
the three hosts in this tree. See D1046.

A program that asks the host for nothing needs no host: `kest_start` takes NULL
there, and what a host writer writes is a build, a call and what came back.

The whole of a host that does provide something is `examples/least.c`, which is
a hundred and seventy lines and counts nothing: compile, bind by name what the program asks
for — against a list of what this host provides, what each of those takes and
what every slot of it is made of, which is what `kest_extern_takes`,
`kest_extern_layout` and `kest_extern_gives` are for — size a
machine from what the program needs, call, print, hand everything back. `examples/embed.c` beside it crosses every part of this boundary and
counts what both sides spend, which is a different thing to read.

A host that drives several functions asks about each of them and takes the worst,
and adds the way back in where it is called back into the program from inside
one of its own:

```c
KestLimits most = {0, 0, 0};
KestLimits one = {0, 0, 0};
kest_needs_from(build, NULL, &most, NULL);
kest_needs_of(build, "rule", &one, NULL);
most.stack_slots += one.stack_slots;
most.call_depth += one.call_depth;
```

and then the same `kest_needs_of` for each name it calls, keeping whichever is
larger. That arithmetic is the host's because the two halves are two questions:
what a function needs on its own, and where the program already is when it
reaches a host function. One call taking a list of names would answer the first
and look like the whole of it.

A host that sized a machine for the functions it calls and then calls one it did
not is refused before anything runs, and told two things: what the program needs
whole, and what the call it made needs, at the declaration of the function it
called. The second is the one it asked for — the first is the number it sized
away from — so a host reads the note, asks for that, and the same call goes
through.

The machine holds itself to that number where it is used. Every call into the
host is checked against what was measured, because a host builds a stack out of
it and would find out otherwise by running out of room somewhere it was told it
would not:

```
error[K0633]: `io.print#text` calls into the host 2 slots and 2 frames in, where `io.write#text` was measured at 2 and 0
```

The two names are the function the refused call is in and the function the
measurement was of, and they are often not the same one: a program reaches the
host from several places and only the deepest of them is what a host was told.
Which of them is which is what says whether the walk measured the wrong
function or measured the right one wrongly.

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

`kest_build_report` is the same question asked of the build. A build that
compiled may still have had something to say — a shape nothing names, a
declaration nothing calls — and `kest_build` writes only what stopped it, so a
host that never asks never hears a warning. It is also the one to ask when
`kest_start` gives back nothing: a program that asks the host for a
name the host has not got is refused before it runs, so there is no machine to
ask why. Nothing is written twice, so a host may ask after every start.

A host that does not want the words on a terminal renders into a file of its
own — `tmpfile` is what C gives every host — and reads them back. One file for
the life of the host, wound back and written over: a host that made one every
time it asked would make one a frame. Read to where the report ended, because
what is after it is the last one. A host that only wants to know whether
something went wrong pays none of this, because every call answers false when it
was refused.

Importing a module is buying the module: it is checked and compiled whole, so a
program that calls one function of `std.text` pays what a program calling five
of them pays, to within a hundredth. What is in a module is what a host can
call, which is why nothing is left out for being unreached.

What a build costs is mostly what a program imports: a program of four lines is
about eleven thousand bytes, one that prints about twenty-two thousand, and one
that makes text about two hundred and fifty-five thousand. Nothing is carried
from one build to the next, so a host that reloads a file of its own reads and
compiles everything that file imports again, every time.

What a place costs is worth knowing for a host that reports often: a refusal
this project measured is 80 bytes of message, 242 shown to a person with the
line it happened on, and 295 sent to a tool. The drawing is most of a report and
costs no memory — where a byte is in a file was worked out when the file was
read — and the form for a tool is the bigger of the two, because names cost more
than carets.

All three of those take the form to write in, and the two forms carry the same set:
prose for a person, and JSON for whatever reads it after — an editor, a build,
a model repairing what it wrote. This is the `--json` the commands have, at the
boundary rather than at a command line, and it is asked for at each of the two
places output is written rather than set once somewhere else:

```c
KestBuild *build = kest_build(path, NULL, stderr, KEST_FORM_JSON, 0);
kest_report(runtime, stderr, KEST_FORM_JSON);
```

A machine that has said nothing since it was last asked writes nothing, in
either form. JSON is one object per call for the run of diagnostics that call
is about, and the count in it is of what that object holds rather than of
everything the machine has ever said.

What a host has been told is the host's, and the room the words were written in
goes back to the machine when they are read. A program refused every frame says
the same sentence every frame — the numbers in it are that call's — so a host
that reads what it is told pays nothing for being told again.

A host that never reads is held to `KEST_MOST_UNREAD`, which is sixteen: a
machine keeps that many of what nobody has asked for, counts the rest and says
how many there were, because a list that stopped where a reader would take it
for the end is the one thing a report must not be.

That is one of the two ceilings a host meets. The other is `KEST_MOST_PLACES`,
which is eight: the places one diagnostic shows — the declaration it is about,
the other declaration of that name, the calls between a promise and the body
that broke it — and, because a frame is a place, how many calls a refusal shows
under the one it stopped at. Both are in the header, and both say so where they
bite, so a host is never left to guess whether it has met one.

```
and 84 more since, which this machine did not keep
```

In JSON that is `notKept` beside `errors`. No command writes it — a command that
refuses stops, and what stops says one thing — so it is a name a host meets and
a reader of a command's output does not.
 Compiling has no such ceiling — a
program with five hundred things wrong with it has five hundred things wrong
with it, and the run that found them ends. A machine does not end.

`include/kest.h` is the only header a host includes and `libkest.a` needs libc
and nothing beyond it. `kest_version` says which Kest it is, for a host linked
against one it did not compile itself — the same string `kest --version`
prints, out of the same place, so the library and the command line cannot
disagree about what they are.

Three more numbers go with it, because a host has three more questions and
they move for different reasons. `KEST_ABI_VERSION` is what shape the doors
below are in, and `kest_abi_version` reads the same number out of the library:
a host compares the two before it crosses at all, because a header and a
library from two versions of this project are a host reading memory that means
something else and nothing in C notices. `KEST_JSON_SCHEMA` is what shape the
objects a command writes are in. `kest_profile` answers with the name and the
number of the profile `deterministic` is a promise about — a promise about a
named profile rather than about arithmetic in the abstract — which a host
keeping a replay or shipping a save writes down beside it. `kest --version`
prints all four:

```
kest 0.0.1, abi 4, json 3, profile kest-det 2
```

The ABI number goes up when anything a host can see changes: arguments, what a
function answers, a struct's fields or their order, an enum's cases or their
numbers, or what any of them mean. It does not go up for something added at the
end, which a host built against the older number does not know about. See D974.

### What 0.0.x promises, which is nothing

**Nothing is frozen.** This is `0.0.1`, and a `0.0.x` promises no program, no
host and no tool anything between one of them and the next. Semantics may
change, syntax may change, the C ABI may change, the shape of the JSON may
change, what `deterministic` covers may change, and what a reference is made of
may change — one of those changed on the way to this sentence, and D1033 is
why.

Kest said `1.0.0` on 2026-09-18 and withdrew it a day later. Nobody outside this
project had written a program in it, two independent readings from outside found
foundational things still worth changing, and a stability promise made before
anyone had leaned on it is a promise that costs its maker nothing and its future
readers everything. The release and the tag are gone; the history is not, and
D1035 says what was withdrawn and on what evidence.

What replaces the promise while this is `0.0.x`: every break is a decision that
says what it supersedes and why the old thing was worse, and `CHANGELOG.md`
says what a reader with a program has to do about it. That is a record rather
than a guarantee, and it is the honest thing to offer before there is anyone to
guarantee it to.

**The four numbers are still four numbers**, and what each is *about* has not
changed — they are the version, the C ABI, the shape of the JSON, and the
deterministic profile, and D974 and D983 say which moves for what. What has
changed is that none of them is a promise yet.

The rest of this section is what those four promises **will** mean when this
project offers them again, kept here because it is the design and not an
aspiration. Read it as what stability would say, not as what `0.0.1` says.

**A program.** A program that checks under 1.x checks under every later 1.x.
Syntax is added and not taken away; a keyword is added only where a program
that used the word as a name would be ambiguous, which is the rule the language
has had throughout. The type system gains types and rules that refuse less, not
more. The standard library gains functions and modules; a function that is
there keeps its name, what it takes and what it answers. A diagnostic keeps its
code: `K0342` means what it meant, because a script that reads codes is a
script that would otherwise break on a Tuesday. What a message *says* is not
promised — the words are written for a reader and are improved.

A program that does not check under a later 1.x is a defect in that 1.x, not a
thing to migrate. 2.0 is what a change that refuses a program is for.

**A host.** The C ABI is frozen: what `KEST_ABI_VERSION` says today is what a
host compiled against this header sees for the whole of 1.x. A function keeps
its arguments and what it answers; a struct keeps its fields and their order;
an enum keeps its cases and their numbers. What may be added is a function at
the end of the header and a case at the end of an enum — neither of which a
host built against the older number can see, and neither of which can be read
as something else. Anything more is 2.0 and a new ABI number. A host compares
`KEST_ABI_VERSION` with `kest_abi_version()` before it crosses, and a host that
finds them different stops: the two numbers being equal is the whole of what
makes the doors below mean what the header says.

**A tool.** The JSON a command writes has its own number, because a compiler
can move without any object changing and an object can change without the
compiler moving. `KEST_JSON_SCHEMA` goes up when a name changes what it means,
goes away, or is added where a reader was told the list was everything — and
not when a name is added beside the others. A tool reads the number first. A
tool written for schema 3 reads schema 3 objects for the whole of 1.x.

**A deterministic run.** `kest-det 2` is the profile `deterministic` is a
promise about. Its number goes up only when what a program can *observe* about
arithmetic changes: which operations are in it, how each rounds, what is
refused. A compiler that answers every one of them the same way is the same
profile however much else moved. A host keeping a replay or a save writes the
profile down beside it, because a run under one profile and a run under another
are two runs, and nothing else in these four numbers says so.

**The bytecode is not a boundary.** It is internal, it has no version, and it
is not promised between any two builds of this compiler — not between 1.0.0 and
1.0.1, and not between two builds of the same commit on two machines. What
ships is the source beside the runtime, and a program is compiled by the
compiler that runs it. `kest emit` prints it for a reader and for this
project's own checks; nothing reads it back.

**Saved state is the host's.** What a world is saved as is what the host wrote
down, and the shapes it was written from are held by the layout marks a reload
compares — that is what refuses a save read back into a program whose shapes
moved. What is *not* promised is that a reference is an identity: a `ref<T>` is
a place and a stamp in one machine's world and means nothing in another, which
is why a host that saves a world saves the application's own identifiers beside
it and remaps on the way back in. See D985.

## A project

A file on its own is a program: `kest run one.kest` needs nothing around it.
A project is what a directory becomes when there is more than one file and
somebody else has to build it.

```
kest new demo
cd demo
kest build
kest test tests/*.kest
kest doctor
```

`kest.project` is lines of `name value` and nothing else:

```
project demo
entry src/main.kest
source src
tests tests
kest 0.0.1
profile kest-det 2
```

`entry` is what `check`, `build` and `run` work on when no file is named, so
being inside a project means not naming one. `source` says where this project's
modules are, and a **dependency is another `source` line** pointing at wherever
somebody put it: there is no registry, nothing is downloaded and nothing is
locked. A name the compiler does not know is refused rather than skipped,
because a misspelt line reads exactly like one that is not there.

**Where an import is looked for.** A file on its own resolves `import a.b` from
its own root — what it calls itself, taken off where it is, so a file that says
`module game.npc` and sits two directories down has the root above those two. **Inside a project the sources are
the whole answer**: every `source` line is looked under, in the order they are
written, and a file's own directory is one of them or the project is written
wrongly. That is what makes every file of a project resolve an import the same
way, and what makes a `source` line a dependency rather than a comment.

Which project a file is in is where the file is: `kest.project` in its
directory or in one above it. A host embedding one file of a project therefore
resolves what the command line resolves.

A module under **two** of the sources is refused. Two directories holding a
module of one name is what a vendored copy is most likely to be, and which of
them an import means cannot be worked out from the line:

```
error[K0707]: `physics.math` is under two of this project's sources
   |
 3 | import physics.math
   |        ^^^^^^^^^^^^ `./vendor/physics/math.kest` and `./teams/physics/math.kest` are two modules of one name, and which one this is cannot be worked out from the line
```

and a module under none of them says which ones were looked under. Two modules
whose names end in the same word are fine and always were — `render.math` and
`physics.math` live in two vendored directories in the same program — because a
name lives under the whole of its module.

`kest build` compiles and says nothing when it compiles. There is no artifact:
the bytecode is not a format anything else reads and is not stable, and what
ships is the source beside the runtime.

**What that costs, measured.** Six hundred modules in six hundred and three
files — a hundred and sixty-five kilobytes of Kest, ten times what anybody has
written in it — compile in **67 milliseconds** and 8 megabytes, and a hundred
and seventy-four kilobytes written as a six-hundred-deep chain of imports
compile in 94. Making a machine from a build that is already compiled is
another 0.05 milliseconds, and a host that starts many machines from one build
pays the compiling once. So startup compilation is not a thing a game has to
plan around, and there is nothing a cache would buy that is worth a format.

What that costs a studio is that the source is the artifact: a game that must
not ship readable Kest packs it the way it packs its other content. The
bytecode is not an answer to that — it is unstable and unversioned on purpose,
and it would have to stop being both to become one. `kest test` runs each program named and
reads what it answered — a test here is a program that checks itself and answers
with which check failed, which is what every example in this tree is, so there
is no framework and no discovery. `kest doctor` is what somebody runs when
something is wrong and they do not know what. See D982.

## What a run did

`kest profile` runs a program and says what it cost, in counts:

```
42 step(s), 15 call(s), 2 crossing(s) into the host, 42 byte(s) of heap
  math.factorial#i32                       4 call(s)
  math.gcd#i32,i32                         6 call(s)
```

A step is what a budget is spent in — the same unit `--fuel` bounds — so a
profile and a frame budget are in one currency and a host can read either
against the other. What it does not say is how long anything took: a machine
counts what it did, and how long that took on the machine it ran on is the
host's clock. `make time` and `kest tick` are where a duration comes from, and
both run something.

There is no count per instruction. Taking one is a test at the top of the
machine's loop and it measured a third of the machine; a profiler that makes a
program a third slower is measuring a different program. The build that checks
itself takes one under `KEST_DEEP`. See D979.

It goes to the error stream, because what the program wrote is the program's
answer. With `--json` it is a `profile` field of the object the run writes.

A host counts the same things through the same doors: `kest_count` turns it on
for a machine, `kest_counted` fills a `KestCounted` with what it has done so
far, and `kest_counted_entry` says how many times one function was entered, by
the number `kest_entry` answered with. A machine nobody asked pays one test of
a pointer that is nothing.

And what the heap under the program did about all that, which is a second
question with a second door: `kest_telemetry` fills a `KestTelemetry` with the
places handed out, the bytes asked for against the bytes the places they were
cut from are worth, the things that grew where they stood, the walks and what
they gave back, the plots asked of the host and handed back, the blocks of
working memory opened, the lends and what was in them, and the bytes copied
because something outgrew its place. There is nothing to turn on: every one of
those is at an allocation, a walk or a lend, and a program runs millions of
instructions between any two of them.

`kest_collect` is the other side of that: it walks now, at a moment the host
chose, and gives back everything nothing can reach. A machine decides for
itself when a walk is worth doing — when it has been handed as much as it is
holding — and that moment is wherever the program happened to be. A host with a
frame to fit into calls this between frames and moves the walk to between
frames. It is refused while a program is running, the same way throwing the
heap away is, because inside a call the host's own C locals are not something
the machine can read.

A hot phase that promises `no.alloc` cannot be interrupted by a walk at all: a
walk happens inside an allocation and there is none. Fifty frames of
`bench/frame.kest` driven over a lent run answer that with one allocation, for
the first lend's header, and no walks.

Two of its fields are nought until a host says otherwise. `kest_clock` gives a
machine the clock it times its own walks with, and what that clock counts in is
the host's to decide — this library is ISO C and there is no monotonic clock in
it, so a duration comes from outside the way everything outside does. Nothing a
program can write reaches it and nothing a program answers changes when it is
set.

`kest_collected` is what a host asks for a distribution with, and it is a
different question from the two numbers above. `walked` is every walk added up
and `worst_walk` is the longest one; reading `walked` on either side of a frame
answers what that frame paid the collector altogether, which is not what one
pause was. A frame that stopped four times for a millisecond each and a frame
that stopped once for four answer the same thing there and are not the same
thing to a budget. So a host that wants pauses is told each one as it finishes:
what it took, what marking and sweeping took of that, how many root slots it
read, what it gave back, what is still held, and how much of the memory the
host gave is in places nothing is using — which is the fragmentation a
non-moving heap has, because a plot goes back to the host only when every place
in it is free. It is called while the machine is stopped, so a host that does
work in there is lengthening the pause it is being told about; keeping the
numbers is what it is for.

`bench/measure.c` does both and prints them as two rows, because printing one
and calling it the other is a mistake this project made: `per call` is what
each call paid the collector altogether and `pausing` is what one walk was.
Over `bench/agents.kest` those are 81 milliseconds and 11.

## Stopping a program

`kest debug` runs a program with breakpoints in it:

```
kest debug game.kest
> break 42
  breakpoint at game.kest:42:9 in game.step
> run
stopped at game.kest:42:9 in game.step
> where
  -> game.kest:42:9 in game.step
     game.kest:88:12 in game.main
> locals
  world                slot 0  12
  hungry               slot 2  3
> next
stopped at game.kest:43:5 in game.step
> continue
```

`break <line>` is a line of the file that was named; `break other.kest:12` is a
line of another. `step` stops wherever the machine goes next, `next` steps over
a call, `where` is the frames with the innermost first, and `locals` is what the
body called its slots and what is in them.

A breakpoint is **written into the code**: the debugger puts an instruction
nothing compiles to over the first byte of one and puts the byte back when the
machine stops there. So a machine nobody is debugging runs the program that was
compiled, byte for byte, and pays nothing at all — there is no test in the
machine's loop, because one measured a third of the machine. See D979 and D991.

A stopped machine is not finished and is not broken: its frames, its stack and
its heap are where they were, and what a `scratch { }` opened is still open.

A host does all of this itself through ten doors. `kest_break_byte` is the byte
to write, which is the instruction nothing compiles to: a host cannot work it
out, because the public header does not hand out the instruction set and should
not, so the machine says which byte it is rather than a host writing the number
down and finding that it moved. `kest_code_of` hands over the
bytes a body was compiled to, which is what a breakpoint is written into;
`kest_came_from` says where in the source the instruction at a byte came from,
which is what makes a breakpoint at a line a breakpoint at an instruction.
`kest_stopped` says how far into a body the machine stopped and `kest_stopped_in`
says which body — both answer -1 for a machine that is running or finished, so
a host tells a stop from a refusal by asking rather than by reading the report.
`kest_resume` carries on. `kest_frames_deep`, `kest_frame_in` and
`kest_frame_ip` are the frames, innermost last, which is a stack trace; and
`kest_frame_wide`, `kest_frame_slot` and `kest_frame_name` are what a frame
holds, what each slot is and what the body called it.

What it does not do: show a frame of the host, which is not a frame of this
machine; or stop inside a call the host makes back in.

## An editor

`kest lsp` is this compiler answering an editor, over the standard streams and
in the Language Server Protocol. It is the same build a `check` is: what is
wrong with the file is the diagnostics that check made, the one form is what
`kest fmt` would write, and what a name is, where it was declared and what else
names it come from the index the checker writes while it resolves. There is no
second parser, because a second parser is a second answer.

It answers `textDocument/publishDiagnostics`, `hover`, `definition`,
`references`, `documentSymbol`, `workspace/symbol`, `rename`, `completion` and
`formatting`. A buffer that has not been saved is what it reads: the client
sends the whole document on every change and the compiler reads that rather
than the disk.

What it does not do: rename across files, because it holds the one file the
editor opened; and incremental changes, because applying an edit twice is the
one way a server can be wrong about what a file says.

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

which says

```text
hello
```

and answers nought. A program written here is run, and what is written under it
is what it wrote.

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

A host can ask what a running program is holding with `kest_heap_used`, which
is a number without a scale until the ceiling beside it is readable. It goes
down as well as up: what a program makes and then replaces is given back, so
this is where a world settles rather than a running total. `kest_heap_taken` is
the running total — every byte the program has ever been handed, which only
`kest_heap_reset` puts back — and `kest_heap_most` is the most it ever held at
once, which is the figure a host has to make room for. `kest_heap_wanted` is
what the allocation that was refused was asking for, and
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

`kest_heap_allow` says how much heap a machine may have from here on, and it is
there because a host with a number to divide cannot divide it before there is a
machine: what a machine costs is the arithmetic a machine is made with, and a
host that works that out for itself keeps a second copy of a sum that is right
until somebody changes one of them. So it makes the machine with a heap of
nothing, asks `kest_runtime_cost` what that took, and says here how much of what
is left the program may have. Nought is no ceiling, which is what a host that
never says it has. It is refused from inside a call for the same reason the
other is — what the program is holding is on the heap, and a ceiling moved under
it is a promise changed after it was made:

```
error[K0613]: how much heap this machine may have cannot be said while the program is running
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

A command that reads a program asks this machine for whatever the program
needs. That is what a compiler does, and nobody notices it until the program is
a broken one: a compiler handed a file it cannot make sense of can ask for
everything there is, and one of them did — sixty-five gigabytes, eight times in
a day, taking whatever else was running with it each time.

`--room` says how much this command may have, all of it: reading and checking
and compiling the program, and the heap the program runs on afterwards. It is a
number of bytes, or one with `K`, `M` or `G` after it.

```
kest run --room 64M examples/game.kest
```

What compiling takes comes off the number and what is left is the heap, because
a ceiling that meant one thing while compiling and another while running would
be two ceilings sharing a name. A command that crosses it while compiling says
what it had taken and what the allocation that crossed it wanted:

```text
error[K0658]: this has taken 440 of the 1000 bytes it was given, and wanted 832 more
```

and one that crosses it while running says so at the line that asked, in the
words any host's own ceiling is reported in:

```text
error[K0617]: the program has used 557106 of the 706528 bytes it was given, and this asked for 1048577 more
```

Without `--room` there is no ceiling, which is what a command line has always
had. The library itself takes the number rather than the words: a host says how
much heap a machine may have in `KestLimits`, and a build is opened with a
ceiling of its own.

### What `no.alloc` is about

`no.alloc` is about the **Kest program heap**: the memory a program's arrays,
stores, text and tables come out of, which `kest_heap_used` reports and
`kest_heap_reset` empties. A body that promises it performs no allocation on
that heap, and may only call host functions whose own declaration permits it.

It is not a promise about the process. Three things allocate outside it and are
outside the contract:

- the compiler's own work, which is over before a program runs;
- the machinery that writes a diagnostic, which runs when something has already
  gone wrong;
- whatever a host's own bound function does inside itself, which this language
  cannot see and does not claim to.

The third is a declared promise rather than a proved one. An `extern` written
`no.alloc` is taken at its word about its own memory; what the machine does
check is that the crossing left the program's heap where it found it, which it
does after every call to a door that promised. A host that allocates with
`malloc` inside such a door is not caught and is not claimed to be.

A frame-critical program that must report a failure without touching the program
heap has `kest_native_failed` for the host side and the fuel and cancel refusals
for the machine's: none of those allocate on the program heap.

Scratch allocation is allocation. There is no exemption for temporary memory.

### When a host cannot do what it was asked

A bound function gives nothing back. One that could not do what it was asked
used to write a value that meant nothing, and the program carried on with it.

```c
static void fetch(KestValue *frame, KestRuntime *runtime, void *context) {
    if (!open_the_file(context)) {
        kest_native_failed(runtime, "the file it wanted is not there");
        return;
    }
    frame[0].integer = read_it(context);
}
```

The call then refuses where it was made, in the same shape as anything else
that fails while running:

```text
error[K0662]: `Host.fetch` could not do what it was asked: the file it wanted is not there
```

What the door wrote into the frame is not read. The host's words are copied and
say what could not be done; the code is the machine's. A door that fails is not
a program that is wrong, so nothing about the program is blamed — the message
names the door.

### How long a program may run

The three ceilings above bound memory. `--fuel` bounds time, and it is the one
a host running code it did not write needs: `while true {}` is a program, the
compiler is right to accept it, and nothing else here stops it.

```kest
fn forever() -> i32 {
    while true {
    }
    return 0
}
```

```text
error[K0659]: this program has taken the 1000 step(s) it was given
```

A step is a jump that goes back or a call. Those are the two things a program
does to go on doing something — code is finite, so a run that never ends is
going round or going deeper — and a budget on them bounds every program that
would not stop. What it does not bound is a long body with no loop in it, which
the program's own size bounds.

And a step is charged for work as well as for going round, because some
instructions do as much of it as the program asked for. Joining two pieces of
text, making text out of bytes, cutting a piece out of one, asking how long one
is, putting two of them in order, filling a run of something, making room for
one, and the push or the add that has to copy what is already there: each costs
a unit for every sixty-four bytes or elements it touched, on top of the one the
instruction itself costs. Without that a program can spend a second inside one
instruction without spending a unit of its budget, which is the whole of what a
budget is for. The ones that are already a loop in Kest — sorting, joining a
list — are charged as the loop they are.

What a program pays is what it did rather than what it holds: `slice` is charged
for the part it walked to and not for the rest of the text, an array made with
`array(n, v)` is charged once for its `n`, and a `push` that could grow where it
stood pays nothing for the growth. A program that says how many there will be
pays for them once.

It is a count and not a duration, so two machines given the same budget stop at
the same place. A `for` over a thousand costs 999 steps, because the thousandth
turn does not go back; a `while` over a thousand costs 1000; and `f(50)` calling
itself down to nought costs 51.

A machine that stops this way is not broken and is not finished. Its stack, its
heap and everything the program built are where they were, so a host that gives
it more through `kest_fuel_set` and calls again carries on. What it cannot do is
carry on from the middle of the call that stopped: that call returned.
`kest_fuel_left` says what is left of a budget, which is what a host watching a
frame reads after a tick to learn what that tick cost, and a machine with no
budget answers it with every bit set rather than with nought — nought is what a
machine that has run out says, and those are opposite things.

What a host's own doors cost the program is the host's to say. A bound function
may read a file, walk a scene or ask another system something, and the machine
sees a call and a value: `kest_fuel_spend` is where a host charges for it, in
the same units and at the same rate the machine charges its own instructions —
one for the crossing and one for every sixty-four bytes or elements of what the
door did. It is called from inside the bound function, where the program's
budget is whole, because the slice the machine was spending is given back before
a door is entered. A host that spends more than is left does not stop the call
it is inside — that call is the host's and the machine is not running — but the
machine stops at the next step the program takes, with the refusal a budget
spent any other way gives. `examples/engine.c` charges for the door it watches
the world through.

## What is exact, and where it is written

The mission this language was put through asks for a list of the things a host
or a program must not have to guess at, each of them written down exactly rather
than implied. This is that list and where each one is.

| | Where |
| --- | --- |
| what `no.alloc` means, exactly | *Cost contracts*, above: the builtins that reach the heap, what a run of a written length does not, and what a foreign function is judged by |
| what `no.host` means | the same section: nothing a body writes crosses out of a program, and the only way out is a call to an `extern` |
| the deterministic profile | *The simulation profile, version 1*, and `examples/determinism.kest`, which answers whether a platform keeps it |
| what text is, and what a buffer costs | *What a piece of text is*, and D940 for `fit` and D956 for what a round costs written two ways |
| store and ref identity and lifetime | *References*, and D934 for what a `ref` carries — a world, a stamp and a place |
| lend and borrow lifetime | *The host boundary*: a lend is the host's block with a header over it, and ending one is where a host drops the handle |
| the scratch rule | *A frame's working memory*: what a host keeps past a rewind must not be anything the program made after the mark |
| what a fuel unit is | *What there is a most of*: a step is a jump that goes back or a call, and work an instruction does is charged by weight |
| what cancellation promises | the same section, and *Who owns a machine* for doing it from another thread |
| the threading model | *Who owns a machine* |
| what a heap ceiling covers | *What there is a most of*: the heap a program runs on, which is not what compiling it took |
| whether the ABI is stable | it is not. The front page says so under **Experimental**, and every door here is a door that has moved this month |
| which backend this is | the stack machine, and D963 says what the second one measured: 1.35 times slower over three shapes of work, built and weighed rather than predicted |

## What this has been run on

Four platforms, two instruction sets and three compilers, every one of them
built and run on every push, and every one held to writing the same bytes for
every example in the tree:

| Platform | Built by | What runs there |
| --- | --- | --- |
| Linux x86-64 | GCC, and again under clang | the whole gate, the fuzzer, and the four families of workload |
| Linux arm64 | GCC | the fast tier, the fuzzer, and the four families of workload |
| macOS arm64 | clang | the fast tier, and the archive unpacked and run in a room of its own |
| Windows x86-64 | MSVC | every example, and the archive unpacked and run in a room of its own |

Nothing else is claimed: this project does not call a thing that was never run a
thing that works. What each of them writes for every example goes into one file
and the four are diffed against each other, so a platform added that does not
agree is a build that fails rather than a paragraph that is out of date. See
D970 and D1058.

The library is C11 and libc and nothing else — `check-header.sh` compiles the
public header to the standard with nothing beyond it — so it should build
anywhere that has a C11 compiler. What "should" is worth is a build nobody has
done. For a reader taking it somewhere else, these are the places that know
where they are:

| | What is assumed | Where |
| --- | --- | --- |
| paths | a separator is `/`, and a backslash is a character a filename may hold | `last_separator` in `src/loader.c`, which every reading of a path goes through |
| the library | it is beside the program, or `../lib/kest`, or where an install put it | `kest_library_path` in `src/loader.c`, and `KEST_LIB` in front of all of them |
| a clock | `CLOCK_MONOTONIC`, then `timespec_get`, then `clock` | `host_microseconds` in `src/main.c`, which is the command line rather than the library |
| widths | `%zu` for a `size_t` and `%llu` for a `uint64_t` | every message, held by the compiler reading the format against what is handed to it |

None of those four is where a port would go wrong, because all four are run.
The table is what a fifth would read first, not a list of what would be wrong.

## What a piece of text is

Bytes and how many there are: two slots, side by side wherever the value is.
The length is part of a piece of text rather than something to go and count, so
`len` is a read, a byte at a place is a comparison and a read, and a cut is a
place inside what it was cut from and how many bytes of it. None of those
reaches the heap, which is what a walk over text is made of and what eight of
`std.text`'s `no.alloc` functions are built on.

Where a value is laid out in memory — inside an array, a store or a struct a
host lays out — a piece of text is sixteen bytes for the same reason: what it
is made of, and how many. See D964.

**Text is UTF-8.** A file is held to it while it is read, and bytes that arrive
at run time are held to it where they arrive: `text(bytes)` in the machine and
`kest_text` at the boundary each walk what they are handed once and refuse a
byte that begins no character. Everything the machine makes out of text that
was already whole is whole, so nothing else walks anything.

**A nought is a character.** `U+0000` is text like any other codepoint: it is
written `\0`, `len` counts it, `==` and `hash` see past it, and a host is handed
it with the length beside it. What that costs is that text the machine made is
not always a C string even when it does end in a nought — the length is what
says how long it is. See D971.

**A cut does not end in a nought.** Text the machine made is written with one
after it: what `text()`, an interpolation and a join write has a nought past
the end, which is not part of it and is not what says where it ends. A cut is a place inside another
piece and how many bytes of it, so the byte after it belongs to what it was cut
from. A host reading text hands the bytes and the length to whatever it is
calling rather than treating what it was given as a C string.

**What this language owns about Unicode, and what a host owns.** Core owns
UTF-8 and nothing above it: that a file is one and that bytes arriving are one,
how many bytes a piece of text is and how many characters, stepping forwards
and backwards over characters, comparing and searching by bytes — which is
right for UTF-8, because no character's bytes appear inside another's — byte
order, which is a total order and is what a store and a sort need, and casing
that is ASCII and says so.

A host owns everything a language decides. Which characters go together on a
screen and how much room they take. Normalization: `e` with an acute after it
and `é` are two pieces of text here. Collation: `äpfel` sorts after `zebra` by
bytes and before `apfel` in a German dictionary, and the bytes do not say which
a program wants. Locale casing: the capital of `ı` is `I` in Turkish and the
capital of `i` is `İ`, and one ASCII table cannot be right for both. And
bidirectional text, shaping and display width. Each of those needs a table that
changes with the Unicode version, and a language that shipped one would be
shipping a table its programs could not replace. `examples/locale.kest` is the
line with a program on both sides of it. See D1056.

**What a host reads it through** is `kest_text_bytes`, which answers the bytes
and how many there are and costs nothing: it reads the second slot. A host that
reads the `text` member itself gets the same bytes and has to know whether the
piece is one the machine made.

## A block of working memory

A frame that builds something to look at it and then throws it away pays for
what it built, and nothing is given back while a program runs (D012). A
`scratch { }` block is where that stops:

```kest
scratch {
    let line = "{name}: {score}"
    io.print(line)
}
```

Entering it marks the heap and leaving it puts the heap back, so the block costs
the same every time round the loop rather than every time round the loop costing
the last one.

**Nothing made inside may be kept.** That is proved rather than asked for. A
value the block made is refused where it would outlive the block: given back
from the function, written into a name declared outside it, put into an array or
a store that is not the block's own, or handed to a call beside something older
that could keep it.

```
error[K0507]: this gives back what the block made, and the block puts it away
```

What is allowed is copying out. A number is a number afterwards. Bytes copied
into a run of bytes are bytes: `text.fitting(out, piece)` where `out` is `[u8]`
copies, because a run of bytes cannot hold what the machine keeps. That is the
idiom for a world whose text changes — build the name in a block, copy it into
the buffer the thing already has — and `examples/churn.kest` measures what it
saves.

**A crossing is a rule rather than a refusal.** A host may be handed what the
block made, because refusing that would refuse `io.print` inside a block. A host
that keeps it past the call keeps a pointer into memory that goes — the same
rule `kest_scratch_mark` asks of a host, said in the language.

**Every way out puts it back.** A `return`, a `break` and a `continue` close the
block on the way past. A refusal, the fuel running out, a host saying no and a
machine cancelled stop a body where it stands, and the machine puts back what a
run left open. A promise of `no.alloc` refuses a block: a body that reaches no
heap has no working memory to put back.

## A frame's working memory

A host that calls a query — something that makes text, or an array, or a store,
and answers — pays for what it made, and nothing is given back while a program
runs. A frame loop that does that every frame is a frame loop where every frame
costs the last one.

`kest_scratch_mark` answers a number saying where the heap is, and
`kest_scratch_rewind` puts it back there. Everything the program made since is
gone and the heap is where it was, so the query costs the same every frame.
Eight of them may be open at once, and putting the heap back to one under
another takes the ones above it with it.

What a host must not do is keep anything the program made after the mark. A
piece of text, an array, a store, or a reference into one, is a pointer into the
heap and the machine cannot tell that it has gone. What is safe to keep is what
the host copies out — a number, or `kest_gave_text` into a buffer of its own —
and what was there before the mark. The program's own state is the same rule
said again: a step that pushes into the world's store puts that memory after the
mark, so a mark goes round a query and not round a step that keeps something.

The machine refuses what it can see: a mark or a rewind while the program is
running, either with anything lent — a lend is a header on the heap and a place
in a list beside it — a mark past the eighth, and a rewind to a number this
machine did not hand out or has already put back to. Throwing the heap away
takes every mark with it.

```c
uint32_t mark = kest_scratch_mark(runtime);
kest_call(runtime, query, frame, room);
kest_gave_text(runtime, query, frame, mine, sizeof(mine));
kest_scratch_rewind(runtime, mark);
```

A `scratch { }` the compiler proves nothing escapes from is what this language
will have instead, and it needs lifetime facts the compiler does not have yet.
Until then this is the host's to get right, which is why it is written down
here. `examples/engine.c` marks round a query and puts the heap back.

## What the heap gives back, and what that costs

A machine's heap is **non-moving mark and sweep**. Text, arrays, stores and
what they hold stand in places of a fixed ladder of widths, inside plots an
address masks to. When the machine decides a walk is worth doing it marks
everything it can still reach, gives back every place nothing marked, and hands
back any plot whose every place is free.

**Nothing moves**, because the machine's slots carry no tags: a walk reads
every eight bytes as though it might be an address, which is only safe where
nothing is written through what was read. A number that happens to look like a
place keeps that place, which costs memory and cannot cost correctness. It is
also what lets a program hold the address of an element or a piece of text cut
out of another: a walk finds the thing an inside place is inside of.

**What it walks from** is the machine's own slots up to where they have got to,
whatever a host is holding by handle, whatever the program lent out, and the
spare lend headers. Nothing of the host's own memory is read, which is why a
walk is refused while a program is running.

**When.** By itself, after being handed as much as it was holding when it last
swept, with a floor of 256K so that a program making almost nothing does not
walk over nothing. A host that is not budgeting a frame raises the multiplier
with `kest_collect_after`, and one that is calls `kest_collect` between frames
and moves the walk to a moment it chose. A `scratch { }` block open stops a
walk, because a block gives its own memory back and a walk inside one would be
a walk over memory that is about to go.

**A body that promises `no.alloc` cannot be interrupted by a walk at all**, and
that is the whole of the frame answer: a walk happens inside an allocation and
there is none. Fifty frames of `bench/frame.kest` driven over a lent run are
one allocation, for the first lend's header, and no walks.

**What a pause costs, measured.** It is linear in what is still reachable, and
nothing else moves it. Over `bench/agents.kest` — a world of entities holding
text, a nested run each and references at each other — from five thousand
entities to forty thousand:

| entities | reachable | pause | marking | sweeping |
| --- | --- | --- | --- | --- |
| 5,000 | 3.9 MB | 2.6 ms | 2.0 | 0.6 |
| 10,000 | 7.7 MB | 5.2 ms | 4.4 | 1.4 |
| 20,000 | 15.4 MB | 10.8 ms | 9.1 | 2.4 |
| 40,000 | 30.8 MB | 21.0 ms | 17.9 | 4.8 |

**0.68 milliseconds for every megabyte still reachable**, of which marking is
0.58 and sweeping 0.16. Roots are four thousand slots at every size and cost
nothing. A sixtieth of a second is therefore about twenty-four megabytes of
reachable heap if a frame may be spent entirely on a walk, and proportionally
less if it may not — which is what a host sizing a world needs, and what
`kest_collected` tells it about the world it actually has.

**There is no incremental marking and there is not going to be one.** Marking
in slices needs a barrier on every write that could store an address, and this
machine cannot tell which writes those are: slots carry no tags and a struct
moves with a `memcpy`. A barrier would be paid by every program on every copy,
which is the same argument that kept the collector from being a count. What a
frame budget has instead is `no.alloc`, `scratch { }`, `kest_collect` between
frames, and the number above.

Measured on the machine this was read on, which is the one every number on this
page was read on.

## Who owns a machine

A build is read-only once it is built. The program, the layouts and every piece
of text a diagnostic points at are written once and read by every machine made
from it afterwards; the one field of a build a machine writes is the count of
how many are standing on it, which is an atomic, so machines may be started and
freed from any thread.

A machine is one thread's while it runs. Its heap, its stack, its stamps, its
world and its report are its own, so two machines of one build may run at once
on two threads and neither can see what the other is doing. That is what makes
a program a thing a host can run on a worker thread, and it is asked rather than
asserted: `make check` runs two machines of one build on two threads and
compares what they answer.

What may be done to a machine from another thread is ask it to stop.
`kest_cancel` is one store of one word, written to be done from a signal handler
or from another thread while the machine runs, and the gate does that too — a
machine stopped from the thread that is not running it says `K0660` at the
instruction it had reached. Everything else is the owning thread's.

Nothing here locks anything. Two threads calling into one machine is two threads
writing one stack, and nothing will say so. Concurrency inside the language —
two things running in one machine — is not in v1.

`kest_cancel` is the other half and costs the same nothing — a host that wants a
running program to stop for a reason that is not a budget sets it and the
machine stops the same way, saying `K0660` instead. It is one store of one word,
so it may be done from a signal handler or from another thread while the machine
runs, and `kest_cancelled` says whether anybody has.

Costing nothing is the point. The check is at the jump and at the call rather
than at every instruction, because at every instruction it was a sixth of
everything: a frame step an entity went from `122 ns` to `142` on the machine
this was read on. Where it is now it does not move that number at all, and a
host that wants no budget writes nothing and pays for nothing.

## What running costs

Three numbers, measured rather than remembered, each over work that is kept in
this tree and run by `make time`. They are not a benchmark suite and there is
nowhere they are written down: they answer one question each — did the thing
this language exists for get slower — and `make check` reads only whether they
ran and what shape their lines are, because a duration is not a pass or a fail.

`tools/frame.kest` is a frame step, per entity, and what the two calls in it
cost. An array of value structs walked in order and handed one at a time to
helpers that take one and give one back, inside a promise that nothing reaches
the heap — the shape a frame in this language is written in — and beside it, in
the same rounds, the same work with those two helpers written out where they are
called. A call is the one thing the machine does that is not one instruction,
and the only way to weigh one from inside the language is to run the same work
without it.

```
117 ns per entity per step, 5 ns of it the two calls it makes, best of 7 over 10000, spread 13%
```

What those nanoseconds are spent on is a thing the machine can be asked rather
than guessed at: the build that checks itself counts every instruction it runs,
and `KEST_DEEP=1` makes it say so. Run at two step counts and two entity
counts and take the difference of the differences — a world is built once
however many rounds there are, and a round has a loop of its own however many
entities are in it — and a frame step an entity is **forty-four instructions**,
of which eight are `load.k`, five are `load`, four are `load2`, three are
`load.n`, three are `store` and one is `store.n` — twenty-four of the forty-four,
near enough three in five, move a value onto the stack or off it. The
arithmetic is six: two `mul.f32`, two `add.f32`, one `add.i.narrow.to` and one
`sub.i.narrow`. That is what a stack machine is, and it is where the next thing
to be gone after was found: it was fifty-seven instructions before `load.k` and
`load2` took the two commonest pairs of pushes and made each of them one
instruction, and what that bought is in D961. It was forty-six until an element
read out of a run went straight into the frame rather than through the stack
(D1012), and forty-five until an addition wrote its answer where it was going
(D1014).

Counting them is not free, and what it costs is the other number this build
says: over those forty-four instructions it asks its own compiler **fifty-two
questions** about what it is about to do — whose slots these are, whose
constants, whether what a frame holds is the shape the chunk was declared with.
That is the machine holding itself to what it was handed rather than trusting
it, and it is why the count above is worth what it says (D907). None of it is
in the build that ships.

Both numbers are held. Every figure in the two paragraphs above is measured by
`tools/check-costs.sh` on the run rather than read back from here, so one that
moves is a gate that fails and a line to change on purpose rather than a
sentence that quietly stopped being true.

`tools/crossing.kest` is a call against a crossing out. Two loops that differ by
one word: one calls a function of the program, the other calls one the host
provides, and both are one argument and one answer, so what is left between them
is the crossing.

```
19 ns for a call and 25 ns for a crossing, which is 6 ns more, best of 7 over 1000000 calls, spread 3%
```

Counted rather than timed, a turn of that loop is **nine instructions** when
it calls a function of the program and **seven** when it crosses out. The dearer
one runs two fewer: a crossing out is one instruction that does a great deal,
and a call is `call`, the frame written between them, and the callee's own
`load` and `return`. It is the clearest case on this page of a duration and a
count disagreeing, and it is why both are printed rather than either alone —
reading the count as though it were the time would have you move work across the
boundary to save two instructions and pay six nanoseconds for it.

`tools/reference.kest` is a hop of a loop, a read through an index and a read
through a reference. A `store<T>` hands out a `ref<T>` and can delete what it holds, so every
read through one asks whether what was handed out is still there; an index into
an array asks whether the place exists and nothing else. Both walks add one
field of the same values in the same order, so what is left between them is the
check and the optional it comes back in.

```
10 ns for a hop of the loop, 12 ns with an index read and 31 ns with a read through a reference, which is 19 ns more, best of 7 over 200000 reads, spread 14%
```

Counted the same way, a hop of that loop is **five instructions**, an index
read is **five** and a read through a reference is **eleven** — **nought** more
than the hop for the index and six more for the reference. The six are what a
reference is: the place it names, the stamp held against the one in the store,
and the optional the answer comes back in, which is a branch whether or not it
is nothing. The index read costs nothing over the hop because reading a field
of an element is one instruction; it was two until D1044, and the nanoseconds
in the paragraph above were taken before that.

The first of those three is what the other two are measured against, and it is
the one worth reading first: a hop of a `for` is ten nanoseconds here, so a read
through an index is about two and a read through a reference about twenty-one.
Every per-item number on this page carries a hop of a loop, because that is what
a program written over a run of things is made of — which is why the hop is the
one of these numbers that has been gone after, and why it is nineteen
nanoseconds no longer. What came off it was three instructions a turn — a copy
of the count into a name nothing wrote, and an arithmetic and the cut behind it
that are one instruction now — and then the machine stopped fetching where it
was and where its slots are through a pointer on every one of the rest.

`tools/inward` is the crossing the other way, and it is C because the thing
doing the calling is the host. One `kest_call` against one hop of a loop inside
one `kest_call`.

```
15 ns for a call in from a host and 18 ns for one the program makes in a loop, best of 7 over 1000000 calls, spread 6%
```

This is the one of the four the machine cannot count about itself. What it can
say is what it did: a crossing in runs **two instructions** of the program and
**three** of its questions, against **nine** and **twelve** for a turn of that
loop. Fifteen nanoseconds for two instructions and eighteen for eleven — which
means almost all of what a crossing in costs is outside anything the machine
counts. It is the frame the host writes, the arguments weighed on the way in and
the answer weighed on the way back, and none of those is an instruction.

That is why a host writing the frame itself and calling straight in is cheaper
than the machine reaching an instruction that starts a call, and it is why
neither number is taken away from the other. What these two counts leave out is
the work, and the duration beside them is the only thing that sees it.

Those numbers are the machine they were taken on and nothing else — six cores,
one of them busy with whatever else was running. What carries from one machine
to another is the shape of them: that a crossing out costs a few nanoseconds
over a call, that a crossing in costs less than a hop of a loop, that a
reference costs about half as much again as an index, and that all of them are
small against a frame step. A crossing an entity on this machine is six
nanoseconds against a hundred and seventeen, which is about a twentieth of the
step — so `no.host` is worth having where a frame crosses many times an entity
and worth little where it crosses once. A reference an entity is nineteen
nanoseconds more than an index, against the same hundred and seventeen, which is
about a sixth: a world of entities that can be removed costs about a sixth of a
frame more than a run of entities that cannot, and that is what the safety is
worth. The index read itself was two when those nanoseconds were taken, and is
one instruction fewer now. And the two calls a frame step makes are
five of its hundred and seventeen — a twentieth, for the shape the language is
written in rather than one body with everything in it.

Each leaves things out on purpose, and they are each other's omissions. The
frame leaves out starting up, compiling, crossing and allocating; the two
crossing numbers leave out everything a frame actually does. Reading one against
another is the point of there being three, and reading either against a number
somebody remembers is not: the first run on a cold machine reads about a fifth
high, because the processor has not decided how fast it is running yet, so two
numbers taken minutes apart can differ by more than a tenth for a reason that is
not the language. Run them twice, believe the second, and compare against a
number read the same way in the same sitting.

Which of these numbers are anybody's. The three lines above are durations and a
duration belongs to the machine this was read on — a processor that clocks
differently reads differently, and that is why `make check` reads only whether
they ran and what shape they are. The instruction counts under them are not: a
frame step is fifty-seven instructions wherever it runs, which is the same count
anywhere, and it is why the gate holds the count to the figure and prints the
duration beside it without an opinion. The three checks that say a number a
machine gave them say so in the sentence a reader reads, and so does this.

The spread at the end of each line is that question asked inside one run: how
far the slowest round was from the fastest. Past a quarter the line says the
machine was somebody else's, which is a thing to know rather than a thing to
fail — a number read while something else was running is not one to compare
against another.

## What only a host can do

A program cannot open a file, ask the time or read the words it was started
with. It asks a host, and a host may say no by not binding the door — which is
the whole of the capability model here and is what `extern` already meant.

`std.os` is the standard set of those doors: `read`, `write` and `exists` for a
file; `argCount`, `arg` and `args` for what the program was started with; `now`
for a clock in microseconds from somewhere nobody promises anything about.

What `now` is worth is the host's. The command line reads `CLOCK_MONOTONIC`
where the platform has it, which only goes forwards; where it does not, it falls
back to `timespec_get` and then to `clock`, and neither of those is that — the
first is a wall clock that can go backwards and the second is processor time.
A host that needs a clock that only goes forwards binds one that is, and the
table in *What this has been run on* says where the command line's lives. This
is a door rather than a promise: `deterministic` refuses it either way.

```kest
fn howLong(path: text) -> i32 {
    if let held = os.read(path) {
        return len(held)
    }
    return 0
}
```

A file that is not there and a file with nothing in it are different answers, so
`read` gives nothing rather than empty text. A file holding a nought byte is
refused the same way, because text that stops at a nought is a file handed over
as less than it is.

The list of what a host binds is the list of what a program may do. `kest` binds
all six and hands a program whatever follows `--` on its command line. An engine
embedding this language binds what it wants a program to have and leaves the
rest, and a program that asks for an unbound door is refused by name before it
runs.

And a body that promises `no.host` can reach none of it, which the compiler
proves. That is what makes `no.host` mean pure rather than merely fast.

## The simulation profile, version 1

A simulation that has to run the same twice — a replay, a rollback, a lockstep
game between two machines — needs to know exactly what is promised. This is
that promise, written as a version so that a later one can differ and say so.

**What it assumes about a target.** Two's complement integers of 8, 16, 32 and
64 bits; IEEE-754 binary32 and binary64 with the default rounding mode, round to
nearest, ties to even; a byte of 8 bits. The rounding mode is not changed by this
language and must not be changed under it: a host that changes it has left the
profile, and nothing here can see that it did.

**Where the state comes from.** A program starts with what it is given. There is
no uninitialised memory a program can read — every allocation arrives as nought
— and nothing is seeded from the run: not an address, not a clock, not the
order two files happened to be read in.

**Whole numbers.** Every width wraps at its own end, in both directions.
Narrowing keeps the low bits. Widening a signed number carries its sign and an
unsigned one does not. Division truncates toward nought and the remainder takes
the sign of the left-hand side. Division by nought and the remainder by nought
are refusals, not values.

**Floats.** `f32` arithmetic rounds to `f32` at every step, so a multiply and an
add are two roundings and never one fused one — no contraction, ever. `f64` is
binary64. Subnormals are as IEEE-754 says: no flush to zero and no denormals as
zero, neither of which this language sets and both of which a host may. Nought
has two spellings and they compare equal and hash alike. Not-a-number has one
spelling whatever a divide left in its sign bit, and compares equal to nothing
including itself.

**Crossing between them.** A float converted to a whole number truncates toward
nought, saturates at that width's end rather than wrapping, and a float that is
not a number converts to nought. The same answer folded and at runtime, because
it is the same function.

**Order.** An array walks by index, a store by place, text by character, a range
from its first to its last. A table walks its pairs in the order the puts and
takes left, which is an order rather than a sorted one — the same operations
give the same order, and a program that wants a sorted one sorts.

**Hashing.** FNV-1a from a fixed start, with nothing taken from the run. The
same value answers the same number in every run on every machine. `std.hash`'s
`join` is the one fold, so two programs folding the same fields fold them alike.

**Randomness.** `std.random` is a seeded source and nothing else. The same seed
gives the same run of numbers, and every function in it promises `no.host`.

**Host promises.** A door is outside this profile unless a host declares it
inside one, and this version has no way for a host to declare that — so in
version 1 the profile is exactly what a body that promises `no.host` can reach.
That is stricter than it needs to be and it is the safe direction: it also
excludes `sqrt`, `floor` and `ceil`, which IEEE-754 does specify exactly.

**Saying so.** A function that keeps the profile writes `deterministic`, which
the compiler proves the way it proves the other two, and which a host asks about
with `KEST_PROMISE_DETERMINISTIC` before it installs a step. The promise is what
a reader and a host act on; the profile above is what it means.

**What is rejected.** `Math.sin`, `Math.cos`, `Math.pow` and `Math.atan2` are
whatever a host binds them to; for the command line that is the platform's libm,
which is not required to round them correctly. Two platforms may differ in the
last bit and then diverge. Clocks, files and the words a program was started with
are outside it for the same reason: they are the host's.

**Conformance.** `examples/determinism.kest` exercises every rule above —
wrapping, narrowing, the crossings from float to whole number, how `f32` and
`f64` round, a nought with a sign on it, how far a number goes before it is
nought, a thing that is not a number, what order a walk goes in, how text
hashes, a seeded stream and a table walked — and folds the answers into one
number. Nine parts, and each is printed so a platform that disagrees says which.

**Tested on four platforms**, and this is what makes the profile a claim rather
than a hope: Linux x86-64 with GCC, Linux arm64 with GCC, Windows x86-64 with
MSVC and macOS arm64 with clang all build in CI, run every example, and are
held to writing the same bytes — the same conformance number and the same
output for every program in the tree. The job that does it diffs the traces and
is the thing that fails if a platform is added and does not agree. See D970 and
D1058.

One platform answering `12017043739776717972` says nothing on its own; four
answering it, on two instruction sets, is the promise.

## What is the same everywhere

Three questions get called determinism and they have three different answers
here.

**Does the same source mean the same thing?** Yes, and it is written down rather
than left to a compiler. Every integer width wraps at its own end; narrowing is
defined at every width; `f32` arithmetic rounds to `f32` at every step, so a
multiply and an add are two roundings and never one fused one; `f64` is IEEE-754
binary64. A `for` over an array walks it in index order, over a store in slot
order, and over text by character. `hash` is FNV-1a from a fixed start, with no
seed taken from the run, so the same value hashes to the same number in every
run on every machine. `std.random` is a seeded source and every one of its
functions promises `no.host`.

**Does the same program on the same machine do the same thing twice?** Yes.
Nothing here reads a clock, an address or an environment unless the program asks
a host for it, and nothing about a run is seeded from one. A reference is the
one value with something in it that is not the program's — the number the
process hands out, which depends on how many places every machine of that
process has made — and a program cannot reach it: there is no text of a
reference, no whole number of one, and `hash` is of the place it names. That
last one was not true until D1054, and a `deterministic` function that hashed
a reference answered differently on the second machine of a run.

**Is a simulation bitwise identical on two different platforms?** For everything
above, yes. For four things, no, and they are all host doors: `Math.sin`,
`Math.cos`, `Math.pow` and `Math.atan2` are whatever the host binds them to,
which for the command line is the platform's libm, and libm is not required to
round those correctly. Two platforms may differ in the last bit and then diverge.
`Math.sqrt` does not have that problem — IEEE-754 requires it to be correctly
rounded — and neither do `Math.floor` and `Math.ceil`.

**The profile that rejects what does not qualify is written down and is a
promise.** A step that has to be reproducible across machines writes
`deterministic`, and the compiler refuses the program if the body can reach
outside the profile above. Every operation in this language whose answer could
differ between platforms is behind a host door, so that reach is a host door and
nothing else.

**Four host doors are inside it, and they are declared so.** An `extern` may
write `deterministic`, which is a declaration and not a proof — a foreign body
is not here to be read, the same way it is not for `no.alloc`. `std.math`
declares `Math.sqrt`, `Math.floor` and `Math.ceil` that way and builds `round`
out of `floor`; the other seven it declares are outside. So a `deterministic`
body may take a square root, and `math.sqrt` is itself `deterministic` without
being `no.host`. That is the difference between the two promises, standing up in
the library rather than waiting for a day to come.

**What a host owes for those four**, because nothing here can prove it: bind
them to something that keeps IEEE 754. A square root has to be the correctly
rounded one, and a floor and a ceiling have to be exact and have to be right
about negatives and about whole numbers. A host that binds a fast approximate
square root has not broken this compiler and has left the profile, and
`examples/determinism.kest` is what says so — it folds all four and the number
it answers is the number the profile says. See D1060.

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
| `boxes.kest` | a shape that takes types, and a copy for every set of them |
| `camera.kest` | `std.vec` and `std.math` where a camera follows something |
| `chance.kest` | numbers that look random, and two runs from one seed |
| `colony.kest` | a world kept and worked on a day at a time, which is a program rather than a rule |
| `churn.kest` | one round over a world whose live set never changes, written six ways, which is what memory costs |
| `determinism.kest` | every rule the simulation profile promises, folded into one number |
| `embed.kest` | the program the engine beside it runs, frame by frame |
| `events.kest` | the host calling in, one crossing for a batch |
| `flags.kest` | bits, which is what a `u8` of state is |
| `frame.kest` | two structs that name each other, and a reference that may be nothing |
| `game.kest` | where a package's directories start, from a module name |
| `grow.kest` | an array whose size nobody wrote down |
| `holding.kest` | what a store holds while it grows, which was wrong until D1005 |
| `host.kest` | what an `extern` declares and what crosses at one |
| `inline.kest` | `[f32; 4]` where it stands, rather than a handle to four elsewhere |
| `inventory.kest` | a container written in Kest rather than built into the language |
| `least.kest` | one extern and one call, for the smallest host there is |
| `lines.kest` | a program that reads, and a host that has to provide the reading |
| `locale.kest` | where text stops being this language's business and starts being a host's |
| `lookup.kest` | a lookup that finds nothing, which is a value and not a crash |
| `math.kest` | a loop, a chain of `if`, and a function that answers with text |
| `numbers.kest` | what a number does at the end of its range, at every width |
| `parse.kest` | reading a line of fields out of the standard library |
| `registry.kest` | the same store and reference asked of assets naming what they are built from, which is not a game |
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

What it offers is something the file can write, and never the word that was
written: `unknown name `vec`, did you mean `vec`?` is a sentence disagreeing with
itself, and what is wrong there is not the spelling. A name from a module this file
has not imported is not a spelling to try, so it is not offered as one — and a
name that is exactly the one asked for, one import away, is not a spelling
mistake at all:

```
error[K0306]: unknown name `round`
   |
 7 |     return i32(round(d))
   |                ^^^^^ `math.round` is in this program, and this file does not import `math`
```

A type is answered the same way, because it is the same certainty:

```
error[K0301]: unknown type `Special`
  |
3 | fn start(s: Special) -> i32 no.alloc {
  |             ^^^^^^^ `two.Special` is in this program, and this file does not import `two`
```

A module of the library is further away than that: nothing imported it, so it is
in no program and no walk over one can find it. What is asked instead is the
library itself, for a file of the name that was written:

```
error[K0306]: unknown name `io`
  |
4 |     io.print("hi")
  |     ^^ `std.io` is in the library, and this file does not import it
```

That is said only to a file that has not imported it. Once the import is
written, the module has been read, and what is unknown is the name under it —
which is answered from what the module actually declares rather than guessed at:

```
error[K0353]: `io` has nothing called `pr1nt`
  --> spelt.kest:6:8
   |
 6 |     io.pr1nt("hi")
   |        ^^^^^ did you mean `io.print`?
  --> lib/std/io.kest:13:4
   |
13 | fn write(value: text) no.alloc {
   |    ^^^^^ this is the `io` that was read
```

A module written to is a module written to, whether or not the name under it
answers, so neither of those files is told its import is unused.

A module written without a name under it at all is a third thing, and is refused
rather than guessed at:

```
error[K0358]: `io` is a module, and this wants a value
  |
6 |     return io
  |            ^^ a module is a place to look and not a value: `io.write` is one of the names under it
```

Where a type is wanted it is the same mistake, and the same sentence about the
other kind of thing that is not one:

```
error[K0359]: `vec` is a module, and this wants a type
  |
5 | fn area(v: vec) -> f32 {
  |            ^^^ a module is a place to look and not a type: `vec.Vec2` is one of the names under it
```

And the pair of the refusal above them both — a name for a value, written where
a type goes:

```
error[K0360]: `SIZE` is a constant, and this wants a type
  |
4 | fn g(v: SIZE) -> i32 {
  |         ^^^^ a constant counts a run rather than naming one: `[i32; SIZE]`
  |
2 | const SIZE: i32 = 4
  |       ^^^^ declared here
```

In a signature this knows about the names declared before this file was reached,
which is what a signature is resolved among; in a body it knows about all of
them.

A generic's own type name is the one of these that is in no table at all — it
stands for whatever the copy being checked was given, and there will never be a
declaration of it to find:

```
error[K0361]: `T` is a type name, and this wants a value
  |
4 |     let n = T
  |             ^ a generic's type name stands for a type and not for a value: name a value of it instead
  |
9 |     return f(1)
  |            ^^^^ this copy was asked for here, where `T` is `i32`
```

The last of these is a type written where a type goes, with the wrong number of
type names after it. A shape that takes one and is written with none is told to
write them; one that takes none and is written with some is told the other way
round, because a shape that is not generic is not an unknown generic shape:

```
error[K0302]: `Plain` takes no types, and 1 is written here
  |
7 | fn take(p: Plain<i32>) -> i32 {
  |            ^^^^^^^^^^ write it without them: `Plain`
  |
3 | struct Plain {
  |        ^^^^^ declared here
```

One sentence for the four ways of getting that number wrong, `ref<T>` and
`store<T>` included: what the shape takes, and what was written. The shape is
named as the reader wrote it — a diagnostic about what somebody wrote calls it
what they called it — and where it came from is what the note carries.

A type name nothing settles is its own refusal, and so is a type name two
places settle differently — `K0343` and `K0363`, one code each, because a code
is what a reader looks up:

```
error[K0343]: what `T` is here cannot be told from what this is built with
  |
7 |     let e = Empty()
  |             ^^^^^^^ what tells `T` is what is passed, or where the value is going
```

That is the whole rule and it is one rule: a type name is settled by the values
a builder or a call is handed, and by where what they give is going — an
annotation, a `return` type, or the argument of another call. A builder and a
call are told the same thing because the same thing is true of both.

```
error[K0363]: two arguments disagree about what `A` is
  |
8 |     return pair(1, "x")
  |            ^^^^^^^^^^^^
  |                 ^ this one makes it `i32`
  |                    ^^^ and this one `text`
```

What it says to write is the names the shape is waiting for, and not a form
built out of them. `Box<T>` is code: `T` is a placeholder where the declaration
wrote it, so a program that also declares a `struct T` makes that form compile
and mean a box of something else. A suggestion that compiles and is wrong is
worse than one that does not, so the names are said as names — `` `T` ``, or
`` `A` and `B` `` — and where they were written is what the note carries.

A diagnostic holds eight places and counts what it could not show — `and 2 more
places` in the words, `leftOut` in the JSON. Which eight is not the order they
were declared in: where a refusal lists what it found, the near misses go first.
A call that named several functions of one name is shown the ones that take as
many arguments as were passed before the ones that do not, and of those the ones
that agree about most of them. A note put on a diagnostic that is
already finished takes the last place rather than being left out, because what
reaches back to one is a sentence about the whole of it: which copy of a generic
a body's sentences are about is not the ninth thing a reader wants, it is what
the other eight are about.

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

Every object a command writes begins with `schema`, which says what shape the
rest of it is in. It is not the compiler's version: a compiler that has moved on
in ways no tool can see writes the same objects, and a field that changes what it
means is a tool reading the wrong thing whatever the compiler calls itself. The
number goes up when a field changes meaning, is taken away, or is added where a
reader was told the list was everything — and a tool that reads it first knows
before it reads anything else whether it understands what follows. It is 2: it
was 1 until `proved` was added to every function `check` writes out, which is a
field added where a reader was told the list was everything.

The same run with `--json` emits the identical set, notes and all, for
tooling and for models repairing their own output, which is this:

```json
{
  "schema": 2,
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
  "errors": 1,
  "cost": 47032,
  "read": [{ "file": "bad.kest", "bytes": 214, "mark": "9ae16a3b2f90404f" }],
  "source": 214,
  "mark": "1c8a7e0b6f53d9a2",
  "module": "doc"
}
```

`module` is the name this file puts its own declarations under, which `check`
says and the others do not: a file that says `module examples.math` declares
`examples.math.factorial`, and a file that imports it writes `math.factorial`.
It is null for a file that names no module, whose declarations are under
nothing. A tool that has this object and a name in the file has where that name
is declared, which is what it is for.

Every declaration says its `module` too, beside its name. A name lives under
the whole of what its module calls itself and what comes after may hold dots of
its own — `std.io.Io.write` is a capability's function inside `std.io` — so a
tool splitting a name at a dot would invent modules that are not there. It is
written rather than left to be worked out, and it is null for a declaration
under no module. See D1039.

Every field of a struct says whether it is `own`, which is whether the module
that declared the shape is the only thing that may name it. A tool that shows a
shape shows what a file writing it would be allowed to write. See D1041.

Every function says its `typeParameters`, each a name and the `wants` beside
it: `[{"name": "K", "wants": ["compares"]}, {"name": "V", "wants": []}]` for
`std.table.set`, and an empty list for a function that takes no types. What a
generic asks of what it is given is part of what it is, so a tool is handed it
rather than left to find out at a call. See D1043.

Beside the diagnostics is what the run cost the compiler: `cost` is how many
bytes reading and checking the program took, and after `emit` how many that and
compiling it took. `lex` and `parse` say it too, and they stop where they stop —
at the tokens and at the tree — so the four numbers beside each other are what
each stage of reading a file costs. For `lib/std/text.kest`, which is 502 lines:
47956 bytes as tokens, 118617 as a tree, 155824 checked and 183233 compiled.
Most of what a check costs is the reading under it, and most of the reading is
the tree.

Those are bytes of memory on the machine this was read on, and they are held:
`tools/check-costs.sh` measures all four over the same file and reads this
sentence back, so a stage that gets dearer or cheaper is a line here to change
on purpose. It had been wrong for a while before anything read it — the file had
grown by two lines and compiling it had got a third cheaper — which is what a
number written down and compared against nothing does.

`read` is every file that cost went on — the one named and everything it
imports, each with how many bytes it is — and `source` is those added up. A cost
on its own has nothing to divide it by: a program of four lines that imports the
library costs what the library costs, and a tool dividing by the file somebody
named would call it fifteen times dearer a byte than it is. Only the compiler
knows which files it read, so it says them. For `lib/std/text.kest` that is one
file and 17323 bytes, against the 183233 it costs to compile.

Four things get called identity, and they are four different questions. What a
`check` listing answers is the second of them.

| | The question | Where it is answered |
| --- | --- | --- |
| the declaration | which declaration is this | its module, name and place, which `check` lists |
| the signature | what does it promise a caller | `signature`, below |
| the body | what did it compile to | `body`, which `emit` says per chunk |
| an object | which value is this, while it runs | a `ref`, which is a world, a stamp and a place |

Each function `check` lists carries a `signature`: a number folded from the
qualified name, the types it takes and gives, and the promises. It survives a
blank line, a comment, a moved declaration, a renamed local, a changed body and
a generic's type parameter renamed — `fn pick<T>(a: T) -> T` and the same
declaration written with `U` are one signature, because the name is the
declaration's own and no caller can see it. It moves when the signature or a
promise moves, which is when a caller has to be told.

```json
{ "name": "id.alpha", "parameters": ["i32"], "gives": "i32",
  "noAlloc": false, "noHost": false, "deterministic": false,
  "foreign": false, "named": true, "signature": "dfe41484f0987403" }
```

What it is not is a name that survives refactoring: a declaration renamed or
moved to another module is a different signature, which is the truth rather
than a shortcoming — a host that has to follow a declaration across a rename
needs a mapping somebody wrote down, and a fold of the current spelling cannot
be one.

Each chunk `emit` lists carries a `body`: a number folded from the instructions
and the constants they reach, and nothing else. It is what a host rebuilding a
body asks — this function's code changed and its signature did not — and it is
not a semantic identity: two bodies that mean the same thing and compile
differently are two numbers.

`parse` says what that tree is made of beside what it cost, and `check` says how
many types it made beside the ones a program declares — one for every signature,
every optional and every run of something:

```json
{ "schema": 2, "diagnostics": [], "errors": 0, "cost": 41180, "held": 41180,
  "askings": 5, "tokenBytes": 12, "tokenRoom": 256,
  "tokens": [], "comments": [] }
```

```json
{ "schema": 2, "diagnostics": [], "errors": 0, "cost": 156080, "nodes": 978,
  "nodeBytes": { "expression": 48, "statement": 48, "declaration": 88 } }
```

```json
{ "schema": 2, "diagnostics": [], "errors": 0, "cost": 178880, "held": 154304,
  "askings": 380, "typesMade": 58, "typeBytes": 168 }
```

`tokenBytes` is the same thing for a token, and the same reason: reading a file
costs the file and the tokens made of it, and telling that from the sizes the
array grew through wants the count and the weight from the run that measured the
cost. `tokenRoom` is how many the array had room for against how many went in
it, the way a compiled function says `room` beside `bytes`. The array grows
where it stands rather than being copied, so what it grows by is chosen for the
room it leaves: a quarter more each time, and never room for more tokens than
there are bytes left to make them out of, because the shortest token there is
is one byte.

`held` is what is still held when the answer is written, against `cost` which is
what was asked for on the way to it. They differ by what a stage left behind for
nobody. The tokens a file is read into are dead the moment its tree is made — a
node holds a span into the source and never a token — and the tree is dead once
the last copy of every generic has been compiled and the promise has been held
against what was emitted. Both are read into arenas of their own and given back
where the stage that reads them ends, which is about half of what compiling a
program asks for. A ceiling refuses against `cost`, because what a host was
asked for is the same number whether it was kept or not.

`askings` is how many times the arena was asked for anything, which tells a
stage that keeps a lot from one that asks a lot. What an arena is asked for is a
thing somebody declared, or an array that doubles, and never an entry at a time
— so the number grows with what a program has in it rather than with how big any
of that is.

`typeBytes` is what one type weighs, and `tokenBytes` and `nodeBytes` the same
for the other two things a build is mostly made of. What a build holds when it
is done is the file it read, the types it made and the module it wrote: the
tokens and the trees are given back where the last stage that reads them ends.

`nodeBytes` is what one weighs on the machine that answered: fifty-six bytes for
an expression here, and something else where a pointer is another width. It is
said rather than left to be written down, so that a tool holding a tree's cost
against what it is made of has both numbers from the same run.

978 nodes for 443 lines, and the 93344 bytes the tree added over the tokens is
about ninety-five a node — of which fifty-six is the node itself for an
expression, sixty-four for a statement and eighty-eight for a declaration. The
rest is the lists a block and an argument list are made of. A host asks the same question with `kest_build_cost`, which
is where the command line reads it from — the compiler's own work, not the
program's, which is what `kest_heap_used` is about. A host that compiles at
startup pays it once; one that reloads a file whenever it changes pays it every
time, and a rebuild costs what the first build cost, because nothing is carried
from one build to the next. What a reload costs is those two numbers and no
others — the build and the machines — and both are the same every time round,
because nothing in this library outlives a build: there is no global state for
anything to be carried in. `examples/embed.c` goes round three times and reads
the same pair each time, and what says the memory went back rather than being
counted twice is the sanitised build, which is told at the end of a run what is
still held.

`kest_build_held` is the other half of that pair: what a build is *still*
holding, against `kest_build_cost` which is what it asked for on the way. They
differ by what a stage left behind for nobody, and a build that was checked and
not compiled holds more than one that was compiled, because the trees are dead
only once every copy has been made out of them.

`emit` says a `codeMark` as well, which is what the machine will run rather than
what was read to get there: the instructions, the constants, the names, the
promises and the shapes that cross the boundary, and not where any of them was
written. A program with a comment added has the mark it had, and one where an
operator changed does not. A host caching what it compiled asks that; a host
watching files asks the one below. `kest_build_code_mark` is the same number.
Neither is about where a file is: a program copied somewhere else marks the same.

Two machines agree about a program's code mark when they lay it out the same
way. The numbers in it are folded low byte first whatever order a machine keeps
its bytes in, so the mark says nothing about the compiler that took it — but the
layouts are in it, and a shape that is eight bytes wide on one machine and four
on another is not the same program to run. That is the mark saying so rather
than hiding it.

```json
{ "schema": 2, "diagnostics": [], "errors": 0, "cost": 230607,
  "codeMark": "d02b0a4a1e5c3f81",
  "folds": 3, "asked": 7, "copies": 66, "copiedBodies": 23,
  "copiedBytes": 3887,
  "holds": { "code": 4608, "origins": 5632, "constants": 1024,
             "layouts": 564, "chunks": 3384, "types": 7560,
             "globals": 2304, "foundBy": 256, "composed": 64,
             "files": 832, "lines": 1764, "paths": 18, "names": 5 } }
```

`holds` is what is still held when the answer is written, by what asked for it.
`held` on its own is a number with nothing under it: the code and the room it
sits in, where every instruction came from, the values worked out where they
stood, the layouts a host is told about and the chunks they hang off are the
module's; the types made, the names registered, the table they are found in and
the composed types kept so that two of one are one are the checker's. The
files read and the shape kept for each, where every line of every file begins,
the paths they came from and the names they call themselves and import are the
loader's. What is left over is the source every one of them was cut from, which
`read` says, and the small change of a build — the structures the three of them
hang off, the diagnostics, and the names made by joining a module to a
declaration. Every number here is part of `held` and none of them is all of it.

`lines` is four bytes a line, held for the life of a build so that a message can
say `12:7` without counting newlines from the top of the file. It is built once
per file, counted before it is taken so that it fits exactly.

`copies`, `copiedBodies` and `copiedBytes` are what one body written for many
types cost this program. A copy exists per set of types a generic is called
with, so the price is the program's rather than the compiler's: `copies` counts
the chunks that are one of several compiled from one body, `copiedBodies` how
many bodies those came from, and `copiedBytes` what the ones past the first are
in code. Two chunks written the same and declared in one place are one generic
compiled twice, which is the same rule a reader of the list below reads them by.

`folds` is how many values this compiler worked out where they were written: one
per constant, whatever a program reads it. A constant is worked out at its
declaration and every use of it reads what came of that, so a program that names
one forty times says the same number as one that names it once.

Each function `emit` prints says `folded` beside what it takes and how deep it
goes: how many of its values were worked out where they stand and put in the
chunk rather than built by instructions every time it runs, with `foldedSlots`
for how big they are — eight values might be eight numbers or eight structs, and
a slot each is the least they can be. A case written in a
body, a hash of a piece of text, a run of numbers indexed by one — each of those
is a value a frame does not pay for. The three kinds of value a compiler works
out — the numbers the checker read, the constants, and these — add up to what a
run says it worked out.

Both are said by every command that builds, so `check` and `emit` can be read
against each other: the checker works values out to refuse a count below nought
where it is written, and the compiler works out every constant, so what `emit`
says is what `check` said and more. `asked` beside them is how many times the
folder was asked and there was nothing to work out — a field of a local, a name that is not a constant. The compiler asks
of anything that might be one, because asking is how it finds out, and the two
numbers together say how much of that finding out answered: 97 of 346 for
`examples/numbers.kest`.

Each file also carries a `mark`, and the object has one for the program: a
number that moves when the bytes move, written as sixteen hexadecimal digits. It
is FNV-1a over the file, which is what `hash` over text is in the language, and
the program's is every file's folded in the order they were read. What it
answers is whether this is the same file, and the same program, which a size
cannot: two edits that keep the length are the same size and a different
program. `kest_build_read_mark` and `kest_build_mark` are the same numbers for a
host.

Each layout `emit` lists carries a `mark` of its own: a number folded from the
size, the alignment, whether anything in it is a tag, and every piece's offset,
kind and name. It is what a host that saved bytes asks before it reads them back
into a program it has just built again — the same number is the same shape and
nothing to migrate, a different one is a field that moved, changed width or
changed name. It does not say which of those it was; a host that needs that
walks the pieces. `kest_layout_mark` is the same number for a host, and
`examples/embed.c` asks it of `Row` across each of its three reloads.

What that cost was paid for is `kest_build_read`, which is every file the
loader read, by position and ending at NULL, with `kest_build_read_bytes` for
how big each of them is and `kest_build_source` for all of them added up. The
list is what a host cannot work out for itself: an import names a path relative
to the file that wrote it, so what a program is made of is settled by the loader
rather than by whoever named the first file. A host that reloads a program when
something changes watches these; one that watched only the file it named would
keep running a program whose library moved under it. It is also what a cost is
divided by — the four-line program that imports the standard library costs what
the library costs, and 39992 bytes of source went into the 629470
`examples/embed.kest` costs.

What a machine is made of is its own, and `kest_runtime_cost` says how much:
the stack, the frames, the table of what the host provides, and the machine
itself. A machine of 4096 slots is 33,392 bytes here and one of 8192 is 66,160
— the difference is the slots, at eight bytes each, because that is what a slot
is. It comes out of the machine's own memory rather than the build's, so
starting one takes nothing from the build and freeing one gives all of it back:
a host that starts a machine, frees it and starts another pays for one machine
rather than for every machine it has ever started. What is left on the build is
the list a machine says things into, which has to outlive it — what a machine
that failed to start said is what the build reports. It is what the compiler has to say about its own work, which
is what it asks of every program it reads — and it is counted before this JSON
is written, because a number that counted the writing would grow with how much
a tool asked to be told.

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
```

So `kest call x.kest math.min 3 7` in a shell is `3` and nothing else, whatever
the program says on its way there. `kest check --json` adds what the program
holds beside what is wrong with it: every type with its
layout and every function with what it takes, what it returns, whether it
promises `no.alloc`, whether the host has to provide it, and where it was
declared.

Beside what each function *says* is what the compiler *proved* about it, under
`proved`. The promises' own walk of the call graph runs whether or not anything
promises anything, so what it found is there to be read: whether the body
reaches the heap, whether it crosses to the host, whether anything in it is
outside the deterministic profile, and — the one field that is advice rather
than a fact — which promise it keeps and does not make. A function the host
provides has `proved` false, because there is no body here to walk and what it
declares is all there is. `kest check --cost` says the same thing to a person:

```
what the compiler proved                 heap   host   varies could promise
math.factorial                           no     no     no     no.alloc no.host deterministic
math.main                                yes    yes    yes
```

There are no durations in it. A count of instructions is not a time, and a
static count printed as nanoseconds is a number nobody measured; what a frame
costs is `make time` and `kest tick`, which run something. See D976.

The file that was named is given in full, with a function the host
has to provide marked as one, and a line for each module it imported.

```json
{
  "types": [
    {
      "name": "doc.Point",
      "module": "doc",
      "kind": "struct",
      "slots": 2,
      "bytes": 8,
      "align": 4,
      "file": "doc.kest",
      "line": 3,
      "column": 8,
      "fields": [
        {"name": "x", "type": "i32", "slot": 0, "byte": 0, "own": false},
        {"name": "y", "type": "i32", "slot": 1, "byte": 4, "own": false}
      ]
    }
  ],
  "functions": [
    {
      "name": "doc.hurt",
      "module": "doc",
      "typeParameters": [],
      "parameters": ["doc.Point", "i32"],
      "gives": "i32",
      "noAlloc": false,
      "noHost": false,
      "deterministic": false,
      "foreign": false,
      "named": true,
      "proved": {
        "walked": true,
        "reachesHeap": false,
        "reachesHost": false,
        "notDeterministic": false,
        "couldPromiseNoAlloc": true,
        "couldPromiseNoHost": true,
        "couldPromiseDeterministic": true
      },
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

And an import nothing writes:

```
warning[K0511]: nothing in this file writes `sort`
      take the import out: a module named here is read and compiled whether anything comes through it or not
```

That one has a number behind it. A module named in an import is read, parsed,
checked and compiled whether or not a name comes through it: `import std.sort`
in a file that never writes `sort` costs 1484 bytes of source read and 12628
bytes of the compiler's memory, measured on the two-line program that is the rest
of this paragraph. What says an import is worth its place is a name written
through it — a call, a type, a case of an enum — and nothing else does.

And a local nothing reads:

```
warning[K0512]: nothing in this body reads `spare`
      take it out: a local is a name for a value in one body, and one nothing reads is a value nobody asked for
```

That is the one of these nobody at all can be relying on: no host can ask for a
local and no other file can name one, so a `let` nothing reads is a value worked
out for nobody. Writing to one is not reading it — `x = 6` and nothing else still
says this.

An `if let` whose name nothing reads is asked about too, and told something else:

```
warning[K0512]: nothing in this body reads `there`
      ask whether it holds anything instead: `if what != none`
```

because that one is a question asked the long way rather than a value nobody
wanted, and taking the binding out would take the question with it. A `while let`
is not asked. A loop that runs while there is something has no other form — a
condition that only asks takes nothing out and runs for ever — so a name it never
reads is the only way to write what it writes.

The position a `for` binds beside an element is asked about for the same reason:

```
warning[K0512]: nothing in this body reads `at`
      write the walk without it: a `for` over one name walks the same and binds no position
```

What a `for` binds on its own is not, and neither is what a `match` case binds:
one is how a program says how many times to go round, and the other is the only
way to write the case at all. The line under all of them is whether the program
had another way to say it.

All four are said about the file that was named and not about what it imported,
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

What the command line refuses before it has read anything is refused the same
way. A mistake in the words themselves — a command there is none of, a command
with no file, a count that is not a number or is outside what a tick can carry,
two counts, a list of events that is not one — is `K0649`, in whichever form
the run asked for:

```
error[K0649]: unknown command `walk`
```

`--json` is one of the words, so which form to answer in is read before any of
them is refused: it is the same object every command writes, on the standard
output where a tool is reading, and the help a person gets goes to the standard
error or nowhere. A file `fmt -w` could not write is `K0706` beside the object
for that file, because the object says whether the file is in the one form and
a file that could not be written is not a file in the wrong form — running `-w`
again would not fix it.


`kest emit --json` adds the instructions: what is laid out, what the host must
provide, what the machine needs before any of it runs, and every function with
its code as an offset, a name and the numbers after it. `bytes` is what that
code takes, which a reader could otherwise only get by adding up the
instructions and knowing how wide each of them is — and the last one listed
starts inside it. `room` is what it is held in, and `constants` how many of
them it keeps: a chunk's arrays double from a floor measured against what a body
holds — the middle one is ninety-two bytes of code in thirty instructions with
three constants — so the room a body ends in is between what it took and twice
that, and half of them never double at all. Beside every byte of code are four bytes saying where in the
source it came from, which is what a message at the line that asked is read off. What it needs is the
two numbers `kest_needs` answers with, and they are null when there is no
answer — a run of calls that comes back round has no deepest frame, and a call
through a value reaches what is not known until it runs, so `why` says which it
was and `where` says in which function. Beside them is `entries`, the same two
numbers for each of `main`, `onEvents` and `onEvent` the file has, in the same
shape:

```json
{
  "layouts": [
    {"bytes": 8, "align": 4, "tagged": false, "of": "doc.Point",
     "pieces": [{"byte": 0, "is": "i32", "name": "x"},
                {"byte": 4, "is": "i32", "name": "y"}]},
    {"bytes": 8, "align": 4, "tagged": true, "of": "doc.Shape",
     "pieces": [{"byte": 0, "is": "i32", "name": null},
                {"byte": 4, "is": "payload", "name": null}]}
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
      "bytes": 7,
      "room": 128,
      "constants": 1,
      "parameterSlots": 1,
      "slots": 1,
      "deep": 2,
      "folded": 0,
      "foldedSlots": 0,
      "noAlloc": false,
      "noHost": false,
      "deterministic": false,
      "body": "f742d7f540da42c1",
      "why": null,
      "where": null,
      "least": {"slots": 8, "frames": 2},
      "code": [
        {"at": 0, "op": "load", "operands": [0]},
        {"at": 3, "op": "const", "operands": [0]},
        {"at": 6, "op": "add.i", "operands": []}
      ]
    }
  ]
}
```

`of` is the type a layout is the layout of, written the way a message writes
it. Without it two layouts that differ only in what they are of read as one —
every `[T]` is one word whatever `T` is, and `u8` and `bool` are both a byte —
and a reader counting what a module holds would say half of them were written
twice. They are not: the machine reads which type a layout is of to pack a
value across the host boundary, to name an enum's cases and to ask whether a
value holds its own memory, so `[i32]` and `[text]` are one shape and two
layouts. What is written twice is what says the same type twice, and that a
reader can now see.

Every one it has is there whether or not it differs from the whole,
because a tool looks one up by name; the text form leaves out the ones that are
the same, because a reader would be reading them twice. What the text form decorates — the value behind a constant, where a
jump lands — is left as the numbers there, because a reader that wanted prose
would not have asked for JSON.

`kest lex --json` says the token stream, which is the whole of what that
command answers:

```json
{"tokens": [{"kind": "identifier", "line": 1, "column": 4, "text": "main", "carries": false}]}
```

`carries` is whether a line ending after that token carries on to the next one,
which is the one thing about a token that cannot be worked out from the token:
the rule is the lexer's, and anything that writes this language back out needs
it. A second copy of it in a tool is a second copy to keep right.

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
back, the peak between calls, what the heap holds at the end and what the run
was handed altogether in the object, and a program that takes no events has
neither key. A handler that gives
nothing has `gave` as null rather than nought, because nothing and nought are
two answers:

```json
{
  "onEvents": {"crossings": 1, "gave": 174933},
  "onEvent": {"crossings": 1024, "gave": 174933, "peak": 24},
  "events": {"count": 1024, "lent": null},
  "machine": {"bytes": 296, "slots": 8, "frames": 1},
  "heap": 24,
  "taken": 24,
  "allowed": 0,
  "thrown": 0
}
```

`allowed` is what the machine was given to put on the heap: what the command
was allowed, less what reading and compiling took and less what a machine for
the program costs. It is nought when nothing said `--room`, because then there
is no ceiling and nothing to divide. A reader holding the three against what
the command was given is reading this command's own arithmetic, which is the
one thing a run that never reaches its ceiling cannot say.

`heap` is what the program is holding when the last event is over and `taken`
is every byte it was handed on the way: they are the same number for a run that
keeps everything it makes and they are not for a run that replaces what it
holds, which is what D996 is. A frame budget is sized by the second and a
memory budget by the first. `kest call --json` says both as well.

The words say the same six things the object does, `cost` included: what
reading and compiling the program cost, what the machine is made of, and what
the heap holds at the end — the three numbers a host pays, in the order it pays
them. `check`, `emit` and `call` write `cost` in the object and not in the
words, because their words are an answer rather than a measurement: `kest call`
prints what the function gave back and a shell reads it, and three lines of
budget beside it would be three lines to strip. `tick` is the command that
measures, so `tick` is where the numbers are said in both.

`machine` is what the machine that ran them is made of, beside what the frames
cost: a host reading this is choosing two things at once, what a frame costs it
and what having a machine at all costs it, and only one of them was here. The
words say the same line. The numbers are small because the command line asks
the program what it needs and hands that over — a machine nobody asked about
takes the usual numbers, which is half a megabyte of stack for a program that
wants eight slots.

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

