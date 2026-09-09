# Decisions

Append-only. A decision that turns out wrong is superseded by a later entry,
not edited or deleted. Each entry says what was decided, why, and what the
claim rests on: a measurement, or an argument.

Evidence marked *measured* comes from the predecessor research programme at
`Rawframe-Project/kest-research`, which produced no language but did close two
comparative workloads and one prototype. Everything else is marked *argued*.

## What a later decision replaced

Nothing here is edited, so an entry that is no longer what this project does
reads exactly like one that is. This is the list of those, and
`tools/check-docs.sh` holds it: a decision whose body says it supersedes
another and is not named here is a check that fails.

| Was | Is now | What changed |
| --- | --- | --- |
| D064 | D115 | a count may be the name of a constant, not only a number |
| D183 | D185 | a name clash is refused for the whole program again |
| D182 | D222 | the example that only resolved runs and checks itself |
| D238 | D239 | a handle is asked where it came from, not what is written at it |

---

## D001. Implement Kest in C11, as one project

**Decided.** The compiler, the runtime, and the tooling are all C11, in one
repository, built by one `make`. No dependencies beyond libc. Public API is
prefixed `kest_`, matching the `maul2d` and `maul3d` libraries.

**Why.** The runtime ships inside a user's game, so it needs a stable C ABI,
zero dependencies, small size, and the ability to link into anything. A
compiler in a second language buys exhaustive matching over a growing set of IR
node kinds, which is real, at the cost of a split toolchain and two build
systems before the language exists at all.

Supersedes the predecessor's ADR-0004, which put the toolchain in Rust and the
runtime in C. Its reasoning holds; its cost does not, at this stage.

*Argued.*

---

## D002. Bytecode virtual machine

**Decided.** Source compiles to bytecode and a VM executes it. Native code
generation is not a day-one concern and LLVM is not a dependency.

**Why.** Iteration and hot reload must not wait on native code generation, and
a VM is the shortest path to a language that runs. An ahead-of-time backend
that emits C stays open as a later addition, because emitting C reaches every
console toolchain the host engine already reaches.

*Argued.*

---

## D003. C-family brace syntax

**Decided.** Braces for blocks. No semicolons; a newline ends a statement. No
parentheses around `if` and `while` conditions. `fn` for functions, `let` for
bindings, `//` for comments, `"{x}"` for interpolation. Keywords are English.
Identifiers are full UTF-8, so `let hız = 5` is legal.

**Why.** Three scenarios decide it, and all three are about editing rather than
reading. A model rewriting lines 12 to 20 of a file must know the block depth
of line 12; under braces that is in the text, under indentation it must be
derived from the eleven lines above. A fragment pasted at a different depth
changes meaning silently under indentation and does not under braces. And
`end`-style terminators do not match their opener, so the error surfaces far
from its cause, which breaks the fix loop that matters most here.

Braces are also what the overwhelming majority of existing code uses, which is
free accuracy from any model trained on it. The predecessor's own research
concluded that unfamiliar syntax on its own is a disadvantage, and that AI
friendliness is a compiler and tooling problem rather than a syntax one.

The two `.kest` files the predecessor did write, `graph.kest` and
`frame-step.kest`, already use exactly this shape. This decision adopts them
rather than re-deriving them.

*Argued, with independent convergence.*

---

## D004. The parser is strict

**Decided.** One canonical spelling per construct. No aliases, no tolerated
near-misses, no accepting `;` or `elif` or `and` as alternates. A program that
is not written the one way is refused.

**Why.** A tolerant parser makes the language two languages: the one that is
documented and the one that is accepted. The second is undocumented, grows by
accident, and can never be removed. Strictness moves the entire burden onto the
diagnostics, which is where D008 puts the effort.

*Argued.*

---

## D005. Static types, inferred inside a body, declared at boundaries

**Decided.** Function signatures carry types: `fn add(a: i32, b: i32) -> i32`.
Bodies do not: `let x = a + b` infers. Host boundaries are always declared and
never inferred.

**Why.** The cost contract in D006 cannot be proved without types, and value
structs need a size that is declared rather than assumed. The predecessor's
research also found static types to carry substantial tooling, refactoring, and
AI value.

The inference boundary is *measured*: on an eighteen-function frame step, four
call hops deep, inferring inside the unit costs 2 annotations where requiring
them at every call costs 16.

*Measured, for the inference boundary. Argued, for static typing itself.*

---

## D006. Value structs beside managed references

**Decided.** A struct is a value. It lives where the frame does, is moved
rather than copied when it is a temporary, and is lent to a callee that neither
keeps it nor writes through it. Managed references exist alongside, for the
object graph.

**Why.** *Measured.* With managed references as the only aggregate, every
helper returning a `Vec3` escapes because a returned object is one. A frame
step written the way somebody would write one took six allocations per frame
inside a promise not to allocate, and no annotation could fix it, because
nothing about the annotation was wrong. The only version that kept the promise
had the vectors taken apart and their components carried separately through
every signature.

With value structs the same program, with all six of its functions and their
signatures intact, runs at zero allocations, zero copies, and zero bytes.

*Measured.*

---

## D007. The host boundary is bulk and borrowed by default, and has two directions

**Decided.** The easy shape to write for crossing between Kest and the host is
one crossing that carries a borrowed view of contiguous storage. Per-value
crossing stays expressible and is visible where it happens. The inward and
outward directions are specified separately and neither is inferred from the
other.

**Why.** *Measured.* Across five languages and seven crossing shapes:

| Shape | Slowest against fastest |
| --- | ---: |
| One crossing per value | 9.63x |
| 500 items per crossing, copied | 15.33x |
| 100,000 items, borrowed slice | 1.05x |

Borrowing is the property, not batching: a copied batch of 500 spreads wider
than a single value, because it leaves every element's conversion cost in place
and moves it inside the crossing.

The two directions do not mirror each other. On the outward path two of the
five implementations swap places against the inward path, one of them by a
factor of two and a half. A single number for a language's boundary cost does
not exist.

This also closes a thesis: a layout-aware ABI is not where Kest can win. On the
bulk borrowed crossing, Daslang's tree-walking interpreter is statistically
tied with C++. There is no room above it to occupy.

*Measured.*

---

## D008. Diagnostics are a feature

**Decided.** Never stop at the first error. Every diagnostic carries a stable
code, a span, and where knowable a suggested fix. Unknown names report the
nearest match. A diagnostic about something deeper than its raise site reports
the path down to the responsible body. `--errors=json` emits the same set as
machine-readable JSON.

**Why.** D004 makes the parser strict, so the diagnostics carry the whole
usability burden. Reporting everything in one pass collapses a five-round fix
loop into one round, for a person and for a model equally.

The path-reporting rule is *measured*: when a refusal named only the entry
point, the author walked a mean of 2.17 call hops to find the body that had
actually changed, worst case 3. Reporting the path down to it took both figures
to 0.

*Measured, for path reporting. Argued, for the rest.*

---

## D009. No AI or network primitives in the language core

**Decided.** There is no `prompt`, `ask`, or model-call expression in the
language. Such a thing is a library, reached by `import`.

**Why.** Three reasons, each sufficient. It puts network calls, credentials,
and asynchrony into the core of a language whose first constraint is a frame
budget. It couples the language's *syntax* to a vendor API that will change
within a year, and syntax is the one thing that cannot be revised cheaply. And
it buys nothing a library does not buy, while being impossible to remove.

*Argued.*

---

## D010. A stack machine, for now, and it is written down as "for now"

**Decided.** The virtual machine is a stack machine: operands are pushed and
popped, and locals live in numbered slots of the frame. A register machine of
the kind Lua 5.4 and Luau use is the faster design and is not being built yet.

**Why.** The register machine's advantage is real and it is also the whole
work: it needs a register allocator, and that allocator is not a piece that
can be added to a stack compiler later, it replaces it. The choice is
therefore between a language that runs this month and a faster instruction
dispatch in a language that does not run.

What makes the trade acceptable is what was already measured. W11 found that
the crossing between the guest and the host is where a frame budget goes, not
interpreter dispatch: a bulk borrowed crossing puts a tree-walking interpreter
level with C++, while a per-value crossing costs four to ten times in every
implementation including the ones with no interpreter at all. D007 puts the
design effort there. Dispatch is a constant factor on a curve whose shape is
set somewhere else.

**What would revisit it.** A Kest program, running, whose profile is dominated
by dispatch. That is a measurement this project cannot take today because
there is no program. Taking the decision now on an argument, and saying so, is
better than taking it later on the same argument and calling it a finding.

**What is kept open by it.** Values carry no runtime tag, and instructions are
typed: an `i32` add is a different opcode from an `f32` add. That is the part
that matters for speed and it is independent of stack versus register, so the
work is not lost if this decision is reversed.

*Argued.*

---

## D011. A struct is built with call syntax

**Decided.** `Vec3(1.0, 2.0, 3.0)` builds a `Vec3`. There is no braced
literal.

**Why.** The grammar, and then the layout.

`if p.y < 0.0 {` parses without any special rule only because no expression in
the language can begin with a brace. A braced literal breaks that, and every
language with one carries a rule about where it is allowed: Rust forbids it in
a condition, Go has a parser flag for it. The rule is invisible until it bites,
and it bites in exactly the place a person is not thinking about grammar.
Adopting call syntax means there is no rule to write, and none to remember.

The second reason is that it costs nothing to compile. Fields are pushed in
declaration order, which is the layout D006 gives a value struct, so the value
is already on the stack when the last argument is. `Vec3(a.x + b.x, ...)`
emits the three additions and no instruction for the construction itself.

**What it costs.** A struct with many fields is built positionally, and a
positional list of eight floats is harder to read than eight named ones. If
that becomes the common case, named arguments are the answer and they apply to
functions too, rather than a second syntax that applies only to structs.

*Argued.*

---

## D012. Arrays are bounds checked, and nothing frees them yet

**Decided.** Every index is checked against the length, and a failure is a
runtime diagnostic naming the index and the length. An array's storage comes
from an arena that lives as long as the program runs, and nothing reclaims it.

**Why the check.** An unchecked index is not a wrong answer, it is memory
corruption, and it is the failure mode that costs most to find. The check is
also the thing a later contract can remove where it can prove the index is in
range, which is the same shape as `no.alloc`: prove it, then it costs nothing.
Removing the check by default first, and adding a way to ask for it back, is
the ordering that never happens.

**Why nothing frees.** Garbage collection against reference counting against
regions is on the predecessor's list of what nobody decides yet, and it is
there for a reason: it is a decision about a language that has programs, and
this one has four. An arena that outlives the run is not a memory model, it is
the absence of one, and it is written down here so it is not mistaken for a
choice later.

**What it costs today.** A program that allocates in a loop grows without
bound. That is acceptable for the programs this can currently run and is not
acceptable for the ones it is for.

**What decides it.** A program whose allocation behaviour can be measured.
Until then, the runtime keeps the heap in an arena of its own, separate from
the compiler's, so what a running program allocates is visible rather than
mixed into what compiling it allocated.

*Argued.*

---

## D013. A lookup that finds nothing is a value, and its name is scoped to the arm that has one

**Decided.** `T?` holds a `T` or nothing. A `T` standing where a `T?` is
wanted becomes one, and that is the only implicit conversion in the language.
`if let x = e { }` is the only way to open one, and `x` exists only inside the
arm where the value did. There is no unwrap operator.

**Why the value.** The alternatives are a sentinel and a crash. A sentinel is
a number that means something else and every reader has to know which, and a
crash on a miss makes a miss into a program failure when it is an ordinary
result. The predecessor's probe 4 asks whether the failure arm reads as noise
at ten thousand call sites, which is the real question and one this cannot
answer yet.

**Why the scoping is the whole design.** Making the failure hard to ignore is
not the same as making it impossible. A checked unwrap that returns the value
and traps on a miss is easy to ignore, because ignoring it is one character.
Binding the name inside the arm means the code that uses the result cannot be
written where the result might not exist: it is not refused, it does not
compile, because the name is not there. `return x` outside the arm is `unknown
name x`.

**Cost.** An optional is what it holds with a tag after it, so a miss is not a
heap event. `some` costs nothing, which is what lets a function promising
`no.alloc` return one.

**What is not decided.** Whether the failure arm reads as noise where there
are thousands of them. That needs programs.

*Argued.*

---

## D014. A reference is a generation and an index into a store, and reading through one can fail

**Decided.** `store<T>` owns its elements and hands out `ref<T>`. A reference
is one slot: the index it names with the generation it was handed out at
packed above it. `remove` marks the slot dead and steps its generation.
Nothing is notified and nothing is counted. `get` returns `T?`, so a reference
that outlived what it named reads as nothing rather than as a pointer that
lies.

**Why this and not the three it is usually a choice between.** Garbage
collection against reference counting against regions is still on the list of
what nobody decides here, and a slot map is none of them. It has no collector,
no count and no lifetime discipline; the store owns what it holds for as long
as it exists, and that is the whole rule.

**Why it is not merely a way out.** W03 read two batches across five languages
and three memory models and found every idiomatic implementation converging on
this: a reference that can go stale, checked on read, with deletion that
notifies nobody. The batches were rejected so it cannot be quoted as a result,
and it is recorded here as a direction rather than evidence. The direction is
that this is what everybody writes anyway.

**What it costs, and the cost is the point.** Deletion is one write and reads
pay a comparison. W03's figure across all three models was that a deletion
costs four to six reads. The trade is taken deliberately: reads are the
frequent operation, and paying a comparison on each is what buys a deletion
that has nothing to walk and a cycle that is not a problem to have. Two
characters pointing at each other is one line here and a lifetime argument
somewhere else.

**What it buys the contract.** `get`, `set` and `remove` allocate nothing, so
a frame step can walk an object graph, follow references and delete from it
inside a `no.alloc` promise. `add` can grow the store, so it cannot. That is
the line drawn where it belongs: spawning is not a frame-budget operation and
reading is.

**What is not decided.** When a store's memory is returned. Nothing frees it,
as D012 says of arrays. A store that is added to and removed from forever
reuses its slots and does not grow, which is most of the case, and one that
only grows still only grows.

*Argued, on a direction that was measured and may not be quoted.*

---

## D015. The host is bound by name, and the name is the one written

**Decided.** A program declares what it needs with `extern fn Host.sqrt(value:
f64) -> f64 no.alloc`. The host binds a C function under `"Host.sqrt"`, and
which is which is settled by name before anything runs. A declaration nobody
provides refuses the program at that declaration, by name.

The name the host binds is the one written, without the module that declared
it. Which file inside a Kest program said it needs `Host.sqrt` is Kest's
business; the host is asked for `Host.sqrt`.

**Why a name and not an index or an order.** An index is a number two sides
have to agree on and neither can check. A name is a thing one side can be
missing and the other can say so about, which is what happens now: the
diagnostic names the function and points at the line that declared it.

**The convention across.** A host function receives the arguments where the
slots are and writes its result over them, which is exactly what a Kest call
does. There is no marshalling step and no argument object, because there is
nothing to convert: a value of more than one slot occupies that many on both
sides.

**What is done and what is not.** This is the inward direction: Kest calls the
host. D007 says the two directions are separate specifications and neither is
inferred from the other, and the outward one is not built. Neither is the
bulk borrowed crossing that D007 makes the default shape, because a borrowed
view of host storage means Kest reading the host's layout rather than copying
into its own, and this machine's slot is eight bytes whatever the type. That
is a value-representation decision and it is not taken here.

So what exists is the shape W11 measured as the expensive one, per value and
converted at the edge. It is written down that way rather than presented as
the boundary being finished.

*Argued.*

---

## D016. Two layouts: slots on the stack, the host's bytes in memory

**Decided.** A value on the stack is a run of eight-byte slots. A value in an
array is what a C compiler would give it: an `f32` is four bytes, a `Vec3` of
three of them is twelve aligned to four, and `struct { u16; bool }` is four
aligned to two. Reading an element widens it into slots, writing narrows it
back, and both are described by a layout the compiler builds from the type.

An array's block is therefore the block a host already has, and
`kest_borrow` hands one over without copying it.

**Why not one layout.** Slots everywhere is what this was, and it makes an
array of `Vec3` twenty-four bytes an element where the engine on the other
side has twelve. That is not a size problem, it is a boundary problem: nothing
can be shared, so everything must be converted at the crossing, which is the
shape W11 measured at 9.63 times for one value at a time and 15.33 times for a
copied batch. Bytes everywhere is the other end, and it means a byte-addressed
machine, typed loads for every width and a rewrite of everything that touches
a local.

The split is where the measurement puts it. W11 found that what costs is the
*crossing*, not the load: a borrowed slice put a tree-walking interpreter level
with C++, within 4.6 percent across five implementations. So the crossing
shares memory and the arithmetic uses slots, and the widen on each element is a
single machine instruction in a loop that was going to load the element
anyway.

**What it costs.** Locals and the operand stack are still eight bytes each,
which is waste that nothing has measured and nothing can share. An array of
booleans is one byte an element and an array on the stack of them is eight.

**What it buys, and what is still missing.** A host can lend its own storage
and the program reads and writes it in place, which is D007's default shape
built rather than argued. The outward direction is still not built, and D007
says it is a separate specification.

*Argued, on a measurement.*

---

## D017. The machine outlives a call, and both directions use one convention

**Decided.** A `KestRuntime` is made once and called into many times.
`kest_call(runtime, "world.update", frame)` passes arguments in `frame` and
receives the result over them, which is exactly what a host function is handed
in the other direction. `kest run` is that call, made once, on `main`.

**Why it outlives the call.** A host calls a script every frame. A machine
that is built and torn down per call cannot hold anything between them, and a
game script holds almost everything between them. What the program allocated
is still there on the next call, which is the point and also the cost: D012
says nothing frees it, so a program that allocates every frame grows every
frame. That is now a thing a host can observe rather than a thing this
project can only argue about.

**Why one convention.** The two directions are separate specifications by
D007, and this is not a claim that they are the same. It is that the shape of
a call has no reason to differ: arguments occupy slots and a result replaces
them, whichever side is calling. Nothing is marshalled either way, so there is
no conversion whose direction could matter.

**What the direction does change.** W11 measured the outward per-item crossing
spreading five implementations over 19.28 times, the widest figure in the
workload, and two of them swapping places against the inward path. So the
shape the language makes easy is a batch: the host lends an array and calls
once. `kest tick` drives a program both ways and they return the same number,
one crossing against one per event.

*Argued, on a measurement.*

---

## D018. Arithmetic wraps at the width the type declares, and a literal that does not fit is refused

**Decided.** `u8 + u8` is a `u8`. Two hundred plus one hundred is forty-four,
and `i32` at its maximum plus one is its minimum. Every integer in a slot is
kept at its declared width, sign extended or zero extended, so a comparison
and a division do not each have to know how wide it is. A literal written in a
type it does not fit in is refused rather than wrapped.

**Why wrapping and not trapping.** The same reason as D016. The point of
matching an engine's layout is getting the engine's answer, and the code on
the other side of the boundary is C, where this wraps. A Kest `u8` that
saturated, or trapped, would disagree with the `uint8_t` it was handed and the
disagreement would be silent.

**Why a literal is different.** A value that wraps at runtime came from
somewhere and wrapping is what its type says happens. A literal came from the
author, in the same line as the type, and there is no reading under which
`let x: u8 = 300` is what they meant. It is refused with the number and the
type in the message.

**What it costs.** A width narrower than a slot pays an instruction after
every add, subtract, multiply and negate, and `i32` is the default integer
type, so a loop counter pays it twice a turn. Folding it into the arithmetic
is six more opcodes and is the obvious thing to do when something measures it
mattering. Nothing has.

*Argued.*

---

## D019. Naming a number type makes one, and the two families do not meet on their own

**Decided.** `f32(n)` and `i32(x)` are conversions, written the way
`Vec3(a, b, c)` is a construction: naming a type makes one. There is no cast
operator and no keyword. Nothing converts without being asked, in either
direction, at any width.

**Why the same syntax as a struct.** D011 chose call syntax for a struct
because a braced literal would need a rule about where a brace may start an
expression. The rule that came out of it is worth more than the reason: naming
a type makes one of it. A cast operator would be a second way to say the same
thing, and the language has one way to say things.

**The two rules under it, which are deliberately different.**

An integer going into a narrower integer *wraps*: `u8(300)` is forty-four.
That is what C does, and D018 is the argument for why matching C is the point.

A float going into an integer is truncated toward zero and *stops at the end
of the range*: `u8(300.0)` is two hundred and fifty-five and `i8(-1000.0)` is
minus one hundred and twenty-eight. C has no answer here, it is undefined, and
an undefined answer is one that differs between machines and between builds.
Saturating is defined, is the same everywhere, and is what a reader expects
when they see a number that will not fit going somewhere it will not fit.

So the rule is: match C where C has an answer, and define it where C does not.
Those two look inconsistent side by side and it is worth writing down that
they are not.

**What is refused.** `bool(1)` and `text(5)`. Whether a number is true has
more than one answer and the author knows which one they mean; text is made
with a string that has a hole in it, and the diagnostic says so.

*Argued.*

---

## D020. Walking a store gives references, and the read inside cannot fail but says it can

**Decided.** `for r in world` binds a `ref<T>` for each live slot, not the
value. Removing during the walk is allowed: a slot goes dead behind the cursor
and the walk does not return to it.

**Why a reference.** A reference is what `remove` and `set` take, and a frame
step over an object graph does both. Binding the value would make the common
read shorter and the two operations the loop exists for impossible, and the
value is one `get` away.

**The cost, which is the interesting part.** `get` returns `T?` because a
reference in general may be stale. Inside this loop it never is: the reference
came from the store's own live set, this turn. So every walk carries an `if
let` whose second arm cannot be reached.

The predecessor's probe 4 asks exactly this — whether the failure arm reads as
noise at ten thousand call sites — and records it as untested. It is now
written rather than asked about, and the shape is on the page:

```kest
for r in world {
    if let npc = get(world, r) {
        set(world, r, Npc(npc.name, npc.health - 1))
    }
}
```

**What was not done, and why.** The type system could be told that this
reference is live, and then the walk could bind the value and the arm would go
away. That is a claim about a reference's lifetime, and this language has no
way to make one: D014 says a store owns what it holds for as long as it
exists, and nothing narrower. Adding an unchecked read that only the loop may
use would be the same claim made by convention instead of by the type system,
and the first person to lift the line out of the loop would find out.

So the noise stays, visible, until something can measure whether it matters or
the type system can say what the loop knows.

*Argued.*

---

## D021. Text is its bytes, and there is no character type

**Decided.** `len(t)` counts bytes. `t[i]` reads one as a `u8`. Two pieces of
text compare by their bytes, which is an order that is the same on every
machine. There is no character type and nothing decodes one.

**Why bytes.** A piece of text in this language is a pointer and nothing else,
because a host hands one over as a `const char *` and D016 is the decision
that says a value shared with the host is what the host has. Anything else
means a header, and a header means the host cannot hand over what it already
has.

**What that costs, plainly.** `len` walks the string, so it is not free and a
loop that asks for it every turn walks it every turn. `"hız"` is four bytes,
not three characters, and a program that wants characters has to say what it
means by one.

**Why no character type.** A character is a decision: a byte, a code point, a
grapheme. Every language that picked one before it had programs picked wrong
for somebody. This has one program's worth of evidence and picks none of them,
which leaves the byte, which is the thing that is actually there.

**What is missing and known to be.** There is no way to take a piece of text
apart into another piece of text: no slice, no substring, no join except
writing a string with holes in it. All of those allocate and the contract
would charge for them, which is right, and none of them exists yet.

*Argued.*

---

## D022. `std` is reserved, and the library is written in Kest

**Decided.** A module whose name starts with `std.` always comes from the
standard library, wherever the program is. Everything else comes from the root
the first file settles. The library lives at `$KEST_LIB`, or `lib/` beside the
compiler when that is not set.

The library is Kest source. `std.text` is a file that uses `len`, `find`,
`slice` and the bytes, the same ones a program has.

**Why reserve the name.** The alternative is a search order, and a search
order means a file can quietly shadow a library module, and then a reader has
to know the order to know which one they are looking at. One name that always
means one thing costs a name and buys that away.

**Why written in Kest.** A builtin is a thing the language cannot express, and
every builtin is a small admission of that. `number` reads bytes and `split`
cuts, and both are things a program can already do, so making them builtins
would say the language could not do what it can. It also means the library is
held to the same rules: `number` promises `no.alloc` and the compiler proves
it, exactly as a program's would be.

**What is not here and why.** `min`, `max`, `abs` and `clamp` are what a game
program asks for next, and this language has no generics and no overloading, so
they would have to be `minInt` and `minFloat`. An ugly name in a standard
library is permanent, and the thing that fixes it is a decision about generics
that nothing has made yet. So they are absent rather than named badly.

*Argued.*

---

## D023. Two functions may share a name when they take different things

**Decided.** `fn min(a: i32, b: i32)` and `fn min(a: f32, b: f32)` are two
functions with one name. Which is meant is settled by what is passed, and by
nothing else. A parameter list is part of what a function is called, so two
that take the same things are still a duplicate and still refused.

**Why this and not generics.** Both answer the question D022 was waiting on.
Generics answer more of it: with them a program writes one body, and here it
writes two.

What decided it is that in this language overloading is almost nothing.
There is no subtyping, no implicit conversion and no ranking, so resolution is
"find the one whose parameters are exactly these" and there is no second rule.
Generics need constraints, or they need instantiation-time errors reported
inside a body the caller did not write, and either is a design taken on an
argument rather than on evidence. D010 is the precedent for not doing that.

**The one place a rule was needed.** A literal has no type of its own to lose,
so `min(3, 7)` fits four candidates. It is settled twice: once letting a
literal match any width of its family, and again requiring the width a literal
would have had on its own. `min(3, 7)` is the `i32` one; `min(x, 1)` with an
`f64` `x` is the `f64` one and the literal follows. When neither pass leaves
one, every candidate is listed with what it takes.

**What this does not do, and it is the same gap D022 named.** A program still
writes `min` twice. Generics stay open, and what would decide them is a
program whose duplication is worth a constraint system.

*Argued.*

---

## D024. The language has no input and no output

**Decided.** There is no `print`. Saying something is `std.io`, which declares
`Io.write` and asks the host for it. Nothing is declared for a program before
it says what it imports.

**Why.** This language is for embedding, and a language that writes to
standard output has decided something for a host that has no standard output.
An engine writes to its console, a server writes to its log, a test harness
collects it, and none of those is a thing to be worked around.

It is also the rule D022 already stated, applied to the one place it had not
been: a builtin is a thing the language cannot express, and `print` was a
thing the host does.

**What it costs.** The shortest program that says anything now has an import
in it, and `io.print` is longer than `print`. That is the price of the host
choosing where it goes, and it is small.

**The one concession.** A program that calls `print` without importing
`std.io` is told where it lives rather than told the name is unknown. It is
the first thing anybody reaches for and the diagnostic is the only thing that
would have told them.

*Argued.*

---

## D025. The host may throw the heap away, and that is all reclamation is so far

**Decided.** `kest_heap_reset` frees everything a running program allocated and
starts again. There is nothing else: no collector, no counting, no scope, and
no way for a program to ask for it. Reclaiming is the host's to decide, at a
moment the host knows and the program does not.

**Why it is safe, and exactly how far.** Nothing of a program's survives a
call. There are no mutable globals, the stack is set up per call and the frames
with it, so between two calls there is nothing in the machine pointing at the
heap. What a reset invalidates is what the *host* is still holding: an array,
a store or a piece of text that came out of `kest_call` is gone afterwards,
and passing one back in is reading freed memory. That is written on the
function.

**Why this and not the decision D012 defers.** It is not an answer to that
question, it is the smallest thing that makes the answer wait honestly. A host
that runs a script every frame and resets between frames has bounded memory
and no collector, which is the arena pattern an engine already uses; a host
that carries values between frames cannot use it and still has no answer.
Which of those a real program is remains the thing nobody here has measured.

**What it buys today.** The cost D012 defers can now be avoided as well as
seen. A thousand events that allocate come to sixty-four kilobytes, or to
forty-nine bytes if the host starts again between them, and both give the same
answer.

*Argued.*

---

## D026. A thing that is one of several, and a `match` that answers all of them

**Decided.** `enum` declares a type that is one of its cases. A case carries
what it carries, by position:

```kest
enum Door {
    Shut
    Locked(i32)
    Open(f32)
}
```

`Door.Locked(7)` builds one, the same way naming any type builds one. `match`
chooses between them, binds what the case carried, and is refused if it leaves
a case out unless it has an `else`.

**Why positional and not named.** D011 chose call syntax for a struct and
D019 kept the rule that came out of it: naming a type makes one of it, by
position. A case is the same shape and takes the same rule. The names are
given where they are used, in the arm that answered that case, which is where
a reader needs them.

**Why exhaustive.** The thing an enum replaces is a struct with a number in it
and a chain of `if`s, and what goes wrong with that is not that it is verbose:
it is that adding a case changes nothing anywhere and every place that
forgot it keeps compiling. A `match` that leaves one out is refused, and the
diagnostic points at the case in the declaration.

`else` is there because not every match is about all of them, and it is a
written decision rather than a silent one.

**The layout.** The tag is a four byte integer at offset zero and the payload
starts after it, which is what a C tagged union is, so an enum can cross the
boundary D016 makes crossable. That is the other way round from an optional,
whose tag is last, and the difference is worth writing down: an optional was
built before anything crossed anywhere.

*Argued.*

---

## D027. A match gives a value when its arms say so, in the arm

**Decided.** An arm is written `Case -> expression` when it gives a value and
as a block when it does something. Every arm of one match is the same kind;
mixing them is refused. A match whose arms give values is a value, and one
whose arms are blocks is a statement.

```kest
return match door {
    Shut -> "shut"
    Locked(key) -> "locked with {key}"
    Open(width) -> "open {width} wide"
}
```

**Why not blocks with values.** The familiar answer is that a block's value is
its last statement when that statement happens to be an expression. In a
language with a semicolon that is at least a visible mark; here there is none,
so a block that yields and a block that does not would look the same, and
which one a reader is looking at would depend on a rule nothing on the page
mentions.

D011 refused a braced struct literal because the rule it needed would be
invisible until it bit. This is the same shape and got the same answer.

**Why `->` and not a new mark.** It already means "gives" in a signature. An
arm that says `Shut -> "shut"` says the same thing about the same kind of
thing.

**What it costs.** An arm that needs several statements *and* a value cannot
be written: it is a block arm, and a block arm returns. In practice the arms
that give values are one expression each, which is what the examples were
already doing with a `return` in front of them. If that stops being true, what
is missing is a way for a block to give a value, and it should be added
visibly rather than by making the last line mean something.

**What is still missing.** There is no conditional expression. `if` is a
statement and there is no ternary, so a value chosen by a `bool` is written
with a `let` and an `if`. That is a separate decision and this one does not
take it.

*Argued.*

## D028 — an `if` gives a value when its arms say so

`if` is now parsed once, as an expression, the same way `match` is after D027.
Which one it is used as is written in its arms:

```kest
fn max(a: i32, b: i32) -> i32 no.alloc {
    return if a > b -> a else -> b
}

fn sign(value: i32) -> i32 no.alloc {
    return if value > 0 -> 1 else if value < 0 -> 0 - 1 else -> 0
}
```

An arm that gives a value is written `-> expression`; an arm that does
something is a block. Both arms of one `if` are the same kind, and mixing them
is refused with the same code D027 allocated, `K0208`, because it is the same
mistake.

**Why not a ternary.** `c ? a : b` needs two marks that mean nothing else, and
one of them, `?`, already means "optional" everywhere else in the language.
D019 and D013 both spend `?` on that. A reader who has learned `Npc?` would
have to learn a second, unrelated `?`.

**Why not a block that yields.** That is the thing D027 refused, and refusing
it there and allowing it here would be worse than either answer alone.

**Why an `else` is required.** A value has to exist on both ways through. An
`if` without an `else` that gives a value is refused (`K0334`) rather than
made to produce a zero, because there is no value the language could pick that
would not be a guess about what the author meant.

**What it changed.** `KEST_STMT_IF` is gone: a statement that is an `if` is an
expression statement holding one, which is what lets an `if` stand in a
`return`, a `let`, and mid-arithmetic. `if let` gives values too, so
`if let held = door -> held.width else -> 0.0` works.

**What it cost.** The giving form has no braces, so it has to fit on one line
or continue through an operator or a bracket, because D003's rule is that a
line ending in a value ends the statement. Twelve functions in `lib/std/math`
went from four lines to one, which is the case that motivated it.

*Argued.*

## D029 — text is built out of bytes

`text(bytes)` makes one piece of text from a `[u8]`. It is the only way to
make text from a value that is not a string with a hole in it.

```kest
fn join(pieces: [text], separator: text) -> text {
    let out = array(0, u8(0))
    let i = 0
    while i < len(pieces) {
        if i > 0 {
            append(out, separator)
        }
        append(out, pieces[i])
        i += 1
    }
    return text(out)
}
```

**What was missing.** There was no way to build a string a piece at a time.
Interpolation builds one whole string per evaluation, so a loop that grows one
allocates on every turn and copies everything it has so far. `std.text` could
write `split`, which only cuts, and could not write `join`, which only builds.

**Why not `push` on text.** D021 says text is its bytes, and two pieces of
text can share them: `slice` hands back a view into a copy, and the language
promises that reading text costs nothing. A `push` that grew text in place
would have to decide what happens to everything already pointing at it. An
array already grows and already has `push`, so the concept exists and text
does not need a second one.

**Why not a builder type.** A builder is an array of bytes with a different
name. Adding one would mean two growable things where the language has one.

**Why this shape is the fast one.** Gathering costs an amortised push per
byte and the copy is paid once, so building a string of *n* bytes out of *k*
pieces is O(n) rather than the O(nk) that repeated interpolation is. That is
the whole reason it exists: the cost is where a reader can see it, on the one
call that names it.

**What it refuses.** `text` of anything that is not a `[u8]` (`K0327`), and at
run time a zero byte in the array (`K0604`), because text ends at its first
zero byte and one in the middle would quietly cut the rest off. `text` counts
as allocating, so a `no.alloc` function may gather bytes and may not finish.

*Argued.*

## D030 — an empty array says what it holds

`array(n, v)` reads what it holds off what it is filled with. An empty array
has no fill to read, and `array(0, u8(0))` was how one was written: a nought
count and a nought byte that is never looked at. Every builder in `std.text`
opened with that line, and it reads like a mistake.

`array()` takes what it holds from where it is going:

```kest
let out: [u8] = array()
```

**Why this and not an empty literal.** `[]` would be a second spelling of one
thing, which D004's strict parser exists to avoid, and it would need the same
rule anyway: an empty literal has nothing in it to read a type off either.

**Why not infer it from the first `push`.** That would make a declaration
mean something written later in the block, so a reader would have to scan
forward to know what a name holds. D005 infers a type from the value a name
is given, and this keeps that boundary: the type comes from the declaration or
from where the value is going, never from a later statement.

**Why it matches `store()`.** `store()` already did exactly this, down to the
diagnostic. Two growable things now answer the same question the same way,
which is one rule rather than two.

Refused with `K0335` where there is nothing to take it from, with the fix
written out.

*Argued.*

## D031 — an array shrinks three ways, and each says its cost

`push` was the only way to change an array's length. There was no way to take
anything out of one, so a program that needed to filter rebuilt the array.

- `pop(a) -> T?` takes the last one off. Nothing moves, so it costs nothing.
  Empty gives nothing, the same way `find` and `get` do (D013).
- `remove(a, i) -> T` takes out the one at a position and gives it. What is
  after it keeps its order, which is what the shift is for.
- `clear(a)` sets the length to nought.

**Why `remove` is spelled the same as a store's.** It is the same word for the
same idea, and which one is meant is settled by what is handed in: a store and
a reference, or an array and a position. That is how `len` already works
across an array, a store and text.

**Why there is no swap-remove.** Taking the middle out by moving the last one
into the hole is O(1) and does not keep the order. That is a store: D014's
generational references exist so that a thing whose position does not matter
can be removed for nothing and still be named afterwards. Adding a
swap-remove would make an array a worse store and blur why a store exists.

**Why `pop` gives an optional and `remove` does not.** An empty array has no
last element, and the language does not pick one. A position that is out of
range is not a case to answer but a mistake, and it fails the way indexing
fails (`K0604`), because `a[3]` and `remove(a, 3)` are the same claim about
the same array.

**What they cost.** None of the three reaches the heap, so a `no.alloc`
function may drain an array it was handed. What it may not do is refill it.

**Borrowed arrays.** A host-lent array cannot shrink, for the reason it cannot
grow: the length is the host's, and so is the extent it lent (`K0608`).

*Argued.*

## D032 — bits, and where they sit in the table

`&`, `|`, `^`, `~`, `<<` and `>>` on integers.

A language for games, simulations and engine embedding could not say what a
byte of state is. Flags were eight `bool` fields, a packed handle could not be
taken apart, and the runtime's own `ref<T>` — a generation and an index in one
number (D014) — was a shape the language could not write.

**Where they sit.** Tighter than the comparisons:

```
*  /  %      <<  >>      &      ^      |      <  <=  >  >=      ==  !=
```

C puts `&` below `==`, so `flags & MASK == 0` means `flags & (MASK == 0)`.
That is the most reported precedence mistake in the language, and every table
written since has moved it. Shifts keep C's place, above the bitwise operators
and below the arithmetic, because `1 << n + 1` has never been the trap that
one is.

**Why not on `bool`.** `bool` has `&&`, `||` and `!`. Two spellings for one
thing is what D004 exists to refuse, and the short-circuiting one is the one
that is almost always meant. `true & false` is refused with `&&` named.

**Why a shift takes a count rather than an operand.** `x << 4` says how far,
not what with, so the count is an integer of any width the way an index is.
Requiring it to be the type of the value would mean writing `u8(1)` to shift
a `u8`, which says nothing.

**What is defined that C leaves open.** A left shift wraps at the declared
width, which is D018 and not a new rule: `u8(1) << 8` is nought. A right
shift brings the sign in on a signed type and nought on an unsigned one, which
is what the two types mean rather than what the machine happens to do. A count
past the width of a slot shifts everything out. A negative count is a mistake
and fails with a message (`K0604`).

**What it cost.** `>>` and the closing of a nested generic are the same two
characters, so `store<ref<Npc>>` had to be handled: closing a type splits the
token and leaves the second half where it is.

*Argued.*

## D033 — a set of named bits is a type

```kest
flags State: u8 {
    Moving
    Airborne
    Hurt
    Armed
}
```

D032 gave the language bits and left `examples/flags` declaring four `const`
values loose at the top of a file. Nothing tied them together, nothing stopped
one being passed where another belonged, and the powers of two were written
out by hand, which is the oldest way to get a flag set wrong.

**Which bit a name is, is where it was written.** Position, not a number. The
one thing a reader could get wrong is the one thing they no longer write.

**Why the width is written.** `flags State: u8` says what a host sees. It
could be counted off the names, but then a ninth flag would silently widen the
type under a host that was already reading it. Writing it means a ninth flag
is refused (`K0338`) and widening is a decision someone made. It must be
unsigned, because a sign bit in a set of flags is a flag whose name is the
sign.

**Why not an enum with numbers.** An enum is a tagged union: a value is one of
its cases, and `match` answers all of them (D026, D027). A flag set is any
combination, so nothing exhausts it and `match` cannot apply. Giving enums
numbers would have made one word mean two things, and the exhaustiveness
D026 is built on would have quietly stopped holding.

**What applies.** `&`, `|`, `^`, `~` give the same set back; `==` and `!=`
compare. Arithmetic does not apply, two different sets cannot be mixed, and
`match` says so rather than failing later. `State()` is the empty one, which
is what `array()` and `store()` already read as (D030). `u8(state)` and
`State(bits)` cross to the number and back, at the declared width only,
because a narrower one would drop flags without saying.

**What it is at runtime.** One slot, and the declared unsigned integer in the
byte layout, so an array of them is the array a host already has (D016).

**Why `flags` is a word and not a keyword.** A keyword takes the name away
from every field and every module, and `npc.flags` is a thing people write.
It is read as a declaration only where a declaration begins, which is where it
cannot be anything else. `no.alloc` is read the same way.

*Argued.*

## D034 — a set of bits is walked, and gives flags

`for flag in state` gives the flags that are set, in the order they were
declared, each one a value of the set with that one bit in it.

```kest
fn count(state: State) -> i32 no.alloc {
    let total = 0
    for flag in state {
        total += 1
    }
    return total
}
```

**Why a flag and not a bit position.** A position would be a number that
cannot be used: shifting is not defined on a set, so there is no way back from
`3` to the flag it names. Giving the flag means nothing has to be asked about
what came out. It is the same answer a store gives, for the same reason
(D020): what a walk hands back is the thing the rest of the language takes.

**So there is no position form.** `for i, flag in state` is refused, the way
it is for a store.

**What it is.** No new instruction. The compiler writes the walk out of what
is already there: a counter to the number of names, the bit at that counter,
and a jump past the body when the set does not hold it. A bit that is not
there lands where `continue` lands, so the two paths are one.

**What it makes possible.** `count` in `examples/flags` was eight turns of a
hand-written `while` over `u8(state) >> bit`, which is exactly the arithmetic
D033 refused on a set and then made the example do through a conversion. It
is now four lines with no conversion in them.

*Argued.*

## D035 — the text of a set of bits is the source that builds it

`"{state}"` gives `State.Moving | State.Armed`, and `State()` for a set that
holds nothing.

D021 writes a value into a string only where it has one obvious spelling, and
until D034 a set had none: the names it holds could not be reached. Now they
can, and the spelling to pick was the one every other type already uses.

**Why the source form.** `"{3}"` is `3`, `"{true}"` is `true`, `"{1.5}"` is
`1.5`. Every one of those is what a program writes to make that value. Text is
the exception, and it is the exception because text in a hole is the content
rather than a way of naming it. A set follows the rule rather than the
exception, so `State.Moving | State.Armed` and not `Moving|Armed`: the
separator is the operator that combines them, and the empty set is the call
that makes one.

**Which name.** The last piece of the name the type is registered under, which
is what a program writes where the set was declared. A set printed from
another module therefore reads without that module in front of it. That is the
limit of "the text is the source" and it is the readable side of the trade.

**What it cost.** One instruction, `text.flags`, taking the layout index the
compiler already had: a layout carries the type it was made for, so the names
came with it and nothing new is stored in a module.

Structs and enums are still refused. An enum could follow the same rule, but
a case with a payload needs text for the payload too, and that is a separate
decision. Its suggestion now names `match` rather than fields it does not
have.

*Argued.*

## D036 — the text of an enum case is the source that builds it

`"{Door.Locked(7)}"` gives `Door.Locked(7)`, and `"{Door.Named("gate")}"`
gives `Door.Named("gate")` with the quotes.

This is D035's rule applied to the other type that had a spelling waiting for
it. Every value's text is what a program writes to make that value; text in a
hole is the exception, and it is the exception because there the text is the
content rather than a way of naming it.

**Why the quotes on a payload.** Inside a case, a string is being named rather
than pasted, so `Door.Named(gate)` would be a case carrying something that
reads like a name. Quotes and escapes make it the source it claims to be.

**When an enum has no text.** When something one of its cases carries has
none. A struct and an array have none, so an enum carrying either has none,
and the refusal names what it was rather than only the enum.

**Structs are still refused.** `P(1, 2)` would be source too, but a struct
names its fields and that form does not, so the obvious spelling is not
obvious: a struct of ten fields in a log line is ten numbers in a row. An enum
has no such second reading, which is why it goes first and a struct waits for
its own decision.

**Prose is a different question.** `describe(door)` giving `locked with 7` is
what a person is told; `"{door}"` is what the value is. `examples/state` asks
both, because they are not the same question and a `match` that gives text is
still the way to ask the first one.

**What it cost.** One instruction, `text.enum`, and one recursive formatter in
the machine shared with `text.flags`. It measures with room of nought and then
writes, so a value of any depth is one allocation.

*Argued.*

## D037 — a `match` chooses between several things at once

```kest
return match door, move {
    Shut, Push -> Door.Open(1.0)
    Locked(key), Unlock(with) -> if with == key -> Door.Shut else -> door
    Open(width), Pull -> Door.Shut
    Open(width), else -> Door.Open(width)
    else -> door
}
```

`examples/state` answered two enums with four levels of nested `match`, thirty
two lines to say a nine-cell table. Each inner `match` needed its own `else`,
and nothing checked that the nine combinations were covered — only that each
inner one was, which is a weaker claim than the code was making.

**Why several subjects and not a tuple.** A tuple would be a new type, new
values, new patterns and a new way to write a return, all to be taken apart
again at the top of the arm. Several subjects is a list where there was one:
no new type, and the exhaustiveness check becomes the product it already
wanted to be.

**`else` in a position.** A position that says `else` answers any case there,
so `Open(width), else` is one arm rather than one per turn. An `else` on its
own stands for every position, which is what an `else` has always meant, and
is the one way to leave a combination out.

**Arms are tried in order.** So a later arm catching what an earlier one left
is the point, and partial overlap is not a mistake. What is refused is an arm
nothing can reach, which is every combination it answers already answered. The
first version refused any overlap and rejected the example that motivated the
feature, which is how the rule was found.

**A limit, written down.** Eight subjects, and 256 combinations to answer. A
`match` past either is asked for an `else` rather than for a list nobody would
write. The refusal says the number and the limit.

*Argued.*

## D038 — a counted walk

```kest
for i in 0..len(a) {
    total += a[i]
}
```

Every counted loop was three statements: a `let` before it, a condition, and
an `i += 1` at the bottom that nothing checked was there. `lib/std/text` had
seven of them and the examples had six more.

**Exclusive.** `0..n` runs `n` times and ends where `len` ends, so a walk of
an array's positions and an index into it are the same numbers. An inclusive
form would be a second spelling for the same walk.

**Not a type.** A range is a way to write a walk, not a value: `let r = 0..n`
is a syntax error. Making it a value would mean a range type, a range value,
and a decision about what walking one twice does, none of which the language
needs to remove the three-statement loop.

**Both ends are one type, and a literal takes the other's.** `0..count`
counts in whatever `count` is, which is the rule an operator already follows.

**The end is worked out once.** `for i in 0..len(a)` with a `push` in the body
runs the number of times the array was long when the loop started. A walk of
the array itself would have the same question and D012 already answers it;
this makes the counted form agree rather than differ.

**No position form.** `for k, i in 0..n` is refused: the number is the
position.

**The count is the loop's own.** The name is a copy of a slot nobody can
reach, the way a walk of an array already works, so assigning to it changes
nothing — and the warning that says so is now true, which it was not in the
first version of this.

*Argued.*

## D039 — a function is a value, and its promise is part of its type

```
fn sort(items: [text], before: fn(text, text) -> bool no.alloc) no.alloc
```

`examples/words` sorted by hand because there was no way to say "and here is
what comes first". The choice was between a `sort` that only works on what the
language can already compare, and a function value.

**The objection, and the answer.** The `no.alloc` contract is proved by a
call-graph fixed point, and a function value is exactly the thing that makes
the call graph unknown: the compiler cannot see which body a value points at.
That is a real conflict with what this language is for, and it is why the
promise goes into the type. `fn(text, text) -> bool no.alloc` is a function
that promises, and the promise is checked where the value is made rather than
where it is called. The contract is then read off the type, and a function
that promises can still call one.

**Variance, stated once.** A value that promises `no.alloc` fits where one
that does not is wanted, and not the other way round. `kest_type_equal` is
called as (given, wanted) and that is where this lives.

**An overloaded name takes the shape of where it goes.** D023 settles a call
by what is passed; this is the other half, and it is the rule a literal
already follows. A name that is several functions in an argument is asked
again with the candidate's parameter type in hand.

**An extern is called and not named.** Which function the host bound is
settled when the program starts, not when it compiles, so there is no value.
Refused with `K0342` and the fix: write a function that calls it.

**What does not apply.** Two function values do not compare — a handle
comparison answers a question nobody asked — and one has no text.

**What it cost.** One instruction, `call.value`, which takes which function it
is off the top of the arguments. A function value is one slot holding an index
into the module, so it is a `u64` at the boundary and costs nothing to pass.

Found while wiring it: the contract graph had never looked at a call it could
not name, so before this an indirect call would have been counted as free.
The hole existed only in theory until there were function values; it does not
now.

*Argued.*

## D040 — types are taken, and a copy is compiled for each set

```
fn sort<T>(items: [T], before: fn(T, T) -> bool no.alloc) no.alloc
```

`lib/std/sort` was the same insertion sort written three times, and every
container a program wants would have been the same again.

**Monomorphisation, because the value model leaves no choice.** A `KestValue`
is eight bytes with no tag (D002) and a struct is laid out flat in consecutive
slots (D006). Type erasure needs a value that can be any type, which means a
tag or a box, which means every struct stops being its own bytes and D016's
crossing stops being free. Compiling a copy per set of types keeps all of
that and costs nothing at run time.

**The cost is copies, and it is written down.** A function called with six
types is six bodies. That is the honest side of the trade and the language
says so rather than hiding it.

**A copy is checked against its own types.** `no.alloc` holds for a copy or
does not, so a generic that builds something is refused for the types where
building reaches the heap and allowed where it does not. This is stricter and
more useful than one answer for all of them.

**A generic function is called and not named.** It is not one function, so
`let f = sort` has nothing to be. Refused with `K0343`.

**What a name stands for comes from the arguments.** A name that appears in no
argument is refused rather than written at the call site: there is no
`sort<T>(...)` spelling, because every other call in the language is written
by what is passed and this is not a second rule. A function argument is
settled after the others, since which overload it is depends on what they
settled.

**What it cost in the implementation.** The tree is shared between copies and
the checker writes types onto it, so a tree carries one copy's types at a
time. The compiler asks the checker to put a copy's types back before it emits
that copy. The first version did not, and a `count<T>` over `[Vec]` was
emitted with the layout of the `[i32]` copy that happened to be checked last:
it moved one slot where a `Vec` is two.

Generic structs are not in this. A function is where the repetition was.

*Argued.*

## D041 — a struct takes types too

```kest
struct Table<K, V> {
    keys: [K]
    values: [V]
}
```

D040 gave functions types and left `[T]`, `store<T>` and `ref<T>` as shapes a
program could use and not write. A container written in the language needs a
struct that takes types, so this is the other half of the same decision and
the same answer: a copy per set, measured like any other struct.

**A shape is not a type.** `Pair` on its own has no size and is never
measured; `Pair<i32, text>` is a struct with two members and a layout. Writing
the shape without its types is refused with what it takes.

**Which copy is being built comes from what it is built with.** `Pair(1, "a")`
is a `Pair<i32, text>`, unified from the fields, and the written type wins
when there is one. That is the rule a generic call already follows.

**A copy remembers its shape and what it was made with.** Without that, a
`Grid<T>` written inside a generic function could not become a `Grid<i32>`
when the function was copied: substitution had nothing to rebuild from. It
also gives unification a way to put two copies of one shape side by side.

**What was found on the way.** A file that declares a function shadowed the
builtin of the same name completely, so `std.table` could not call the array's
`remove` from inside its own. A builtin is one more thing a name could mean
now, and which is meant is settled by what is passed — D023's rule, which had
only ever been applied between declared functions. A parameter that mentions a
type name is asked about its shape rather than compared exactly, so `Box<T>`
could take a `Box<i32>` and could not take a `[i32]`.

Generic enums are not in this. The machinery is the same and the layout pass
is the one that would need the work; nothing has asked for one yet.

*Argued.*

## D042 — `hash` stands for what compares

`hash(x)` gives a `u64`. It applies to integers, floats, `bool`, text and a
set of bits, which is exactly what `==` applies to.

`lib/std/table` walked its keys to find one, which is right for a few dozen
and wrong for a few thousand. What it needed was a number standing for a
value, and the only question was where that number comes from.

**Why a builtin over what compares, and not a promise a type makes.** A trait
or a protocol would be a whole second way of saying what a type is, and the
set of types that can be hashed is already written down: it is the set that
compares. Two values that are equal have to hash the same, so defining `hash`
anywhere `==` is not defined would be defining it where nothing says what
equal means.

**Why not told, the way `sort` is told what comes first.** `sort` is told
because there is more than one right order and the caller knows which. There
is one right hash for an `i32`, and a table that had to be handed one at every
call site would be worse at the one thing it is for. A key that is a struct is
refused, and the fix is in the message: combine the fields that decide it.

**Minus nought.** `0.0` and `-0.0` are one value to `==`, so they are one
value here. A NaN is not equal to itself and needs no special case.

**What it is.** One mixing round over the bits of a slot, and FNV-1a over the
bytes of text, which is what D021 says text is.

*Argued.*

## D043 — an enum compares, and hashes over the same parts

`door == Door.Locked(7)` asks what it looks like it asks. Two values of an
enum are equal when they are the same case carrying the same things.

`examples/state` compared doors by building text out of them and comparing
that. A `match` was the only other way to ask, and asking whether two things
are the same is not what a `match` is for.

**Why an enum and not a struct.** A value of an enum is its case and what that
case carries; there is nothing else it could mean. A struct is a bundle of
named fields, and "are these the same" often means "are the fields that
identify it the same" — an `Npc` with the same name and different health is
one reading and two `Npc`s is another. Both are common, so the language does
not pick, which is D011's shape of argument. An enum has no second reading.

**When it does not compare.** When a case carries something that does not:
a struct, an array, a store. The refusal names what it was rather than only
the enum.

**`hash` covers the same ground, over the same parts.** D042 tied the two
together and this keeps them tied: the tag mixed with the hash of whatever the
case carries, which is exactly what equality reads. Two things that cannot be
told apart cannot hash apart.

**What it turned up.** An array of enums had never worked and nothing had
tried one. `KestLayout` is one scalar per slot, and a tagged union is not
that: which type a payload slot holds depends on the tag. The pieces for an
enum were one short and the rest were whatever was in the arena, so
`push(ks, Kind.Rope)` stored a `Sword`. A layout says whether it holds a tag
now, and a value that does is moved by reading the tag and using that case's
offsets. That is what D016's two layouts always meant for a union; nothing had
said it.

*Argued.*

## D044 — a host lends an array of tagged unions

`examples/embed.c` declares

```c
typedef struct {
    int32_t tag;
    union { struct { float x, y; } moved; int32_t hit; const char *named; } as;
} Event;
```

and `examples/embed.kest` declares

```kest
enum Event {
    Idle
    Moved(f32, f32)
    Hit(i32)
    Named(text)
}
```

They are the same sixteen bytes, aligned the same way, with the payload at
eight in both. The host lends an array of them and the program walks it in
place: nothing is copied at the boundary, and what the program writes is what
the host reads back.

This is not a new decision so much as the first proof of two old ones. D026
said an enum is a C tagged union and D016 said an array is the host's bytes;
nothing had ever put the two together, and D043's turn found that an array of
enums did not work at all. This is the shape that says the layout is real.

**Why it belongs in the example rather than in a test.** The host boundary is
the one thing a program cannot check about itself. `examples/embed` is the
only thing that crosses it in both directions, so `make embed-debug` builds it
under the sanitisers; it had never been run under them before this.

**What it costs.** One `Array` header per lend, sixteen bytes, and nothing
else: the heap reading held at 256 bytes across the whole run.

*Argued.*

## D045 — a host says what it is lending, and the program says how wide it is

```c
frame[0] = kest_borrow(runtime, events, 4, "Event", sizeof(Event));
```

`kest_borrow` took a stride and trusted it. A host that lent
`sizeof(double)` where the program held `f32`, or whose struct had gained a
field, got whatever that produced and the program could not ask.

**The stride comes from the program.** It is the one number that cannot be
wrong, because it is the one the program is going to use. The host no longer
passes it.

**The size is there to be disagreed with.** `sizeof(Event)` is what this host
thinks the shape is, and the runtime compares it with what the program laid
out. A disagreement is a message naming both numbers, and a value whose
`object` is NULL. That is the whole check: the two declarations are written
twice, in two languages, and this is where they are put beside each other.

**Only a type the program holds in an array can be lent.** The layouts a
module carries are exactly the element types it uses, so a name that is not
one of them is refused rather than guessed at. A name that means two types in
two modules is refused too, with the fix: write the module.

**A host can now ask why.** `kest_report` writes what the program has said
since the last time it was asked. Before this a host that got `false` from
`kest_call` had no way to find out what happened: the diagnostics existed and
only the command line could reach them.

**The command line is a host and had the same hole.** `kest tick` hands a
batch of `i32` to `onEvents`, and `examples/embed` now declares an `onEvents`
over an enum. It read the wrong bytes and crashed. It asks what the entry
takes and says so instead.

*Argued.*

## D046 — a call says how wide its frame is

```c
uint32_t needed = kest_frame_slots(build, kest_build_name(build, "spawn"));
kest_call(runtime, kest_build_name(build, "spawn"), frame, 4);
```

`kest_call` copied `param_slots` out of the host's array on the way in and
`returned` back over it on the way out, and neither number came from the host.
A frame one slot short of what a function takes was read past; one too narrow
for what it gives back was written past. That is the same class of mistake a
lend was before D045, at the other end of the same boundary.

**The host says the width and the program says the requirement.** The host
knows how many `KestValue`s it allocated and nothing else does; the program
knows how many it needs and nothing else does. Putting the two beside each
other is the whole check, and it is the shape D045 already used.

**`kest_frame_slots` so the host can size it rather than guess.** It gives the
wider of what a function takes and what it gives, because those are the same
slots. `examples/embed` asked for a comment that said "wide enough for the
most any of these calls passes or returns"; it asks the program now.

**What this does not check.** The right number of slots holding the wrong
things is still the host's to get right: a `Vec3` written as two floats and a
zero is three slots either way. Runtime carries no types, and giving it some
to check arguments with would cost every call to save a host writing `sizeof`
wrong. The boundary catches what it can see, and that line is where it is.

*Argued.*

## D047 — the header stands on its own, and so does the library

`include/kest.h` is the only header a host includes, and `libkest.a` needs
libc and nothing beyond it. Both were true and neither was checked.

`tools/check-header.sh` writes a host that includes the header and nothing
before it, names every function the header declares, and links against the
library with no `-lm` and no other library. The compiler proves the header
stands on its own; the linker proves the library keeps every promise the
header makes, and needs nothing else to keep them.

**Why naming rather than calling.** Taking a function's address forces the
linker to resolve it without anything having to be given arguments that mean
something. A function pointer converts to another function pointer and to no
object pointer, so the array is of function pointers rather than `void *` —
which `-pedantic` is what said so.

**Why it is worth a tool.** The example beside the header happens to include
only that. Nothing said it had to, and a header that quietly grew an include
from `src/` would have kept working for every build in this repository and
for nobody else's. That is the shape of thing a check exists for.

**What it turned up.** `libkest.a` never needed the maths library; only the
command line's host functions do. Two link lines were carrying `-lm` for a
library that has no floating point call in it.

*Argued.*

## D048 — the documented programs parse

Every fenced `kest` block in `docs/language.md` and `docs/decisions.md` is
syntax this language has, and `tools/check-docs.sh` says so.

The reference described a language and nothing checked that the language it
described was this one. That is the mistake documentation actually makes: a
shape that was true when it was written and is not any more, or one that was
never true because nobody ran it.

**Parsing and not checking.** A fragment carries no types — `let health =
math.max(hit, 0)` names two things that are not in the block — so asking
whether it means anything would need scaffolding invented for each one, and
invented scaffolding is a second thing to keep true. Parsing needs nothing and
catches what is worth catching.

**A block is declarations, statements, or both.** The tool splits a block at
the first line that is not part of a declaration and puts the rest in a
function, because "here is a `fn`, and here is a call of it" is how the
reference is written.

**The worklog is not held to this.** It records what went wrong, so it holds
code the parser refuses on purpose — the leading-operator break that D003's
newline rule forbids is in there because refusing it was the entry.

**A thing that is not a program is not fenced as one.** A signature on its own
is not Kest: `fn sort(...) no.alloc` with no body is only ever written as
`extern fn`. Two decisions showed one that way, and they are fenced plainly
now.

**What it turned up.** Three blocks used `...` for a body nobody wanted to
write out. That is not an elision this language has, and each is now the code
it stood for.

*Argued.*

## D049 — everything is one command

`make check` builds both ways, runs both hosts, runs or resolves every `.kest`
file, sweeps every command against every file under the sanitisers, and runs
the five tools. It takes no arguments.

Five tools each checked one thing and each was run by hand. "Everything
passes" was a sentence somebody typed, and twice a column had quietly gone
missing from a sweep run that way: once a command that does not exist, and
once one that was never added. Both times the sweep reported success.

**No list of files.** A list is the thing that goes stale, which is exactly
how a file gets left out. It finds every `.kest` under `examples` and `lib`,
and it worked out immediately that one file had been outside every by-hand run
of `check-commands.sh`.

**What a file has to do comes from the file.** One with a `main` runs and
answers nought; one without resolves. Which it is is read off the refusal
rather than off a name written here, so a new library file is held to the
right thing without anything being told about it.

**It looks at what a command said, not at what it returned.** A command that
refuses for a reason is fine; one that walks off the end of an array is not,
and it returns nought while doing it. That distinction is why the sweep exists
and it is why it is written this way.

**Proved by breaking it.** An example answering wrong, a documented block that
does not parse, and a header promise with nothing behind it were each
introduced and each reported, with the file named.

*Argued.*

## D050 — one measurement, and nowhere it is written down

`make time` prints how long a frame step takes per entity, in nanoseconds.
There is one of them, `make check` does not run it, and no file records what
it said.

This language exists to run inside a frame budget and nothing could say
whether it still did. The reason nothing could is that the predecessor died of
measurement: 33 MB of research, a hypothesis board, a benchmark harness, and
no language. So the question is not whether to measure but how to measure once
without that happening again.

**One number.** Not a suite, not a set of workloads, not a comparison against
another language. The one thing this language claims is that an array of value
structs can be walked, read, computed on and written back inside a promise
that nothing reaches the heap. That is what is timed and nothing else is.

**Nowhere it is written down.** A recorded number becomes a series, a series
becomes a graph, and a graph becomes the work. It is printed and it is gone.
Somebody who wants to know runs it twice and compares with the number they
remember.

**Not part of `check`.** A duration is not a pass or a fail. Putting it there
would mean choosing a threshold, and a threshold on a number this noisy is a
thing that fails for reasons that are not about the language.

**Written in Kest, and honest about the noise.** The host already provides a
clock, so the measurement needs no C at all — it is a Kest program using the
language it measures. It takes the best of seven rounds, because anything else
sharing the machine only ever adds time. It still reads about a fifth high on
the first run or two, because the processor has not decided how fast it is
running yet, and the file says so: it will show a change of a quarter and it
will not show a change of a tenth.

**What it does not measure, deliberately.** Starting up, compiling, crossing
the boundary, allocating. Each is a different question and each would want its
own number, and having one number is the point.

*Argued.*

## D051 — a field of an element is read from its address

`a[i].health` reads four bytes. It used to take the whole element out of the
host's layout, keep one piece and drop the rest.

Writing one already worked: `a[i].health = 0` compiled to an address and a
store at an offset. Reading did not, because a field goes through the same
path whether the thing it belongs to is a local, a call's result or an array
element, and only the local had a shortcut. So an element of five fields was
unpacked five ways to answer one question.

**Both shapes are right for what they do.** A pass that touches every field
should read the element whole: one unpack against five. A pass that touches
two of five should read those two. Both are now what they say, and neither is
a rewrite of the other.

**Measured, because it is the kind of claim that should be.** A pass over ten
thousand entities touching two of five fields went from 79 to 66 nanoseconds
an entity, which is above the noise the one measurement admits to. The
measurement itself did not move, because what it times reads the element
whole — which is the honest limit of having one number, and the reason D050
says what it does not measure.

**Asked before emitting.** `compile_address` writes instructions as it walks,
so a caller with somewhere else to fall back to cannot use it as a test. There
is a predicate beside it now that answers the same question without emitting.

*Argued.*

## D052 — a walk that only reads fields does not copy the element

```kest
for one in world {
    if one.health > 0 {
        alive += 1
    }
}
```

This copied all seven fields of a `Npc` out of the host's layout to look at
one. Written the other way, `for i in 0..len(world)` with `world[i].health`,
it read four bytes — so the readable spelling was the expensive one, which is
the opposite of what a language for frame budgets should do.

**One rule, decided by the body.** If every use of the walked name is a read
of a field of it, the name holds where the element is instead of the element.
Any other use — passed to something, returned, compared, assigned to, or a
field of it written — and it holds the element, because that is what such a
body asked for. There is nothing to write and nothing to choose.

**The address is worked out every turn.** An array that grew under the walk is
followed rather than remembered, which is what the copying form did too. A
walk that pushes while it walks gives the same answer either way, and that is
tested.

**What it costs.** Nothing, and that is the point: the two spellings are two
spellings. Walking ten thousand entities and reading one of seven fields went
from 66 nanoseconds an entity to 46, which is what the counted form costs.

**What it turned up.** `Local` gained two fields and neither binder zeroed
them. Slots are reused between scopes and between functions, so a stale "this
holds an address" made `best = took` — an assignment to a plain local — compile
to a store through a null pointer. Both binders clear a name before they write
it now.

*Argued.*

## D053 — the walked name is what the element was, and that is not negotiable

D052 stopped a walk copying the element when the body only reads fields of it,
and in doing so quietly changed what the body meant. This says what the rule
is and pays for it.

```kest
for i, e in w.enemies {
    w.enemies[i].health = 0
    seen += e.health
}
```

Before D052 that added up the healths. After it, it added up noughts, because
the name had become a view of memory the same turn was writing. Nobody chose
that and nothing would have caught it: no example wrote what it was walking.

**The name is the element as the turn began.** That is what a copy means, it
is what the warning about assigning to a walked name has always assumed, and
an optimisation does not get to change it.

**So the address is only taken when nothing could write.** The body must not
name the thing being walked, must write nothing through an index, and must
hand no array, store or reference to a call. That is coarse on purpose: two
handles cannot be told apart here, and there is no global mutable state in
this language, so a write reaches an array only through a name in scope or
through a call that was given one. Refusing both is sound; telling them apart
is a question this compiler does not ask.

**What it costs.** The shapes that matter keep the speed: a walk that reads
fields and counts is still 46 nanoseconds an entity against the 66 it was.
A walk that writes what it walks copies, which is what it always did.

**The lesson is about the check and not the compiler.** `make check` passed
with the hole in it, because every example that wrote what it walked happened
to read before it wrote. `examples/world` now writes first and reads after,
and the old compiler answers 6 on it.

*Argued.*

## D054 — a host resolves a name once and calls by what it found

```c
int32_t spawn = kest_entry(runtime, kest_build_name(build, "spawn"));
kest_call(runtime, spawn, frame, 4);
```

`kest_call` took a name and searched for it, every call, over everything the
program defined — twice, because a host writes `spawn` and a function is
compiled under `spawn#i32`, so the exact pass fails before the prefix pass
runs.

Measured, since D007 says this crossing is the wider of the two and nothing
had ever put a number on it. A call into `return n + 1` was 62 nanoseconds in
a program of six functions and 430 in a program of sixty-one. The cost of
calling into a program grew with the size of the program, which is not a cost
anybody would choose.

It is 21 nanoseconds now, in both, because the search happens once.

**Why a handle and not a faster search.** Indexing the module would have made
the search cheap and left a host paying for one every frame anyway. Finding
what a name means is a start-up question and this makes it look like one,
which is the same reasoning that put the stride and the frame width where they
are (D045, D046).

**It is also how a host asks whether something is there.** `kest_entry` gives
-1 for a name the program does not define, so `kest_defines` is gone: one
question, one answer, one function fewer.

**What it says about D007.** A crossing at 21 nanoseconds against two or three
for an element of a batch keeps the bulk-first shape right, by about the
margin the predecessor measured. The design stands, and now there is a number
behind it that was taken here.

*Argued.*

## D055 — one thing to ask, and it is the runtime

```c
int32_t spawn = kest_entry(runtime, "spawn");
uint32_t needed = kest_frame_slots(runtime, spawn);
kest_call(runtime, spawn, frame, 4);
```

A host held two objects to prepare one call: `kest_entry` against the runtime,
`kest_frame_slots` against the build, and `kest_build_name` to make the name
both of them wanted. Three calls and two objects for one function.

**They are one question about one thing.** Where a function is and how wide a
frame it needs are both properties of the compiled program, and the runtime is
what a host has while it is running. `kest_frame_slots` takes what `kest_entry`
gave, so the name is resolved once for both.

**The name is the one the file writes.** The module knows what the file that
was named calls itself, so `kest_entry` tries the bare name and then the
qualified one. A host writing `spawn` does not have to know the program
registered `embed.spawn`, which is a fact about the program's files and not
about the boundary.

**Not one call returning both.** A `{where, slots}` would tempt a host to pass
the program's own answer back as its frame width, and D046's check is the host
saying how wide *its* array is. Two questions with two answers keeps that
honest.

**What it cost.** `kest_build_name` left the public header — the command line
still uses it, to name into the program's symbol table, which is a different
thing that happens to have had the same spelling. With `kest_defines` gone
last turn, the header is sixteen functions where it was seventeen, and a host
that calls into a program touches the build for two of them.

*Argued.*

## D056 — while a program runs, the runtime is the only thing to ask

`kest_report` is asked of the runtime and not of the build.

A host that got `false` from `kest_call` had to reach back to the object it
compiled with to find out why. That is the shape D055 just took out of
`kest_frame_slots`, one function over: what failed while running is a fact
about the machine that was running.

**Nothing from before the machine started.** A runtime records how much had
been said when it began, so what failed to compile is not its to report. That
already went to the `errors` stream `kest_build` was given, and would have
been said twice otherwise.

**The build is for building.** Four functions touch it: compile, free, start,
and the name the command line needs internally. Everything a host does while
its program is running — call, lend, ask how wide, ask what happened, ask what
was allocated, throw the heap away — is the runtime.

**A host that only wants to run still has to compile.** A Kest program is
source and somebody has to turn it into a program; there is no compiled
artefact to load, and inventing one to save a host two lines would be a file
format, a version, and a way for the two to disagree. What the boundary can do
is make the compiled thing something a host hands over once and then forgets,
and that is now what it is.

**What it turned up.** A refusal about a frame named the function as it was
compiled — `embed.spawn#store<embed.Npc>,i32` — which is a name no host ever
wrote. It says `embed.spawn`.

*Argued.*

## D057 — the program says how much room it needs

```c
KestLimits limits = {0, 0};
if (kest_needs(build, &limits)) { }
```

`kest_start` took a stack size and a call depth and a host had nothing to base
them on. It picked numbers and found out at the worst moment whether they were
enough — `examples/embed` said "fifty frames of a hundred and twenty slots"
and neither number came from anywhere.

The program knows. Every chunk carries the slots it needs and the depth its
own stack reaches, and the bytecode carries who calls whom, so the deepest run
of frames and what those frames take together is a walk of the call graph.

**Enough for every function, not least for one.** A host may call anything the
program defines, so the answer is the worst of them. It is loose for any
particular call and that is the right looseness: a host wants one number, not
one per call site.

**No answer is an answer.** A program that can reach itself has no deepest run
of frames. Neither has one that calls through a function value, because what a
value points at is not known until it runs — D039 put the promise in the type
and did not put the target there. Both say so rather than guessing, and a host
that is told there is no answer does what every host did before: picks.

**Verified by running at exactly it.** Eight examples were run with the stack
and depth it gave and each answered nought. That is what turned up the bug:
the walk sized a jump at seven bytes because `JUMP` and `BACK` are their own
operand kinds, so decoding went out of step after the first `if` and half the
calls in a program were never seen. The numbers were too small and the
programs would not start.

*Argued.*

## D058 — a promise is proved against what was emitted, as well as what was read

`no.alloc` is checked twice: once by walking the tree, which is where a
refusal can name the path down to the body that allocates, and once by walking
the instructions, which is where there is nothing to miss.

The two are not one graph and cannot be. The tree walk runs before anything is
emitted and reports against source spans; the instruction walk runs after and
knows what the machine actually does. What they can be is checked against each
other, and this is that check.

**Why it is worth having.** The tree walk has had two silent holes: it never
looked inside a `match` arm, and it never looked at a call it could not name.
Both were found by accident and both meant a promise the compiler had allowed
and the code did not keep. The instruction walk asks the machine's own list of
which opcodes call the allocator, so the only way to have a hole is to add an
allocating instruction and not add it to that list — which is one line beside
the one that allocates.

**It is a backstop, not a replacement.** Its message is worse on purpose: it
points at an instruction and says the promise was allowed and the code says
otherwise, which is a fault in the compiler rather than in the program. If it
ever fires for anyone, the tree walk is what needs fixing.

**What it does not follow.** A call through a function value, because the
bytecode does not say what the value is. That is the one place the type is the
only evidence and D039 is what makes it evidence.

**Proved by holing the tree walk.** With `walk_expr` made to skip an `if`, a
`no.alloc` function that builds an array inside one is allowed by `check` and
refused by `emit`, at the right line.

*Argued.*

## D059 — one answer to how wide an instruction is, and a check that it is right

`kest_op_width` is the only place that knows how many bytes an instruction
takes. The disassembler prints operands its own way and moves by that answer;
so do the two walks over the code.

There were two answers. The disassembler had a switch that printed and
advanced together, and `width_of` had another that only advanced, and D057's
bug was exactly the disagreement: a jump was seven bytes in one of them and
three in the other, so a walk went out of step after the first `if` and half
the calls in a program were never seen.

**One answer is not enough on its own.** Nothing says the one answer is right.
So every chunk is walked to the end when a program is built, and it has to
land exactly on it. The last instruction of every chunk is a return, so a
width that is wrong for anything before it either overshoots or stops short,
and a program that cannot be walked is refused with `K0406` before it runs.

**Proved by making it wrong.** With a jump declared seven bytes wide, building
`examples/state` says `state.next` has 170 bytes of code and a step that lands
on 171. That costs one pass over the code per build and it is the difference
between a wrong number and a wrong number that says so.

**The two graph walks are not merged.** `kest_module_needs` computes two
numbers over a call graph and `kest_module_prove` looks for the first
allocation in one; putting them together would make one function that does
neither clearly. What they share is the stepping, and that is what is shared.

*Argued.*

## D060 — the compiler's checks on itself are checked

`tools/check-backstops.sh` puts each of the two refusals the compiler keeps
for its own mistakes out of order, one at a time, and requires it to fire.

D058 and D059 added checks that only speak when the compiler is wrong. Nobody
had ever seen either of them speak except by hand, and a net nobody has seen
catch anything is indistinguishable from no net.

**Each break is a hole this compiler has actually had.** A tree walk that does
not look inside an `if` is the shape of the two holes `contract.c` really had;
a jump that says it is a different width is D057's bug exactly. This is not
mutation for its own sake and there is no framework here: two named breaks,
each with the program that should be refused and the code that should refuse
it.

**In a copy of the tree.** The tool never edits `src`, so there is no way for
it to leave the repository broken — which matters more than it sounds, because
the last entry lost a turn's work to a `git checkout` used for exactly this.

**It says when the code it breaks has moved.** A break that no longer applies
is reported rather than passed over, so a rename cannot quietly turn this into
a check of nothing.

**What it taught.** The walkability check could be fooled by a program small
enough that a wrong step lands back on the end by luck. It asks more now: every
step has to land on something that is an instruction, and the last one has to
be the return every chunk ends with. A four instruction program can still
align; a program with a loop and a branch cannot, and that is what the tool
uses.

`make check` runs it, and is nine seconds.

*Argued.*

## D061 — `defer` runs on every way out, in reverse

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

The reference said `type` and `defer` were reserved and the compiler let both
be used as names. One of the two claims is now true because the word does
something, and the other because the word is refused.

**Why this and not something else.** A function that takes something from the
host has to give it back on every way out, and the ways out multiply as a
function grows: three checks is three places to remember. Written once, beside
what it undoes, a `return` added later cannot forget it. That is what the word
is for and it is the shape this language is for.

**In reverse.** What was taken last is given back first, which is the only
order that undoes things.

**On every way out.** Off the end of the block, through a `return`, through a
`break`, through a `continue`. A `return` runs everything outstanding, a
`break` runs what the loop it is leaving added, and a block runs what it added
itself — unless it left through one of those, which already ran them.

**After the answer.** `return f()` works out `f()` first and then runs what
was deferred, so a deferred call sees what the function decided rather than
changing it.

**A call and nothing else.** `defer 1 + 1` is refused: a `defer` runs
something. A deferred block would want its own scope rules and nothing has
asked for one.

**It costs what it runs.** A deferred call counts against a `no.alloc`
promise, because it still runs. There is no new instruction: the compiler
writes the calls out where the ways out are.

**`type` is kept back.** Using it as a name is refused with nothing promised
about what it will mean, so nothing has to be renamed the day it means
something.

*Argued.*

## D062 — `defer` costs what writing it out costs, and that is the whole answer

Measured, because the last entry left the question open: a function with three
deferred calls and five ways out compiles to 223 bytes, and the same function
with the three calls written before each of the five returns compiles to 223
bytes. The same fifty-seven instructions.

That is the finding. `defer` is not a tax; it is what a careful person would
have written, written by the compiler instead so that a `return` added later
cannot forget it.

**The cheaper shape is a different program.** Splitting the work into an inner
function and doing the cleanup once in a wrapper is 123 bytes, and costs a call
on every invocation and a function that now exists. That is a design decision
about where the boundary is, not something a compiler should make.

**Not a list at run time.** Keeping what was deferred in a structure the
machine walks on the way out would trade code size for bookkeeping on every
call that defers anything. This language is about cost being visible, and a
call that quietly does more than it says is the opposite.

**So there is nothing to fix.** Recording the number is the point: the
question was whether the duplication mattered, and it is exactly the
duplication a person writing the same program would produce.

*Argued.*

## D063 — the library has vectors

`std.vec` is `Vec2` and `Vec3` of `f32`, with add, sub, scale, dot, length,
distance, direction, lerp, perpendicular and cross.

Three examples each declared their own two or three component vector and wrote
their own length and scale. A language for games and simulations shipping no
vector is a gap anyone would find in the first hour.

**Value structs, so a host can lend them.** A `Vec3` is three slots on the
stack and twelve bytes in an array, which is the twelve bytes an engine
already has. That is D006 and D016 and this module is where they meet
something anybody would use.

**`vec.add(a, b)` and not `a + b`.** There is no operator overloading and this
is not the module to want one: a symbol means one thing here, and a language
that lets `+` mean whatever a type says is a language where reading a line no
longer tells you what it costs.

**A vector of nought length has no direction.** `direction` gives an optional,
because that is a question with no answer rather than a zero to return
quietly, and D013 is the shape the language already has for it.

**`length` needs the host.** The square root is `std.math`'s, which is an
extern, so importing `std.vec` means a host that binds it. That is the honest
cost of a library that can measure things, and `lengthSquared` is there for
everything that does not need to.

**Not `examples/frame`.** It declares its own vector and keeps it: that file
is about boundary declarations and mutual struct references, and declaring is
what it is for.

*Argued.*

## D064 — `[T; N]` is that many where it stands

```kest
struct Transform {
    m: [f32; 4]
    tag: i32
}
```

Twenty bytes, with the floats inside. Before this a Kest struct could hold
`[f32]`, which is eight bytes and a handle to something elsewhere, so a C
struct with an array in it could not be described at all — and that is the
shape a host lends: a transform, a colour, a fixed run of samples.

The question this came from was `std.vec` writing ten functions twice, and
that is not what this is for. Writing `Vec2` and `Vec3` separately is fine;
what was missing was a struct that matches what a host already has.

**A value, not a handle.** Copying one copies all of it, passing one passes
all of it, and a struct holding one holds the whole thing. `[T]` is the other
thing and stays the other thing: shared by every name that holds it, and able
to grow. That is why `push` is refused on one of these and why the two are not
the same type.

**The count is written and known.** `len` is a constant with nothing loaded.
An index is checked against it while running, because an index is worked out
while running.

**Not a name for the count.** A count that could be a constant somebody
changes is a size that could change, and the layout is the thing this exists
to pin down. It is a literal between one and 65535.

**`;` moved from the lexer to the parser.** It was refused where it was read,
which meant deciding statement structure in the lexer; `[f32; 16]` has no
statements in it. It is a token now and the refusal is where a statement ends,
with the same message.

**What it cost.** Three instructions: two for reading and writing one of them
where the run is in slots, and one for stepping an address by an index where
the run is in memory the host laid out. All three check the index.

*Argued.*

## D065 — `std.vec` keeps its names, and `[T; N]` gets walked

Two answers, one no and one yes.

**No, `std.vec` should not be `[f32; 2]` and `[f32; 3]` underneath.** The
question was whether writing `add` twice could become writing it once over a
count. It could, if generics took one — but the price is `v.parts[0]` where
`v.x` was, in the code everybody reads, to save ten short bodies in the code
almost nobody does. `x`, `y` and `z` are what a vector's parts are called and
a library should call them that.

**Yes, a fixed run should be walked.** `[T; N]` shipped indexed and counted
and not walkable, so `for i in 0..len(m)` worked and `for one in m` did not.
That is an inconsistency in the feature rather than a decision anybody took.

**The walk is over a copy.** That many of something is a value, so walking it
where it stands would let a write to the run inside the body change what the
walk reads — which is exactly what D053 refused for an array, and there is no
reason for the two to differ. The copy is the run's slots once, and the loop
is already that long.

*Argued.*

## D066 — an index written down is not an index

`m[2]` on a `[f32; 4]` compiles to the instruction `a.z` compiles to, and
`m[5]` on four of them is refused where it is written.

It did not. A written index went through the same path a worked-out one does:
a constant pushed, a bounds check that could not fail, and an instruction that
takes a base and a stride. Three bytes became ten, for an answer known before
the program ran.

Measured, because the question was what a fixed run costs against a struct
with the same fields. Now: 93 nanoseconds against 94 over ten thousand of
them, which is the same number, and the bytecode says why — it is the same
bytecode.

**Everywhere, not only reading.** A written index folds into a slot when the
run is in slots and into a byte offset when the run is memory the host laid
out, for reading and for writing alike. Four paths, one rule.

**Out of range is a refusal now.** How many of them is written down and so is
the index, so the answer is known when it is read. It says so with the number
and the count, where the index is.

**And walking costs what a loop costs.** `for one in run` came out at 251
nanoseconds against 263 for the same loop written by hand with a count and a
worked-out index — the same, which says the copy D065 makes costs nothing
measurable and the difference from unrolled reads is the loop itself. There
was nothing to fix there, which is worth knowing rather than guessing.

*Argued.*

## D067 — what is lent has a name

A host lends an array of `Point`, not an array of `[f32; 3]`.

`[[f32; 3]]` works inside a program: it is built, indexed, written and walked
like anything else, and the nesting was already right. What had never been
tried is lending one, and it cannot be: a lend names a type (D045) and a run
is spelled out of other types and has no name of its own.

**That is the rule rather than a gap to close.** The alternative is a host
spelling `[f32; 3]` and the runtime matching that string, which puts a
boundary check — the whole point of which is catching a disagreement — on
getting a space right. A declared struct gives both sides a name, costs one
line, and is the same twelve bytes.

**So the refusal says so.** A name with a bracket, an angle or a question mark
in it is a type spelled out of others, and the message writes the struct that
would give it a name.

`examples/embed` lends an array of a struct with a run inside it now, in both
directions of the build, which is the shape D064 exists for and had only ever
been crossed in a scratch file.

*Argued.*

## D068 — what can be lent is what the declarations say

`kest_borrow` looked a name up among the layouts a module happened to have
made, and a layout is made where a body reaches into an array. So a program
declaring `fn how(all: [Point]) -> i32` and only counting them could not be
handed any: the signature said `[Point]` and the boundary said no.

What a host can be handed is a question about what the program takes, and that
is written in its declarations. Every element type a signature mentions gets a
layout now, whether or not a body ever reached one.

**Not everything the program declares.** A type that appears in no array and
no store is still refused, because there is nothing to lend an array of. The
message is the same and is still right.

**Why it was hard to notice.** A program that takes `[Point]` almost always
indexes one somewhere, and one function doing so is enough for every other. It
takes a program that can only count what it was given, which is rare enough
that it went a long time without being tried and rare enough that a contrived
example covering it would be worse than this paragraph. `make check` covers
the lend that a host actually does.

*Argued.*

## D069 — a handle says what it is

An `Array` and a `Store` each begin with a word saying which they are, and
every instruction that takes one checks it.

A host holds both — an array from `kest_borrow`, a store from whatever the
program handed back — as opaque values it cannot tell apart. D046 drew the
line at frame widths and said the right number of slots holding the wrong
things is still the host's to get right. This is the one case where that is
not good enough: handing a store where an array was wanted reads a slot map as
a run of elements, which is not a wrong answer but a wrong memory read.

**Found by asking whether a store can be lent.** It cannot, and the answer
came with a demonstration: a host lending an array of `Npc` and handing it
where `store<Npc>` was wanted got a count of nought and no complaint. The
previous entry had made that reachable by laying out store element types,
which was wrong on its own terms — a signature saying `store<Npc>` does not
say it takes an array of them — and that is undone.

**It costs nothing.** `make time` was 160 to 172 nanoseconds an entity before
and 142 to 153 after, which is to say the compare is free and the header
growing by a word did not hurt. Measuring first was the point: the argument
against this was that a check on the hot path is not free, and it turned out
not to be a check on anything that could be measured.

**A store still cannot be lent.** It is a slot map with generations, live
flags and a free list, and nothing a host has is one. A host that wants one
asks the program to make it, which is what `examples/embed` does.

*Argued.*

## D070 — `'a'` is one byte, and exactly one

`std.text` decided whether a byte was a space by writing `byte == 32 ||
byte == 9 || byte == 10 || byte == 13`, and lower case by `>= 97 && <= 122`.
Nobody reads that; they take it on trust, which is the opposite of what a
library is for.

`'a'` is a `u8` whose value is that byte.

**Not a character type.** D021 says text is its bytes and there is no
character, and this does not add one: `'ı'` is two bytes and is refused, with
the reason. What is written between the quotes has to be one byte, and the
diagnostic says how many it was.

**The same escapes a string has.** `'\n'` and `"\n"` are the same byte read
the same way, because two spellings of one byte is what D004 refuses.

**The type is `u8` and only `u8`.** A byte is what indexing text gives, so the
comparison a program writes is between two of the same thing without either
being converted. There is no widening: `i32('0')` is written where a number is
wanted, the way every other conversion is.

**What it turned up.** A new token kind has to be added to the list of what a
newline may end a statement after, and nothing says so. Without it, a line
ending in a byte literal swallowed the next one. That list is a third parallel
thing beside the two `check-tables.sh` holds, and this one has no mechanical
rule to check against — so it has a comment saying what it is instead.

*Argued.*

## D071 — `while let` is `if let` asked every turn

```kest
while let task = newest(queue) {
    spent += task.cost
}
```

Emptying something was two levels for one idea: a `while` counting what was
left and an `if let` opening what came back, with the count and the lookup
having to agree about a thing that cannot fail. Now the question is asked once,
where it is answered.

**The same shape as `if let`, because it is the same question.** What the
optional held is named for as long as there was something to name, and the
name exists only inside the loop. Nothing new is decided; D013 already decided
it and this is the loop form of it.

**Where the turn that stopped leaves its value.** An optional is what it holds
with a tag above it. The jump takes the tag; the turn that ran stores what is
below it into the name, and the turn that stopped has to drop it. So the way
out of the loop is not where a `break` lands — a `break` happens after the
store, with nothing left to drop.

*Argued.*

## D072 — a host may call in from inside a call

A bound function that calls `kest_call` used to write over the frame it was
called from. It started every run at the bottom of the stack and at frame
nought, so the arguments of the inner call landed on the locals of the outer
one. It segfaulted, which is the good case.

It starts above what is already running now. Where the machine is, is written
down before a bound function is invoked, and a run started from inside one
puts its frame and its stack there.

**Why support it rather than refuse it.** An engine whose rules live on both
sides calls the program from inside the program's call to it: it asks how much
a step costs, and the answer is the program's. D007 says the shape to reach
for is one crossing carrying a batch, and that is still true; this is the
other shape, and a segfault is not an argument against it.

**Room is the host's to ask for.** `kest_needs` answers for one call in,
because how many times a bound function will call back is the host's to know.
A host that does adds what it needs, and running out is `K0602` where it
happens rather than a wrong read: a thousand and twenty-four levels deep,
which is the frame limit, and the unwinding is clean.

**The command line is a host with no engine.** It binds `Engine.decide` to
answering one, because it has nothing to ask; `examples/embed` binds it to
asking the program. Two hosts, two answers, one program — which is what the
boundary is for.

*Argued.*

## D073 — a bound function may not take away what the program stands on

`kest_heap_reset` and `kest_runtime_free` are refused while the program is
running, and say so.

The previous entry taught the machine where it is while a bound function runs,
which made a call back in work. The same knowledge answers what else that
function may do. Three things were reachable from there and only one had been
thought about.

**Resetting the heap read what it freed.** A program holding an array called a
bound function that reset the heap; the array's block was freed and the program
read it on the next line. The sanitiser named it. D025 said a host may reset
the heap and the header said what that costs the *host*; neither said it is
between calls and not inside one.

**Freeing the machine is worse and quieter.** The frames and the stack are what
the interpreter is standing on. It is refused and told, and the heap then waits
for `kest_build_free` — a leak rather than a read of what was freed, which is
the right way round.

**Lending from inside is fine and stays fine.** It puts something on the heap
rather than taking the heap away, which is the distinction: what a bound
function may not do is remove what is already there.

**How the machine knows.** It is running exactly when a bound function is on
its stack, which is what D072 wrote down for its own reasons. Nothing was added
to find out.

*Argued.*

## D074 — a name the host provides is bound once

`kest_host_bind` refuses a name that is already bound, rather than replacing
what is there and answering that it worked.

The old answer was true until a machine had started. `kest_start` resolves
every extern the program declares against what the host holds at that moment
and keeps what it found; a rebind afterwards changes the table and not the
machine, so the call reported success for something that would not happen. A
host swapping a function for a frame — a different clock, a stub in place of
the real thing — got the old one and no way to tell.

Two answers were available. Rebinding could reach into every runtime the host
has started and repoint them, or the second binding could be refused. The
first makes a name something that changes underneath a program, which is the
kind of thing that is fine until a call is in flight, and it makes the host
table own the runtimes it has produced. The second is a line of code and says
what it means: this is what the machine will be built from.

A host that wants to swap a function binds one that decides. That is C it was
going to write anyway — the choice lives in the host's own state, where the
host can see it — and it works during a run, which repointing a table never
would.

The refusal is the same shape as running out of memory: `false`, and the host
looks at what it asked for. There is nothing to add to the diagnostics, because
binding happens before there is a program to report against.

*Argued.*

## D075 — a name that is there and cannot be called says why

`kest_entry` answers -1 for a name nothing knows and says nothing about it. For
the two names the program has and cannot hand over, it writes a diagnostic and
`kest_report` says which.

-1 was doing three jobs. A name the program does not define is one, and it is
the job the function is partly for: a host asks whether a program defines
something and gets an answer without anything being wrong. The other two are a
host mistake wearing the same clothes. A generic is compiled once for each set
of types it is used with, so `pick` is `pick#T,T$i32` and `pick#T,T$f32`;
`kest_module_find` answers a single copy under the plain name and refuses when
there are several, which is right and was silent. And an extern is a function
the host provides, so a host asking for one is asking the program for its own
function back.

Both of those sent a host looking for a typo in a name that was spelled
correctly. The generic one is worse, because the name it has to pass instead is
not written anywhere in the program: `K0615` lists the copies, and those names
resolve.

The alternative was more return codes — -2 for generic, -3 for extern. That
puts the explanation in a number the host has to look up, and every caller that
tests `< 0` keeps working only by accident. The diagnostics already exist, are
already how a host finds out why a lend or a call did not work, and carry a
span: `K0614` points at the `extern fn` line that asked for the function.

A miss stays silent. Reporting one would make asking a question cost an error,
and the answer to the question is the return value.

*Argued.*

## D076 — zero is a width, so the other zero says what it is

`kest_frame_slots` answers zero for an index that is no function and reports
`K0616` while doing it. Zero stays the answer, because zero is also true.

A function that takes nothing and gives nothing needs a frame of no slots, and
that is the honest width rather than a stand-in for failure. An index that is
no function has no width at all, and the same zero came back. A host that
passed `kest_entry`'s -1 straight through without looking at it got a number
that means both, sized nothing, called, and found out from `K0607` — a message
about the call rather than about the mistake, one step late.

The rule in `CLAUDE.md` says a function that can fail returns a status rather
than a sentinel the caller can forget to check, which argues for
`bool kest_frame_slots(runtime, entry, uint32_t *slots)`. It was not taken.
The answer to "how wide" is a width, and every host writing
`KestValue frame[kest_frame_slots(...)]` would grow a temporary and a branch to
carry a failure it already has a better way to see. The sentinel is what the
rule is against; the sentinel is gone the moment the number stops being the
only thing that says anything, which is what the diagnostic does.

This is D075's answer applied one step along: -1 out of `kest_entry` is
explained, and now what a host does with a -1 it did not look at is explained
too. Both are the same shape, both go where a host already reads.

*Argued.*

## D077 — the boundary says things in two forms

`kest_build` and `kest_report` take a `KestForm`. `KEST_FORM_TEXT` is the prose
a person reads and `KEST_FORM_JSON` is the same set as JSON. Nothing is in one
form and not the other.

The commands have had `--json` since the beginning, because a model repairing
what it wrote should not be parsing carets. A host embedding the library had
prose and nothing else, which made the third goal true of the CLI and false of
the thing the CLI is one host of.

The form is a parameter at each of the two places output is written, rather
than a setting on the build or a second pair of functions. A pair —
`kest_report_json` beside `kest_report` — doubles with every form there ever
is, and the two would drift the way two tables of names drift. A setting on the
build is a mode: something written far from the call it changes, which then has
to be remembered when reading the call. A parameter is at the call, is what it
affects, and `KEST_FORM_TEXT` at a site says what that site does.

Both places take it, and not only the one the work started at. A host with JSON
for what failed while running and prose for what failed to compile would have
to parse both, which is worse than having neither.

Asking twice writes two objects rather than one that grew, because a report is
of what has been said since the last one. The count inside is of what that
object holds; it was of the whole run, which is the sort of number that is
right until somebody reads it.

*Argued.*

## D078 — a host can read what the program asks it for

`kest_build_extern(build, at)` answers a name the program declares as an
`extern fn`, and NULL past the last one.

`kest_start` refuses a program whose externs are not all bound, one diagnostic
per name, which is the right refusal and the wrong way to find out. A host
embedding a program it did not write bound what it guessed, started, read the
names out of the refusal, bound those, and started again. The information was
always there; nothing let a host ask for it.

It is asked of the build rather than of the runtime, because the whole point is
to ask before there is a runtime, and the build is what a host has then. It
answers one name at a time and NULL at the end rather than a count and an
array: a count and an accessor are two things that can disagree about how many
there are, and an array of `const char *` is a lifetime the host would have to
be told about.

The list is what the program declares and not what it calls. A file importing
`std.math` for one function asks for all seven, because that is what starting
will hold it to, and a list that did not match the refusal would be worse than
no list.

*Argued.*

## D079 — no least is two answers, not one

`kest_needs` takes a `KestReason *`. False still means there is no least, and
the reason says whether something can reach itself or something calls through a
value, and which function it was found in.

The two are not the same news. A run of calls that comes back round is a shape:
a host that did not know its program had one can open the function named and
decide whether that was meant. A call through a function value is the language
working, and leaves a host nothing to do but pick a number. One false was
telling both, so a host could not tell the program it might be able to size from
the one it never can.

The return stayed a `bool` and the reason came in beside it. Returning the enum
instead reads better and breaks silently: every `if (kest_needs(build, &limits))`
already written keeps compiling and means the opposite, because the value for
"there is an answer" is the one that is zero. A new parameter is a compile error
at every call site, which is what a change of shape should be.

`KEST_REACH_UNASKED` is there so that every false has a reason. Not compiled and
no room to work it out are not "the program has no least", they are "nobody
asked", and the last two turns were both about a single value quietly meaning
two things.

The function is named because there is otherwise no way to find it. Nothing
else in the boundary reports a call graph, and "something in your program can
reach itself" without a name is a search.

*Argued.*

## D080 — what a lend will be held to is readable before the lend

`kest_build_layout(build, name, &layout)` answers how many types of that name
the program lays out, and what one of them is when there is exactly one.

`kest_borrow` compares a host's `sizeof` against the program's stride and
refuses when they disagree, which is the check that matters and was the only
way to run it. A host lending in a loop found out at the first lend, and a host
that lends nothing until frame nine found out at frame nine. What the check
compares against was in the build all along.

The answer is a count and not a pointer, because a name can fail to mean one
type in two different ways: the program does not hold it in an array, so it
cannot be lent at all, or the program has two of them and the name needs its
module in front of it. Those are the two refusals a lend has besides the size,
and a NULL would have said both.

The whole layout is handed over rather than the size alone. It is already a
public type, it is what the machine itself reads to pack and unpack, and the
pieces let a host check its struct field by field rather than trusting that two
sizes agreeing means two shapes agree.

The lookup moved to `kest_module_layout_of` and the lend now asks it. Two
walks over the same table, one deciding what a lend is allowed and one telling
a host what to expect, is how a host is told one thing and refused for another.

*Argued.*

## D081 — a diagnostic with nowhere of its own still has its notes

`kest_diags_render` dropped every note on a diagnostic whose own span is not in
a file. It now renders them, and a refused lend uses them to point at the
declaration it is about.

A lend happens at a host, not in a file, so `K0610` has no span: the renderer
printed the message and the suggestion and went to the next one, notes and all.
The JSON form kept them, which is the half of this that is worse — the two
renderings are supposed to be the same set, and one of them had been quietly
losing what the other showed. Nothing had noticed because nothing had put a
note on a diagnostic that has nowhere of its own until now.

What a refused lend shows is the program's side. Two types of one name carry a
note at each declaration, and the fix names the one that can be asked for: a
name with its module in front is the only one of two a host can say and get.
A size that disagrees carries the fields and their byte offsets at the
declaration, up to four of them and a count of the rest.

Which field moved is not said, because it cannot be: the library never sees the
host's struct, only the size of one. Saying "these are the bytes I have, and
here is where they were written" is the whole of what this side knows, and it
is what a host needs to put the two side by side.

*Argued.*

## D082 — a lend suggests the nearest name that can be lent to

`kest_module_nearest` answers the closest name the program holds in an array,
and a lend that does not know the name given offers it.

Every other stage does this. An unknown type in a file is met with the nearest
declared one and an unknown field with the nearest member, because a name that
is one edit away is almost always the name that was meant. A lend was the one
place that said only that it did not know, and a host looking at a name it had
spelled correctly a moment ago has nothing to go on.

Only what can be lent is offered. The program's whole type table has names a
lend would refuse for a second reason, and a suggestion that fails the same way
the first attempt did is worse than none, which is what the rule about wrong
suggestions already says.

What is offered is what a host can write and get back one type: the plain name
when it means one, and the whole of it when the plain one means two. That
answer was already being worked out where a lend refuses two of a name, so it
is one function now — `kest_module_askable` — and not two rules that could
disagree about which name a host should write.

The measuring is `kest_edit_distance`, which was the checker's and is now
shared rather than copied, along with `kest_type_written`: what a program calls
a type is matched on in one place, printed in another and suggested in a third,
and three copies of a `strrchr` is how they stop agreeing.

*Argued.*

## D083 — no frame is a frame of no slots

`kest_call` with a null `frame` is a call with nothing in it, and a function
that takes anything is refused rather than run.

Every check in the call was written `frame != NULL && ...`, which read as
carefulness and was permission: a host passing no frame skipped the width
check, skipped the copy in, and ran the function on whatever the stack floor
was still holding — the arguments of the last call, usually. It said it had
worked. `kest_call(runtime, twice, NULL, 0)` on `fn twice(n: i32)` doubled the
number the previous call had left there.

A null frame is a real thing to pass. A function that takes nothing and gives
nothing needs no array, and making a host declare `KestValue frame[1]` to call
it would be ceremony. So it stays allowed and means what it says, which is
nought slots, and the checks that were already there do the rest: the message
for it is the one a too-narrow frame already gets, because that is what it is.

Nothing below the checks guards against a null any more. It cannot reach them:
a frame of no slots that copies anything in or out has been refused. A guard
that can never fire is read by the next person as a case that can happen.

*Argued.*

## D084 — what comes back is known before the call

`kest_call` refuses a frame too narrow for the result before it runs the
program, rather than after. `kest_module_prove` holds the emitted code to what
that answer is worked out from.

The check was `returned > slots`, asked once the function had returned: the
program had already done whatever it does, and the answer it did it for was
thrown away for a frame it could have been told about first. A host calling
`make` with a frame one slot too narrow watched it print and then fail.

The width is in the declaration. `kest_frame_slots` has always answered from
`result_slots`, so the call now refuses against the same number the host was
told to size by, and the two cannot disagree about a call.

That trusts the emitted code, so the emitted code is held to it. `K0407` is
the third invariant the compiler proves about its own work: no `return`
carries a width greater than the declaration. Less is allowed and happens —
the `return` written past the end of a body gives nothing and is there for a
body that falls off it — and more is what would be read back into a host's
frame past the end of it, which is the direction that matters.

An invariant nobody has seen fire is indistinguishable from none, so
`check-backstops.sh` breaks it on purpose: a compiler that emits `size + 1`
must be caught, and is.

*Argued.*

## D085 — the heap has a ceiling the host sets

`KestLimits` gains `heap_bytes`. Zero is no ceiling, which is what every host
had before there was one, and crossing it is `K0617` at the instruction that
asked.

The stack and the depth of calls were what a host could say, and both are
things `kest_needs` works out because they do not change while a program runs.
The heap is the one that does. D012 defers freeing, so what a program has
allocated only goes up between resets, and a host in a frame budget had one
number to watch and nothing to hold it to: the machine would take what the host
wanted for something else and the host would find out from the operating
system.

It is refused rather than reported afterwards, and refused where it happened.
`K0605` was already the message for an allocation that did not happen, and the
two are not the same news — a machine that has run out is nobody's mistake and
a ceiling is the host's own number coming back — so a ceiling says so, with
the number it was given.

The ceiling lives in the arena rather than in the machine. Ten instructions
allocate and every one of them already handles being answered NULL, because a
host can always run out; a check in each of the ten would be ten places to keep
in step with each other. Refusing there also costs nothing: it is asked before
a block is taken from the host, so a program held to its ceiling does not
allocate to find out it may not.

The arena keeps a running total instead of walking its blocks to answer. That
walk was fine for a number a host asks for now and then and is not fine for one
asked at every allocation, which is what a ceiling makes it.

*Argued.*

## D086 — a machine says what it is running with

`kest_allowed(runtime, &limits)` fills the struct `kest_start` was given, with
what is actually in force.

None of the three numbers could be read back. A host that kept its own copy was
fine and a host that did not had nothing to ask, which made `kest_heap_used` a
number with no scale: 32 kilobytes is either nothing or everything depending on
a ceiling that lived in the machine and could not be seen. Worse, a host that
passed nothing got the built-in stack and depth, and those are constants in a
source file it does not have.

It answers all three rather than the heap alone. The question a host has is
what this machine is running with, and answering a third of it invites two more
functions later. Filling the struct it was made with also says the shape of the
answer: what comes out is what would have gone in to ask for the same machine.

Zero comes back for a heap with no ceiling, because that is what no ceiling is
and is what the host would pass to ask for the same thing. The stack and the
depth are always a number, because a machine always has both, and that number
is the built-in one when the host had no opinion — which is the part that could
not be found out at all.

*Argued.*

## D087 — a machine says what it said, and not what another did

`kest_start` gives each machine its own diagnostics. Two machines from one
build share the compiled program and the arena the strings live in, and nothing
else.

They shared the build's run of diagnostics, and the header's two promises about
`kest_report` — that nothing is written twice and that nothing from before this
machine started is written at all — were both false as soon as there were two.
A machine started before another failed had that failure inside its own range,
so it reported it as its own, and then the one it belonged to reported it
again. Neither of them was told anything true.

Ownership per diagnostic was the other way to do it: a field saying which
machine raised each one, stamped at every site that adds one. That is eight
places in the machine to keep in step for a field that only exists to be
filtered on, and the filter is a renderer that walks a set and skips most of
it. A machine that holds its own is the same answer with nothing to keep in
step.

The command line still goes through `kest_start`, which is the door a host
uses, and takes the set rather than a rendering because it sorts what running
found together with what compiling did. That is `kest_diags_absorb`, and it is
the reason a machine's diagnostics are on the build's arena rather than its
own: the two runs end up in one and nothing is copied twice.

What is shared is read-only while a program runs. A generic is copied per set
of types while compiling; nothing adds a function, an extern or a layout to a
module once it has been built.

*Argued.*

## D088 — a header declares what is there and nothing nobody calls

`tools/check-dead.sh` holds every header to two things: what it declares
exists, and something other than the file it lives in calls it. The public
header is held to the same rule through the two hosts in this tree.

Three things were found by looking, which is two more than the reason for
looking. `kest_vm_run` had no callers and the worklog says it was removed when
the command line started going through the same door a host does; it was not,
and nothing noticed for as long as the file has been there. `kest_ast_dump_all`
printed every file's tree and nothing asked it to. `kest_load` was declared in
`loader.h` and never written at all — a promise the linker would have kept
quiet about until somebody took it up.

Five more were reachable only from the file that defines them, which means the
header was announcing a module's interface that no module uses:
`kest_lexer_init`, `kest_lexer_next`, `kest_fn_of`, `kest_nearest_type` and
`kest_op_width`. They are static now. The last one is the interesting one: its
comment says anything that walks a chunk asks it and nothing works the answer
out for itself, which is still the rule and now says that the day something
outside walks one, this stops being static rather than being copied.

Two public functions had no host in this tree using them, `kest_build_extern`
and `kest_host_find`. Both are now what `examples/embed.c` reads before it
starts: what the program asks for, and whether this host has it. What a host
cannot be shown using is what nobody has run, and `make check` runs both hosts.

The symbols are read out of the objects rather than out of the text. A name in
a comment is not a call and a name in a string is not a definition, and this
project has been caught by exactly that kind of reading before.

*Argued.*

## D089 — the second host is compiled once

`examples/embed` is built from `build/release/embed.o` rather than straight
from its source, and `check-dead.sh` reads that object instead of making one of
its own.

The tool needs to know which of the public functions a host calls, and that is
readable in an object and gone once it is linked: after the link every name is
defined, so nothing says who wanted it. It was compiling the file a second time
to get one, with its own flags, which is a copy of what the build does and a
copy that would go on agreeing with it right up until the day it did not.

The object also has to be there rather than made when missing. A tool that
quietly builds what it cannot find is a tool that passes when the thing it is
checking has not been built, and the whole point of it is to say that a public
function has no host. It says to build first.

*Argued.*

## D090 — the checks about the tree are broken on purpose too

`check-backstops.sh` now puts `check-dead.sh` out of order as well as the
compiler's three proofs about what it emitted.

It was written for the compiler catching itself, and a hole named a program to
run and a code to look for. A tool that says a header declares what is there is
the same kind of thing: it only fires when this project is wrong, so nobody has
seen it fire, so nothing says it works. A hole now names either a program to
run or a tool to run, and the two new ones add a declaration for a function
nobody wrote and a function nothing outside its file calls.

The copy grew to hold `tools` and `examples`, because a tool break has to run
the tool and the tool reads what the second host calls. That is the cost of
checking the check, and it is four seconds.

The alternative was to have the tool prove itself, which is a tool that fails
on purpose inside a run that is meant to say whether things are right. One
place breaks things and it is not the place being broken.

*Argued.*

## D091 — the bottom of a walk is one instruction

`KEST_OP_NEXT` adds one to a walk's own count and goes back. It replaces the
five instructions every counted walk in the language ended with.

Where the time goes was measured before anything was changed, with a counter in
the dispatch loop that was thrown away afterwards. `tools/frame.kest` runs 632
million instructions for eighty million entity-steps, which is seventy-nine per
entity, and more than half of those are moving values about: thirty-seven per
cent `load`, eleven per cent `const`, ten per cent `store`. Five of the
seventy-nine were the same five at the bottom of the walk — load the count,
push one, add, store it back, jump — and every walk this language has ends with
them, whether it is over a range, an array, a store or a run.

They are one instruction now. It is the shape the language exists for, which is
the argument for spending an opcode on it: a frame walks an array of value
structs in order, and the walk itself should not cost five dispatches an
element.

Nothing is checked in it and nothing is narrowed. The count is the walk's own,
made by the compiler where nothing else can reach it — D-for-the-hidden-counter
is why the name a program writes is a copy — so it is an integer that was made
here and is compared against a length.

The measurement, five runs of each alternating: 175, 190, 177, 213, 215
nanoseconds an entity-step before, and 158, 155, 158, 163, 162 after. The
instrument says it shows a change of about a quarter and not one of a tenth,
and this is at the edge of what it can say; every run after is below every run
before, which is the part that is worth trusting.

*Argued.*

## D092 — a counted walk's turn is one instruction

`next.less.i` and `next.less.u` add one to a counted walk's own count, compare
it with the limit beside it, and go back while it is less. The test that
decides whether there is a first turn at all is written once, above the loop.

D091 made the bottom of a walk one instruction and left the top at four: load
the count, load the limit, compare, jump out if it is not less. Those four ran
every turn because the walk went back to them. Moving the test to the bottom
makes them run once, and the instruction that counts is the instruction that
tests, so a turn of `for i in 0..n` costs one dispatch of control rather than
five.

Only the counted range gets it. A walk over an array asks the array how long it
is every turn, a walk over a store looks for the next live slot, and a walk
over a set of bits counts to a number that is in the program rather than in a
slot; none of those is two slots and a comparison. They keep `next`, which is
D091's, and nothing in the shape of a loop had to be made general to hold both.

Signed and unsigned are two instructions rather than one with a flag, which is
what the language does everywhere else it compares: `lt.i` and `lt.u` are two
opcodes for the same reason. A counted walk over an unsigned range is rare and
is not a reason to make the common one ask a question at runtime.

The count is still the walk's own, which D038 decided and D053 leaned on: the
name a program writes is a copy, so assigning to it moves nothing, and the
warning for doing so is still raised.

Measured either side, five runs each, alternating: 156, 167, 159, 161, 159
nanoseconds an entity-step before and 152, 153, 155, 158, 151 after. Every run
after is below the one before it, and about four per cent is at the edge of
what the instrument claims to resolve. The count of instructions is the harder
evidence: four fewer of the seventy-five a frame step spends on an entity.

*Argued.*

## D093 — a small number stays in the table

An instruction carrying a small whole number instead of an index into the
constant table was written, measured and taken out again.

The reasoning for it was good: nought and one are most of what a program
pushes, and reading one back is three loads that depend on each other — the
frame's chunk, the chunk's constants, and the element. `const.i` with a
sixteen-bit value in the instruction replaced about half the pushes a frame
step makes.

It made no difference. Six runs of each, alternating: 152, 155, 155, 153, 155,
156 nanoseconds an entity-step with the table and 156, 158, 157, 153, 153, 157
without it. There is no direction in that.

The number that explains it: seventy-five instructions and about a hundred and
fifty nanoseconds is around six cycles an instruction, which is what a
dispatch that predicts reasonably well costs on its own. Three dependent loads
out of a table that is hot in the first level of cache disappear behind that.

So the finding is about what to do next rather than about constants. Removing
instructions works — the two before this took four and four out of a turn and
both showed — and making one cheaper does not. Anything that costs an opcode
and buys nothing measurable is not worth the place it takes in a set that is
meant to stay small.

*Argued.*

## D094 — a walk is over what the array held when it began

The length of an array walk is taken once, into a slot beside the count.
Pushing inside the body does not lengthen the walk; removing makes the walk
reach for what is no longer there and say so.

It asked the array how long it was every turn, so a `push` in the body extended
the walk it was inside and a `pop` shortened it. Neither was decided. D053 says
what the walked name is — the element as it was when the turn began — and
nothing said what the walk was over, which left both of these true by accident.

A loop whose length its own body decides is a loop with no bound, and this
language is for programs with a frame to fit in. `for e in queue { push(queue,
...) }` is a program that ran until the heap did, and now it is a program that
walks what was there and leaves the rest for the next walk, which is the thing
the author would have written by hand.

Removing is the harder half. With the length fixed, a body that pops reaches
past the end on a later turn, and that is a message rather than a wrong read:
the existing one, at the `for`, saying the index is outside an array of that
length. Refusing it while compiling was the other way, and it cannot be done
honestly — a function the body calls can pop, and a rule that catches only the
`pop` written in front of you is a half of a net.

It is also five instructions a turn: load the count, load the array, ask its
length, compare, jump out becomes the one instruction D092 already had for a
counted range, because a length in a slot is what that instruction wants.
Being able to use it is a consequence of the decision rather than the reason
for it; the reason is that a walk should be over something that does not move.

*Argued.*

## D095 — every walk that counts, counts the same way

A walk over that many of something and a walk over a set of bits put their
limit in a slot and end in the instruction that counts and tests. Only the
store walk still ends in a plain step, because only it does not count.

Both of them knew their limit when they were compiled: `[T; N]` has the number
written in the type and a set of bits has one for each name it declares. Both
pushed that number onto the stack every turn and compared it. Putting it in a
slot beside the count costs one store before the loop and makes the turn the
one instruction D092 wrote for the counted range.

A walk over a store looks for the next live slot rather than counting to a
limit, because slots go dead and D020 says removing while walking is allowed.
There is nothing to compare it against, so it keeps `next`, which is what D091
wrote and what that instruction is for.

Four of the five shapes are now one shape underneath: a count, a limit beside
it, and one instruction a turn. That is worth more than the instructions it
saves, because a walk is what this language is for and there is now one thing
to get right rather than four.

*Argued.*

## D096 — a store's walk looks the way the others count

`seek.from` and `seek.next` find the next live slot of a store, write it where
the walk keeps its place, and leave or go back. They are to a store's walk what
`next.less.i` is to every other one: a test above the loop and one instruction
a turn.

It was eight instructions and a step. Every turn loaded the store, loaded the
place, searched, stored the answer, loaded it again, pushed nought, compared
and jumped. The search is the part that cannot be removed — slots go dead, so
there is no limit to count to, which D020 decided when it allowed removing
while walking. Everything around the search could go, and did.

`KEST_OP_NEXT` went with it. D091 wrote it for the bottom of every walk, D092
and D095 gave four of the five shapes something that counts and tests, and this
turn gave the fifth something that looks and tests. Nothing was left calling
it, so it is out of the instruction set rather than sitting in it as a thing
that once had a use. Two turns is a short life for an opcode; leaving one that
nothing emits is worse than admitting the shape moved under it.

The disassembler grew two ways of printing a jump target because a forward one
and a backward one are not the same arithmetic, and one printed as the other
gave four billion and change, which is what said the class was wrong.

Every walk in the language now has one shape: something before the loop that
decides whether there is a first turn, and one instruction at the bottom that
does the turn and the deciding together.

*Argued.*

## D097 — text is walked like everything else

`for b in t` gives the bytes of a piece of text, and `for i, b in t` gives the
position with them.

Text was the only sequence the language had that `for` did not walk, so a
program reading bytes wrote `for i in 0..len(t)` and an index while a program
reading anything else wrote the walk. That is two shapes for one idea, and the
one it forced is the slower of the two by a lot: `t[i]` measures the string to
know whether the index is inside it, so walking a piece of text an index at a
time measures it once per byte. Four thousand bytes takes 196 microseconds that
way and 77 walking it, and the gap grows with the length because one of the two
is quadratic and the other is not.

The walk reads with `text.in`, which does not measure. It is allowed not to
because the walk took the length when it began, nothing in the language writes
a byte of text, and the count it reads with is the walk's own. `text.at` is
what a program's own index compiles to and that one measures, because there
nothing knows where the index came from.

It gives bytes, and that is the whole of what it gives. The language says text
is its bytes and has no character type; a walk that pretended otherwise would
be the first place it lied. `"hız"` is four bytes and a walk of it takes four
turns.

*Argued.*

## D098 — `find` starts where it is told

`find(t, needle, from)` looks from a place and answers where it is in the whole
of `t`. The two-argument form is the same thing starting at nought.

Finding every place something appears was the shape that had no answer. `find`
only ever found the first, so a program looking for the second sliced the rest
of the string and looked in that — a piece of the heap per step, which a
function promising `no.alloc` cannot do at all. The thing a program most wants
to do with text was the thing the language made most expensive.

The answer is an index into the whole string rather than into the part looked
at, because a scan then reads `at = found + len(needle)` and the number it
carries means one thing throughout. An index relative to where it started would
have to be added back at every step, and every program would add it back the
same way.

Starting outside the string is a message. Starting exactly at its length is not:
that is where a scan arrives when it has consumed everything, and it finds
nothing, which is the answer.

The wording that made this turn's question worth asking is fixed as well.
`find` was documented as costing nothing, which is true of the heap and not of
the reading: it looks through the string. In this project "costs" has meant
what reaches the heap, and where that is not obvious the sentence now says
which of the two it means.

*Argued.*

## D099 — the rest of a piece of text is a place inside it

`rest(t, at)` gives what is left of `t` from `at` and copies nothing.

Cutting a line into fields copied the whole remainder at every step, because
`slice` is the only thing that could say "from here on" and `slice` makes text.
A program that only wanted to look at the fields had paid for a copy of each
one and of everything after it, and a `no.alloc` function could not do it at
all.

It works because of what text is: its bytes, ending where they end. A piece of
text is a pointer to bytes with nothing after them, so the rest of one is a
pointer further along the same bytes — the same value, the same ending, nothing
allocated. `slice` still copies, because a piece cut out of the middle has to
end where the piece ends and the bytes it came from do not.

What it costs is what it steps over. It walks to `at` rather than measuring the
whole string, so a loop that takes the rest of the rest reads each byte once
between all its turns. That is the shape to write, and the reason is worth
saying plainly: an index into a piece of text costs the index, because nothing
carries the length. `t[i]` in a loop reads the string again for every byte;
`for b in t` and `rest` read it once.

`std.text` was written out of `len`, `find`, `slice` and the bytes, so it had
the same problem twice over: `split` copied the remainder per piece, and
`append` and `number` walked by index. They walk now, and `ends` compares the
rest with the suffix instead of stepping through both. The library is held to
the same rules as a program, which includes this one.

*Argued.*

## D100 — comparing a place in text with a piece of it

`matches(t, at, needle)` says whether `needle` sits at `at` in `t`.

It is the last of the three things a program does to text that could not be
written for what it should cost. `find` says where something is and `rest` says
what is left; asking whether a piece is *here* had to be done by stepping
through both strings an index at a time, and an index into text costs the
index, so a prefix test cost the length of the subject times the length of the
prefix. `std.text.starts` was that loop.

It compares rather than looks. `find(t, needle, at) == at` answers the same
question and reads the whole string to do it when the answer is no, because
finding is for finding. Two things that answer one question at different prices
is the kind of thing that makes a program slow quietly, so the cheap one is
written down and the expensive one keeps its own job.

The place is an argument rather than something `rest` is asked for first. Both
cost the walk to it, so this is not about the price; it is that a parser
already holds a place and should not have to make a value out of it to ask a
question about it.

`starts` and `ends` are one line each now, and `std.text` still holds: nothing
in it is a builtin, and what it is written out of is `len`, `find`, `slice`,
`rest`, `matches` and the bytes.

*Argued.*

## D101 — a token the lexer refused is reported once

A parser error about a token the lexer has already refused is not written.
`error_at` looks at the token it is about, and says nothing when that token is
the one the lexer could not read.

Every unreadable character produced two messages: what is wrong with it, from
the thing that knows, and then "expected an expression, found invalid token"
from the thing that does not. The second is always vaguer than the first and
about the same place, and a reader who fixes what the first says fixes both.
`kest_diags_mute` was written for exactly this and says so where it is
declared: reporting the same thing twice is worse than not reporting it once.

Recovery is unchanged: the parser still marks itself recovering and still skips
to where it can start again. What is dropped is the sentence, not the handling.

The suggestion beside the first message is new as well. A backslash inside a
hole is the mistake everybody makes once, because every other language with
holes needs the escape, and this one does not: a hole holds code, so a string
in it is written the way a string is written anywhere. Outside a hole the same
character gets the other half of the answer, which is that an escape is written
inside text and that place is not inside any.

*Argued.*

## D102 — `kest lex` shows what it lexed

The token stream is printed whether or not something in the file was refused.
The tree is not, and the difference is what each of them is after a mistake.

A token stream is whole. The lexer makes a token for what it could not read and
carries on, so what comes out is every token in the file with one of them
marked. That is exactly what somebody running `lex` on a file that will not
compile wants to see, and it was the one time the command showed nothing.

A tree is not whole. A statement the parser refused is missing from it, and
printing what is left as though it were the file says the file is something it
is not. `parse` still shows nothing when something was refused.

The file is lexed twice, because reading it parses it and the command wants the
tokens. The second one is muted: what is wrong with the file was said by the
first, and `kest_diags_mute` exists for a pass whose purpose is to find out
rather than to report.

This is also the answer to whether `invalid token` was a name for something
nothing could print. It was, for one turn, because the parser had stopped
naming a token the lexer refused (D101) and this command refused to show one.
Now the only thing that prints it is the one command whose job is to show what
the lexer made.

*Argued.*

## D103 — a partial tree is shown and says it is one

`kest parse` prints the tree it made whether or not something was refused, with
a line above it saying how many things were.

D102 said a tree is not printed after a mistake because printing what is left
as though it were the file says the file is something it is not. That is the
objection, and it is about the "as though". A line saying `this is what parsed;
1 thing refused` answers it: what is shown is the parser's answer and is
labelled as the parser's answer, which is what somebody debugging a parse error
is asking for and the only time they ask.

So D102 stands and this is the other half of it. A token stream needs no label
because nothing is missing from it; a tree needs one because something is.

The label goes on the tree rather than beside it. `--json` still gives only the
diagnostics, so nothing reading this by machine sees a tree at all, and the
person reading the text sees the sentence before the first line of it.

*Argued.*

## D104 — `fmt` says what it did not do

A file that does not parse is not formatted, and the command says so beside
what is wrong with it.

It is the one command that shows nothing after a mistake and the one with the
strongest reason: what it prints is meant to go back over the file, and a form
of half a program would delete the other half. `lex` shows the tokens and
`parse` shows the tree, because nothing is going to be written from either.

What was missing was the sentence. `kest fmt -w broken.kest` printed the
diagnostics and left the file alone, which is right, and a person who did not
read them carefully would think it had been formatted and found nothing to
change. Refusing and appearing to do nothing look the same until one of them
says which it was.

It goes on the standard error beside the diagnostics, never on the output,
because the output is a file's contents and a sentence in it would end up in
the file. In JSON there is no sentence: what is reading it asked for the
diagnostics and can see there are some.

`fmt` renders its diagnostics as JSON when asked now, which every other command
already did. It was the one place `--json` meant "everything this command says"
and did not.

*Argued.*

## D105 — `--check` names a file it could not read

`kest fmt --check` names a file that does not parse, beside the files that
parse and are not in the one form.

The question `--check` asks is whether every file is already in the form this
prints. For a file that is not a program the answer is not yes, and it was
answered by printing nothing: a caller looping over the names saw a pass where
there had been a refusal. The exit status said one, but it says one for the
other reason too, so it cannot tell them apart either.

The name means what it has always meant — this file is not in the one form —
and which of the two reasons it is, is on the standard error where the
diagnostics and the sentence D104 added already are. A list of names stays a
list of names.

*Argued.*

## D106 — what a command says as JSON is JSON

`kest check --json` wrote plain words inside a JSON array for any program with
an enum, and had done since enums were laid out. `kest fmt --json` printed a
file's contents where an object was asked for. Both are fixed, and
`check-commands.sh` now parses what every command says with a JSON parser
rather than looking at the first character.

The first is the worse one. `--json` exists so that a tool, or a model
repairing what it wrote, does not have to read carets, and it had been handing
those readers something that stops parsing in the middle. Nothing noticed
because the check for it was "does this start with a brace", which an object
that goes wrong later does.

`fmt` now says one object a file: what was wrong with it, and whether it is
already in the one form. It does not print the formatted text in JSON, because
a stream that is a JSON object and a file's contents at once is neither. The
text is what `fmt` is for and it is still what `fmt` prints; `--json` is for
what it *says*.

The two JSON string writers became one. Three files compose JSON now and the
string is the part that has to be right.

The net is the part that matters. A tool that says a thing has a shape has to
try the shape: `check-commands.sh` parses every command's JSON for every file
it is given, and it was proved by putting the same fault back in a copy of the
tree and watching it caught.

*Argued.*

## D107 — `emit` says its answer as JSON too

`kest emit --json` puts the instructions in the object: what is laid out, what
the host must provide, and every function with its code.

It printed the diagnostics and nothing else, which meant asking for JSON threw
the command's whole answer away. That is the same fault `fmt` had one turn ago
and the opposite of the one `check` had: `check` said its answer and said it
wrongly, `emit` said nothing at all.

Where the line falls is what `fmt` settled. What a command *prints* can be a
product — a file's contents, meant to go back over the file — and that stays
text. What a command *says* is a report about a program, and a disassembly is
a report: nothing writes it anywhere, a person reads it or a tool does, and the
tool should not have to read columns.

The instructions carry their offset, their name and the numbers after them, and
nothing else. The text form decorates: it prints the value behind a constant
and works out where a jump lands. A reader that wanted those has the numbers
and the same tables. What it must not have is a second walk of the code that
could step differently from the first, so how many numbers follow an
instruction is `(width - 1) / 2` and the width is the one answer there has ever
been.

*Argued.*

## D108 — `call` says what came back, where it says everything else

`kest call --json` puts what the function gave back in the object. It used to
print it on the standard error, so the one command whose answer is a value was
the one that said its answer where nothing was reading.

The reason it was there is real: the standard output has to be the JSON and
nothing else, and the value had to go somewhere. Beside it was the wrong
somewhere. In the object is where everything else this command says already is.

It is written the way the language writes it, in a string, and it is written
once: the same function makes the characters a person reads and the characters
the string holds, so the two cannot come apart. A number in a string rather
than a JSON number is the price of that, and the type is in
`kest check --json` for a reader that wants to know which it is.

A function that gives nothing back has no `result` in the object, and neither
has a call that could not be made — the exit status says one and the reason a
person needs is on the standard error, which is where D104 put the same kind of
sentence.

*Argued.*

## D109 — `tick` says its numbers, and only one thing writes them

`kest tick --json` puts the crossings, what they gave back, the peak between
calls and what the heap holds in the object. It used to print those lines to
the standard output and then the object after them, so what came out was not
JSON at all.

The check that would have caught it did not run on this command. `--json` was
parsed for six commands and `tick` was not one of them, because `tick` takes a
count and the loop that sweeps did not. It does now, and this was the last
command outside it.

`drive_events` fills a `Ticked` rather than printing. The lines a person reads
and the numbers the object holds come from the same place, which is the same
answer D108 gave for `call`: two writers of one answer come apart, and the way
they come apart is that one of them is not updated.

Driving a program that takes no events said nothing and printed a heap of
nought, which looks the same as driving one that took them and did nothing. It
names the two shapes it looked for now, on the standard error where the other
sentences of this kind are.

*Argued.*

## D110 — one mistake, one sentence, and the nearest name

Driving a program with events says one thing about one mistake, and when it
finds neither entry point it says what the nearest name in the program is.

The sentence D109 added — nothing here takes events — was printed beside the
one that had already said what was wrong. A program with an `onEvent` that
takes the wrong thing was told what it takes and that it does not exist, in
that order, and the second is false. It is said now only when neither name is
there at all.

When neither is there, the likeliest reason is a misspelling, and every other
part of this language answers an unknown name with the nearest one it has.
Asking `kest_nearest_global` costs nothing at a point where the program is
already about to do nothing, and `e.onEvnt` is a better answer than a list of
what could have been written.

Nothing is added to the JSON for it. What a machine needs is there already: no
`onEvent` and no `onEvents` in the object is the same fact, and a sentence
about a spelling is for a person.

*Argued.*

## D111 — one place qualifies a name

`kest_build_name` is the only thing that puts a module in front of a name, and
it reads the field `kest_entry` reads when it takes one off.

The command line had a second one. `entry_name` built the same string from the
root unit's alias, for the two names `tick` drives with, while `main` and `call`
went through `kest_build_name`. Two functions doing one thing agreed because
they were written from each other, which is the arrangement that lasts until
somebody changes one.

They read different fields, too: the unit's alias and the module's. They are
set from each other, so they were the same string, and the way that stops being
true is a compiler that starts emitting under something else. Now one function
reads the module's alias, and the machine's own lookup reads the same field.

`drive_events` takes the build rather than three things out of it, which is
what let the second function go: the program, the arena and the root unit were
being carried separately to somewhere that had all three in one pointer
already.

*Argued.*

## D112 — a value is written one way, wherever it is written

`kest call` prints what came back through the same function that fills a hole
in a string, and refuses the same types the compiler refuses there.

It had its own writer. It knew integers, floats, truths, text and optionals,
and answered `<[parse.Field]>` for everything else — a shape where a value was
asked for. The machine's writer knew integers, floats, truths, text, sets of
bits and enums. Two writers, each missing what the other had, and a program
printing a value and the command line printing the same value could disagree
about it.

One writer now. `call` gains enums and sets of bits, which it never had, and
the rule about what can be written at all is `kest_type_has_text`, moved out of
the checker so that what the compiler refuses in a hole and what the command
line refuses to print are the same sentence about the same rule.

Optionals gained text on the way through. They were refused in a hole, and the
reason given for refusing a struct — that it has several spellings and the
author knows which one they meant — is not true of one: an optional is `none`
or what it holds, and both are what a program writes. The rule was already the
right rule; it was only missing a case.

Text on its own stays the content rather than the source that spells it, which
is D035's exception, and the command line makes it in the same place for the
same reason: `"one \\"two\\""` inside a value and `one "two"` on its own.

A call that cannot say what came back exits non-zero. The command is to call
and say what came back, and it did half of that.

*Argued.*

## D113 — the two lists that decide what has text are held together by the compiler

`kest_type_has_text` and the writer in the machine both list every tag the
language has, without a `default`, so a tag added to either without a decision
about the other does not build.

They are one rule in two places and they have to be: one answers whether a
value can be written and the other writes it. D112 made the first the only
gate, which made the writer's last branch unreachable — the `?` it returned for
a type it did not know. Unreachable by agreement is not the same as unreachable
by construction, and the agreement was two switch statements written from each
other.

Now neither has a `default`. `-Wswitch` is part of `-Wall` and `-Werror` is
part of the build, so adding a tag to `KestTypeTag` stops the build in both
files until somebody says what its text is and whether it has any. Proved by
adding one in a copy of the tree and reading the two errors.

The branch that cannot be reached says `<no text>` rather than `?`, because a
fault should read like a fault and not like data. It is still there because C
wants a value at the end of a function.

*Argued.*

## D114 — a constant is worked out where it is written

`const CELLS: i32 = WIDTH * 9` is a constant. Anything a number, a truth or a
piece of text can be made of by arithmetic is, including other constants.

Only a literal was, and the compiler said so with `only a literal constant is
compiled yet` — a K05xx, which is the range for what it cannot emit rather than
for what the language refuses. It was a gap and it was marked as one.

It is worked out at compile time rather than emitted at each use. A constant is
a name for a value: making the value in three places would be three chances to
make it differently, and one instruction to push it is what a name for a value
should cost.

It wraps at its declared width, because the arithmetic that made it is the
arithmetic the language has: `const NARROW: i8 = 120 + 10` is -126 in a
constant for the same reason `x + 10` is -126 in a function. A constant that
disagreed with the language about what its own arithmetic means would be worse
than no constant.

Two things it cannot be are named rather than lumped together: dividing by
nought, and a constant made out of itself. The second is caught by depth rather
than by a set of what is being worked out, because thirty-two is deeper than
any real constant and the message is the same either way.

What this does not do yet is let `[T; N]` name a constant for its count. D064
decided that on purpose and the reason it gave — a size that could change — is
worth revisiting now that a count could be a constant somebody reads, but it is
a decision to revisit rather than a gap to fill.

*Argued.*

## D115 — a count may be the name of a constant, superseding D064

`[T; N]` takes a number or the name of a constant that is one. D064 asked for a
literal and gave the reason: a count that could be a constant somebody changes
is a size that could change.

Two things have happened since. A constant is worked out where it is written
(D114), so it cannot change while a program runs and is not a name for a place
somebody assigns to. And what a host has to match is readable from outside:
`kest check` prints `[f32; 4]` and the byte size, `kest_build_layout` hands the
same numbers to a host before it lends anything (D080). The layout is pinned
and inspectable whichever spelling made it.

What is left of D064's reason is that a literal is visible at the declaration.
That is true, and the answer to it is that a program with the same number in
five places has five things to change and no name saying they are one thing.
`[Npc; MAX_NPCS]` is the shape a host's own header has.

Constants are declared before struct fields are resolved now, because a field
may be that many of something. Only their names and their declared types are
read there; what one is worth is worked out when something asks.

A count that is not a constant, or is a constant of the wrong kind, is refused
where it is written and says which: `a count is a number or a constant that is
one`.

*Argued.*

## D116 — a constant may be a struct

`const ORIGIN: Vec2 = Vec2(0.0, 0.0)` is a constant, and so is one built out of
other constants: `const FIRST: Box = Box(CORNER, 7)`.

A struct is a value laid out flat, which is D006, and that is the whole of why
this works: a constant that is one fills a slot per scalar in it, and using it
pushes that many. Nothing new is on the heap and nothing is copied at runtime
that was not copied before — a constant struct is the same instructions as
writing the fields out where it is used, with a name in front of them.

The folding is one function still. It fills slots now rather than one value,
and the scalar arithmetic underneath is untouched: every field of a struct
constant is folded the way a constant of that field's type would be.

What the bits of each slot mean is worked out by walking the type in the
compiler rather than carried out of the fold. The classes are the disassembler's
business and the type layer does not know about them; a walk of the members in
slot order is the same walk that laid the struct out.

Every field or none. A struct built where it is written with a field missing is
already refused by the checker, and a fold that filled some of the slots would
be a value that is partly there.

A piece of text with a hole in it says so rather than sharing the message with
everything else that cannot be worked out. Filling a hole is what the machine
does, and a constant is worked out before there is one.

*Argued.*

## D117 — a constant may be that many of something

`const WEIGHTS: [f32; 3] = [1.0, 0.5, 0.25]` is a constant, read where it is
used the way any other value of that type is.

A struct constant works because a struct is a value laid out flat (D116), and
that many of something is the same thing with the same layout (D064). The fold
fills a slot per element and the compiler pushes them; nothing new was needed
except saying so in the two walks that lay a value out.

Indexing one needed something. A run is indexed where it *is*: in a slot run,
or at an address the host lent. A constant is neither — it is a value where it
stands, the same as what a call gives back — so it goes into slots of its own
first and is indexed there. That is what a walk of one already did, and it is
now what an index does, which means `M[i]` with an index worked out while
running reads a constant table.

The lookup for what a constant is written as is one function, and it tries the
symbol table before the file. Constants are declared after struct fields are
resolved, because a struct's fields are what a constant of that struct is
measured from; but a field may be that many of something and that many may be a
constant, so a count reads the file it is in. A count names one name and a name
from another file has a dot in it, so the file is the whole of where to look.

*Argued.*

## D118 — a constant is read, not rebuilt

A constant that is a struct or that many of something is one instruction and
one copy: `const.run` names where the run starts in the chunk's constants and
how many. A field or an element of one is worked out where it is written and
costs a single push.

It was a push a slot. A table of sixty-four numbers was sixty-four instructions
every time it was read, which is the wrong shape for the thing a table is for.
The values were already in the chunk beside the code; what was missing was
reading them as a run.

Runs are stored once. A constant is compared as a whole run rather than a value
at a time, so a table read in ten places is in the chunk once, and the entries
of a run double as the scalars they are: a program that also writes `1`
somewhere shares the first element of `[1, 2, 3, 4]`.

`T[0]` and `p.at.x` are not read at all. An element of a constant run and a
field of a constant struct are constants, so the fold answers them and one push
follows. What was there before copied the whole table into slots and read one
of them back; that path is still what an index worked out while running needs,
and now it is only what that needs.

The fold is asked at two more places in the compiler — a field and an index —
and it says no quickly for everything that is not a constant, because the first
thing it looks at is whether the name is one.

*Argued.*

## D119 — one of a constant run is read where the run is

`const.at` reads one element of a constant run at an index worked out while
running, from the chunk where the run already is. Nothing is copied into slots
to read one of it.

A run is indexed where it is, which was a slot run or memory a host lent. A
constant was neither, so D117 copied it into slots first — correct, and the
wrong price for a lookup table: `fn look(i: i32) -> i32 { return T[i] }` copied
eight slots to read one of them, and needed nine slots to do it. It needs one
now, and the whole function is a load, a read and a return.

It is the same instruction `load.slots` is, one table over: an index, a stride
and how many, with the same refusal in the same words when the index is outside
the run. The constants are in the chunk beside the code and are not written to,
so reading one of them needs nothing kept anywhere.

Copying into slots is still what something that is a value where it stands and
is not a constant needs — what a call gave back, indexed straight away. That
path is unchanged and is now only for that.

*Argued.*

## D120 — that many of something is sized from what it holds, after it is measured

A struct holding `[Inner; 2]` was one slot and one byte, with its next field on
top of the first. `[f32; 4]` was right, which is why nothing had noticed: a
primitive is measured before anything is resolved and a struct is measured
after.

That many of something is composed while a struct's fields are resolved, and it
caches how many slots and bytes it is from what it holds. What it holds is
measured in the pass after that. So every run of a declared type in a struct
was sized from noughts, and `measure_held` — the thing that measures what a
member holds — walked into structs and enums and stepped straight over runs.

It sizes them now, where what they hold has just been measured. A run of a run
works by the same recursion.

The other half is a value that does not fit. Sizes are sixteen bits because
that is what a layout says, and `[i32; 20000]` is eighty thousand bytes: it
wrapped to fourteen thousand and laid the struct out wrong. It is refused now,
at the count where the count is known and at the struct where it is not, with
one sentence and two places to say it.

`examples/rows.kest` is the shape this is for: a struct with a run of structs
inside it, walked, indexed, written into where it stands, and read out of what
a call gave back. Nothing in `examples` had one, which is why a headline
feature was broken in a way `make check` could not see.

*Argued.*

## D121 — a type written where a value is wanted says so

`Box` where a value is expected is `` `Box` is a type, and this wants a value ``
rather than `unknown name`, and a generic one says where its types go.

The name that made this worth writing is `Box<i32>(7)`. Inside an expression
that is three comparisons — `Box < i32 > (7)` — and it was reported as an
unknown name followed by a type error about `>`, which is two messages about
neither of the two things that were wrong. Kest has no explicit type arguments
at a call, by design: a copy is chosen by what it is built with, and `<` in an
expression is a comparison. So the answer is to say so where the mistake is.

Nothing else changes about generics. `let b: Box<i32> = Box(7)` is how it is
written, which the message now says.

A comparison of something already broken is broken too, rather than a truth.
That is the other half of the same turn: `if missing < 3` reported the unknown
name and then, because the comparison answered `bool` regardless, nothing else
— but `Box < i32 > (7)` reported the second comparison as well. One bad name is
one message, and an error type that keeps its poison through an operator is how
that stays true.

*Argued.*

## D122 — a line may end in `>`

A newline after `>` ends a statement. A struct field whose type takes a type —
`giver: ref<Npc>`, `jobs: store<Job>` — is a whole field, and the field after it
is a field rather than a continuation of it.

It was not, and the shape it broke is an ordinary one: any struct holding a
reference or a store with anything written after it. `examples/quests.kest` had
`escort: ref<Npc>?` and `giver: ref<Npc>`, and both worked — the first because
`?` already ended a line and the second because it was the last field. One more
field under it and the file did not parse, with a message about the field
below.

`ends_statement` is the list of tokens a line may end after, and this is the
second time something has been missing from it. It is the lexer's list, so it
cannot know that this `>` closed a type: what it can know is which of the two
readings is worth having. A field ending its line is written every day; a
comparison split after its operator is written by nobody, and is refused where
it is written rather than read as two statements.

The example holds the shape now. A field after a `ref<T>` field is one line in
`quests.kest` and it is checked by what the example already returns.

*Argued.*

## D123 — the list of what a line may end after is held by the compiler

`ends_statement` has no `default`. Every token kind the language has appears in
it, so adding one stops the build until somebody says whether a line may end
after it.

Twice a kind has been added without anybody thinking about this list, and both
times the symptom was the same and not obviously about the lexer: a line
swallowed the one under it and the message was about the line below. `byte` did
it once and `>` did it again a turn ago.

Nothing can check that a decision is *right* — what a statement can end with is
the grammar's business, and the lexer does not have the grammar. What can be
checked is that a decision was made, which is what `-Wswitch` inside `-Wall`
inside `-Werror` does once the `default` is gone. That is the same medicine
D113 used for the two lists about what has text, and this is the third list
in the tree that has to be complete.

Proved by adding a token kind in a copy of the tree and reading the error.

*Argued.*

## D124 — a value nobody takes is refused

A statement that is only an expression has to do something. A call does; an
`if` or a `match` whose arms are blocks does. Anything else is refused with
`K0345`.

`a == b` on a line of its own was accepted and did nothing, and it is what
somebody writes when they meant `a = b`. So was `2 + 3`, and so was a `match`
whose arms give values — which is the shape somebody writes when they expect
the last thing in a body to be what the function gives back.

A call is the exception and not an exception to anything: a call is written to
make something happen, and what it gives back is often not the point. `push`,
`remove`, `set` and every host function are called for what they do.

The other half is the message for the shape this came from. A function whose
body ends in a `match` that gives values is told that it can end without
returning, and now told what to do about it: the arms give a value, so it is
one, so write `return` in front of it.

*Argued.*

## D125 — fusing the commonest instruction made the machine slower

An instruction that read two slots at once was written, measured and taken out
again. It removed two dispatches an entity-step and cost twenty nanoseconds.

The reasoning was D091's and D092's: what pays is removing instructions, and
`load` is thirty-seven per cent of what a frame step runs. An operator between
two names reads two slots one after the other, and the two are emitted in one
place with nothing able to land between them, so they could be one.

They were, and the emitted code is what it should be: `load 8` and `load 1`
became `load2 8 1`, twice in the hot loop. Five runs of each, alternating: 140,
142, 142, 140, 141 nanoseconds an entity-step before and 161, 163, 161, 161,
160 after.

The case being in the switch is not what costs. With the instruction defined
and the compiler not emitting it, the number is what it was — so it is running
the thing that costs, not having it.

What is left is the dispatch. `load` is the commonest instruction there is, so
the indirect branch that jumps to it is the best predicted one in the machine;
`load2` runs twice an entity among seventy-five, so it is the worst. Two
mispredictions cost more than the two dispatches saved. That is the whole
finding, and it refines what D091 and D092 showed rather than contradicting it:
fusing pays when it takes four or nine instructions out of a turn, and does not
when it takes one out of a pair of the commonest.

The instruction set stays small for a reason that is now measured rather than
assumed.

*Argued.*

## D126 — the program says where each argument starts

`kest_frame_takes` says how many arguments a function takes and `kest_frame_at`
says where the one at a position begins, in slots.

A frame is one slot a scalar, so a host filling one for `between(a: Point, b:
Point)` had to know that a `Point` is three scalars and write the second one at
slot three. It could work that out — the layout of a type is readable and its
count is its slots — but it would be adding up fields to arrive at a number the
program already has, which is what `kest_frame_slots` exists not to make hosts
do for the total.

What it costs is a small array a function: how wide each argument is, in the
order they are written, filled where the parameters are declared. Both places
that compile a function fill it, the plain one and the copy a generic makes.

`kest_frame_at` answers the whole width when it is asked past the last
argument, because that is where a result written over the arguments begins and
a host asking for it means that.

*Argued.*

## D127 — an argument says what it is, in the layout everything else uses

`kest_frame_layout` gives the layout of the argument at a position: the same
`KestLayout` a type has by name, so a host checks what it is passing the way it
checks what it lends.

A host could ask how many arguments there are and where each starts, and had
nothing to check them against. Writing the right number of slots with the wrong
things in them is the mistake that leaves: a signature that changed under a
host still takes four slots, and every one of them is read as something else.

It costs nothing that was not already there. A function kept how wide each
argument is; it keeps which layout each one has instead, and the width is the
layout's own — one piece a slot. Every type a signature mentions already has a
layout, which D068 arranged for the lending side.

The check a host writes is the one it already had. `examples/embed.c` compares
the argument's pieces with its own `offsetof`, the same function it uses before
it lends a `Point`, and a copy of the tree where `between` takes something else
is refused before the call rather than read wrongly inside it.

*Argued.*

## D128 — and what comes back says what it is

`kest_frame_gives` gives the layout of what a function gives back, or nothing
when it gives nothing.

D127 made what goes in askable and left what comes out a width. A host reading
`frame[0].real` after a call is deciding, on its own, that the program wrote a
float there — and it is a slot either way, so a function that started giving
back an integer would be read as a float made of its bits.

It is the same layout again, which is the point: the host has one way of
saying "this is the type I know", and it now uses it for what it lends, what it
passes, and what it reads back. `examples/embed.c` asks for one piece and an
`f32` before it reads a `double` out of a slot, and a copy of the tree where
`between` gives an `i32` is refused before the call.

Nothing is given for a function that gives nothing, rather than a layout of one
slot that means nothing. A shape for something that is not there is a thing a
host would write a check against and pass.

*Argued.*

## D129 — a host walks the functions of a name

`kest_entry_of(runtime, name, at)` gives the one at a position of the functions
of that name, or -1 past the last. A name that is one function is that function
at nought and nothing after it.

`kest_entry` names the candidates when a name is several functions, and the
names are the program's own: `add#i32,i32` carries what it takes because that
is how two functions of one name are told apart. Reading that out of a message
and writing it back in is a host knowing how the compiler spells things, which
is the one thing the boundary has been keeping from it — `kest_entry` was
written so a host would not have to know that a file's module qualifies its
names, and this is the same rule one step further.

The walk composes with what the last three turns added rather than adding
anything of its own: a host asks each candidate what it takes, with
`kest_frame_layout`, and calls the one it meant. `examples/embed.c` picks the
`lengthOf` that takes a `Point` out of two that share the name, using the same
piece comparison it uses before it lends one.

Asking for the second one is also the question "is this name several
functions", which a host wants to ask before asking for an index that is not
there — and asking `kest_entry` that raises a message about it, which a host
that is about to resolve the ambiguity itself does not want.

*Argued.*

## D130 — `std.random` is a value a program holds

`std.random` gives numbers that look random out of a `Source` the program
carries: `source = random.next(source)`, and what comes back is both the next
state and the next number.

Every simulation writes one of these, which is the whole argument for a
standard library: it is written in the language, out of what the language has,
and holding it once is better than every program holding it again.

Nothing about it is global and nothing asks the host. A generator behind a name
nobody passes cannot say where its numbers came from, and a simulation worth
running twice has to be able to say exactly that: two sources from one seed
give the same numbers, which `examples/chance.kest` checks by laying out fifty
spots twice and comparing them.

The state is the number. `next` gives a `Source` rather than a number because
the two are one thing, and a program that wanted them separately would be
holding two things that have to be kept in step.

It is xorshift64: small, fast, and good enough to place things with. It is not
for anything that has to be unguessable, and the module says so where somebody
would look for it.

The checks on it are loose on purpose. A quarter of a thousand is between two
hundred and three hundred, not two hundred and sixty-six: a check that is tight
is a check that fails on a machine that rounds differently, and what is being
checked is that the thing is a chance rather than that it is this chance.

*Argued.*

## D131 — a remainder is even enough, and a shuffle is worth writing once

`random.below` says what it is: a remainder, so a count that does not divide
the whole range leaves the first few numbers likelier by about `count` in
eighteen billion billion. Nothing counting them will see that.

Throwing away the numbers that cause it is the other way, and it is the wrong
way here. It makes the function a loop that might go round again, and this is a
language where a frame has a budget: a cost that is usually one thing and
occasionally another is the kind of cost `no.alloc` exists to keep out of a
frame. The bias is smaller than the difference between two machines' clocks.

What was actually missing from the module is what a simulation asks for after a
number: `shuffle` rearranges an array in place, allocating nothing, and gives
back the source it left off at, because it used as many numbers as the array is
long. `one` picks an element and gives nothing when there are none, which is
the same answer `pop` gives for the same reason.

Both are generic, the way `std.sort` is: they are about the arrangement and not
about what is arranged.

*Argued.*

## D132 — what can be built out of what is declared, is built

`std.math` grew `sin` and `cos` for `f32` and `round` for both widths. It did
not grow an `extern`.

A frame works in `f32` — a position, an angle, a velocity — and `sqrt`, `floor`
and `ceil` already had the `f32` shape that widens, asks and comes back.
`sin` and `cos` did not, so `math.sin(angle)` on an angle was a type error and
the program widened it by hand. That is the module doing half a job in a way
nobody would choose on purpose.

`round` is `floor(value + 0.5)`, which is what a program writes when it wants
one, and it says which way a half goes rather than leaving it to be found out:
`round(2.5)` is 3 and `round(-2.5)` is -2.

What it does not do is declare `Math.round` or `Math.tan`. Every `extern` in
this module is a function every host of every program that imports it has to
provide, so one more is every host changed. `std.math` names what cannot be
written in the language and writes the rest, and that line is worth keeping
where it is.

*Argued.*

## D133 — a mark counts as spoken for

`std.table` refills itself when more than half of it is spoken for, and a mark
is spoken for. Twice the room is for the pairs; the same room again is for the
marks, and which is asked for is which of them is crowding it.

A slot that is emptied is left marked rather than empty, because a probe that
was going further has to carry on past it. That is right and it is not the
whole of it: nothing counted the marks, and the table only grew when the pairs
did, so a table that things are put into and taken out of again fills with
marks while holding almost nothing. Every lookup for a key that is not there
then walks the whole table, and nothing about the answers changes — which is
why this is the kind of thing that is found by looking rather than by a
failure.

Two hundred rounds of putting one in and taking it out again left a hundred
and thirteen marks in two hundred and fifty-six slots, on top of sixty-four
pairs. It is thirty-six now, and five thousand rounds leave forty-seven: the
refill keeps it where it belongs whatever is done to it.

How many marks there are is an array of one rather than a number, for the same
reason everything else in that struct is behind a handle: a struct is a value,
so a number written in a function would be written on that function's copy.

`examples/inventory.kest` does a thousand rounds and checks that everything
that was there is still there. What it cannot check is the reading being fast,
because a slow table gives the same answers.

*Argued.*

## D134 — a removal probes for the two slots it changes

`std.table` finds the two slots a removal changes by probing for the keys kept
in them, rather than walking every slot looking for two numbers.

Taking a pair out changes two things: the slot that held it becomes a mark, and
the slot that held the last pair now holds where that pair moved to. Both are
found by hashing a key, which is what the table is for. Walking to find them
was the whole table for one pair, and a table is walked when it is refilled and
not when something is taken out of it.

Removing two thousand pairs from a table of four thousand took eight hundred
and ten milliseconds and takes one and a half. That is not a tuning: it is the
difference between a removal that costs what the table holds and one that costs
what a lookup costs.

The probe is one function now. `find` wants the pair and `remove` wants the
slot, and a second probe written for the second of those would be a second
place to get the walk past a mark wrong.

The order matters and is the whole of the care in it: the moving pair's slot is
found while it is still where it was, before the slot being emptied becomes a
mark, because a mark is exactly what a probe walks past.

*Argued.*

## D135 — a table is not made smaller

`std.table` never shrinks. A table that held a thousand pairs and holds ten
keeps the room for a thousand, and the module says so where somebody would
look.

The reason is this language's memory rather than this table's design. Nothing
frees anything until the heap is thrown away (D012), so making the slots
smaller would make a new array and leave the old one exactly where it was:
shrinking costs memory rather than giving it back. A host that wants the room
back has `kest_heap_reset` between calls, which is the whole of what giving
memory back means here.

What is worth saying beside it is how the pairs are read, because the answer is
what the packing is for: `keys` and `values` are in step and there are
`count(t)` of both, so a table is walked over them. The order is what putting
things in and taking them out left — an order, and not one to lean on.

*Argued.*

## D136 — writing a field of what you were handed says so

A function that gives nothing back and writes a field of a struct parameter is
warned about, `K0346`. The write is on this frame's copy and there is no way to
hand it over.

`std.table` and `std.random` were said to disagree about what a struct is for.
They do not: they are the two answers to one rule. A struct is a value, so a
thing with state either gives the changed one back — `source = random.next(
source)` — or holds what changes behind a handle, which is what a table's three
arrays are. Which to pick is whether the thing has an identity or is a number a
program carries, and the reference says both now.

The warning is for the shape that is neither. `bump(c)` on a `Counter` compiles,
runs, and does nothing, which is what somebody writes who is used to a language
where a struct is a reference. It is a warning rather than a refusal because a
parameter is also a place to work: a function that gives something back writes
its parameter and returns it, which `examples/physics.kest` does every frame,
and that is why the warning is only for the ones that give nothing back.

Writing through a handle reached from a parameter — `t.values[at] = value` — is
not this and does not warn: a handle is shared, which is the whole of why the
other answer works.

*Argued.*

## D137: a status that does not fit is said, not cut

`kest run` handed back `exit_code & 0xff`, because that is what a process can
carry. A `main` answering 256 therefore exited 0, which is the one answer that
means nothing went wrong: the program failed and the shell was told it passed.

The number is now checked before it is handed over. Outside 0 to 255 it is
`K0618` and the status is 1, so a program that cannot say what it wants to say
says something wrong instead of something false.

This also settles what `run --json` holds, which is nothing beyond the
diagnostics: the status is the whole answer now that it cannot be a truncation,
so there is nothing for the object to add that the caller does not already
have.

## D138: `main` has a shape and it is checked where it is written

Nothing checked what `main` was. `fn main() -> bool` compiled, and its `true`
became a status of 1, which is failure; `fn main() -> text` answered with the
pointer; `fn main(x: i32)` was called with a slot nobody had written.

`main` is the one function nothing in the file calls, so the file cannot say
what shape it has to be: it takes nothing and gives `i32` or nothing, and that
is `K0347`, `K0348` and `K0349` in the checker. In the checker rather than in
the runner because it is a declaration, and a program that compiles and then
cannot be run has been told it was fine.

Only the root unit is held to it. `main` in an imported file is a name like any
other and nothing will call it, so nothing is wrong with it.

Together with D137 this is the whole of the boundary: the shape is checked
before the program runs, and the one number it can answer with is checked as it
leaves.

## D139: what an event handler gives is checked, and driving nothing fails

`tick` checked what `onEvent` took and not what it gave, so a handler giving
`text` had its pointer added into the total and printed as a measurement. What
it gives is now held to a whole number or nothing, which is the other direction
of the rule that was already there.

Giving nothing is a handler somebody would write, so it is allowed rather than
required to answer: `gave` is null in JSON and the number is left out of the
line, because nothing and nought are two answers and printing them the same way
is the thing this decision is about.

A `tick` that drove nothing now exits 1 — nothing here takes events, or what
does could not be called. It exited 0 before, which is what a run that happened
answers. This is D137's rule at the other entry point: a command that did not
do what it was asked does not report the status of one that did.

## D140: what tick says about a handler is a diagnostic

`tick`'s complaints about a handler went to standard error as `kest:` lines
with no code and no span. So `tick --json` on a program with a handler it could
not call printed `{"diagnostics":[],"errors":0}` and exited 1: the one form
said nothing was wrong and the status said something was.

They are about a declaration in a file, which is what a diagnostic is for.
`K0619` is what a handler takes, `K0620` is what it gives, and `K0621` is a
file with neither handler — that one has no span, because what is wrong is that
there is nothing there to point at, the same as `K0603` for a missing `main`.

The name in the message is the one the file wrote. This host looks a handler up
by its qualified name because that is how a name is registered, and a message
about a line says what is on the line.

## D141: tick asks once, and its status is what it said

`tick` asked twice whether a handler was there: `kest_entry` for the compiled
name and `kest_lookup_global` for the declared one. Where the two disagreed
nothing was driven and nothing was said, and the flag added in D139 turned that
into an exit status without a reason attached to it.

It asks the declaration now, which is the one that can say what is wrong with
what it finds, and only then asks for something to call. The disagreement had a
shape: a generic handler is declared and has no copy, because nothing inside
the file calls one. That is `K0622`.

So the flag is gone and the status is what was said: 1 when there is a
diagnostic and 0 when the run happened. Two things deciding one answer is how
they come apart, and this is the smaller of the two.

## D142: a name for a function is that function, and cannot be assigned to

D039 put the promise in the type so a cost could be proved through a value
whose body is unknown. A name bound to a function is the other case and was
not covered: `let f = quiet` gives the local the type of `quiet`, symbol and
all, and the proof follows that symbol into that body.

Assigning to it broke the proof. `let f = quiet` inside a `no.alloc` body,
then `f = grows` under an `if`, then `f(n)`: the promise was proved through
`quiet` and the program allocated in `grows`, with nothing said at compile time
or at run time.

So a name bound to a function is that function, and `K0350` refuses an
assignment to it. A variable that holds any function of a shape is a different
thing and is written as one, with the shape on the `let` — where the symbol is
not part of the type, the proof reads the promise off the shape, which is what
D039 built. The written type is what makes it a variable rather than a name for
one function, which is the same rule constants already follow.

Dropping the symbol on assignment instead would have been unsound: a body is
walked once and a loop assigns after it reads.

## D143: a call nobody promises about is not a call that allocates

The prover marked a call through a value with no promise as an allocation,
because that is what it costs the proof: unknown and heap-reaching are the same
thing to a fixed point that has to be conservative.

They are not the same thing to a reader. `K0401` pointed at `f(n)` and said
"this allocates", which is a claim about a body nobody has seen, and the fix it
implies — find the allocation and remove it — is not the fix. The fix is to
write the promise into the shape, and now `K0402` says so and prints the shape
with `no.alloc` on the end of it.

The reason travels with the trace, so a promise broken three calls down still
names what is actually wrong at the end of the path rather than at the top of
it.

## D144: the machine checks the promise at the call the proof cannot follow

D058 proves `no.alloc` twice, and the second proof walks the emitted code
following `call`. It stops at `call.value`: which chunk that enters is a number
on the stack, and the number is not there until it runs.

So the machine checks it. A compiled function carries what it promised, and at
`call.value` both are in hand — the frame's chunk and the one it is about to
enter — so a body that does not promise, entered from one that does, is
`K0623`. It is a fault in the compiler and says so, the same as `K0405`: the
checker refuses a function that does not promise where a shape that does is
wanted, so reaching this means that refusal did not happen.

The cost is one branch on the one instruction that needs it, which is nothing
next to the call it is part of, and it is not on the path of a call to a named
function.

`check-backstops.sh` holds it: with the variance in `kest_type_equal` turned
off, an allocating function is handed to a promising shape, and the machine has
to catch what the checker stopped catching.

## D145: a type written at a call is recognised, and still refused

Types go where a value is going and never at a call, because inside an
expression `<` is a comparison and a language that made it two things there
would be guessing. That is D040 and it stands.

What did not stand is what happened to somebody who wrote `store<Node>()`
anyway, which is what a person who has met another language writes: the parser
read two comparisons, ran out of expression at the `)`, and said "expected an
expression, found `)`" — a place two tokens past the mistake and a reason that
is not the reason.

The shape is recognised now and refused as itself, with `K0211`. It is not
parsed: a name and a `<` written against each other, then nothing but what a
type is made of, then a `>` with a `(` after it. A comparison has a space or an
expression in it and stops the scan at the first token that is not part of a
type, so `a < b > (c)` is what it always was.

Recognising a mistake is not tolerating it. Nothing here accepts the syntax:
the file is refused, and the difference is that the reader is told where and
what.

## D146: what the formatter refuses to do is a checked property

`fmt -w` is the only thing in this project that replaces somebody's source, and
it already refused a file that did not parse: what it writes has to be the same
program, and a form of half a program would delete the other half.

That was true and nothing held it. `check-fmt.sh` holds it now, as its fourth
property beside output that parses, means the same and formats to itself: a
file that does not parse is handed to `fmt -w`, and it has to come back byte
for byte with a non-zero status.

`check-backstops.sh` has the hole to go with it. With `read` allowed to be true
after a parse with errors, the formatter writes the tree that recovered, and

```
fn main() -> i32 {
    let n = (1 +
    return n
}
```

comes back as a body with the `let` gone. The check catches it, which is the
only way to know that the check does anything.

## D147: not in the one form and not a program are two answers

`fmt --check` printed the name of a file that did not parse in the same list as
a file that is merely untidy, and `--json` gave both `formed: false`. One of
the two is fixed by running `-w` and the other is not, and nothing reading
either answer could tell which.

The list `--check` prints is now the files `-w` would rewrite, which is what
makes it a list worth acting on. A file that did not parse is on the standard
error with the diagnostics that say what is wrong with it, and the status is 1
either way, so nothing is lost by moving it: the two channels now mean two
things.

In JSON `formed` is null for that file rather than false. Whether a program is
in the one form is not a question about a file that is not a program, and null
is the answer that says there was none — the same as `gave` for a handler that
answers nothing (D139).

## D148: shortest is counted in characters

`kest_write_real` said it wrote the shortest spelling that reads back as the
same number and tried six digits and then nine, so an `f32` needing eight got
nine: `1.0 / 3.0` came out `0.333333343` where `0.33333334` reads back as the
same number.

It counts up from one digit now, which is what "shortest" meant. Most numbers a
program prints are short, so counting up is usually where the answer is as
well.

Shortest is counted in characters and not in digits, which is the part that is
not obvious: `%g` moves to an exponent when the digits it is given run out, so
`123456792` at eight digits is `1.2345679e+08` — fewer digits and more to read.
Once a spelling without an exponent in it reads back, nothing wider can be
shorter, so that is where the search stops.

## D149: what to write is written one way

Two messages tell somebody what to write where a type belongs, and they said it
differently. `K0302` printed the module in front of a name the file had written
without one — `p2.Pair` about a line that says `Pair` — and `K0344` suggested
`let b: Pair<i32> = Pair(7)` for a shape that takes two types, which is a
suggestion the compiler refuses.

Both go through `kest_type_shape` now: the name as the file would write it,
without the module in front when the module is the file's own, and with the
shape's own names for the types it takes. `Pair<A, B>`, and `table.Table<K, V>`
from a file that imported it.

The rule is the same one as D140 and it is worth stating once: a message about
a line says what is on the line, and what it says to write has to be something
that compiles.

## D150: a comparison and the jump that reads it are one instruction

Nine of the fourteen comparisons in the measured frame are immediately followed
by the jump that reads them, and the jump only ever reads what the comparison
had just written. That is a dispatch, a push and a pop for nothing.

There are six of them now — `jump.false.lt.i` and its five neighbours — and the
compiler makes them where it emits the jump. A comparison is one byte with
nothing after it, so it is the last instruction when it is the last byte; the
jump takes it back and writes itself instead. Doing it there rather than
looking for pairs afterwards means nothing has been written yet that could
point at the byte being taken away.

Six paired runs, alternating: 150, 147, 147 nanoseconds an entity-step with the
fusion against 153, 150, 160 without, after three of each to warm up. Every
pair went the same way, and the first pair either side of a cold start did not,
which is why they were paired.

Whole numbers only, and only where the answer is branched on. A comparison
whose answer is a value is still its own instruction, because that is a
different thing and this fusion cannot see it. D125 is the reason to say what
was measured rather than what was expected: the last instruction fused on the
same reasoning made the machine slower.

## D151: the jump asks the question it is given

`a || b` asks whether the first one is true, and the machine had no way to ask
that: it wrote `not` and then a jump that reads what `not` wrote. Every `||` in
a program paid a dispatch to turn an answer round and another to look at it.

`jump.true` asks it directly, and the compiler makes it the same way it makes
the fused comparisons — the jump takes the `not` before it back, so `!x` in a
condition costs what `x` does. `not` is still an instruction, because an answer
that is a value rather than a branch is still turned round: `let flag = !(a >
2)` emits it.

Six paired runs, alternating: 148, 147, 147 nanoseconds an entity-step with it
against 149, 149, 148 without, and two warm-up pairs either side that went the
same way. A nanosecond is at the edge of what this measurement resolves and
six pairs out of six is what makes it a number rather than a hope; the frame
being measured has two `not`s an entity-step and no more.

## D152: every comparison a jump reads goes into the jump

D150 fused whole-number comparisons with the jump that reads them, and D151
gave the jump the other question to ask. What was left was the two halves that
did not meet: a float comparison read by a jump, and a jump asking whether
something is true reading a comparison of any kind.

They meet now. Twenty-four fused instructions, six comparisons over two kinds
of number and two directions to ask, and the compiler makes them where it emits
the jump, walking back at most twice — the `not` first and then the comparison
under it, so `if !(a < b)` is one instruction where it was three.

Four paired runs, alternating, after two warm-up pairs: 133, 131, 134, 138
nanoseconds an entity-step with them against 146, 147, 153, 148 without, and
four more with the order turned round: 138, 141, 139, 135 against 153, 156,
158, 158. The two sets do not agree about the absolute number — the machine
drifts over ten minutes — and they agree about the gap, which is why the runs
are paired and why the order was reversed. That
is about a tenth, and it is more than two dispatches an entity-step ought to
buy — the two that were fused are in the inner loop of the frame and nothing
else in the disassembly changed. The number is what it is; the reason it is
that big is not something this measurement can say, and D125 is why that
sentence is here rather than a guess.

Unsigned comparisons are not in the set. `next.less.u` already carries the
loop shape they appear in, and nothing measured asked for the rest.

## D153: a condition is compiled for where it goes

`a || b` was compiled the way any expression is: work out an answer and leave
it on the stack. In a condition that answer is read once, by the jump under it,
and then thrown away — so a `||` cost a `true` pushed, a jump over it, and a
comparison that could not be fused because a jump did not follow it.

A condition is compiled for where it goes now. `branch_when` emits the halves
of `&&`, `||` and `!` as jumps: what falls through is one answer and what jumps
is the other, and nothing is built. The comparison at the end of each half is
followed by its own jump, so D152 folds it in. `x < 0.0 || x > 100.0` is two
instructions where it was six.

The way out of a condition is a list now rather than one place, because each
half leaves by its own jump and they all go to the same one. Sixteen is the
room for them; a condition with more `&&` and `||` in it than that is compiled
as a value, which is what everything did before and is always allowed. An
`if let` and a `while let` are compiled that way too: what their condition
leaves on the stack is the value they bind, not an answer.

Seven paired runs in both orders: 125, 128, 128, 125, 124 nanoseconds an
entity-step with it against 134, 134, 135, 134, 135 without, and two warm-up
pairs that went the same way. About a fifteenth.

## D154: multiply and add in one instruction bought nothing

`p + v * dt` is what a frame integrates with, and the multiply in it is only
ever read by the add above it. One instruction for the pair — two operations
and two roundings, not a fused multiply-add, which rounds once and is a
different answer — removes a dispatch twice an entity-step.

It was written, and it is not here. Nine paired runs in both orders came to
about half a nanosecond an entity-step, against a spread of five: 124, 125,
128, 131, 127, 134, 124, 126, 131 with it and 126, 125, 130, 130, 124, 131,
125, 131, 133 without. Four of the nine went the wrong way. The two turns
before this one had every pair going the same way with eight and thirteen
nanoseconds in it, so this is what a change that does nothing looks like beside
one that does.

The answers were the same to the last digit, which is the part that had to be
checked before anything else: `0.1 + 0.7 * (1/3)` and a loop accumulating
`n + 0.1 * 3.0` printed what they printed before.

Why the compare-and-jump fusions paid and this did not is not something this
measurement can say. What it can say is which of the two to keep. D125 is the
same shape and the same conclusion.

## D155: an instruction is not free before it runs

A slot written and read straight back is the commonest pair this language
emits: `let x = f()` and then reading `x`, two hundred and fifty-nine times in
the examples and the library. `store.keep` — store and leave it where it is —
folded two hundred and twenty-six of them into one instruction each. The other
thirty-three are pairs something jumps between, which is the one case where the
two cannot become one, and the compiler knows because it keeps where the
furthest landing is.

The frame got six per cent slower. Seven paired runs in both orders: 137, 134,
131, 130, 131 nanoseconds an entity-step with it against 129, 124, 124, 123,
124 without.

Then, because that made no sense — the one pair it folded in the measured
function runs once a call and not once an entity — the instruction was left
defined, with its case in the machine, and the compiler was stopped from ever
emitting it: 133, 134, 134 against 125, 125, 125. Moving it to the end of the
enum, so that no opcode a frame uses was renumbered: 135, 135, 136 against 127,
126, 128.

So it is the case being there, not the instruction running, and not where it
sits. That is the opposite of what D125 found by the same test, and both were
measured. What is different is the size: the switch has a hundred and forty-six
cases now, and the two turns before this added nineteen of them and made the
frame a fifth faster, so the price is not a case, it is where this build's
dispatch happens to land when there are a hundred and forty-seven.

The instruction is not here. What is written down is that the instruction set
has a cost of its own, paid by everything, and a fusion has to be worth more
than that before it is worth anything.

## D156: the dispatch stays a switch, and the measurement says how noisy it is

D155 left a question: the machine's dispatch is a `switch` in a loop, so what
it compiles to is the compiler's to choose, and it changed its mind over one
case. A label per instruction and a jump through a table of them is the other
way.

It is not being done, for two reasons and one measurement.

The first is that it is not C11. `&&label` and `goto *` are a GNU extension, so
the machine would either be written twice or the language would stop being
portable C, and neither is worth a tenth of a frame.

The second is that the conversion is not mechanical. There are a hundred and
forty-six cases and thirteen of them hold a loop or a switch of their own,
whose `break` means that one and not the case, so a script that rewrites
`break` into a dispatch is a script that quietly changes what those thirteen
do.

The measurement is of the cheap approximation: `-fno-crossjumping -fno-gcse`,
which is what an interpreter asks for to stop a compiler merging the copies of
its dispatch back into one. Seven paired runs, five of them worse with the
flags and none better than the noise.

What did come out of it is that the noise is worth printing. The same binary
measured 124 nanoseconds an entity-step in the morning and 171 in the
afternoon, and nothing in the number said which of those to believe. `make
time` prints the spread between its best round and its worst now: at nine per
cent the number is a number, and at nineteen the machine is deciding how fast
it wants to run and the answer is worth as much.

## D157: a number written down is read where it is written

The machine refuses a count or a place below nought while it runs, which is the
only moment it can for one that came from somewhere. For one written in the
line it is late: `array(-2, v)`, `slice(t, 0, -1)` and `a[-1]` are mistakes on
the line they are on, and the compiler already works those numbers out for
other reasons.

It reads them now: `K0351` where a count is below nought and `K0352` where a
place is. How many and where are two different rules, which is why they are two
codes; the fix for either is the same and the message says it.

What can be worked out is what `kest_fold_const` folds — a number, a constant,
or arithmetic on them — which is the same thing `[T; N]` counts with, so one
answer serves both. An index into a `[T; N]` was already measured against `N`
here, by reading the digits of a literal; it goes through the folder now, so a
constant index is measured too.

Nothing was taken out of the machine. A count from a parameter is still its to
catch, and the two say the same thing in the same words.

Every place the language takes goes through the same two lines: an index into
text, an array or a `[T; N]`, the `at` of `matches` and `rest`, where `find`
starts, the position `remove` takes out, and where `slice` starts. One function
says it, so a place that is added later is a call to it and not a message
written again slightly differently.

## D158: there is still one measurement

Two turns of reading numbers where they are written put `kest_fold_const` on
the path of every index and every place a builtin takes, and nothing in this
project says how long a build takes. So the question was whether to have a
second measurement.

It was asked once instead. Checking every file in `examples` and `lib` five
times over, with the folding and without it: 219, 221, 215 milliseconds against
221, 217, 225. That is a hundred and eighty files each way and no difference,
so the answer to what prompted the question is nothing, and the answer to the
question is no.

A second measurement is a thing that has to be kept true, and the one this
project has is kept true because a frame budget is what the language is for. A
build that takes a millisecond a file is not a thing anybody is going to notice
getting worse, and a number nobody would act on is a number nobody should
maintain. When a build is slow enough that somebody says so, that is when this
is decided again, and the sentence in CLAUDE.md about there being one
measurement stays as it is.

What did come out of asking: an index into a `[T; N]` worked its number out
twice, once to see whether it was below nought and once to measure it against
how many there are. It works it out once and hands it on.

## D159: how long an array is stays a question for the machine

`remove(a, 3)` on an array made three long a line earlier is the same claim as
`a[3]`, and the compiler could read it — if it followed what happens to `a`
between the two lines. `push` changes it, a call it is handed to changes it, a
loop changes it, and an array is a handle so a copy of the name is the same
array.

That is flow analysis, and what it buys is a rule that holds sometimes: refused
here, allowed with a `push` in between, allowed again when the `push` is behind
an `if`. A language whose refusals depend on how hard the compiler looked is
one nobody can predict. So the line stays where it is: what the type says is
read where it is written, and how long an array is is not in its type.

A `[T; N]` is the other side of that line, and the count is in the type there,
so an index into one is measured against it. That is why `len` of one is a
number — and why nothing is loaded to be counted now: the run was copied onto
the stack and thrown away to answer a question its type had already answered.
A call in there is still made, because a call is the point of the line as often
as it is not.

## D160: a function value is called from wherever it is

D039 made a function a value, and the compiler called one through a name or a
dotted name and refused everything else: `only a named function can be called
so far`. So a function in a field could be called and one in an array could
not, which is not a rule anybody would write down — it is where the code that
looks up names happened to stop.

The instruction was already there. `call.value` takes which function it is off
the stack, so what puts it there can be an index, a field, a name, or anything
else that gives a function. What is looked up as a name is a name and a dotted
name, and everything else is called through what it is.

The refusal that is left says what it means: a call whose callee is not a
function at all.

## D161: what a person is shown is the file they asked about

`kest check` printed every declaration of every file it read. For
`examples/ants.kest` that is ninety lines, twelve of which are the program: the
rest is `std.math`, `std.vec`, `std.random` and `std.io` written out in full
because the file imports them.

The file that was named is printed in full now, and each module it imported is
a line saying how much it holds and how much of that the host has to provide.
Reading one of those modules is `kest check` on it, which is the same question
asked about that file.

`--json` did not change and does not: a tool wants everything, holds it without
scrolling, and is the thing that would break if the text form were what it read.
That is the point of having two forms rather than one that has to be both.

## D162: what a program needs is printed where the program is

`emit` shows what the machine will run, so unlike `check` it shows all of it:
every function of every file, because a host may call anything the program
defines and that is what `kest_module_needs` is worked out over. Summarising an
imported module there would be summarising the code, which is the one thing
this command is for.

What was missing is the number that goes with it. `kest_needs` has answered
since there was a host boundary and nothing printed it, so a host writer had to
write a C program to find out how much stack to give. `emit` prints it now, in
both forms, and prints instead what there is to say when there is no answer:
which of the two shapes it was and which function it was found in.

## D163: a host that knows what it calls can ask about that

`kest_needs` answers for every function the program defines, because a host may
call any of them. That stands: it is what a host that has said nothing has to
be given, and D162 made the number visible, which is what made the size of it
visible too. A program that imports `std.math` for one function is sized by the
deepest thing in `std.math`.

So there is `kest_needs_of(build, name, ...)`: the least for one function and
what it reaches. Nothing about the program changed — every function is still
compiled and still callable — and what changed is that a host which knows which
ones it calls is not made to pay for the rest. A host that calls several asks
about each and takes the largest.

`kest emit` prints the whole-program number and, under it, what `main` costs on
its own when that is less. The difference is what the rest of the program costs
a host that only calls `main`.

Which function a name means is one lookup now — the name as written, then the
same name under the module of the file that was named. `kest_entry`, this new
one and the disassembler asked it three different ways before, and two of them
would have gone on agreeing by having been written in the same week.

## D164: the command line gives a program what it says it needs

`kest run` gave every program the same machine: sixty-five thousand slots and a
thousand frames, because that is what a host gets for saying nothing. A chain
of calls twelve hundred deep therefore stopped at a thousand — while `kest
emit` on the same file printed `needs 3602 slots and 1201 frames`. The answer
was in front of it and it was not being used.

It is now. `run` asks about `main`, `call` asks about the function it was
given, and `tick` asks about the whole program, because either handler may be
the one the file has. What comes back is never taken as less than the numbers a
host gets for saying nothing: what is measured is a least, and this host prints
from inside the call it makes.

A program with no answer — one that reaches itself, one that calls through a
value — gets those numbers and finds out, which is what every program got
before.

Those two numbers are in `kest.h` now, as `KEST_STACK_SLOTS` and
`KEST_CALL_DEPTH`. A host could only ask for them by leaving a zero before, and
a host that wants to say "as much as usual, and this much heap" could not say
the first half.

## D165: a command asks about what it will call

D164 had `tick` ask about the whole program, because either handler may be the
one the file has. It can ask about both and take the larger of the ones that
are there, which is the same answer when the handlers are the deepest thing in
the file and a much smaller one when they are not: a file with an `onEvent` of
two slots and three hundred functions of its own behind it needed nine hundred
slots and three hundred frames, and needs two and one.

So `room_for` takes the names a command will call rather than one. A name the
program does not have is one this host will not call either, and is skipped; a
name it has and cannot answer for ends the question, because a host that cannot
be told picks a number.

`kest emit` prints the same for each of `main`, `onEvents` and `onEvent` the
file has. Those three because they are the ones a command line calls, and a
host with its own names has `kest_needs_of` for those.

## D166: the entry names are written once each

`main`, `onEvents` and `onEvent` were written in seven places between the
checker, the library and the command line — in the lists a command asks about,
in the lookups it does, in the line the usage prints, and in the disassembler,
which had them because that is where the per-entry line was added.

They are one `#define` each now, and every list is built from those. `main` is
in `kest.h`, because the checker holds a function of that name to the shape a
host can call and a host that wants to call the entry point should write the
same name the language does. The two handlers are the command line's own, so
they live in `main.c` with everything that uses them, including the usage text
that names them.

The library stopped knowing them. `kest_module_disassemble` takes the names
whose own cost is worth printing, because which functions a host will call is
not a library's business — it was printing a list of names a command line calls
and calling that a property of the module.

## D167: one function writes what room an answer needs

Three places said what a program needs and each said it in its own way: the
text disassembly, the JSON one, and now `kest call`, which is the command that
knows exactly which function it called and said nothing about it.

They go through `kest_module_needs_json` now, which writes the fields of the
answer without the braces around them, so a caller puts them wherever they
belong: in the program's object, in one of the entries beside it, or beside
what a call gave back. The shape a tool reads for a program and the shape it
reads for a function are the same shape, nulls and why and all.

`kest call --json` is the useful end of it. `emit` answers about `main`,
`onEvents` and `onEvent` because those are what a command line calls; a host
that wants to know what its own entry point costs can ask for that function by
name from the command line, without writing a program to ask.

## D168: the tree is written in the one form

`check-fmt.sh` held the formatter to what a formatter has to be: its output
parses, means the same, and formats to itself. Nothing held the files
themselves, and three of them had drifted — two written before the formatter
learned what to do with a line they hold, and one written this afternoon.

Every file the tool is given has to be in the one form now. A language whose
answer to "how should this be written" is one form has that form in its own
files, or the answer is one it gives and does not take.

## D169: "not yet" was a promise the compiler was not keeping

Eleven refusals in the compiler said a thing could not be done *yet*: an
operator not compiled yet, a field not reachable yet, something not assignable
yet. Ten of them cannot be reached by any program. The checker refuses each one
first — a `for` over a number is `K0317`, a call of a number is `K0308`, an
assignment to a literal is `K0205`, a `[T; 0]` is `K0326` — so what those
guards catch is the two halves of this compiler disagreeing, which is nobody's
mistake but this project's.

They say that now, with `K0505` and in the words `K0405` uses for the same kind
of news. `K0501` is gone from the tree; the eleventh, a `match` of more than
eight things at once, turned out to be refused by the checker as well.

`check-backstops.sh` has the hole to go with it: with the checker's refusal of
a walk over a number taken out, `for x in n` reaches the compiler and the
compiler says whose fault that is.

A "not yet" in a diagnostic is a promise. A compiler that makes one it is not
keeping teaches a reader to expect a feature that was never planned, and hides
the fault it is actually reporting.

## D170: a number a program can run into is written where it can be read

The compiler holds a program to seven numbers: names in a function, loops
nested, breaks and continues and defers, how far a jump reaches, how many
things a `match` chooses at once, how many a `[T; N]` holds. Six of them were
written only in the code that enforces them, and two of the messages did not
say the number at all — "this jumps too far to encode" is a refusal that does
not say how far is far.

Every one of them says its number now, and the reference has the table. The
messages and the table are the same numbers written twice, which is the one
kind of repetition this project takes: a reader of a program looks in the
reference and a reader of a refusal reads the refusal, and neither should have
to read `compile.c` to find out what "too far" means.

## D171: the one number nobody could see is gone

D153 gave a condition sixteen ways out, and a condition with more of them was
compiled the old way — an answer built and then read by one jump. Nothing was
refused, so nothing was said, which makes it the only number in this compiler
that changes what is emitted rather than whether it is emitted at all. A cost
that depends on a number nobody is told is the thing this language says it does
not have.

The list grows now. A condition of forty `||` is forty branches and no answer
built, and there is no number to know. `make time` is where it was, which is
what it should be: nothing in the instrument has a condition long enough for
the old fallback to have fired.

## D172: a call gives what the function gives

A host call carries how many slots its answer takes, and the compiler took that
number from the expression rather than from the function. Where a value stands
in a place an optional is wanted, the checker widens the expression to the
optional and the compiler emits the tag afterwards — so a host call in that
place said two slots for a one-slot answer, wrote over the slot beside it, and
handed back the wrong number.

`std.math.asin` was the program that found it: `atan2(value, sqrt(1 - value *
value))` returned the square root. Every part of it is right and the answer was
the second argument.

A call gives what the function gives now, in all three of the ways one is made
— a name, a host, a value. The tag is still emitted after it, which is what the
widening means and is now counted once rather than twice.

## D173: the angle a direction points

`std.math` asked a host for six things and none of them turns two numbers into
where they point. A program that has a difference and wants an angle — every
program that turns something towards something else — had to work it out from
`sin` and `cos` backwards, which is the shape of a thing a library is for.

`Math.atan2` is the seventh, and it is the one that cannot be built out of the
others. `tan`, `asin` and `acos` are written in Kest out of it and `sqrt`, so a
host provides one more function and a program gets four.

## D174: a host can ask what it is expected to take

A host binds a C function to a name and nothing checked that the two agree
about what crosses. A function bound to a name that takes one thing and written
to read two reads whatever is beside it — the frame is the host's to read, and
the program's idea of it was not something the host could ask for.

It is now: `kest_extern_takes`, `kest_extern_layout` and `kest_extern_gives`,
the same layouts `kest_frame_layout` gives for a function the host calls. The
compiler writes them down where it registers the extern, out of the declaration
it is compiling the call against.

`examples/embed.c` says what it believes about the two functions it binds and
compares, which is the shape the layout check there already had: reading it out
of the program instead would be checking the program against itself. It names
every missing binding now rather than the first, because a host writer wants
the list and this project's own rule about diagnostics is not to stop at the
first.

## D175: what a host is asked for is what the program can reach

The reference said the list of externs is what the program declares rather than
what it calls. It is not: an `extern` is registered where a call to it is
compiled, so one that nothing calls is not on the list and starting does not
hold a host to it.

The rule as it stands is right, and the sentence was wrong. What a host is
asked for is what the program can reach, which for an imported module is all of
it — every function of it is compiled and each of them calls what it calls. A
declaration nothing reaches is a name in a file.

`K0506` says so where it is written. A warning and not a refusal: a declaration
nobody uses is not wrong. It is worth a line because the file is what a host
writer reads, and binding a name nothing will ever ask for is work with nothing
on the other end.

## D176: adding while walking a store is gathering and adding after

Removing while walking a store is defined and documented: the slot goes dead
behind the cursor and the walk does not go back to it. Adding was neither.

What happens is deterministic and not worth relying on. A walk is a scan over
live slots in slot order, and a store hands out the slot it last took back — or
a new one at the end when it is holding none. So something added inside a walk
lands where the walk has already been as often as where it has not, and whether
this walk reaches it depends on what died before it.

Bounding the walk to the slots that were live when it began would make it a
rule, and would cost an instruction to read the extent and an operand to carry
it. D155 is why that is not free, and gathering into an array and adding after
the walk is one line more in the one place it comes up. `examples/quests.kest`
spawns that way and the reference says both halves of what happens if somebody
does not.

## D177: a host hands text over by copying it

A host could give the program an array over its own memory and could not give
it a piece of text at all. `KestValue.text` is a `const char *`, so a host
function could write one — and whatever it pointed at would have to outlive
everything the program did with it, which a host cannot know and nothing said.

`kest_text` copies into the machine's heap, which is where the program's own
text is, so the answer is the machine's and the host's copy is its own business
afterwards. A zero byte inside the length is reported rather than cutting the
rest off silently, the same as `text(bytes)` inside the language.

Both hosts in this tree hand their own name over that way, and `embed.kest`
asks twice and compares, which is the check that the first answer is still
there after the second.

## D178: what a host offers beyond what a module declares

`std.io` writes and does not read, and a program on a command line has to be
able to get a line in. Declaring `Io.read` there would ask every host of every
program that imports `std.io` for a standard input, and an engine has none.

So the command line provides it and a program that wants it declares it. That
is the rule the boundary already had, seen from the other side: what a module
declares is what every host of it must have, and what a host offers beyond that
is between the host and the program that asks. The reference says which host
has what, because a program that declares one is a program that runs under some
hosts and not others, and that should be a thing somebody chose.

Everything at once rather than a line at a time: `std.text` splits, and reading
a line at a time would ask a host to keep a place in a file between calls,
which is state the program cannot see and cannot reason about.

Every check that runs a program now gives it nothing on the standard input. An
example is a program that answers the same thing every time or it is not one,
and until this there was nothing to read so nothing said so.

## D179: `type` is a name

`type` was a keyword the parser accepted nowhere. The reference said it was
kept back and had no meaning yet, so that nothing would have to be renamed the
day it got one.

That is the same promise the compiler was making with "not yet" until D169, and
it costs something every day it is not kept: `type` is what somebody calls the
kind of an event, and a struct could not have a field of that name.

It is a name now. `flags` is how this language takes a word back when it needs
one — it declares where a declaration begins and is a name everywhere else — so
a `type Health = i32` can arrive the same way, on the day somebody designs what
it means, without a word being taken from every program until then.

## D180: when a word is a keyword

Nothing said which of the two a new word should be, and the language has both:
`flags` declares a type where a declaration begins and is a name everywhere
else, and `struct` is a keyword everywhere.

The rule, in CLAUDE.md now: a word is a keyword only when a program that used it
as a name would be ambiguous where it stands, and no word is kept back for a
feature that does not exist. The cost of a keyword is paid by every program
that wanted the name, and it is paid every day.

`check-tables.sh` holds the third list this project has to keep complete: the
keywords the lexer holds beside the ones the reference prints. It found three
missing the first time it ran — `enum`, `match` and `none` were keywords the
reference did not mention, so a reader was shown a list and told it was the
whole of it.

It also found that the helper reading the spellings had been taking the first
letter of each. Nothing had noticed, because the only thing asked of it until
now was how many there were.

## D181: not a number has one spelling

`0.0 / 0.0` printed `-nan`, because that is what the machine left in the sign
bit and what C prints for it. The sign of a not-a-number says something about
which operation made it and nothing about the value, and this language's rule
for writing a number is the shortest spelling that reads back as the same one.

It is `nan` now, whichever divide made it. An infinity keeps its sign, because
that one means something.

Neither reads back: there is no way to write either in the language, and a
program that wants one divides. That is the one place the rule about spelling
cannot hold, and the reference says so rather than leaving a reader to find a
number that does not survive being printed and read.

## D182: a file is where it says it is

`examples/frame.kest` called itself `game.frame`. Nothing imported it, so
nothing noticed, and an import is a path — `import game.world` is
`game/world.kest` beside the file that wrote it — so a file whose `module` line
does not match its own path is a file nothing can ever import.

It is `examples.frame` now, and `check.sh` holds every `.kest` in the tree to
it: what a file calls itself has to end where the file is. That is a rule with
no exceptions and it was already true of every other file, which is why the one
that was not had gone eighty commits without anybody noticing.

The two files called `frame.kest` say which is which now. One is checked and
not run and holds the shapes a frame is declared with; the other is the one
measurement and runs when somebody asks.

## D183: a name clash is a question about one file

Two modules whose names end the same way put their names under the same one,
and that was refused for the whole program: any two files anywhere in it with
the same last segment.

`kest check examples/*.kest` is what found it. The reference says that command
checks a project as a project, and this project holds `examples/math.kest` and
`lib/std/math.kest` — neither of which imports the other, and neither of which
is ambiguous about anything. The refusal was about two files that never meet.

It is about one file now: what a name in a file can mean is that file's own
module and what it imports, so the clash is between two of those and the
message points at the import that brought the second one in. A project may hold
a `math.kest` of its own beside `std.math`, which is a file name somebody will
want, and a file that reads both is still told.

## D184: the same file spelled two ways is the same file

`kest check` over every file in this tree at once refused it: `random.Source`
already declared, pointing at `lib/std/random.kest` and at
`./lib/std/random.kest`, which are one file. A command line names one spelling
and an import from the library root works out another, and the loader compared
what it was given.

It compares one spelling now. Only the ones that come of putting paths
together — a leading `./`, a doubled slash, and a step into a directory and
back out of it — because two paths that reach one file by different routes
through the file system are two files as far as a compiler that reads what it
is given is concerned, and answering otherwise would mean asking the operating
system questions this language does not ask.

`make check` reads the whole tree as one project now, as well as file by file.
Reading them one at a time never asks whether two of them can be read together,
which is the question both this and D183 came out of.

## D185: a shared alias is refused for the program, and D183 is wrong

D183 made a name clash a question about one file: two modules ending the same
way were only refused where one file read both. Reading every file in this tree
at once found what that let through.

The table names go in is the program's. `math.min` is one entry however many
modules end in `math`, so two of them share a namespace: a file importing
`mine.math` and calling `math.min` found `std.math`'s, which another file had
imported and this one had not. Names leaked between modules that never met.

So it is refused for the whole program again, wherever the two are and whoever
reads them, and D183 is superseded. What would make it a question about one
file is keying the table by the whole of a module's name and resolving a
written prefix through the file's own imports — a change to every lookup in
this compiler, worth making the day somebody wants `math.kest` beside
`std.math` badly enough to pay for it.

`make check` reads `lib/std` as one project, which is one, and not the examples,
which are thirty programs that live in one directory.

## D186: a file that names no module is a program, not a module

A file may say what it is called, and one that does not puts its names under
nothing. That is what somebody writing one file to answer one question wants,
and it is what this project's own generated programs are.

Importing one of those worked, and what it did was put the imported file's
names into the importing file's own: `helper()` rather than `thing.helper()`.
Every other import in this language writes where a name came from at every use
of it, and this was the one that did not.

It is `K0702` now, at the import, suggesting the name the import asked for —
which is the name the file should have, since an import is a path and the two
are the same thing written twice.

## D187: a failure while running says how it got there

`10 / n` divided by nought said where, and not how it came to be called with
nought. Three functions deep that is a line and a guess; in a frame that fails
once every few thousand it is a line and nothing.

Every failure carries the calls under it now, one note each, outermost first,
so the reader follows the way in rather than reading a stack backwards. The
frames are there at the moment of the failure — every one below the failing one
has an `ip` just past the call it made, so the byte before it is where that
call is written.

Eight is what a message holds. A run of calls deeper than that says how many
were left out rather than showing the middle of it, because a number is what a
reader of a deep one wants.

It costs nothing while a program runs: the walk happens where a failure is
already being reported, and a program that does not fail never does it. `make
time` is where it was.

## D188: which of a name's two meanings was meant is what it takes first

A file that declares its own `add` and calls it with the wrong number of
arguments was told about the builtin one: "expected 2 arguments, found 3", and
then "`add` works on a store, found `i32`". The second is about a function the
reader did not write and did not call.

Which of the two was meant is decided by what the call hands over first, and it
was decided by how many it hands over as well: a user's function whose arity
did not match the call was passed over, and the builtin of the same name
answered instead. How many is a mistake in the call; what it takes first is
what says which function it is.

The other half of the same reading: `p.len()` is what somebody writes who has
met a language with methods, and the message said `Point` has no field `len`
and stopped. It says what to write instead now, and looks for the name in the
language's own, in what the file declared, and under what it imported — so
`t.upper()` is answered with `text.upper(...)` and not with a shrug.

## D189: a slot that has used all its counts is retired

A reference is a slot and a generation in one value: thirty-two bits each. The
generation is what makes a stale reference stale, and it counts removals of
that slot — so four thousand million removals of one slot bring it back to
where it started, and a reference from the first occupant reads as the newest
one. That is the one thing a reference is for, failing quietly.

A slot whose count has come round is not put back on the free list. One
comparison on the removal path, and the failure is gone rather than made
unlikely.

It is not demonstrated by anything that runs: reaching it takes four thousand
million removals of one slot, and a store with a narrower count would be a
different program. What can be said is what it costs — one slot in a store that
has been removed from that many times — and that a long-running one can reach
it: a thousand removals a frame at sixty frames a second is twenty hours.

## D190: a store with nothing in it reaches nothing

A walk over a store scans its slots and skips the dead ones, so what it costs
is how far the store has ever reached and not how much is in it. A level that
spawned a million and ended with none would walk a million dead slots for the
rest of the program.

The cheap half of that is now free: a store whose last live slot is removed
goes back to reaching nothing, because everything a walk would step over is
dead. The free list goes with it, since every slot below the extent is
available again.

What is kept is what each slot has counted. That is what makes a reference from
before stale, and losing it here would be the easy mistake: filling the store
again hands back slot nought, and a reference to the first occupant of slot
nought must still read nothing. So a fresh slot is given its first count only
when it has never been used at all, which is what `high` is for.

The other half — a store that is fragmented rather than empty — is left alone.
Shrinking to the highest live slot means taking slots out of the free list,
which is a scan, and nothing has measured the walk over the dead ones as worth
one.

## D191: the tag on a handle is a net, and it has been seen catching

Every handle the machine hands out carries what it is — an array, a store, a
piece of text — and every instruction that follows one reads that first. It is
`K0612` when the two disagree, and nothing a program can write reaches it: the
checker refuses an array where a store is wanted long before.

So it was a net nobody had seen catch anything. `check-backstops.sh` has the
hole for it now: with `kest_type_equal` told that an array and a store are the
same type, `len` on a `[Npc]` handed to a `store<Npc>` reaches the machine, and
the machine says which of the two it actually has.

The trace makes it a better answer than it was: the message points at `len` and
the note says where `count` was called, which is where the array was handed
over.

## D192: what is said about a copy says which copy

A generic is checked once per set of types it is called with, and what a copy
cannot do is reported at the line in the body that cannot do it. The body reads
the same for every copy, so the reader is left working out which call made the
one being complained about — and a file with two calls to one generic gives no
clue at all.

Every diagnostic raised while checking a copy carries a note at the call that
asked for it now. Each of them, not the last: a body says more than one thing,
and all of them are about the same copy. Which needed a way to add a note to a
diagnostic other than the newest one, which `kest_diags_note_at` is.

The call kept is the first one that asked, because that is the one that made
the copy; a second call with the same types is the same copy and has nothing to
add.

## D193: a message is built where it is kept

Every diagnostic message used to be formatted into a fixed buffer belonging to
whoever raised it, and copied into the arena from there. Four modules had one,
all `char message[512]`, and twice in a week a message ended in the middle of a
name because of it.

The buffer was the only reason there was a length. `kest_diags_addv` formats
into the arena, which takes its size from `vsnprintf`, so a message is as long
as what it says. A caller that wants to leave something out says so — the note
about a copy's types counts what it dropped — but that is a decision about the
message, made where the message is written, not a limit every message shares
because of where it was built.

## D194: a line that does not fit is shown around its span

A source line is shown to a hundred columns. Past that, a window of a hundred
is shown around the span, marked with `...` at whichever end was cut, and the
carets are placed and clipped to match.

The alternatives were to print the line whole, which is what it did, or to wrap
it. Printing it whole makes a diagnostic about a machine-written file unusable
in a terminal, and hides the answer among the noise. Wrapping keeps everything
but breaks the one thing a frame is for: a caret is under its span because they
are on the same line, and a wrapped line has no such line. A window keeps the
caret, keeps the context on both sides of the span where there is any, and says
what it dropped.

A hundred and not eighty, which is what the formatter writes to, because a
formatted file has no line that reaches this at all: the ones that do come from
a file the formatter could not read or from a generator, and the extra twenty
columns mean a line that merely overran the limit is still shown whole.

## D195: a tab is shown as the spaces it stands for

A diagnostic frame shows a tab as spaces to the next stop, four columns wide,
rather than passing the file's own tab through.

Passing it through means the caret line has to guess what the terminal does
with a tab. Whatever it guesses, a terminal set differently puts the carets
under the wrong thing — which is what happened: a body indented with two tabs
had its carets six columns short.

Showing it as spaces makes the guess be about how wide the line looks and not
about where the caret lands, because the line and the caret line under it are
measured by the same walk over the same bytes. Four because that is what this
language is written with. The file is not changed; a frame has never been the
file's bytes, it is what is being shown of them (D194).

## D196: a character that is not on the screen is not in a file

A source file has to be UTF-8, and every character in it has to be one that can
be seen. A byte that starts no character is `K0107`; a space that is not the
space, a mark with no width, a mark saying which way the line reads, and a line
break that no line ends with are `K0108`.

Names take any character over ASCII, which is how somebody writes a name in
their own language without this project carrying a table of every character
there is (`is_ident_start`). The cost of that rule on its own is that two names
which look like one name are two names, and a reader has no way to see it. The
same holds a line further out: a mark saying which way to read makes the line
on the screen a different line from the one in the file.

So the file is walked once before it is lexed. This is not tolerant parsing in
reverse: it refuses what a reader cannot check, and what it refuses is a list
short enough to read, not a Unicode table. A file that means one of these
characters can gain a way to write it, and that will be a decision then.

## D197: a message printed in the reference is one a run says

Every diagnostic quoted in the reference or the decisions is held to a message
the compiler raises that code with, by `check-docs.sh`, which reads the code
and the format out of the source.

Documentation about a compiler is mostly prose, and prose is checked by being
read. A quoted diagnostic is not prose: it is a claim about what a program
does, and it was wrong — a code meaning a number without digits over a message
about an unknown function, invented and never noticed.

The match is by shape, not by letter: a `%s` in the format stands for anything,
so a document keeps its own names and its own numbers. What it cannot keep is a
message the compiler does not have. The reverse drift — a message reworded in
the compiler while the document keeps the old one — is the one that will happen
again, and it is the direction the backstop breaks.

## D198: a suggestion is looked for where the name was looked for

The nearest match for an unknown name is looked for in the three lists a name
is looked up in, in that order: the locals this body declared, the names the
language answers to on its own, and the globals this file can reach.

It used to be looked for in one of them, the globals, and compared against the
name they are held under. Globals are held under their module and written
without it, so `hurt` was compared with `player.hurt` and nothing was ever near
enough to suggest. A rule this project has had from the start — an unknown name
reports the nearest match — had never once fired for a name in the file that
was being checked.

A candidate is compared with the part of it that was written the same way, and
suggested the way it would have to be written: the last piece alone when the
file declared it, and under its module otherwise, which turns the common
mistake of leaving the module off into a suggestion that can be pasted. A
module the file did not import is not a candidate, because a name from it is
not one the reader could have meant.

## D199: a name under a module blames the part that is wrong

`io.prnt(...)` says that `io` has nothing called `prnt`, at the span of `prnt`,
and suggests the nearest name under that module written the way it would have
to be written.

What it said before was that `io` was an unknown name, because a dotted name
that is not found as a whole was taken apart and its first half checked as a
value. Where a module is spelt like a type — `text` — the first half resolved,
to the wrong thing, and the message was about a type being named where a value
goes. Both blamed the half that was right, which is the worst thing a
diagnostic can do: it sends the reader to the part of the line that is correct.

A module is not a thing this program holds. It is what the names under it have
in common, so the test is that something is declared under that name and this
file imported it. Both lists are looked through, types and globals, because
`shape.Point` and `shape.zero` are written the same way and the reader has no
reason to know which list either is in.

## D200: a near miss is shown instead of the list, and a list says where it stops

A case or a bit that is not there is answered one of two ways. When something
in the declaration is nearly what was written, that one is suggested and
nothing else is shown. When nothing is, every case is shown with a note at its
own line, which is what this did before in both cases.

The list is there to say what could have been meant. A suggestion says the same
thing better when there is one to make, and beside a suggestion the list is
noise — ten lines under an answer that is one word.

A diagnostic holds eight notes (`KEST_MAX_NOTES` in `diag.h`), and a list
that stops at eight without saying so is worse than no list: a reader counts
what they were shown and believes it is all of them. The last note there is
room for says how many more there are, which is what the note about a copy's
type names already does.

## D201: two letters the wrong way round is one mistake

The distance every suggestion is measured with counts a swap of two letters as
one mistake, not as the two substitutions it takes to write it as one.

A suggestion is allowed a third of what was written in mistakes, which is one
for a name of four letters. Most of the names this language answers to on its
own are that short, so `psuh` was near nothing while `push` was in the list.
Swapping two letters is how a word is most often mistyped, and the rule that
kept wrong suggestions away was throwing out the likeliest right one.

It costs a third row of the table, kept for the row before last. Nothing else
changes: the limit is the same, and the walk still stops early on a row that is
already too far.

## D202: the pipeline is held to the tree

The list of modules in `CLAUDE.md` is checked by `check-tables.sh`: it names
every `src/*.c` once and nothing else, and every `#include` in a module's own
files points at a module at or above it in the list.

The list is where this project says what may depend on what, and it had drifted
in every way a list can: a module that does not exist, a module that does and is
not named, and two in an order the includes contradict. Meanwhile the rule it
states is real enough to have redirected the work in the entry before this one,
where a suggestion the parser wanted had to move because the parser is above the
types.

A rule that decides where code goes cannot be kept in prose. The list stays in
`CLAUDE.md`, where a reader meets it, and a check reads it from there — a
document that is also an input is a document that cannot rot without something
noticing.

## D203: a run of a written length allocates nothing

The `no.alloc` proof over the tree used to call every array literal an
allocation. A literal whose type is a run of a written length is not one: it is
laid out where it stands (D064), in the frame's slots or inside the struct it
is written into, and the compiler emits nothing for it but the values and a
store.

This project proves the promise twice, over the tree and over the emitted code,
and the two disagreed: the tree refused `P([1.0, 2.0])` and the emitted code
had no allocating instruction to point at. When two proofs of the same thing
disagree, one of them is wrong, and it was the one that decided by the shape of
the syntax instead of by the type the checker settled.

The refusal fell exactly on the language's own shape — a fixed run of floats
built inside a frame step — which is the thing `no.alloc` exists for.

## D204: recovery follows a block the failed line opened

When a statement is refused, the parser skips to the end of its line, and if
that line opened a block it goes on to the brace that closes it.

Skipping the line alone leaves the block's contents to be read as statements of
the block around them, and the closing brace then ends a block it did not open.
Everything after is one level shallower than the file really is, so a mistake in
one line is followed by a message about a line that is correct — and the worst
kind, one that says a file holds `module`, `import` and `fn` in the middle of a
function.

The cost is that mistakes inside the skipped block are not reported in that run.
That is the right trade: the block belonged to a statement that was refused, so
what it holds is being read under a header the parser could not make sense of,
and a second guess about it is a guess. One mistake, one message, and the file
carries on being read at the depth it is written at.

## D205: the end of a file is a place

A parser diagnostic raised at the end of a file points just past the last
character in it, on the last line with something on it.

The end of a file is a token with no width, and a span with no width is shown
as a path with no line under it. That is right for a diagnostic about a whole
program, which has no line of its own, and wrong for this one: a file that runs
out in the middle of something is exactly when a reader wants to be shown where
it got to.

Just past the last character rather than under it, because the last character
is not what is wrong — what is wrong is that there was nothing after it.

## D206: an `if` that gives a value may put its `else` on the next line

The value form of `if` is one expression on one line, and a line has eighty
columns. `let rounded = if scaled >= 0.0 -> i64(scaled + 0.5) else -> i64(scaled
- 0.5)` is eighty-one, and neither the writer nor the formatter had anywhere to
put the break: a line ending in a value ends the statement, so an `else`
beginning the next one was refused.

The parser looks past a line break for `else` now, and only for `else`. Nothing
in this language begins a statement with that word, so the lookahead takes no
program away from anybody and adds no shape a reader has to learn: it is the
same expression, with the break in the one place it can go.

The formatter puts it there when the line will not hold the whole, and nowhere
else. Which leaves the lines this tree cannot fit into eighty columns as what
they should be: text with holes in it, and a comment.

## D207: a comment goes above what it is about

A comment shares a line with nothing. Whatever a file was written as, the one
form puts each comment on its own line, at the indent of the thing under it,
and that thing is what it was written about: a comment at the end of a line
goes above that line, and one inside something printed as a single line — the
value of a match arm — goes above the whole of it.

The alternative is to keep a trailing comment where it was, which means two
places a comment can be and a reader having to look in both. This language has
one place, for the same reason it has one form.

What made this worth deciding rather than leaving to the formatter is where the
words went before: `let x = 1 // trailing` left `// trailing` above the *next*
statement. Nothing was lost and everything was moved onto something it was not
written about, which a reader has no way to tell.

`check-fmt.sh` holds it: every comment in a file is in the file the formatter
writes, in the order it was written, over a file nobody has formatted as well
as over this tree. A formatter that dropped one would keep every other promise
it makes.

## D208: a diagnostic counts the places it had no room for

A diagnostic holds eight notes. The ninth used to be dropped by
`kest_diags_note` without a word, and whether a reader was told depended on the
caller remembering to count — which three of them do, each in their own words,
and the rest did not.

The count is kept where the dropping happens. A diagnostic that left places out
says how many, in both forms: `and 3 more places` under the notes, and
`leftOut` beside them in JSON.

A caller that has something better to say still says it: the path from a broken
promise says `and 24 calls under that` at the last note there is room for,
because what is under it is calls and not places, and the enum that has more
cases than fit says which one the last shown is. Those keep room for
themselves, so they leave nothing out and this says nothing. What it is for is
the caller that has not thought about it, which is the one that would otherwise
show a list that reads as the whole of what there was.

## D209: a list of what somebody could have called is not cut

A diagnostic points at eight places and counts the rest (D208). That is the
right answer for a place: what is missing is a line somebody could have gone
to look at, and knowing there are three more of them is most of what looking
would have told them.

It is the wrong answer for a name that is several functions. There the list is
what a reader picks from, so the ninth of them is not a place they cannot see:
it is a function they could have called, and a count does not tell them it
takes a `text`.

So past the room there is, `kest call` stops pointing and says them instead:
all of them, by what they take, in one line. Under it, the frames are better —
they say where each one is written, which is where somebody choosing between
three has to look anyway.

This is what D200 said about the cases of an enum, arrived at from the other
side, and the cases are now counted by the diagnostic rather than by a rule of
their own: a case that is not shown is a line in a declaration, which is a
place, and a place is what a count is right for.

## D210: what a message puts marks round

A pair of backticks goes round the whole of one thing and never inside it. What
a function takes is one thing, written the way the language writes it —
`(i32, i32)`, and `()` for one that takes nothing — rather than a list of
separately marked types with commas of the message's own between them.

Three shapes had grown for the same sentence: `i32`, `i32` in a note, `(i32)`
beside `(i64)` in a list, and `(i32, i32) -> bool` in the listing `kest check`
prints. The third is what the language writes and the other two were the
message inventing its own.

A list of several things is still a list of marked things — `` `(i32)`,
`(i64)` `` — because each of them is whole. What is not allowed is the marks
falling inside one of them, which is what tells a reader that the commas are
the language's and not the sentence's.

## D211: a module and its function do not share a name

`sort.sort(items, sort.ascending)` and `table.table()` are what sorting and
making a table were written as, because the module and the one function that
carries its purpose had the same name. They are `sort.by` and `table.empty`
now.

Nothing was wrong with either: both resolve, both are unambiguous, and the
compiler never minded. What is wrong is that every call to them reads as a
stammer, and a name is read far more often than it is written.

`by` says what the second argument is for, and `empty` says what comes back
rather than what module it came from. The rule this leaves is small: a module
is a place, so its functions are named for what they do there, and the place is
already said by the caller.

## D212: a generic named where a shape is wanted is the copy that fits

A generic function could be called and not handed over: `sort.by(items,
sort.ascending)` was refused, because which copy of `ascending` was meant is
not knowable from the name. It is knowable from where it is going. The
parameter says `fn(text, text) -> bool`, and there is exactly one copy that
fits.

So a generic named where a function type is wanted is that copy, made the same
way a call makes one. D023 settles a call's type names by what is passed; this
settles them by what is wanted, which is the same question from the other side,
and the two now meet: `sort.by` is generic in what it sorts, and the order it
sorts by is generic in the same name.

What this bought is a standard library that no longer names the types it knows.
`sort.ascending` was three functions — `i32`, `f32`, `text` — and a fourth type
meant writing your own. It is one function now, and it works for every type the
language can compare, including the ones nobody thought of. A shape with no
order is refused in the copy that asked for it, at the line that asked, which
is where the reader is.

## D213: a copy is asked for by whoever asked for the one it is in

The note that says where a copy of a generic was asked for names the line the
reader wrote, however many bodies down the copy is.

It used to name the nearest asking, which for anything in a library is another
line of that library. A struct with no `hash` handed to `std.table` was told
about `slotOf`, asked for by `find`, asked for by `set` — and `set` is the line
somebody wrote. The two lines it named were both the library's, and the reader
had nothing to go and look at.

A copy asked for while a copy is being checked takes that one's asking, which
is already the outermost, so the chain collapses as it is built rather than
being walked afterwards. Three bodies down comes out as one line, the one
that started it.

What is lost is the middle of the chain, and it is worth losing: the library's
own calls are not a mistake anybody made, and the message already names the
line inside the library where the type does not fit.

## D214: a tagged layout is walked, and the tag says only which pieces to ask about

`tagged` on a layout means some piece of it is a payload whose type the tag
decides. It does not mean the layout is not worth walking, and it never did:
the tag is at a known offset, the payloads are where the widest case put them,
and every other piece names its type as it always has.

The host that lends it had it the other way round. `same_pieces` in
`examples/embed.c` refused any tagged layout outright, so the one enum this
project lends across the boundary was compared by size and nothing else — and
size is the thing two layouts of different shape agree about. It says what it
believes now, `offsetof` per piece like the rest, and a payload put four bytes
too early is caught before the machine starts.

This follows from the pieces having offsets worth reading. A struct that holds
an enum is tagged too, because a host that moves one by hand has to know a tag
is in there somewhere; its own fields are still pieces that say what they are,
and refusing to compare them because of an enum three fields along would give
up the whole struct for one slot of it.

## D215: what a program can be told it has is the language's number, not a host's

A host says how much stack a machine gets, how deep the calls may go and how
much heap there is, and every one of those is a resource: a program that runs
into one was right and the machine it was given was small. `KestLimits` is that
list and nothing else belongs in it.

What `len` can count to is not a resource. It is the range of an `i32`, which
is a type this language has and every program reads. A host that could lower it
would refuse a program another host runs, and nothing in the program would say
which of the two was right — the same file would mean two things depending on
who started the machine. That is the one thing a language cannot let a host
decide.

So `MAX_COUNTED` stays in the machine, beside the instructions it holds back,
and `tools/check-ceilings.sh` reaches it by building a tree with a smaller one
rather than by asking a host for a smaller one. The price is a build, and the
price is paid in a second now that a copy of the tree brings the objects it was
built from: what is being made is one file, not sixteen.

The other reading was reasonable enough to write down. A host that lends an
array can already say how long it is, and a host embedding this in a frame
budget has an opinion about how big anything gets. But an opinion about size is
what the heap is for, and it is answered by `K0617`, which says what the host
gave and not what a program may hold.


## D216: the sanitised build is told what the arena handed out

An arena takes one block from the host and hands out pieces of it, so every
read one element past the end of a piece is a read of memory the arena owns.
No sanitiser has a word to say about that, which means the whole compile-time
half of this project — tokens, syntax, types, every message built in the arena
— was outside what `make check` could see. The one backstop held under the
sanitisers had to walk off a host's stack to be caught, because walking off a
block would have been caught by nothing.

So `mem.c` poisons a block when it takes one, opens each allocation to its own
size, and leaves a gap after it that stays poisoned. Reading past a thing is a
report now, and the tree passes with it on, which is the first time anything
has said so.

Two things are kept out of it. The gap is not counted as handed out, so what a
program is told it used and what a ceiling refuses are the same numbers in both
builds — a check that answers differently under the sanitiser is a check that
holds two different programs. And the release build includes nothing but ISO C:
the header is the sanitiser's own, behind `__SANITIZE_ADDRESS__`, in a build
that is already standing on the sanitiser runtime. A dependency a shipped
library does not have is not a dependency.

What this does not catch is a read inside a thing the arena handed out, which
is what an array's spare capacity is. An index past the end of an array is
still inside the block that array owns, and what refuses that is the machine's
own check, which is where it belongs.


## D217: a store can be told how many it will hold

`store(n)` makes room for `n` before anything is put in. It is not a new kind
of thing: `array(n, v)` has always said how many there will be, and this is the
same sentence for the container that grows.

What it is for is which frame pays. A store doubles, so one `add` in eight
takes a bigger block and copies the old one into it, and the host watching a
frame budget sees a frame that costs two hundred bytes beside four that cost
nothing. A budget is set by the worst frame. Moving that growth to the line
that says how many there will be is the only thing in the language that can
move it, since nothing frees and nothing else can be asked for room.

`len` of a store with room for sixteen is nought, because room is not what it
holds. That is the whole of the difference from `array(n, v)`, which makes `n`
of something and says so.

The count is checked twice, which is what every count in this language gets: a
number written down is read where it is written, and one worked out while the
program runs is refused where it runs. `examples/embed.kest` says `store(16)`
in `create`, and every frame in the host beside it now costs nought.


## D218: a fill of nought is not written

`array(n, v)` writes `v` into every element. When every slot of `v` is nought
it writes nothing, because the arena hands out memory that is already nought
and says so where it is declared.

This is what makes the two lines a reservation. `array(n, v)` and `clear` are
what this language has instead of a word for asking an array for room, and
until now the asking cost a pass over the memory that nothing would read: two
hundred reservations of a hundred thousand numbers took 116 milliseconds and
now take 9.

Every slot being nought is every byte being nought. A slot is eight bytes read
as whichever kind it is, and nought is nought as a number, as a float and as a
handle; a float that is minus nought has a bit set and is written, which is
right, because it is not the same value.

What this stands on is the arena's promise, and the promise is older than this:
`kest_arena_alloc` has always answered zeroed memory and `kest_arena_reset`
clears what it hands back. If that ever stops being true this is wrong, which
is why the two are written down beside each other. `examples/borrow.kest` fills
an array with `true` and answers with which check failed, so a machine that
skipped every fill rather than a fill of nought is caught by running it.


## D219: an array grows where it stands when nothing is above it

A `push` that fills the last slot used to take a new block and copy the array
into it, always. It takes the room next to what it has instead, when what it
has is the last thing the arena handed out and the block it is in has the room.

The copy was never the expensive half. Twenty thousand arrays of a thousand
numbers took 570 milliseconds and now take 543, which is five per cent and
would not be worth writing code for. What it cost was the block it came from:
nothing is freed while a program runs (D012), so every doubling left its old
block behind and an array built by pushing held twice what it holds. That is
now four thousand one hundred and sixty bytes where it was eight thousand three
hundred and thirty-six, which is a frame budget rather than a benchmark.

It is not a special case in the machine. `kest_arena_extend` asks the arena
whether a thing is the last it handed out, which is a question a bump allocator
can answer and nothing else can, and the caller does what it always did when
the answer is no. A loop that fills one array gets it; a loop that fills two,
or one that makes text between pushes, does not, and pays what it paid before.

An array over sixty-four kilobytes has a block of its own, sized to fit, so
there is nothing beside it to take. That block is made bigger instead, which is
the same question asked of the host rather than of the arena: nothing else is
in the block, so nothing else moves, and the caller is told where the thing is
now. What the host gets back is the block a copy would have left behind. A
program building an array of four million numbers reached 34 megabytes and now
reaches 18.9, which is the array and not two of it.

Making dedicated blocks bigger than what was asked for was the other answer,
and it is worse: it is memory nobody asked for, kept in case of a growth that
may never come. Asking for a bigger one at the moment it is wanted costs the
same copy in the worst case and none of it when the host can move the mapping,
which for a block of megabytes it usually can.


## D220: text does not grow where it stands, and an array does

An array grown by `push` takes the room next to what it has when it is the last
thing the heap handed out (D219). Joining text looks like the same shape and
cannot have the same answer.

An array is a handle. The bytes belong to it, one thing points at them, and
moving them is a write to the header that everything holding the array sees at
once. A piece of text is the bytes: what a program holds is where they start,
and what ends them is the nought after the last one. Two names for one piece of
text are two pointers to the same bytes, and neither is told anything.

So writing over the nought at the end of one — which is what growing it where
it stands means — makes every other name for it longer than it was. `let a = t`
before `t = "{t}x"` would leave `a` reading a byte that was never its own. The
machine cannot know whether there is another name, because nothing counts them,
and nothing counting them is what makes text cost what it costs to pass around.

What the language has instead is the array: gather the bytes, which grow where
they stand, and make the text once. The reference has said so for as long as
there has been a `text(bytes)`; what it lacked was the number, which is a
hundred and eight times for six hundred bytes and worse the longer it gets.


## D221: a promise about a host is held by the machine

`extern fn Io.write(value: text) no.alloc` says a host function does not take
from the program's heap. Nothing about that is checkable where it is written:
the host is the one thing in a program this project does not compile, and a
declaration is somebody writing down what they were told.

So the machine checks it, once per call, where the promise is used: what the
heap holds before the host function is called and what it holds after. They
differ only if the host made text or an array, which are the only things it can
do to that heap, and then the call is refused with `K0631` at the line that
made it and the line that called that.

This is what lets `std.io` promise. `io.write` hands a pointer over and makes
nothing, so it can say `no.alloc` and a frame that promised the same can write
something out — which is the whole of what a promise is for, since a frame that
cannot say anything is a frame with a debugger and no print. Every function in
`std.io` promises now, and `check-costs.sh` counts it among the modules it does
not have to weigh because the compiler already proved the answer.

What that cannot do is speak about a path nothing runs, and a promise is at its
most dangerous where nothing runs. So the two hosts in this tree are read as
well: `check-costs.sh` finds what each promised extern is bound to and holds
that function to calling neither `kest_text` nor `kest_borrow`, which are the
two ways a host takes from the program's heap. What a host does by calling back
into the program is not read, because that cost is the program's and `K0631`
already holds it. A promise nothing in this tree provides is counted and named
as such, since a check that passes over what it cannot see looks like one that
covered it.

The cost is two reads of one number on a call that crosses the boundary, and
only when the declaration promised. A host that wants to allocate says so by
leaving the promise off, which is what `Engine.name` in `examples/embed.kest`
does: it makes text, so it promises nothing.


## D222: the frame example runs, which is what an example is for

This supersedes D182, which said the file was checked and not run.

`examples/frame.kest` was checked and not run, and D182 said so. It runs now
and answers with which of its own checks failed, like every other example here.

What it held was the shapes a frame is built out of: two structs that name each
other, a reference that may be nothing, an array of references, and a step that
promises to reach no heap. Resolving those proves the types exist. Running them
proves the instructions do, and the file said in a comment that they did not —
"`ref<T>` and the optional that holds it have a size and a layout, and no
instructions yet". That has been untrue for a long time: a reference kept in a
struct is followed with `get`, an optional one is taken out with `if let`, and
an array of them is walked and read. A comment nothing runs is a comment
nothing corrects.

The two externs it declared and never called went with the change. The compiler
warns about those — `K0506`, nothing calls this, so no host is asked for it —
and a warning in a file that runs is either a mistake to fix or noise to learn
to ignore. What they were there for, a boundary declaration, is exercised by
`examples/host.kest`, which calls what it declares. `check-costs.sh` now counts
no promise in this tree that nothing here provides.

The rule this leaves is worth writing down: a file that only resolves earns its
place only while nothing can run it. Every shape in this one could be run, so
not running it was a hole with a comment over it.


## D223: a program is held to naming its own functions, and a run is not counted

The checker knows which function a name meant, so it can say which functions
nothing named. That is `K0507` for a program — a file with a `main` in it —
and `check-dead.sh` reads the same answer over the library, per function rather
than per name.

What it stops short of is counting what a run reached. A counter on every call
is a store in the hot path of the one thing this language exists to be fast at,
and a second build that carries one is a coverage harness with another name.
This project has no test suite and no benchmark harness on purpose, and the
line is the same here: what can be answered while compiling is answered, and
what would need a machine kept running to watch itself is not asked.

The gap that leaves is real and small: a function named inside a branch nothing
takes is named and never entered. Every example here names what it checks in
the condition of an `if`, which runs whether the body does or not, so the gap
is not where these are. Somebody who wants the other answer wants a different
instrument, and this project would rather say so than half-build one.

`K0507` is a warning and not a refusal, because a host may ask for a function
by name and `kest call` does exactly that. It is said about the file that was
named and not about what it imported, which is what makes it quiet enough to
have: a library checked on its own would otherwise light up from end to end.


## D224: a function is an entry until a host says otherwise, so nothing warns about one

`K0507` said that a program — a file with a `main` in it — had a function
nothing named. It lasted two turns and it is withdrawn. The code is spent and
will not be used again.

What withdrew it was the tree. `examples/embed.kest` has a `main` that runs one
frame on its own, and eight functions the host beside it calls by name:
`heaviest`, `lengthOf`, `between` and the rest. Nothing in the program names
them and nothing should — a host reaches them with `kest_entry`. The warning
was right about the program and wrong about the world, and the pattern it fires
on is the one this language exists for.

A constant and a shape are different, and the difference is not a matter of
taste: a host cannot ask for either. `kest_entry` takes the name of a function.
What a host may lend is a type the program holds in an array, and holding one
is naming it. So `K0508` and `K0509` stand, and there is nothing between them
about functions.

The other half of this is that the sweep now lives in `check.sh`: every `.kest`
file in the tree is held to saying nothing about itself. That is what caught
this. A warning that fires on a project's own examples is either a warning to
withdraw or an example to change, and deciding that by looking at what a real
host does is why the sweep is worth having.


## D225: a name inside a shape is answered, not warned about

`named` is on a bit of a set and a case of an enum now, the same as it is on a
function, a constant and a shape. Nothing warns about one that is false, and
the reason is D224's: a set of bits and an enum are shapes a host lends. A
program that never writes `Hurt` may still be handed one by a host that sets
the bit, and the name is what the boundary is written in.

What changed instead is that the answer exists and can be asked for. A set of
bits was not in `check --json` at all — the text form printed it and the
machine-readable form said nothing, so a tool reading the second one could not
see a type the first one describes. It is there now, with the width it is kept
in and which bit each name stands for, beside the enum's cases and their tags.

The tree has none: every bit and every case in every example, tool and library
file is named by something. That was worth finding out before deciding not to
warn, because a rule nobody can break is a rule nobody needs, and this one is a
rule somebody could break tomorrow with no host in sight.


## D226: a command answers its own question, and reads the file once

`lex` read every file twice. The first reading parsed it, because that is what
loading a file is, and the diagnostics came from there; the second lexed it
again with the diagnostics muted, to have the tokens to print. Two readings and
a parse, for an answer that is one pass over the bytes.

It reads once now. `kest_read_source` reads a file and does not parse it, which
is the whole of what `lex` needs, and the tokens it prints are the tokens it
reported about.

What changes for a reader is that `lex` no longer says what a parser thinks. A
file whose tokens are fine and whose shape is not now lexes without complaint
and is refused by `parse` and by `check`. That is the better answer as well as
the cheaper one: `lex` was saying `expected identifier, found ->` about a
stream of tokens it had no complaint about, and a command that answers a
question nobody asked it is a command that will one day answer it wrongly.

The reading is still reported the same way. A file that is not there, or is a
directory, is `K0701` from the same place it always was.


## D227: a division comes back to its width like every other arithmetic

`+`, `-`, `*` and `<<` are cut back to the type's width after they run, because
each of them can leave it. `/` was not, on the grounds that a quotient is never
bigger than what was divided — which is true of every pair of numbers except
one. The least number over minus one is one past the top of the width, and that
pair is the one C has no answer for.

The machine already knew about it at sixty-four bits and answered the least
number, which is the wrap. At every narrower width the answer came back as a
number that width cannot hold: `i32(0 - 2147483647) - 1` over minus one was
2147483648 in an `i32`. Putting it in a name cut it back and using it where it
stood did not, so the same expression was two answers depending on whether it
went through a `let`.

It is narrowed now, one instruction on the one operator that was missing it,
and the answer everywhere is the wrap: the least number, with nought left over.
`examples/numbers.kest` runs both widths of it, and the reference says what the
answer is beside the other place C has none.

What this leaves is a shape worth remembering: an arithmetic that cannot leave
its width is a claim about every pair of operands, and the one pair nobody
thinks of is the one at the end of the range.


## D228: the arithmetic operators keep their cases, and the comparisons keep the table

Six comparison operators asked the same four questions — text, float, unsigned,
or the plain one — and each wrote its own four instruction names out. That is a
list where a wrong name reads exactly like a right one, so it is a table now
and the six cases are one.

The arithmetic operators are not the same shape and are staying as they are.
`%` has no float form, `&`, `|` and `^` have no float and no unsigned form,
`<<` has one instruction whatever it is shifting and `>>` has two, and `/` is
the only one where a narrow float has an instruction of its own as well as
everything else. A table over those is a table of columns that do not apply,
with a sentinel for the ones that do not exist and a fault where the sentinel
lands — which is more machinery than the six short lines it would replace.

What makes that safe is the checker: `%` on a float, `&` on a float, `+` on
text and `<<` on a float are each refused before the compiler sees them, with
`K0314`. The compiler's own `default` is a fault about a compiler bug, not a
thing a program can reach — every one of those was asked before this was
written down.


## D229: a deferred call is given what its names hold when it runs

`defer f(x)` holds the call and not a copy of `x`. When the block ends, `f(x)`
is run there, reading `x` where it is: a name that changed since the `defer`
was written changes what runs.

The other reading is Go's, where the arguments are worked out at the `defer`
and kept until the block ends. What that costs is a copy per deferred call,
somewhere, and this language does not spend memory quietly: `defer` is a thing
a `no.alloc` function may write, and it stays that way only because there is
nothing to keep.

The two readings agree about what `defer` is for. Giving back what was just
taken names a handle that does not change — `defer give(slots, held)` — and
every use of it in this tree is that shape. Where they differ is a loop that
defers something about the turn it is in, which is a thing to know rather than
a thing to fix, so `examples/borrow.kest` runs it and the reference says it
where `defer` is described.

## D230: the proof that reads what was emitted names every instruction

The second proof of a `no.alloc` promise walks the code the compiler emitted
and asks, of each instruction, whether it reaches the heap. That question used
to be answered by a list of the fifteen that do and a `default` for everything
else. Every instruction there has ever been is in one group or the other now,
and there is no `default`, so an instruction added to the language stops the
build in the one place that has to have an opinion about it.

The `default` was the shape this project forbids everywhere else, and it was
worse here than it is elsewhere. The two proofs are not equals: the first walks
the tree and can name the path a promise was broken down, and the second reads
the emitted code and is what says the first was wrong. A first proof that goes
out of date is caught by the second and reported as `K0405`. A second proof
that goes out of date is caught by nothing — the promise is simply kept by not
looking, which is the failure this pair of proofs exists to make impossible.

So the list that cannot be allowed to go quietly out of date is the second
one's, and it is the one that was written with a `default`.

## D231: the promise's first proof has an opinion about every builtin

The walk over the tree that proves a `no.alloc` promise used to hold a list of
the five names that reach the heap. A builtin outside that list was one it said
nothing about — not one it judged harmless, one it had never heard of.

What that costs is the message. A promise broken by a builtin the walk does not
know is caught by the second proof, which reads the emitted code and says
`K0405`: a fault in the compiler. That is the one message in this language that
blames this project rather than the program, and it would have been printed for
a program's own mistake, with no line naming where the promise was broken.

So the list is every builtin now, each with a reason it reaches the heap or
nothing where it reaches none, and `check-tables.sh` holds it to the names the
checker knows. A builtin added to the language does not compile into a proof
that quietly ignores it; it stops `make check` until somebody says which of the
two it is. `text` is asked about beside the table rather than in it, because it
is a conversion and not a builtin, and the table is held to the builtins.

This is D230 for the other proof: the two of them are the only things that say
a promise was kept, and each has a list that must name everything of its kind.

## D232: what a chunk carries is a thing to look at

The machine checks one promise while running: at a call through a value, where
the second proof stops, it reads what the chunk it is about to enter carries.
That flag is written from the declaration when the chunk is made, and for a
copy of a generic it is written by substituting into a type — and nothing
anywhere could see it. `emit` printed how wide a function is and how deep it
gets and said nothing about the one thing the machine reads.

So it prints it, in both forms: `promises `no.alloc`` on the function's line
and `noAlloc` beside the widths in the JSON. `check-commands.sh` holds the two
forms to each other like every other pair, and holds the chunk to the
declaration `check --json` says it came from — two commands rather than two
forms of one, because that is where a promise could be lost in the making of a
chunk and no program would run differently for it.

A foreign function has no chunk, so there is nothing to hold: what a host
promises is checked where it is called and said as `K0631`.

## D233: where the machine is when it calls into the host is a number

A host function may call back into the program, and what it starts stands above
what is already running. What that costs was left to the host to guess: the
host in this tree doubled both numbers `kest_needs` gave it and said in a
comment that the call made from inside one was its own to account for.

A guess is not what this project tells a host anywhere else, and the number was
there to be worked out. The walk that measures a run of calls stops at a call
into the host, because a host function runs on the host's own stack — so what
it can also answer is where it had got to when it stopped. `kest_needs_from`
gives that: the frames and slots in use at the deepest place the program
reaches a host function. A host that calls back in adds what the function it
calls needs on its own, which is `kest_needs_of` for that one.

The number is an upper bound rather than the exact place: the slots counted are
the whole of the chunk that makes the host call, not the operand stack at that
one instruction, which nothing knows without running it. Erring the wide way is
the only direction that is safe to err in for something a host sizes a stack
from.

It changed what this tree's host asks for from 68 slots and 6 frames to 35 and
3, which is the whole program's own 34 and 3 with the re-entrant call's one
slot on top. The doubling was covering something that never needed covering,
which is the usual fate of a number nobody could check.

## D234: the machine holds itself to what a host was measured to need

D233 gave a host the frames and slots in use where the program calls into the
host, so that a host function calling back in has a number to stand on. A
number a host sizes a stack from is a promise, and the only thing that could
tell a host it was wrong is the machine running out of room in the middle of
something — reported at whatever instruction happened to be there, which says
nothing about where the wrong number came from.

So the machine works the same walk out when it starts and checks it at every
call into the host: the frames in use and the slots between the floor of this
run and the top. Over is `K0633`, and it is a fault in the compiler in the same
words `K0405` and `K0407` use, because nothing a program does can cause it.

The cost is two comparisons at a boundary that already crosses into somebody
else's code, which is the cheapest place in this language to put a check. The
walk itself is done once, when the machine is made, and not per call.

The check is against the whole program's number rather than the entry's: a
machine runs whichever function it is handed, and a host that asked about one
of them asked something narrower than what this holds.

## D235: a host says what it is about to write into a frame

A host fills a frame with slots and calls. `kest_call` sees how wide the frame
is and refuses one too narrow, and that is the whole of what it can see: a slot
holds whatever was put in it and carries nothing that says what that is. A host
that writes a number where the program reads a float hands over a value the
program reads as something else, and every check in this language would pass.

`kest_borrow` has the answer to this already: the host says what it thinks it
is lending and is told when the program disagrees. `kest_frame_fills` is that
at the other crossing — the host says what it is about to write, one kind a
slot, in the order `kest_frame_layout` gives the arguments in, and the program
says what it takes.

Saying what some of the slots hold is refused rather than accepted for the ones
that were said, because a host that stops short has not checked the rest and
would read a pass as though it had. That is `K0634`, and unlike the faults the
compiler reports about itself it is a host's own mistake, said in the words a
host mistake is said in.

What it cannot do is make a host ask. Nothing crosses this boundary that would
carry the answer, so the check is a thing a host does once for each frame it
drives, the way it checks a lend once for each type it lends.

## D236: reading a frame back is the same saying as filling one

D235 let a host say what it is about to write into a frame. Reading one back is
the same mistake in the other direction — a slot holding a float read as a
number of the host's own is a number nobody wrote — so it is the same saying:
`kest_frame_reads`, one kind a slot, over what the function gives back.

They are one walk with two sets of words. What differs between filling and
reading is the layouts it is against, which the chunk has both of, and the verb
in the message: `takes` for what goes in and `gives back` for what comes out. A
function that gives nothing back has nothing to read, and a host saying it
reads a slot out of one is told about the width, because the number of slots is
what it is wrong about.

Two functions rather than one with a direction to pass, because a host writes
one of them where it writes and the other where it reads, and a call that says
which way it means is a call that can say it the wrong way.

## D237: a host hands over words, and the machine lays them out

D235 and D236 let a host say what it is about to write into a frame and what it
is about to read back. Both are a host's word about slots it fills itself, and
the way to be right about slots is not to fill them.

`kest_takes_text` is that: the arguments as words, written the way a program
writes them, read as the types the declaration says and laid out by the
machine. It is the half of `kest_gave_text` that goes the other way — one says
what a frame holds without a host reading a slot, the other fills one without a
host writing any.

The reader was already written. The command line had it, because what is typed
at a shell is words, and it was a static function in `main.c` where no other
host could reach it. It is `kest_value_read` in `value.c` now, which is the
module that owns what a value is, and the command line and the public door are
two callers of one reader rather than two readers that agree until they do not.

What cannot be written as a word is refused rather than guessed at: a struct,
an array, a store, a reference, a handle. A host holding one of those lays out
the slots itself and says what it wrote, which is what D235 is for. Being able
to hand over everything was never the point; being unable to be quietly wrong
about what can be handed over is.

## D238: a thrown-away heap leaves nothing that reads as a handle

`kest_heap_reset` says in the header that every handle the host is still
holding is gone and that passing one back in is reading freed memory. What
happens when a host does it anyway was never decided: the machine reads the tag
at the front of a handle, and whether that read said anything sensible depended
on what the reset had left behind.

A reset now clears what was handed out of every block, and not only out of the
one it keeps. A handle from before it reads as noughts, which is not any kind
of handle, so a host that hands one back is told `K0612` — this is not an array
— at the instruction that used it.

The other way was tried first: an age in the handle's header, checked against
how many heaps the machine has had. It does not work, and the reason is worth
writing down. A handle is a bare pointer, so what carries the age is the memory
it points at rather than the handle itself. Memory handed out again holds the
age of whatever is there now, and a stale handle pointing into a newer object
reads as current — which is the dangerous case and the one an age cannot see.
Making a handle carry its own age would mean making it something other than a
pointer, and every read of an array would pay for it.

So what can be promised is what a reset leaves, and nothing beyond it: a host
that hands back a handle after the machine has given that memory to something
else is reading what is there now, and no check inside the machine can tell.
Under the sanitisers it does not get that far — the arena poisons what it takes
back, so the read itself is caught, one step earlier and harder.

## D239: a handle is asked where it came from, which supersedes D238

D238 made a thrown-away heap clear what it had handed out, so that a handle
kept across a reset read as noughts rather than as the array it had been. That
was the best that could be done while the only question asked about a handle
was what is written at it.

There is a better question, and it is about the pointer rather than about the
bytes: did this machine hand that address out? A heap knows — it is a walk of
its blocks — and it answers no for a handle from before a reset, for a pointer
of the host's own, and for a handle another machine made, which is the one a
host running two worlds has to hand every frame. The tag can be faked by any
four bytes that happen to read as it; where a pointer came from cannot.

It is asked once at a call in, which is the only way a pointer from outside
gets in at all, and not at the instructions that use one. Inside a call the tag
is the whole of it, because what got in has already been asked.

So the clearing D238 added is gone. It caught nothing this does not catch
earlier, and a check nobody can see catching anything is worse than none: the
backstop written for it stopped firing the day this was added, which is how it
was noticed rather than a thing that had to be argued. What survives from that
turn is the clamp on what the reset clears, which was a read past the end of a
block waiting for the sanitised build to reach it.

What is still beyond saying: a handle into memory this heap has handed out
again since. It is in the blocks, so the walk says yes, and what is written
there is whatever is there now.

## D240: a lend ends when the host says it does

`kest_borrow` hands the program a view of memory the host owns, and the header
said the caller must outlive the program's use of it. That is a sentence in a
document: nothing in the machine knew how long a lend was good for, so a host
lending what it owns for the length of a frame had no way to say the frame was
over, and a program still holding the handle read whatever the host did next.

`kest_lend_ends` says it. Nothing is freed — the block was the host's the whole
time — and the header stays where it is, saying what happened to it. Every use
of that array afterwards is `K0637` at the instruction that used it.

The header stays rather than going back to the heap because the alternative is
the program reading whatever is handed out next, which is the thing this exists
to prevent. A lend costs a header for as long as the machine lives, which is
what saying something costs.

Only a lend can be ended. An array the program made is the program's for as
long as it holds it, and a host that has one is holding something it was handed
rather than something it owns. That is refused rather than obeyed, because a
host that could end the program's own arrays could take the ground out from
under a running program.

## D241: a lend the host ended is the next lend's header

Nothing of a lent block is on the machine's heap — the block is the host's —
but the header is, and a host lending a batch every frame leaves one there
every frame. That is a frame budget that grows for a program doing the same
thing every time, which is the one thing this language is for.

So a header the host has given back with `kest_lend_ends` is the header the
next lend is made out of. They are kept on a list linked through the block
pointer, which an ended lend has no use for, so a waiting header costs nothing
beyond itself. A thousand frames of lending and ending cost what one does.

What is reused is the header and never the block. The block belongs to whoever
lent it and this machine has never had an opinion about it.

The cost is D239's line about memory handed out again, and it is the same line:
a handle the program kept reads as ended until that header is lent again, and
as the new lend afterwards. Nothing can tell those apart, here or anywhere else
a pointer is handed back after its owner has finished with it. A host that ends
a lend and keeps handing the handle around is a host holding what it was told
to let go of.

A heap thrown away takes the waiting headers with it, because they were on it.

## D242: text is asked where it came from, and saying it twice pays twice

A call in asks every handle it is given whether this machine handed that
address out. Text is a pointer with no header at all, so it needed the question
more and had never been asked it: a host handing over a string of its own was
undertaking to keep those bytes for as long as the program held them, and
nothing said so or checked.

It is asked now, in the two places a program's text can live: the heap, where
anything made while running goes, and the arena the program was compiled into,
where the text a file wrote lives. A pointer in neither is refused, and the
message names `kest_text`, which is what copies a host's bytes onto the heap so
that what the program holds is the program's.

The command line was handing over `argv` before this, which was true for as
long as that run lasted and is a habit no other host could copy. It goes
through `kest_takes_text` now, which is the door a host outside this library
uses for the same job, so what is typed at a shell is read and copied the way
anything else a host hands over is.

Saying the same bytes twice pays twice, and there is no table of what a host
has said before. Interning would put a lookup on every crossing and a table
that grows on the heap the crossing is being counted against, to save a host
from doing what a host can already do: keep what `kest_text` answered and hand
that back. A name a host says once costs once.

## D243: the machine answers whether what a host kept is still there

A host function is handed the program's values and may keep one past the call.
They last as long as the heap they are on: text is never written over, and a
handle stays where it is even when what it holds grows. What ends them is
`kest_heap_reset`, and the pointer a host is holding looks exactly the same
afterwards.

Only the host that threw the heap away knows it did, which sounds like the
host's own business until it is a host with a name cached from three frames ago
and a reset in a branch it did not write. `kest_still_holds` answers it: the
memory is the machine's, so the machine is what can say whether it still has
it.

It answers about memory and not about what is written there. A lend the host
itself ended is still the machine's memory and this says so — the host that
ended it knows it did, and what this exists for is the one thing a host cannot
see for itself.

What a host may not keep at all is a pointer into what a handle holds. An array
that grows moves its elements; the handle is what knows where they went, and it
is the handle a host keeps.

The other way — the machine refusing to reset while a host says it is holding
something — was not done. It would make the machine keep a list of what a host
has kept, which is a promise no host asked for, and it would put the frame
budget of a program in the hands of a host forgetting to say it had let go.

## D244: what the crossing's question costs is bounded by the answer, not the heap

Every crossing now asks whether this machine handed an address out, and the
answer came from a walk of the heap's blocks. That is a loop as long as the
program has grown, at something that happens every frame — a cost that goes up
because a program has been running a while, which is the shape this language
exists to avoid.

Two things bound it. What all the blocks sit between is kept as the blocks are
made, so a pointer outside it — a host's own string, a handle another machine
made — is refused by two comparisons. And the block that answered last is kept,
because a host handing the same world over every frame asks about the same
block every frame, and that block may be an old one at the end of a long list.

What is left walks: the first crossing of a handle nobody has asked about, and
a host alternating between handles in different blocks. Both are a walk of a
list that is one block per sixty-four kilobytes the program has grown into,
which is the cost this question has and not a cost that grew out of it.

The bounds widen and never narrow while blocks are added. A bound too wide
costs a walk that answers correctly; a bound too narrow answers wrongly, and
the only way to narrow one honestly is to walk the blocks, which is the thing
being avoided. A reset sets them to the one block it keeps.

The arena also keeps the block it started with rather than walking to the end
of the list to find it, which is a walk a reset was doing for no reason beyond
not having written it down.

## D245: the arena is held to what it keeps, in the build that says things

D244 gave the arena four things it keeps rather than works out: the block it
started with, the block that answered last, and what all of them sit between.
Every one of them is a shortcut, and a shortcut that stops being true is a
reset keeping the wrong block, or a handle refused because a bound never
widened. A program behaves exactly the same either way, which is what makes it
the kind of mistake nothing here would have found.

So the sanitised build walks the blocks after every change and holds the four
to what a walk says. It is the walk everything in D244 exists to avoid, which
is why it is in the build nobody runs a frame in — the same build that is
already told what the arena handed out, for the same reason.

It says which of them disagreed and stops there. This is a fault in the arena
and not something a program did, and the machine has no diagnostic to put it
in: what a program is doing when this fails is nothing wrong. A line on the
error stream and an end is what a broken invariant gets, in a build whose whole
job is to end at the first one.

## D246: an allocation arrives as nought, and the sanitised build reads it

Everything above `mem.c` reads an allocation expecting nought: a header whose
unwritten fields are noughts, a length nobody has set yet, a slot nobody has
stored to. Three separate things make that true — a block is taken zeroed, a
reset clears what had been handed out of the block it keeps, and an extension
clears what it gains — and none of them is the whole of it. A fourth place that
hands out memory without clearing it would be a promise broken in a way that
looks like a bug in whatever read it.

So the sanitised build reads every allocation before the caller does and stops
if a byte of it is not nought. It costs a walk of what was just written, which
is the same order as the writing, in the build that already pays for being
told what this arena handed out.

Like D245 it ends the run rather than reporting. What a program is doing when
this fails is nothing wrong, and there is no diagnostic for a promise the
memory made.

## D247: what an arena says it handed out is the sum of what it handed out

`kest_arena_cap` is a ceiling and `handed` is what it is refused against. The
number is kept rather than counted, because a ceiling is asked about at every
allocation and walking the blocks to answer would make an arena slower the
longer a program runs — which is D012's shape and the right call.

What was missing is that nothing held the number to the blocks. A total that
has drifted up stops a program early and one that has drifted down lets it past
what a host allowed it, and neither says anything about where the number went
wrong. It is exactly the kind of mistake that reads as a bug somewhere else.

The two are an equality, not a bound. What a block gives away is what was asked
for, the padding before it — a hole a block is left with has been given to
nobody and can be given to nobody — and the gap the sanitised build keeps after
it. So the blocks' `used` is the total plus one gap per allocation, and the
arena counts its allocations for the sake of saying so.

The sanitised build checks it after every change, beside the four things D245
holds. Both of them are the walk the rest of this file is written to avoid,
in the build that is already paying for a walk of everything.

Where a check like this goes is after the numbers it compares have been
written, not before: the first version of this was placed a line too early in
the path where the host moves a block, and it said the arena had handed out
sixty-five kilobytes less than it had — which was true, for one more line.

## D248: a ceiling says what it refused

A heap ceiling stops a program at the allocation that would have crossed it, so
what a host reads afterwards is a total that stopped short of what it allowed.
The message said the ceiling and the host read the total, and the difference
between them — what the program was reaching for when it was stopped — was
written down nowhere.

That difference is the whole of what a host does next. A frame that missed by
eight bytes and one that missed by a megabyte are the same message otherwise,
and they are not the same problem: one is a ceiling to raise a little and the
other is a program to write differently.

So the arena keeps what its last refusal was asking for, `K0617` says it beside
what the program has and what it was allowed, and `kest_heap_wanted` is where a
host reads it. The three numbers are one number said three ways: what was used
plus what was refused is over what was allowed, and a host can check that as
this tree's own does.

It is what the last refusal asked for and not a list of them. A program is
stopped at the first one, so there is one to know about; a host that carries on
after a refusal and is refused again has the second one, which is the one it is
deciding about.

## D249: a heap that ran out says what was growing

D248 made a refusal say what it was asked for. That is not what a host raises a
ceiling by: something that doubles asks for the double again at the next
allocation, so a ceiling raised by the last refusal buys one more allocation
and the same message.

What decides anything is what was growing and how far along it was. Both are
there where the refusal happens — an array knows what it holds and what each of
them is, a store the same — so the message says it: what it was, how many of
what size it held, and what it was growing to. A host reading that knows
whether the ceiling was nearly enough or whether the program is doubling its
way past any ceiling it will be given.

It is said where the two things grow and not at every allocation that can fail.
A fresh array or a piece of text that did not fit is not growing anything: it
asked for what it asked for, which D248 already says.

## D250: what was being made, where nothing was growing

D249 says what was growing when a heap ran out. Half the ways to run out are
not growth: an array of a million asked for in one go, a piece of text written
out of a value, two joined into a longer one, a store made with room for more
than there is. Those said the number they wanted and nothing about what wanted
it, so a program that asks for everything at once read exactly like one that
arrived there a bit at a time — and the first is a number in the program while
the second is a ceiling.

Every place that can run out says what it was doing now: which of them it was
and how big it was going to be. It is a sentence a piece, not a helper with a
verb passed to it, because what an array is making and what a piece of text is
making are different sentences and a sentence built from parts reads like one.

Writing them turned up one of the same kind of mistake they exist to catch. The
two array sites take their count from different places — one is written into
the instruction and one is what the program said — so the same sentence at both
would print an `i64` through a `%u`. They are two sentences with two widths,
and the check that would have caught it does not exist: nothing in this tree
holds a diagnostic's arguments to the shape of the words it puts them in.

## D251: the compiler reads every message this compiler writes

A diagnostic is a sentence with numbers in it, and the words say what shape the
numbers are. A `%u` handed an `i64` prints a number nobody wrote, and reading
the message does not show it: it is a sentence either way, with a plausible
number in it. D250 found one of those by accident, in a message written that
same hour.

The compilers this is built with read format strings against what is handed to
them, and say so as an error like any other. So the four functions that take a
message and a list — and the machine's own wrapper around them — say which
argument is the words and which is the first of the numbers, and every build
reads every message. A compiler that cannot do this is one this project is not
built with; the attribute is behind a `__GNUC__` and costs nothing where it is
not understood.

It found one on the first build: a message whose words were a caller's rather
than a literal, which is how a `%` in somebody else's sentence becomes an
argument nobody passed.

This is the third list this project holds by making the build stop rather than
by writing a check, after the type tags and the instructions. It is also the
first of those with a hole of its own: the backstops now understand a hole
whose catch is a build that does not finish, which is what a check made of
`-Werror` looks like from outside.

## D252: every list held by the build stopping has a hole

Three lists in this project are held by there being no `default` in a switch
over them: what a value can be written as, what a line may end after, and which
instructions reach the heap. They are the strongest checks here — the build
stops rather than a tool complaining — and until now they were the only checks
with nothing to show they worked.

D251 taught the backstops to expect a build that does not finish. So each of
the three has a hole now: a case added to the enum in a copy of the tree, and a
build that stops naming it. They are the cheapest holes here to write and the
ones that took longest to arrive, because the harness had been built around
running a broken tree rather than failing to make one.

What each proves is different. A type tag nothing says how to write is a value
a program could hold and nothing could print. A token kind nothing answers for
is a line ending somewhere nobody chose. An instruction the second proof of a
promise does not know is a `no.alloc` kept by not looking, which is D230.

That leaves the lists held by a `_Static_assert` and a tool, which have holes
of the ordinary kind already: the tool is what fails, and a tool that fails is
a thing this harness has always been able to see.

## D253: the counts have holes too, and they are the same three lines

Three tables in this project are held to naming everything of their kind by a
`_Static_assert` on how many there are: the token names, the instruction names,
and the names of what a piece of a layout can be. The names themselves are
`check-tables.sh`, which has had holes for a long time. How many there are had
none, and it is the half that goes wrong quietly: a table one name short does
not fail to compile on its own — every kind after the missing one answers to
the name of the one before it, which is a message that names the wrong token
and a disassembly that says one instruction and runs another from there on.

A name taken out of each table is the hole, and the assert's own words are what
catches it. Three of them, three lines each, in the same shape D252 gave the
lists with no `default`.

With these there is nothing left in this project's own table of lists that
have to be complete without something that has been seen catching a break in
it. What holds each row is now one of three things — a build that stops, a
tool that complains, or a run that fails — and every one of them has been
watched doing it.

## D254: a check that reads the source refuses to read nothing

`check-tables.sh` holds the lists that have to be complete by reading them out
of the source with patterns. A pattern that stops matching — a table written
with different spacing, a name that moved, a declaration split over two lines —
finds nothing, and nothing agrees with everything: two empty lists are in step
with each other, and a loop over none of them checks none of it. The check
passes and says so.

Every list it reads now goes through one door that refuses an empty one and
names which list it was. That is thirteen lists, and the door costs a line each.

It is the same shape as the counts D253 gave holes to. A table one name short
compiles; a pattern one shape out matches nothing. Both are checks that stop
checking without stopping.

What this cannot catch is a pattern that matches less than it should rather
than nothing at all. That one is caught by what the list is compared against —
a name missing from one side and present on the other is what these comparisons
are for — and the empty case was the only one where both sides fell silent
together.

## D255: a check that reads documents refuses to read nothing

D254 held every list `check-tables.sh` reads out of the source to being found.
`check-docs.sh` reads the documents the same way — the blocks of Kest they
show, the messages they print, the JSON they hold, the decisions that are
written — and a fence written another way or a heading renamed would have left
it holding nothing to anything and saying so in a count nobody reads as a
failure.

Every sweep refuses to find nothing now. What is different from D254 is how it
is seen to work: no single edit to a document empties a sweep, because what
would do it is every fence at once. So `check.sh` asks the check about a
document with nothing in it, which is what a pattern that stops matching looks
like from outside, and requires it to refuse and to refuse for that reason.

That is a probe rather than a hole, and it belongs with the others `check.sh`
writes on the spot: a file that holds nothing, a file whose lines end the way
another machine ends them, a `main` that gives nothing back. No document in
this tree is empty and none can be made so to ask this, which is exactly why
the question is asked with one written for it.

## D256: a check reads all of what it reads, or says how many it missed

D254 and D255 held two checks to finding something. Finding something is not
enough. `check-costs.sh` reads what a host binds with a pattern, and a pattern
that reads most of them leaves a promise nobody is holding to anything —
without the count going to nought, so nothing said a word.

So where the thing being read is countable in the file, the count is compared:
how many `kest_host_bind` a host has against how many this reads. That is the
whole of the difference between a check that covers something and one that
looks as if it does, and it is a line.

It found one the first time it ran. The pattern for a bound name was letters
and dots, and `Math.atan2` has a digit in it, so the one promise this project
makes about a host function with a number in its name had never been read.
Fourteen promises are held now where thirteen were, which is the same thirteen
plus the one nobody could see was missing.

The lesson is the older one from D253 in a new place: a list one short does not
look like anything. What made it visible was counting what should be there
rather than believing what was found.

## D257: what a library makes, held against what its headers declare

The declarations check reads headers with patterns and holds what they declare
to being there and to being called. It never asked the other half: what the
library makes that no header declares. Nothing can call such a function, so
every check about declarations passes over it in silence — and a declaration
written in a way the pattern cannot read looks exactly the same from there.

That half is answered by `nm` rather than by a pattern, which is what makes it
worth having beside the others: the objects say what was made, and a name in
them that no header declares is either a function nobody can reach or a
declaration nobody can read. Both are worth a line.

It found two names that were neither: statics wearing the public prefix.
`CLAUDE.md` says an internal function is plain snake_case and this is why — a
reader looking for where `kest_nearest_type` is declared finds nothing, and
cannot tell a private name from a declaration that went missing. They are
`nearest_type` and `fn_of` now, and a name only one object can see wearing the
public prefix is said about from here on.

A compiler that splits a function into pieces names them after it with a dot in
between, and those are the same function under another name. They are passed
over, which is the one thing here that knows anything about a compiler rather
than about this project.

## D258: a check handed nothing says so

Three checks here read the files they are handed. Handed none, every sweep in
one of them runs no times, every count it prints is nought, and what it says at
the end is that everything it looked at was right. `check.sh` builds those
lists by finding files, and a find that comes back empty is the whole gate
passing without reading a line.

So the three refuse an empty list, and `check.sh` refuses one of its own before
it starts. There is no number to hold them to — a count is the thing that goes
stale — but there is a floor, and the floor is one.

This has no hole, and cannot: the guard is the only thing that would catch its
own absence. It is a probe instead, beside the document with nothing in it that
D255 put there for the same reason, and the same shape — the check is handed
what nothing in this tree is, and has to refuse.

Every hole that runs one of those checks now names a file for it, which is what
a check that reads files should always have been given.

## D259: what was heard is held to what was asked

`check.sh` asks nine checks at once and reads what they said afterwards, out of
a file each one writes. A run that never started writes no file — a shell that
could not fork, a directory that could not be written to, a tool that is not
executable — and a file nobody wrote reads exactly like a check that had
nothing to say. The gate would print eight lines instead of nine and pass, and
counting the lines is a thing nobody does.

So the name is written down where the asking happens, and at the end the names
asked are held against the names answered. One that was asked and said nothing
is said about by name.

This is the third guard here that cannot have a hole, after D255 and D258: what
would catch its absence is itself. It was watched working in a copy of the tree
with one check made to leave no answer, which is what a hole would have done,
and what is left in the tree is the guard rather than the watching.

The other half of the pair — a check that is never asked at all — has been held
for a long time by `check-tables.sh`, which holds the files in `tools` against
what `check.sh` reaches for.

## D260: the gate says what it did, and is held to the list

`check.sh` does eleven things itself before it asks the nine checks in `tools`,
and five of them said nothing when they passed: a file written on the spot that
is refused as it should be, a host that asks before calling, every module line
against where its file is, the library read as one project, a check handed
nothing. Silence is what those looked like, and silence is also what a line
deleted from the middle of the file looks like.

So every one of them says a line now, and `CLAUDE.md` holds the list of what
the gate does beside the tools it asks. `check-tables.sh` holds the two to each
other, which is the fourth list of checks it holds after the files in `tools`,
what `CLAUDE.md` names, and what `check.sh` reaches for.

A run of the gate is now a run of names: what it prints is what it did, and
what it did is what somebody wrote down. A check that stops happening stops
being printed, and something says so.

The counts in those lines — how many files, how many runs — are printed and
held to nothing, because a count is a thing that goes stale. What is held is
that the line is there at all.

## D261: the gate builds before it reaches, and what it built answers

Half the probes in the gate pass when a command fails: a program that must be
refused, a lend that must not be taken, a check that must say no. For those, a
binary that is not there is a pass. Everything in `check.sh` runs after the
build today and nothing said that it must, so the day somebody moves a probe up
the file it would keep passing and mean nothing.

Two things hold it now. `check-tables.sh` reads the gate and holds the first
line that reaches for what was built to coming after the line that builds it,
which is the one thing about the gate that is an order rather than a list. And
the gate asks the four things it built whether they are there and whether they
answer, because `make` saying nothing is not the same as there being something
to run — a binary that cannot start would otherwise be every check below it
reporting its own confusing failure.

The order rule is written against the build rather than against the line that
says the build happened, because the build step tests what it made and that
test is a reach of its own. It is the same distinction as everywhere else here:
what a check is allowed to do is not what a check is for.

## D262: the Makefile is read like everything else

`make check` is what "it passes" means and the `Makefile` is the file nothing
here had ever read. What it says is what a reader is told to type, what the
gate builds, and what is left on a machine afterwards, and every one of those
is a list that can go one line short in silence.

Three things are held now. Every target `CLAUDE.md` tells a reader to type is a
target the file has. Everything the gate builds is something `clean` removes —
the list comes out of the gate itself, so a fifth thing built is a fifth thing
to clean and nobody has to remember. And every file an install puts on a
machine is one an uninstall takes away, because an install and an uninstall are
one thing said twice and the second is the half nobody runs until it matters.

What is not held is that the rules build what they say: that is what building
is for, and it fails loudly. This is about the lines that only matter on the
day somebody runs them.

Writing the sentence in `CLAUDE.md` broke the check that reads it, which is the
right kind of accident: the words `make something` in a sentence about targets
read as a target. The rule reads what a reader is told to type, so what it
reads has to be typed the way a reader would type it.

## D263: what a check is, written down

`tools` is nine files and nothing said what one of them is. A tenth would copy
the shape of whichever it was written beside, and the shapes differ: some make
somewhere to work and some do not, some say what runs them at the top and all
of them happen to, and one of them once wrote to a fixed name under `/tmp` —
which is a gate that failed one run in six for no reason anybody could see.

So the shape is written down as a check rather than as a paragraph. A check is
something to run, it says what runs it, it stops on a name nobody set, it
writes where nothing else writes, and it takes away what it made. Five lines to
read and five things a tenth check is held to on the day it is written.

The file that holds broken copies of the others is exempt from the one about
fixed names, because what it has in it are quotations of code — a name written
there is one it is asking about rather than one it writes to. That exemption is
in the code with its reason beside it, which is the only kind this project has.

## D264: what a check says it did is a line, and only a line

`check.sh` reads the last line of a check as what it did. That is a convention
nothing held: a check that printed nothing would leave a blank where a sentence
goes, and one that said what it did and then said something else would be read
as the something else — a detail line shown as a summary, a gate that looks
like it passed differently.

So the last line is held to being there and to being a summary. A detail in
this project begins with a space and a summary does not, which is the shape
every check here already writes, and now the shape they are held to.

This is the fourth guard the gate makes about itself with no hole of its own,
after D255, D258 and D259. Each was watched working in a copy of the tree, and
`CLAUDE.md` says so once rather than each decision saying it again: what would
catch one of these missing is itself.

## D265: a hole is caught when the check says it, refuses, and says it first

A backstop was caught when the broken tree said the words. Two things were not
asked. Whether the check refused: a check that says what is wrong and comes
back nought is a gate printing the complaint in the same green as everything
else, as though it were what the check had to say for itself. And whether it
said it first: a reader with a failing gate reads the first few lines under the
name, so a complaint below a summary or below thirty lines of detail is an
answer further down than anybody looks.

Both are asked now, of every hole. All fifty-nine already did the right thing,
which is the answer worth having — what this holds is that they go on doing it,
and that the sixtieth does it too.

It was watched failing by making the tables check come back nought while still
saying everything it says: two holes said their words and were called misses,
which is what a check with a broken exit would look like from here.

## D266: the gate is made cheap by what it does not copy, and still not timed

`make check` puts fifty-nine broken copies of this tree out of order, and each
copy was the whole tree: the sanitised objects, both built hosts, everything.
Most holes want none of that — a hole about the compiler wants the compiler,
and the nine megabytes of sanitised objects beside it are carried for nothing.

A copy takes what its hole asked for now. The sanitised objects come only when
the hole asks for a sanitised build, the two hosts never come because they are
built into the copy that wants them, and what no build writes into is the same
bytes under another name where the machine allows a name to be that. Where it
does not — a scratch on another filesystem, which is where this ran — it is a
copy again and what it saves is nothing rather than everything.

Linking rather than copying means a file in the copy and a file in the tree can
be one file, so a broken file is written by making a new one where the old name
was rather than by opening that name and cutting it short. Opening it is
opening the tree's own.

It was looked at once while this was written and the number is not here. That
is D012's rule and this is the first time it has been tested by wanting to
break it: the gate is fast enough to run, what was made cheaper was made
cheaper because it was obviously wasted work rather than because a number said
so, and a second number would be a thing to keep true from now on.

## D267: a hole the build catches names the object that must refuse

Seven holes here are caught by a build that stops: a list with a case nobody
answered for, a message whose words disagree with its numbers. Each of them
asked for the whole compiler to be built, which compiles whatever comes before
the file the hole is about and then stops — work nobody wanted, and worse than
that, a catch that does not say where it came from. A build that stopped for
some other reason would have counted.

Each names the one object that has to refuse now. `make build/release/value.o`
either compiles that file or does not, and when it does not, what stopped it is
the file the hole is about.

The saving is small, because a build that stops stops early anyway. What is not
small is that the catch means what it says: the file, the message, and nothing
in between.

## D268: the two ceilings a machine has, reached

The ceilings check reached the three a program can be told it has — an array,
a store, a piece of text, each at what `len` can count to — by lowering the
number in a copy of the tree, because the real ones are a minute and four
gigabytes away.

It reached neither of the two a machine has. How deep calls may nest and how
much stack there is are the host's numbers, not the program's, and a program
reaches both in a moment: a function that calls itself a hundred thousand times
deep, and one that does the same while holding enough that the stack runs out
first. They are the two messages a host embedding this is likeliest to meet,
and they were the two nothing here had ever seen.

Both are reached now, with the tree's own compiler and no copy at all. The
second is the same ceiling from the other side — what a call needs is what it
holds and not how many of it there are — which is why it is a frame wide enough
rather than a call deep enough.

The copy the other three need carries what it builds now: the release objects
and not the sanitised ones, and names linked rather than copied where the
machine allows a name to be a second one for the same file. The number that was
lowered is lowered with a tool that writes a new file over the old name rather
than opening it, so the tree's own is left as it is either way.

## D269: the heap ceiling is reached by a host written for it

Six numbers stop a program while it runs and five of them were reached. The
sixth is the heap a host says the program may have, which is the one this
project talks about most: it is the number a frame budget is made of, and the
only thing here that had ever reached it was the engine, which asks for a
megabyte and spends it in the middle of doing something else.

There is no way to reach it from a command line, because how much heap a
program may have is a host's to choose and this command line does not choose.
So the check writes the host: twenty lines, a heap of sixty-four kilobytes, a
program that grows an array, and the message read back rather than printed.

What it holds is the whole of that message and not the code alone — that it
says how much of what it was given has been used, that it says what this asked
for, and that it says what was growing. Those three are D248 and D249, and
before this the only thing that read them was a host in `examples`, reading its
own report out of a temporary file.

## D270: a machine that cannot be made says which number it was

A host gives a machine a stack and a depth, and both are taken before anything
runs. Asking for more than the machine underneath can give came back as
nothing: `kest_start` answered NULL, and a host with a number too big and a
host with a program that would not compile got the same nothing, which is a
host halving the wrong number forever.

Which one could not be had is said now, with the number that was asked for.
`K0638`, in the words a host mistake is said in, because it is one.

Reaching it is the interesting part. On a machine that lets a program ask for
memory it will never touch, a stack of four billion slots is granted and
nothing goes wrong until something walks that far, which the depth ceiling
stops first. So the check asks for it under a limit on what the run may take:
a gigabyte, and a stack of sixty-four. What it holds is the message, and what
it needed to hold the message was a machine small enough to refuse.

That is the seventh number a run can be stopped by, and the first that is about
what a host asked for rather than what a program did.

## D271: nothing has a reason, on both sides of the boundary

Three doors here answer nothing when the machine underneath has nothing to
give: `kest_build`, `kest_host_new` and `kest_start`. D270 gave the third one
its reason. The first said nothing at all, so a host with a program that would
not compile and a host on a machine with no memory left got the same NULL and
the same silence; the second has nowhere to say anything, which is a different
problem with a different answer.

`kest_build` says it now, in the form the caller asked for, written by hand
because what writes a diagnostic is the arena that could not be made. `K0705`,
in the family of what cannot be read, because a program that cannot be read for
want of memory is one that cannot be read.

`kest_host_new` answers nothing and has no diagnostics to say why, so what it
gets instead is a boundary that refuses to be written through: binding into
nothing is false rather than a host's unchecked answer made worse. That is what
a boundary owes a caller when it cannot say anything.

The command line has said `out of memory` for as long as it has existed and
nothing had ever seen it: a hole that makes an arena unmakeable now reaches it,
which is the only failure in this project that is about the machine underneath
rather than about a program or a host.

## D272: the diagnostic with no arena is written where the others are

`K0705` is said by a build that could not be opened, which is the one place
here with no arena to make a diagnostic in. It was written by hand in both
forms, in `build.c`, beside nothing else that writes a diagnostic — so a name
changed in the writer every other diagnostic goes through would have left this
one saying the old name, in the only message a host reads when it has nothing
else to read.

It is written in `diag.c` now, beside the writer, and `build.c` hands it a code
and a message. There is nothing to hold the two to each other because there is
no longer a second one: what a shape written twice needs is a check, and what
it needs less is being written twice.

## D273: a diagnostic said two ways is one diagnostic

Every diagnostic is written twice: once in words with a caret under the place,
and once as JSON for whatever reads it after. Nothing held the two to each
other. A fix shown in the words and left out of the JSON is a fix nothing
machine-readable knows about — an editor offering nothing where a reader is
offered a name — and a note in the JSON that the words do not show is a place
nobody is told about.

They are held now: the same codes in the same order, the same messages, the
same first place, the same fix, and the same notes with the same places. The
words are read the way a reader reads them — a code line, an arrow to a place,
a caret with what is said about that place after it — because what is being
checked is what a reader is shown and not what the writer meant to show.

The file it happens on is written on the spot, because no file in this tree is
wrong and this needs one that is wrong in three ways at once: a name that is
nearly another, a function declared twice, and a body that calls neither.

## D274: every command that says a diagnostic is asked the same question

D273 held the two forms of a diagnostic to each other for `check`. Three other
commands say diagnostics — `run` when a program goes wrong, `tick` when it goes
wrong in an event, `call` when there is nothing of that name to call — and
each writes what it says around them differently: a status, what crossed and
what the heap did, what came back.

A diagnostic is the same thing whichever command it came out of, so the
question is asked of all four now. It found the shape a diagnostic has when it
has nowhere to point: `call` says its fix on a line of its own, indented and
under no caret, because there is no place to draw. Reading the words the way a
reader reads them means reading that shape too.

What the commands say around the diagnostic is not compared, because that is
what each command is for and is held elsewhere. What is compared is the
diagnostic.

## D275: what a frame cost is the same number in both forms

`tick` says what a frame cost: how many times the boundary was crossed, what
came back from each side, and what the heap did. That is the whole of what the
command is for, and it is written twice — once padded into a line for a reader
and once into an object for whatever reads it after — with nothing holding the
two to each other. `check` and `emit` have been held that way for a long time;
this was the one that was not.

Both are read now and compared name by name. A number that differs by one in
the JSON is caught, which is what a number that drifts looks like: not a
missing field, not a broken shape, one number that is not the other.

Reading the words means reading them as they are printed, which is padded into
columns — the first version of this looked for one space where the line has
three, and found nothing to compare rather than a disagreement. A check that
reads what a reader is shown has to read what is actually there.

## D276: what a tick threw away is a number, and the numbers mean something

Two forms of the same wrong number agree with each other. D275 held what a
tick says to being the same in words and in JSON, which catches drift and
nothing else: a peak that is not the most the heap held is the same lie twice.

So the numbers are held to their meanings as well. As many crossings as there
were events, one crossing for the batch, and a peak that is at least what the
heap was holding at the end — the last of which is true whether the heap was
thrown away between events or never at all.

And a tick that throws the heap away says how many times it did. `heap 0 bytes,
none of it freed` was what a run that allocated nothing said and what a run
that threw everything away said, which is true of the first and the opposite of
the second. It is `thrown away 3 times` now, and `"thrown": 3` beside the heap
in the JSON, nought when nothing was thrown away.

## D277: a tick says what it was run over

`kest tick file 4,5,6` lends three numbers and `kest tick file 3` counts three
up from nought, and what either printed afterwards was the same four lines: how
many crossings, what came back, what the heap did. Two runs of the same shape
over different events are two measurements, and a program whose answer depends
on which events it was given is one nobody can read the numbers of without
knowing which they were.

So it says. In words, `events 3 lent: 4, 5, 6` or `events 3, counted up from
nought`; in JSON, an `events` object with the count and the numbers, or the
count and null where they were counted up. Null rather than the numbers,
because a thousand events counted up from nought is a thousand numbers a reader
already has and a tool can make.

What holds it is the same pair of questions as the rest of a tick: the two
forms say the same thing, and what they say means something — as many events as
there were crossings.

## D280: what can be written and what writes it are one list in two places

Two switches say which types a value of can be put in a hole: the checker's,
which refuses a program that asks for one that cannot, and the machine's, which
writes the ones that can. Each names every tag and has no `default`, so a tag
added to the language stops the build in both — and neither was held to the
other. A tag moved from one side to the other in one of them compiles, and what
a program gets then is `<no text>` where it asked for a value, or a refusal for
something the machine writes perfectly well.

They are read out of the source and compared now. The comment in `types.c` has
said "the two lists are what has to agree" since the day it was written; this
is the day something agreed them.

One tag is left out of both sides with its reason: the error type. The checker
says it can be written so that a program already wrong is not told twice, and
the machine never meets one because a program with one in it does not run.
Neither of those is about what can be written down.

## D281: a number written down is read back

What the writer promises about a float is the shortest spelling a reader gets
the same number out of. That is a promise about reading, and what held it was
an example quoting the digits `0.33333334` — digits that stay right while the
promise goes wrong, because a writer that stopped looking after six digits
would print `0.333333` and every example would still pass.

So a number goes out through the writer and comes back through the reader now.
Ten of them, five in each width: a third, a tenth, a large negative with a
fraction, a number with an exponent, and one wide enough to lose its tail. The
program says whether what came back is what it had, which is the only question
worth asking about a spelling.

Two of them cannot be compared and are asked what they are instead: what a
division by nothing makes, and what a number that is not one makes. Nothing
equals a number that is not one, so equality is the wrong question and being
one is the right one.

## D282: the reader the promise is about is a host's

A number is written the shortest way that reads back as the same number, and
D281 held that by reading it back — with this project's own reader, which is
the one the writer chose the spelling with. A writer and a reader that agree
with each other agree whatever either of them does.

The reader that promise is about is the one a host already has. So the host in
this tree takes what a program answered, asks the machine for it as words, and
reads those words back with `strtod`: the whole line, nothing left over, and
the same number to the width the program had it in.

What it catches is a spelling no host can read — digits with something after
them, a form a C reader does not know. What it cannot catch is a machine whose
`strtod` is not this one's, which is the same limit every promise about text
has.

## D283: a block lent twice is taken back once

A host may lend the same block twice — the same rows to two calls, a buffer as
two views — and what it gets is two handles over one block. What it takes back
is the block. Ending one of them left the other alive, reading memory the host
had said it was finished with, which is the thing ending a lend exists to stop.

So the machine writes down what it has lent, and ending a lend ends every
handle over that block. The list is on the heap beside the headers, so a lend
costs a header and a place in a list, and a heap thrown away takes both; a
handle removed from it when the lend ends means the list is as long as the most
that were lent at once and no longer.

Two handles over one block are two handles, not one: a host that lends twice
gets two, and the second is not the first with a second name. What makes them
one thing is the block, which is the host's, and it is the host that says when
it is finished with it.

## D284: what a program copies out of a lend outlives the lend

A lend is the host's memory and costs the program nothing. The one place that
stops is `text` of a lent run of bytes, which copies them onto the heap where
everything else the program holds lives — and the number said so already: text
of a lend costs at least what the run holds.

What nothing said or showed is why that matters, which is what happens
afterwards. The host in this tree makes text out of what it lent, takes the
lend back, writes something else into the block, and asks the machine whether
what the program is holding is still there and what it says. It is, and it says
what the bytes said when they were copied.

That is the shape of every promise about a lend read from the other end: the
block is the host's and what came out of it is not the block.

## D285: what a host takes back is memory, not an address

D283 ended every handle over the block a host handed back, which was the block
found by its address. A host that lends the tail of a block on its own has two
runs that share their ends and two different addresses, and ending the whole
left the tail alive over memory its owner had finished with.

What a host lends is a run of bytes and what it takes back is all of it. So a
lend ends every handle whose run touches the run being ended, which is two
comparisons per lend the machine is holding and no more than the walk that was
already there.

Two views of one block under different names are the same case with the same
answer: they overlap, so they go together. The types they were lent as never
enter it, because what is being taken back is not a type.

## D286: the host's word is weighed where it can be weighed

A lend says how many there are, and that number is the host's word. A build
that ships cannot weigh it: the block is the host's and its end is written down
nowhere this library can read. A host that lends four rows out of an array of
two hands the program a run it can walk off the end of, and every check here
about lending has been shaped around not being able to say so.

The sanitised build can say so. It is told where every block a host has ends,
which is the one thing about somebody else's memory a library cannot work out
for itself, so the lend is weighed there and refused where it is made rather
than found where it is read. It is the same shape as the arena's poisoning: the
build whose job is to catch this is where it is caught.

The message is a lend's message, `K0610`, because it is a lend that is wrong
and the host is who is wrong about it. What it says is what a host can act on:
how many it said and that it does not have that many.

Nothing changes in the build that ships, and nothing pretends otherwise. What
this buys is that every host written against this library is run once under a
build that would notice.

## D287: where a lend starts is the host's word, and only arithmetic is weighed

A lend is an address, a count and a name. D286 weighed the count where the
build can weigh it. Where it starts is asked one question and one only: whether
a value of that type may sit at that address. That is arithmetic — an address
and an alignment — and a field read across a word boundary is a read the C
standard has no answer for, so it is refused.

Nothing else about where can be asked. A lend that starts in the middle of a
row is aligned, is inside the block, holds as many as it says it does, and is
not what the host meant; the bytes are the host's and what they mean is the
host's word. A library that guessed at meaning here would be inventing a rule
its caller never agreed to.

So the refusal that is there has a hole of its own now, and the one that cannot
exist is written down as not existing. What a crossing cannot check is worth
saying as plainly as what it can, because a reader who does not find a check
assumes there is one somewhere else.

## D288: a lend names one type or is refused, and it has a check of its own

A lend is an address, a count and a name. The first two are held as far as they
can be. The third was written and never reached: two modules may each declare a
`Row`, and a host writing `Row` means one of them — the machine refuses that
and says which two it meant and what to write instead, and nothing in this tree
had ever seen it, because every program here is one module.

So the program is written by a check: three files, two of a name, and a
ten-line host that lends to it. That check is `check-lends.sh`, the tenth in
`tools`.

It started as a probe inside the gate, where the other written-on-the-spot
hosts are, and moved out for one reason: a hole that breaks the refusal needs
something to catch it, and the only thing that could was the whole gate. A hole
that runs the gate runs everything the gate runs, including the holes, which
took the backstops from twelve seconds to thirty-seven. A check of its own is
run by the gate like the others and by a hole on its own, which is what the
shape of this project is for.

## D289: the two ways an import may not resolve are run

An import is a path. Two things can be wrong with one and both are refused: the
file is not there, and the file is there and calls itself something else. The
first is `K0701` and the second is `K0703`, and neither had ever been run —
every file in this tree is where it says it is, and the reference quoted the
second without anything making it happen.

They are written on the spot now, beside the other programs that are wrong on
purpose: two files where the imported one calls itself another name, and one
importing a file that is not there. They live in the commands check, which is
where the written-on-the-spot refusals are and, more to the point, where a hole
can reach them without running the whole gate.

The turn began on a premise that was wrong. `examples/game.kest` imports
`examples/game/npc.kest` and has for a long time, so a program of more than one
file was already here and already run. What was missing was not the example but
the refusals, which is a different thing in the same place.

## D290: where a package starts is run from four directories down

A file settles where the package directories start by having its own name taken
off its path: `module a.b.c` at `x/y/a/b/c.kest` means the root is `x/y`. The
reference says it and a comment in an example says it, and nothing had ever run
it — every program in this tree is named from beside its own package, where the
rule and the file's own directory give the same answer.

A program four directories down is where they differ. It is written by the
commands check now: two files under `x/y/a/b`, one importing the other by the
name they both live under, run by naming the deep path. If the root were the
file's own directory the import would look for `x/y/a/b/a/b/d.kest`, which is
what the hole makes it do.

The reference needed nothing: it already said the rule, which is what reading
before writing is for.

## D291: the library is found from where the command is, and that is run

`std` resolves from a path built out of the name the command line was run
under: beside the binary in a source tree, beside its directory once installed,
and where it was installed to when neither is there. `KEST_LIB` says otherwise
and overrides all of it.

Every check here ran `./kest` from the root of this tree, where the path to the
command and the directory somebody is standing in are the same thing — so the
rule and the mistake give the same answer, and the mistake is what anybody who
has installed this would meet. It is run from somewhere else by its whole name
now, which is the only way that difference shows.

The other half is what happens when a library is named and is not there: a
message about the library, saying where it looked. That is `K0701` like any
file that cannot be read, and what makes it useful is the path in it — a host
that set `KEST_LIB` to the wrong place reads its own mistake back.

## D292: this project installs itself somewhere and runs what it installed

`std` can be in three places and two of them were run. The third is where an
install put it — `../lib/kest/` beside the binary's own directory — which is
the one every person who installs this meets, and the one nothing here had ever
been in, because nothing here had ever installed anything. What held `make
install` was a check reading the `Makefile`: the lines being there rather than
the files arriving.

It installs into a directory of its own now, runs what it put there from
somewhere else on a program that imports the library, and takes it away again,
holding that nothing is left. Three commands, no privileges, and the same
`DESTDIR` a package build would use.

That closes the three ways a library is found. What is still read rather than
run is the fourth, the path compiled in, which is where a build says the
library will be before anybody has put it there — and running that would mean
writing into the machine this is built on, which no check here will do.

## D293: where a build says the library is and where an install puts it

The last place a program looks for `std` is a path compiled into every object.
Where an install puts the library is a line in a rule. They are the same path
said twice, and nothing said so: a build told one and installed to the other
finds no library and reports from a path nobody can fix by moving anything,
because the path it names is not where anything is.

They are read out of the `Makefile` and compared now — every place the install
rule puts them, not one of them, because a rule that makes a directory in one
place and copies into another is two paths and both have to be the one the
build was told. The first version of this compared one and the hole walked
straight through it.

That is the fourth way the library is found, and the only one that is read
rather than run. Running it would mean writing into the machine this is built
on with the prefix a real install uses, which no check here will do.

## D294: which library a program gets when there are two

A tree being installed has two libraries: the one beside the command somebody
just built and the one under the prefix. Which a program reads is the order the
search asks in, and every check here but this one runs somewhere only one of
them exists — so the order was right or wrong without anything changing.

It is run now. Three libraries that differ by one function, a command with one
beside it and one under its prefix, and the answer says which was read: the one
beside the command, which is the one somebody just built. A library named by a
host beats both, because saying where it is is the only way to be sure.

The reference had the order and had it short by one place; it says all four
now, and says why the first of them is first.

## D295: a library has no version, and a name that is not there says which

There is no version on a library here and nothing for one to protect. A library
is Kest source, compiled with the program every time it runs, so a program read
with a library that is not the one it was written against cannot call into
something else: it asks for a name that is not there and is refused before
anything runs. That is the answer to the question a version scheme is usually
bought to answer, and it costs nothing because it is what compiling from source
already does.

What was missing is the other half of the message. `io` has nothing called
`print` says which name and where it was asked for, and said nothing about
which `io` — and a program read with another library has exactly one question,
which is which library. It carries a note now, pointing at the file that module
was read from.

The note points at the first thing declared under that module, because a module
is a file and any line of it names the file. Which line is arbitrary and the
file is not.

## D296: a module is a name a reader can get wrong

What a file writes as often as it writes anything is the name in front of the
dot. A misspelt one got no suggestion at all: the nearest-name search knew
locals, builtins and everything declared, and a module is none of those — it is
a file, and nothing declares it.

It knows them now, worked out from the names registered under them, because a
module that has something in it is a module a file can write. `ioo.print` says
`did you mean \`io\`?`, which is the answer to the only question that message
leaves.

Module names are only compared against a name written without a dot in it, and
the same distance every other suggestion uses decides. A name that is nearer to
something declared still gets that: what changed is that the list is no longer
missing a kind of name a reader writes every day.

## D297: a suggestion says what it knows, which is sometimes two names

The nearest name is one name, and two names are often exactly as near: `health`
and `wealth` are both one letter from `xealth`. What was said was whichever the
search met first, which is choosing for a reader and not telling them there was
a choice.

Both are said now. More than two level and nothing is said at all, because a
list of names is not a suggestion and this project has held since it started
that a wrong suggestion costs more than none.

The same word offered twice is one answer, not two. A name reachable under its
module and by its last piece is written two ways and meant once, so what the
search compares is the words rather than where they came from.

## D298: a short name is answered for, because a wrong answer cannot happen

Nothing shorter than three letters was ever suggested for, and the reason was
sound: every short name is one edit from every other, so what a reader would
get is a name picked out of a crowd. `io` is two letters and a file writes it
on every line that says anything.

D297 took the crowd away. Two names equally near are both said, and three or
more are said as nothing at all — so a short name either has one answer or has
none, and neither of those is a wrong answer. What the length rule was
protecting against cannot happen.

So two letters is the shortest a name can be and still be answered for, and the
distance stays one letter until a name is six long. One letter is still nothing,
because everything that size is one edit from everything else and a reader
would be handed a list that says nothing about what they meant.

## D299: two letters the other way round is one mistake, and now it is asked

The commonest way to write a name wrong is to write two of its letters the
other way round, and an edit count says that is two mistakes: take one out, put
one in. Any reader says it is one.

The machine has counted it as one since the distance was written — the row
before the last one is kept for exactly this — and nothing had ever asked it
to. `pirnt` for `print` is the question, and it is asked now.

The premise this turn started from was that the count got this wrong. It does
not, and reading the code came before writing any. What was missing was not the
rule but anything holding it: a line that could be deleted with every check
still passing, which is the definition this project uses for something that is
not held.

## D300: the longest name a suggestion is measured over

The distance between two words is worked out in three rows of a table, and the
table was sixty-four wide. Names are compared qualified — what somebody wrote
against `examples.game.npc.Npc` — so sixty-four is a length a real name
reaches, and a name past it was near nothing with no word about why: no
suggestion, and nothing to say one had been looked for.

It is two hundred and fifty-six now, which is past anything a reader writes
down twice, and three rows of it is three kilobytes of a stack nothing else is
using. Past that a name is still answered for with nothing, which is the honest
end of any ceiling: what changed is where it is, not that there is one.

The comparison gives up as soon as two words are further apart than the limit,
so a wider table costs nothing for the short names everything else is.

## D301: a line that cannot fit is not a reason to stop arranging it

A name may be longer than a line. The formatter cannot break one — half a name
is a different name — so a line holding it stays long, and what it does about
everything else on that line was undecided: nothing in this tree has a name
that long, so both answers looked the same.

The answer is that what can break still breaks. A list beside an unbreakable
name goes one item to a line exactly as it would anywhere else, because the
rule is about the list and not about whether the line it started on could ever
have fitted.

The check writes the file, since no file here has a name of ninety letters, and
holds three things about it: it comes out the same twice, every line over the
limit is one holding that name, and the list beside it is broken. The third is
the one that says anything the others do not — a formatter that gives up when a
line cannot fit passes the first two.

## D302: a file with nothing in it but a comment keeps the comment

Every rule the formatter is held to is about what a declaration looks like: a
list one item to a line, a comment above the thing it is about, a name that
cannot break. A file with no declarations has none of those to be true of, so a
formatter that wrote nothing at all for one would parse the same, mean the
same, and come out the same twice.

What it may not do is lose what somebody wrote. A file of nothing but a comment
comes back as that comment, on one line, with the blank lines around it gone
and nothing added — and it is written by the check, because no file in this
tree is one.

It is the same rule the formatter already keeps everywhere else, said where
everything else it is held to falls silent.

## D303: one file, eight commands, eight sentences

A file holding one comment and nothing else is a file that says something and
declares nothing, and every command has its own sentence for it: `fmt` keeps
what was written, `check` and `parse` say it declares nothing, `emit` says
there is nothing to run, `lex` says where it ends, `run` says there is nothing
to run and why, `tick` says nothing here takes events, `call` says there is no
such function. Eight answers to one file, and none of them had ever been asked
for.

They are asked now, in one place, so what a file with nothing to do says is
read as a set rather than eight sentences nobody compares. The empty file has
been asked five of them for a long time; this is the file that is not empty and
has nothing in it either, which is a different thing to be told about.

`tick` was the one nothing had ever asked. Without its refusal it ticks a
thousand events into a program with no handler and reports the crossings, which
is a measurement of nothing presented as a measurement.

## D304: the first file named is the one `check` answers about

`check` reads a program: the files named and everything they import. What it
writes out is not all of it — that would be thirty files of declarations for a
project of thirty files — but what the first file named declares, with a line
per other module saying how much it holds.

That rule was in the code and nowhere else. It is in the reference now and held
by a check: two files, named both ways round, each order writing out the file
that was named first and counting the other. Naming the same files in another
order is a different question, and it gets a different answer.

A file that declares nothing is the same rule with nothing to say: named first,
what is written out is nought of its own and a line for every module it stands
on, which is what a file of one comment beside a program is for.

## D305: what a reader is shown and what a tool is given, for a whole program

`check` writes out one module and counts the others, because a reader asked
about one file. `check --json` writes every function there is, because a tool
wants the program. Those are two different answers on purpose, and until now
they were held to each other only for one file at a time, where the two are the
same thing said twice.

For a program of more than one file they are held by the shape they differ in:
what the words write out in full is what the JSON has under the first file's
module, and every module the words counted holds that many functions in the
JSON. A count in one and a list in the other agree or they do not.

That is the one comparison this project has that is not "the same thing said
two ways". What makes it worth making is that either side can be wrong on its
own: a tool given half a program reads half a program and says nothing about
the rest.

## D306: a program that did not check is not written out, and is said in full to a tool

`check` answers two different questions depending on who is asking. A reader
whose program did not check asked what is wrong with it, and a listing of what
a half-worked-out program holds is a list of things that may not be there. A
tool reading a file somebody is still writing wants what has been worked out so
far — an editor greys out what it cannot see yet rather than forgetting it.

So the words say what is wrong and nothing else, and the JSON says what is
wrong and what was worked out. Both are held now: the words carry no listing
when a program did not check, and the JSON carries both an error count and the
functions it managed to name.

The exit status is the same either way, which is the part that had me reading a
pipe's status instead of the command's for the second time in a fortnight. What
a command answered is not what the last thing in a pipe answered.

## D307: which stream each half of an answer goes to

A command line says two kinds of thing and a shell keeps them apart: what is
wrong with a program goes where errors go, and what a program holds goes where
answers go. `kest check x.kest > held` writes the answer to a file and shows
the mistakes on the way past, which is what anybody typing it expects and what
nothing here had ever asked for.

In JSON there is one stream. A tool reads one thing, and an object split over
two is neither: the diagnostics are in the object beside everything else, and
the other stream stays empty. A message written there is a message nobody sees,
in the one form written to be read by something that cannot look.

Both are held now, in four questions: nothing on the answer stream when a
program is wrong, the mistake on the error stream, nothing on the error stream
in JSON, and the listing on the answer stream when a program is right.

## D308: what a program printed is written before what went wrong

The two streams are kept apart on purpose, and a shell may put them back
together: `kest run world.kest 2>&1 | less` is what anybody does with a program
that prints. What a program printed is buffered until the run ends when it goes
to a pipe, and what went wrong is not — so the failure arrived first, before
the lines that led to it. A machine that watched both happen was telling a lie
about the order.

What a diagnostic does now, when it is written anywhere but the answer stream,
is empty the answer stream first. A reader gets what happened in the order it
happened, which is the whole of what a reader is reading for.

It is done where diagnostics are written rather than at each of the places that
write one, because there are seven of those and one of this.

## D309: a run that ends well leaves nothing unwritten

D308 was about the order two streams arrive in when something goes wrong. The
other half is a run that goes right: a program that prints ten thousand lines
and answers seven has to hand over ten thousand lines and answer seven, and
what stands between it and half of that is the command line ending in a way
that empties what it is holding.

It does, and now something asks. Ten thousand lines is more than a stream holds
at once, which is the point: a program that printed a thousand and lost the
last of them looks exactly like one that printed nine hundred, and no check
here had ever printed more than a buffer's worth.

There is no hole for it. What would break it — ending without emptying what is
held — loses every command's output at once, and the probes that ask whether a
command says anything catch it first. A break that cannot be aimed at one check
is caught by whichever it reaches first, and that is still caught.

## D310: the three ways a nought gets into text, all refused and all asked

Text ends at its first nought, so a nought inside a piece of it is a piece of
text that says less than it holds. There are three ways one could get in and
all three are refused: written into a literal, where the lexer says so and
points at `[u8]`; gathered out of a run of bytes the program holds, where the
machine says which byte it was; and handed over by a host, where the machine
says the same and answers with nothing.

The first two were asked for. The third — a host calling `kest_text` with a
nought inside the length it gave — was not, and it is the one where a program
would have ended up holding a name cut in half with nobody told: the refusal is
in a library and the mistake is in somebody else's C.

The host in this tree hands over five bytes with a nought among them now, and
is refused. Which closes the set: every way into text with a nought in it is
refused, and every refusal has been watched happening.

## D311: what a program writes into a lend is what the host has

A lend is the host's memory, and the reference has said since it was written
that everything a program does with one but making text of it reads and writes
that memory. Nothing had ever written to one. Every probe here read: how many
there are, what the heaviest is, whether text can be made of them.

The host in this tree now hands over four bytes and asks the program to write a
nought into the second, and reads its own array back. What the program wrote is
what the host has — and the byte is a nought on purpose, because that is the
one byte this project refuses everywhere text is made and allows everywhere a
run of bytes is held. The two rules meet in one array and neither bends.

The write is the second half of a lend and the first half was held from the
day lending was written. That is what happens when a promise is read as one
thing: the half everybody uses gets watched and the half in the same sentence
does not.

## D312: whoever is running is the one writing

A lend is memory and both sides have it. Who may write to it is whoever is
running: a call holds the machine until it comes back, and between calls the
host has it. A host function called from inside a call is the only thing
running while it runs, so there is no moment when both are writing — not
because anything forbids it, but because there is one thread of control and it
is in one place at a time.

What that buys is that neither side ever reads something stale. There is no
copy anywhere to go out of date: the program reads what the host wrote between
calls, and the host reads what the program wrote during one. Both halves are
now run by the host in this tree, which had only ever written the first.

There is no hole for the second half, and the reason is the first half of the
same fact: what would make a program read something stale is a lend that
copies, and a lend that copies grows the heap by what it copied — which the
probe that asks what a thousand lends cost catches before this one is reached.
Two faces of one property, and the cheaper face is watched.

## D313: a host may keep a reference, and is told when it names nothing

A store is the program's and a host cannot look inside one: what it holds is a
handle it hands back to calls. What it may also hold is a reference — a number
naming a slot and how many times that slot has been used — and a reference is
the one thing that crosses the boundary and can go stale while the host is
holding it.

That was true and untried. The host in this tree keeps one across three calls
now: what it names is there at the second and gone at the third, because the
program dropped it in between, and what says so is the count that tells a
reference to something dropped from a reference to whoever is in that slot now.

It is the same generation check every reference inside a program goes through.
What makes it worth asking from outside is that a host holds one for as long as
it likes, across as many calls as it likes, which nothing inside a program
does.

## D314: a stamp comes from the machine, so no two places share one

A reference is a place in a store and the stamp that place was handed out with.
The stamp used to be the store's own count of how many times that place had
been used, which told a reference to something dropped from a reference to
whoever is standing there now — and said nothing at all about which store it
came from. Two stores of the same shape both start their places at one, so a
reference into the wrong one named somebody else's value and gave it back with
nothing wrong said about it.

The machine hands out stamps now. No two places in any two stores are ever
stamped the same, so a reference carries where it came from without carrying a
store: what it names in another store was stamped by something else and is not
there.

It made the rest simpler rather than harder. Giving a place back no longer
counts anything, because the next stamp is new whatever happened before; and
the rule that retired a place whose count had come round is gone, replaced by
one number the machine watches. What runs out is how many places a machine has
ever handed out, which is a store of one filled and emptied four thousand
million times, and `check-ceilings.sh` lowers that number in a copy to watch it
happen.

## D315: a reference says which store it came from without carrying one

`ref<Npc>` and `ref<Row>` are one number each, and a host holding both holds
two numbers that look alike. The checker keeps them apart inside a program and
there is nothing at the boundary to keep them apart at all — a host that hands
the wrong one to a call is C handing a machine an integer.

It does not need anything. D314's stamps come from the machine, so a reference
handed to a store it did not come from names a slot stamped by something else
and reads nothing. What a type would have told the boundary, a number already
tells it.

The host in this tree now makes a second store, takes a reference out of it,
and hands that to a call about the first: nothing is what it names. That is the
mistake this boundary is shaped to survive, made by the only thing here that
can make it.

## D316: the stamps belong to the build, so two worlds are one program

D314 made the machine hand out stamps, which closed a reference from one store
naming somebody in another. Two machines from one build are two worlds of one
program — an engine running a level and a menu, a test running a fixture beside
the thing it is testing — and each of them counting from one puts the same
stamp on the first place of each world.

The count belongs to the build now. It is state hanging off the thing the host
owns rather than anything global, which is the shape everything in this library
has, and it makes a reference from one world name nothing in the other.

Two machines from two builds still count separately, and that is where this
stops: a host holding references from two programs holds two numbers with
nothing to say which is which. What it cannot do is hand one of those a store
from the other — a handle is refused across machines — so the pair is never
whole, and half a pair names nothing.

Watching it took three machines: the second and third are both as new as each
other, so what they stamp first is the same thing counted twice, which is
exactly what a shared count prevents and a per-machine count does not.

## D317: two hosts in one process share nothing, and nothing holds them apart

*Argued.* A `KestHost` is a list of bindings its caller owns. A machine reads
the list it was started from and keeps its own copy, and after that nobody
remembers the host: `kest_host_free` beside `kest_start` is the shape every
host here is written in. Two of them in one process are therefore two lists
that never meet, and the same name bound in both to two contexts is two
answers.

There is nothing to add. No call lets one host at another's machines, and no
call lets a program ask which host started the machine beside it, so the thing
that would have to be refused cannot be written down. What makes this true is
what is absent, which is the one kind of guarantee a reader cannot find by
reading the code: every page of it is a page where nothing happens.

So it is said in the reference and watched by the host. `examples/embed.c`
starts a fourth machine from a second host, binds the same three names in it to
a decider of its own, asks both machines what they are running under, and stops
if the two answers agree — while the first host's decider is swapped under its
own machine, so the two are moving apart rather than sitting still. The hole
for it starts every machine from the first host anybody used, which is the
mistake a machine that remembered its host would make, and the answers become
one answer.

## D318: one room per check, and a gate that says what was left in it

*Measured.* The gate stopped at `No space left on device` with nine hundred
directories under `/tmp`, every one of them holding the two files
`check-ceilings.sh` writes. That check makes a scratch, traps it, makes a
second place to work and traps that — and a second `trap ... EXIT` replaces the
first rather than adding to it, so it handed back one of its two rooms every
run for as long as it has existed. A hundred more came from `check-docs.sh`,
which takes its room away on its last line and is a check, so most of its runs
end before that line.

So a check makes one room and takes it away once, and everything else it needs
is a directory under the one it has. `check-tables.sh` holds every check to
that: one `mktemp -d` or `mkdtemp()` where a room is made, one `trap`, and no
fixed name under `/tmp` — quoted or bare, because the one this project had was
bare and the pattern that only looked inside quotes read past it. A check
written in Python hands its room back from `atexit` rather than from its last
line.

And the gate hands the whole run one place to work, `TMPDIR` under its own
scratch, and looks at it when everything is done. What is still there is what
somebody made and did not take away, and the names are printed rather than
counted: a check leaves a directory shaped like its own name, so one of them
says which check it was. That is `room`, the twelfth thing the gate does
itself.

There is no hole for the gate's own half of this. What would catch a gate that
stopped looking is the gate, and a hole that runs the gate costs three times
what every other hole costs (D288). What a hole is aimed at is the half that
reads: the second trap, put back in the check it was actually in.

## D319: a run that has no memory left records one bit and says one line

*Measured.* Every allocation in this compiler answers NULL when the machine has
nothing left, and every caller handles it by giving up. What a caller gives up
with is a diagnostic, and a diagnostic is written into the arena that has just
refused — so the run recorded nothing, counted no errors, printed nothing, and
came back nought. Four bands of `ulimit -v` did exactly that: `kest run` over a
program that compiles found no memory for the checker, or for the machine's
frames, and answered like a program that had run and printed nothing.

A run cannot make a diagnostic when it has no memory. It can set a bit.
`KestDiags` has one now, `kest_diags_starve` sets it and counts an error, and
the three places a diagnostic used to be dropped in silence set it: the two
that could not reserve a place in the list or write the message, and the one
that could not copy one run's diagnostics into another's. Two more set it where
there was never a diagnostic to drop: `kest_start` with nowhere to put what the
machine would say, and `kest_runtime_new` with nowhere to put the machine. And
one stage that answered no with nothing said — the checker, out of room for the
program — is read as the same thing, because a caller told no and given no
reason is a command that stops and prints nothing.

The renderers say it last, after whatever was said before it, because it is
about what is missing from that. It is said once: what keeps a diagnostic from
being said twice is a count of how many have been written out, and this one is
not in the list, so it has a bit of its own.

The command line said `kest: out of memory` in five places, which is a sixth
form of the same thing and not one a tool can read. It says the line and the
object every other refusal is said in now, through the door that writes one
diagnostic without an arena.

What holds it is a ladder rather than an argument: `check-ceilings.sh` finds
the level of `ulimit -v` this program runs in, walks down a hundred kilobytes
at a time to the level where the C library itself cannot be mapped, and holds
every rung to running or refusing in words. Both kinds have to happen, because
a ladder that never crossed the line walked no rung that says anything.

## D320: running out says the two numbers whether or not a host set the ceiling

*Measured.* `K0617` — a host's heap ceiling, crossed — says what the program
had used, what it was given, and what the allocation that failed was asking
for. `K0605`, the machine itself running out, said `out of memory`. That is the
one sentence a reader already has before they read it: the thing they do next
depends on whether this is a program that wants a gigabyte or a machine with a
megabyte left, and those are exactly the two numbers it left out.

It says them now. What made that possible is one line in the arena: a
ceiling's refusal wrote down what it had been asked for and the host's refusal
did not, so the number a message about running out would have printed was
whatever the last ceiling refused, or nought. The refusal that the host made is
written down in the same field, in the one place the host is asked — the block
a fresh allocation needs. A refusal to make an existing block bigger is not
written down, because everything that grows here asks for a bigger block, is
told no, and then asks for a new one: the second refusal overwrites the first,
so writing the first is a line whose effect nothing can read.

Neither path had been run. `check-ceilings.sh` runs both now, under a `ulimit
-v` a program is given less than it wants: one that grows an array a push at a
time and one that asks for a hundred million at once. Each is held to the code,
to the words, to the line it happened at, and to both numbers being numbers
rather than nought — the last of which is what catches an arena that stopped
writing down what it was refused.

## D321: the machine says which of the two refused an allocation

*Argued.* `kest_heap_wanted` is one number for two things that happened. A host
reading it after a program stopped cannot tell whether it was told no by the
ceiling it set or by the machine underneath, and those are the two things it
would do something different about: a ceiling is a number to raise, and a
machine with nothing left is a machine that will refuse the raised one too.
D320 gave the message both numbers, which is what a *reader* needs; this is the
same fact for a host that reads numbers rather than words.

The arena remembers which, in a bit beside the number, because a refusal of
nought bytes is not a thing that happens: the number says whether there was one
and the bit says who made it. `kest_heap_refused_by` answers a machine's three
states — nothing, the ceiling, the machine — as a list with nothing else in it,
so a host that switches over it and a fourth answer are a host that stops
compiling rather than one that prints two of the three.

Both are now walked. `examples/embed.c` spends the megabyte it gave itself and
is told it was its own ceiling; `check-ceilings.sh` runs the same host over a
program that grows, with no ceiling, on a machine given less than it wants, and
is told it was the machine. The holes are the two mistakes this could make:
answering the machine as a ceiling, which is a host raising a number forever,
and answering a ceiling as the machine, which is a host giving up on a program
that was inside a number it chose.

## D322: what a host may not do while the program is running, walked

*Argued.* Two calls in the public header are refused while a program is
running: `kest_heap_reset`, because what the program is holding is on the heap,
and `kest_runtime_free`, because the stack it is standing on goes with the
machine. Both were written, both said `K0613`, and neither had ever been asked
for. A refusal nobody has seen is the same as no refusal.

`examples/embed.c` asks for both from inside the function the program calls it
back through, which is where a host is running inside a call. It reads what the
machine said where it asked rather than afterwards — the words are on the
build's memory and not on the heap, so they are readable either way, and
reading them there keeps them out of what the run reports — and counts the two
refusals. A refusal that did not happen stops the host there and then, because
what runs after one is a machine reading memory it has given back.

It is asked for through a function that is already bound rather than through a
new one. An extern is a name every host of that program must provide, and the
command line runs this program too: a name only the host beside it can answer
would make `kest run examples/embed.kest` a program no host has.

The header said `kest_heap_reset` also answers false when the host is out of
memory, and that it leaves the machine unusable when it does. That was true of
a version that made a new heap and freed the old one. It is the same heap
emptied now, keeping the block it started with, so it asks the host for nothing
and cannot fail that way. The sentence is gone: a promise that describes an
older implementation is worse than no promise, because it is the one a host
writes code against.

Adding a name found one more thing on the way, kept after the name went. This
host looked its entry points up into an array sized by the last name in the
list beside it, so a name added after that one wrote past the end of the array
— the sanitised host caught it at once and the other build would have written
it. It is sized by a count at the end of the list now, with a `_Static_assert`
holding the two lists to each other: the rule this project has for every list
that must be complete, applied to the host that is here to show the rules being
kept.

## D323: freeing the machine answers whether there is one

*Argued.* `kest_runtime_free` was `void`. Refused from inside a call it said so
in the report and went back, so a host that does not read reports — which is a
host in a frame loop — carried on believing the machine was gone and held one
it thought it had given away. The other two things a host can be told about the
machine it holds, `kest_heap_reset` and `kest_start`, are read from what they
answer; this one was the odd one.

It answers now: true when it freed a machine, true when there was none, false
when it was refused. Nothing to free is not a refusal, because what the caller
asked for is that there be no machine and there is none. The three states a
host cares about are two, so this is a `bool` rather than a list: what it does
about false is the same whatever the reason, and there is one reason.

What it does about false is come back when the call returns. The refusal lasts
exactly as long as that call, there is nothing to retry inside it, and nothing
takes the machine away by force — so a host that asks in a loop and never
returns keeps the machine and everything on it. That is said in the reference
rather than refused: the alternative is freeing what a running program is
standing on, which is worse than a leak in every way that matters.

Both answers are walked. `examples/embed.c` is told no from inside the function
the program calls it back through, and told yes for both of its machines when
nothing is running on them and for no machine at all.

## D324: a build is refused while a machine is standing on it

*Argued.* `kest_build_free` was `void` and freed the arena whatever was on it.
Everything a machine runs is on that arena — the chunks it executes, the
layouts it reads, the names it looks up, and the text every diagnostic it might
raise points at — so a host that freed the build first had machines reading
freed memory at the next instruction. Nothing refused it, nothing said it, and
nothing survives it.

The build counts what is standing on it. The count lives in the module, beside
the stamps and for the same reason: what two machines from one build have in
common is the build, and this is the part of it they all touch. A machine
counts itself up where it is made and down where it is freed, which is two
lines beside each other in one file rather than one at each end of the library.

Freeing is refused while the count is not nought, with the count in the message
because a host that has lost one machine of four is looking for which.
`kest_build_free` answers the same three-into-two as `kest_runtime_free`: true
when it freed one, true when there was none, false when it was refused. So the
order is the only order there is — every machine, then the build — and a host
that gets it wrong is told rather than left to find out.

A machine refused its own freeing is still standing, so a host inside a call
that asks for both is refused both. That falls out of the count rather than
being written twice.

## D325: the host list is the one thing in this family nothing has to be told

*Argued.* Starting a machine, throwing its heap away, freeing it and freeing
the build all answer now, and all four can be refused. `kest_host_free` is the
fifth and answers nothing, because there is nothing it could refuse: after
`kest_start` no machine points into the list. What a machine keeps is the
function and the context, copied into arrays of its own, and the names it was
found by are the program's, on the build.

That is what makes freeing the list early safe, and it was held by nothing. It
is held by a hole now: a `kest_host_find` that hands back a pointer into the
binding rather than the context in it. Both hosts in `examples/embed.c` are
freed before anything runs, so a machine that pointed into one would be reading
what the allocator has since given to somebody else — and what catches it is
the pair of machines from two hosts answering the same, which is D317's probe
reading a difference that is no longer there.

What the context points at is the host's own, and the rule for it is the
opposite one: the machine keeps the pointer and not what it points at, so it
has to outlive every machine started with that list. That is said in the
reference; there is nothing to check it with, because a host's own memory is
not this library's to know about.

Starting with no host at all is now walked as well. Every extern is unbound and
the report says which of them, and the machine that did not start is not
counted as standing on the build — a failed start that counted itself would be
a build nobody could ever free, which is the second hole here.

## D326: how many names a program may ask the host for

*Argued.* A call to an extern names it in the instruction, in two bytes. The
list of them had no ceiling: `kest_module_extern` handed out a slot per name
and the compiler wrote `(uint16_t)slot` into the call. The sixty-five-thousand-
and-thirty-seventh name would therefore be called as whichever one that number
wraps to — one of the host's own functions, handed this call's arguments, with
nothing said by anybody.

Nobody will write a program with that many externs. That is not a reason to
leave it: every other number of this kind in this project is a message with the
number in it at the line that asked, and the ones nobody meets are exactly the
ones nobody has seen work. `MAX_EXTERNS` is 65536, the refusal is `K0502` like
every other how-many, and a `_Static_assert` beside it says why the number is
that number: a build that raised it past what two bytes hold would stop rather
than wrap.

Reaching it takes a program with sixty-five thousand names in it, which is
slower to compile than anybody will wait for — the list is walked by name to
give a slot out, so it is quadratic in the number of names. So this joins the
other ceilings nobody can reach in a tree of their own: `check-ceilings.sh`
lowers it to four in its copy and asks for five. It is counted with what the
compiler refuses rather than with what a machine runs into, because the copy is
lowered so that a program can reach it and not so that it happens elsewhere.

The row is in the reference's table beside the rest, which is what makes it a
number a reader can find rather than one that is only enforced.

## D327: the names a program declares are looked up through an index

*Measured.* Every name a program uses is settled by looking through the list of
what it declares, and every declaration looks through the same list to find out
whether it is already there. The list was walked, comparing name after name, so
what it cost was the program's own size squared. The last turn's line said this
was the extern list; it is not. A program of two thousand externs and a program
of two thousand ordinary functions cost the same, and a program with two
thousand calls to one extern cost nothing: the walk that mattered was over the
program's own globals, which every program has.

Nothing this project ships is big enough for it to show. Programs written by
something other than a person are, and a compiler whose cost is the square of
the file is a compiler that stops being usable at exactly the size where a tool
starts generating.

So the globals carry an index: a slot per name, twice as many slots as names,
holding one more than the place it names so that nought is an empty slot. It is
open, and nothing is ever taken out of it, so everything under one name is a
run of slots ending at the first empty one — in the order it was declared,
which is what the walk gave and what the overload rules read. The two lookups
that were walks read the run instead; the one that measures how near a name is
to every other name is still a walk, because that is what it is for.

What is not here is a measurement written down. `make time` is the one this
project keeps, and it is a frame of a program running rather than a compiler
reading one.

## D328: a table is held to the list it indexes, in the build that says so

*Argued.* D327 put an index over the names a program declares, which is the
first thing here that is a table rather than a list. What holds a list to being
complete is a count the compiler checks and a tool that reads it; a table is
held to something else — that it says what the list says.

Half of that is caught already and for nothing: a name in the list and not in
the index is a name the program cannot find, so the first program that uses it
says so. The other half is caught by nothing. A place in the index that the
list does not have, or one place in it twice and another not at all, is a
lookup answering with somebody else's declaration, and no program says which of
its names that happened to.

So the sanitised build says it, where the arena already says its own: after
every declaration and every rebuild, the index holds as many places as there
are names, every one of them names a place there is, and they add up to the
numbers from one to as many as there are. Adding them up rather than ticking
them off is what the arena does with what it handed out, and it costs no memory
in a check that runs inside the thing it is checking.

It is a walk of the whole table per declaration, which is the walk the index
exists to avoid — the same trade the arena makes, and the same answer: it is in
the build nobody runs a frame in.

## D329: a rebuilt index keeps the order the list has

*Argued.* The line said nothing here declares enough names to rebuild the
index. It does: every program in this tree rebuilds it two or three times —
the library and one example together pass sixty-four names — so the growing was
walked by every compile the day it was written. What was not held is what a
rebuild can lose that appending cannot.

Everything under one name is one run of slots, and which of them a lookup
answers with is which went in first. Appending keeps that for nothing. A
rebuild puts every name in again, and a rebuild that put them in some other
order would answer with the last `abs` rather than the first, and tell the
writer of a second declaration that the first one is on the line of the last.
That is a message pointing at the wrong line rather than a program that runs
differently, which is exactly the kind of wrong nothing else here would notice.

So the sanitised build asks for it beside the rest: for every name, what a
lookup finds is not declared later than the name being asked about. It is a
lookup per name per declaration, in the build nobody runs a frame in, which is
the same trade D328 made.

The hole reverses the rebuild. It is caught by a program with two functions of
one name declared before the table fills up — `world.kest` and `embed.kest`
both are, without being written for it, because a library of overloads and a
file that uses one is the ordinary case rather than the awkward one.

## D330: the build that checks itself says so

*Argued.* Four walks in this library are checks a build makes about itself: an
arena against the shortcuts it keeps, and an index against the list it indexes,
in three ways. All of them are in the sanitised build only, because each costs
more than the shortcut it is checking saves.

They were behind the name one compiler defines for that build. Another compiler
does not define it and answers a question instead, so the same source under
that one is a build with none of these checks in it — running every file,
finding nothing, and printing the same line at the end. The holes aimed at
those checks would go missed, which is the gate failing, but what it would say
is that four unrelated things stopped being caught rather than that the build
has no checks in it.

So it is written once. `KEST_CHECKED` asks both compilers their own way, every
file asks `KEST_CHECKED`, and `check-tables.sh` holds the sanitiser's own name
to being spelt in the one place that answers it. And the build says which it
is: `--version` says `checked` or does not, the gate asks both builds, and each
has to give the other's answer back. A guard that stopped matching is one line
in the gate now rather than four holes going quiet.

## D331: the options are held to the same two places the commands are

*Argued.* What the command line answers to is written twice — in `main`, where
the first argument is compared, and in `help`, where a reader looks — and
`check-tables.sh` has held the two lists to each other for a long time. It held
the commands, which are words. The options, which are written with dashes in
front of them, were held to neither list: `-h` and `--help` both worked and
nothing anywhere said they were there, which is the mistake this rule exists to
catch, sitting inside the check that catches it.

The same rule reads them now. It found those two the first time it ran, and
`help` says them.

`--version` is the one thing here a tool asks for rather than a person, and it
was held to nothing at all: printing nothing and coming back nought is what
every command in this project is checked against doing, and this was not a
command. `check-commands.sh` holds it to naming and numbering itself, and holds
the three ways of asking for help to being the same words — a reader who typed
one of them has read the other two nowhere.

## D332: what `help` says is held to something running it

*Argued.* `help` is one string, and every line of it is a promise. The commands
in it are held to being answered and so, since the last decision, are the
options. The rest was held by nothing: the sentence about `KEST_LIB`, the one
about what an exit status carries, the one about `4,5,6` lending three events.
All three turned out to be walked already — by the library-path probes, by the
two exit-status probes, and by the tick probes — which is luck rather than a
rule, because the next sentence somebody writes is walked by nothing.

So the names `help` marks out — what is in backticks, and what is in capitals
that is not this file's own C — are held to being named by the check that runs
the command line. It is the same shape as the reference's table of maxima being
held to the programs that reach each row: a promise nobody has run is a promise
nobody has seen work.

The options are held one step further, to being typed by some check rather than
only answered by `main`, and that found one: `--reset` was printed, answered,
and run by nothing at all. It is the one option that changes what a program is
standing on rather than what is printed about it, and what holds it now is the
number rather than the words — a heap thrown away by nobody says the same
sentence as one thrown away, and the bytes left on it are what tell them apart.
Two checks are left out of that rule: the one that quotes the options as holes,
because a broken copy of a thing is not a run of it, and the one that states
the rule, because a rule about a name is written with the name in it.

## D333: the third side of the triangle

*Argued.* Two documents describe the command line. `help` is one of them, held
to what `main` compares the first argument against and, since D331, to what it
reads as options. The reference is the other, and it writes the same commands
and options in its own words: `kest check *.kest` checks a project as a
project, `--json` says both, `-w` writes each file it is given. Nothing held
that side of it. A command renamed would have left the two documents
disagreeing, with nothing to say which of them is the program.

So `check-docs.sh` holds what the documents type at a command line to what the
command line does: every `kest <command>` they write is one it answers to, and
every option they mark out is one it reads. An option is two dashes and a
word, or a dash and one letter, because `-inf` is a number this language
writes rather than something anybody types.

It is one direction rather than two. A command the reference does not name is
not a mistake — `help` and `parse` are not in it, and a language reference that
had to name every switch of every tool would be a worse reference. What is a
mistake is a document telling somebody to type something that does nothing, and
that is the direction this holds.

## D334: a block that calls the library is held to the library having it

*Argued.* The line said a library function nothing in `examples` calls is one
the reference describes and nobody runs. It is not so: `check-dead.sh` holds
every function the library declares to being named where the checker can see
it, the eight that no example names by module are named inside their own
modules by functions that examples do reach, and every module of the library is
imported by an example with a `main` in it. That side is held.

What was not held is the other direction. A `kest` block in the reference only
has to parse, and a call to a function that is not there parses like every
other call. The first block in the document — the first code a reader sees —
called `math.distance(p, e)`. There is no `distance` in `std.math`; there is
one in `std.vec`, and it takes vectors. The block imported a plain `math`,
which by this language's own rule is a module the program wrote, so it was not
wrong so much as unreadable: a reader of page one has no such module and will
read it as the library's.

It imports `std.math` now and calls `math.abs`, and `check-docs.sh` holds every
call a block makes into a module it imports from the library to being a
function that library has. A block that imports a module of its own is left
alone, which is why what is read is the imports rather than the calls.

## D335: a block that is a program compiles, and nothing calls a `print`

*Measured.* Sixty-nine `kest` blocks in these documents were held to parsing.
Twenty-two of them would also compile, and the other forty-seven fail on names
and types the prose around them declares — that is what a fragment is, and it
is why the blocks were only ever parsed. Reading the codes of what they report
does not tell the two apart either: a fragment whose type is unknown reports
the ambiguity underneath it as well, and a run of statements wrapped in a
function reports a `return` the wrapper cannot have.

What does tell them apart is what the block declares. A block with a `main` in
it is a program: everything it uses is in it or imported by it, and it can be
held to the compiler rather than to the parser. There was one, and it did not
compile — it called `print`, and this document says on another page that there
is no `print`, which is the shortest way of saying what the seven blocks that
called one were showing a reader.

They say `io.print` now, with the import beside them where the block is whole,
and `check-docs.sh` holds both: a block that declares a `main` compiles, and no
block calls a bare `print`. The second is one name held on its own rather than
a rule about names, because everything else a block calls bare is either a
builtin or something the prose beside it declares, and this was neither.

## D336: a block fenced as nothing is held to not being Kest

*Argued.* These documents fence a thing that is not a program without the word
`kest`: a signature on its own, a message a run prints, what a command printed.
Nothing reads those blocks, which is the point of the fence — and it is also
the hole in it. A program fenced that way stops being parsed, stops being
compiled now that the programs are compiled, and says nothing about having
stopped: the count goes down by one and there is no number written anywhere for
it to go down from.

So every block fenced as nothing is held to not being Kest: split and wrapped
the way a `kest` block is, and refused if it parses. Fifty of them, and not one
of them parses today — a signature has no body, a message is prose with a caret
under it, and a listing is a table. What the rule catches is the day one of
them is a program.

It is the parser that decides rather than a reader, and the same wrapping a
`kest` block gets, so the rule is exactly "this would have passed as one".

## D337: the reference says what it is, and what holds it

*Argued.* The reference opened with three sentences saying it described what
was decided rather than what was implemented, that the worklog said what ran
today, and that anything without an entry there was a target. That was true
when the document ran ahead of the compiler. It has not been true for a while:
every block in it is parsed, the programs among them are compiled, every
message is one a run says, every command and option is one the command line
has, every call into the library is one the library has, every row of the table
of maxima is reached by a program, and every example is named where a reader
looks for one.

Searching it for prose describing something unbuilt found nothing — no `will
be`, no `not yet`, nothing in the future tense that was not ordinary English
about how something behaves. A promise that is no longer true is worse than
none, because a reader takes it as a warning about the rest of the document.

So it says what it is and what holds it, and it says what is not held: prose.
That is why the table under `Where each rule is run` says which example runs
each rule, and why the worklog says when each of them arrived.

And the things it points at are held to being there. A path that starts with
one of this tree's own directories is a reader being sent somewhere;
`x/y/a/b/c.kest` in a paragraph about where imports resolve from is a program
somebody is imagining and was never a file. Thirty-seven of the first kind are
named across the two documents, `CLAUDE.md` has been held to the same thing for
as long as it has had a layout in it, and now the sentence at the top of the
reference — which names the check that holds it — is held by that check.

## D338: a diagnostic that has all four is asked for all four

*Argued.* The reference says what a diagnostic carries: a stable code, a place,
a suggestion where one is knowable, and a note for every other place it is
about. What held that was the two forms being held to each other — the words
and the JSON say the same message, the same place, the same fix, the same notes
in the same order. Two forms that agree are two forms that lost the same thing.
A suggestion that stopped being recorded is missing from both, and the check
that compares them says they agree.

So one diagnostic that has all four is asked for all four in each form. It is a
call that allocates inside a function that promised not to, reached through a
second function: the code, the place with its line and column, the fix under
the caret, and two notes — the promise and the call between them — each with a
place of its own. Three places pointed at rather than one, which is what a note
is for.

The two holes are the two ways of losing something in both forms at once: a fix
that is never recorded, and a note rendered only when it has nowhere to point
at. Neither is visible to the check that compares the forms, and both are
visible here.

## D339: a note points at a line its own words are on

*Argued.* A note is the second place a diagnostic is about, and the last
decision held every one of them to being there and to pointing somewhere. What
it points *at* was held by nothing. `\`stepFrame\` promises it here` under the
wrong line says the right words about the wrong place, and there is no way for
a reader to know: every note in this compiler looks like that one.

What a note is about is in the note. The message names it, in backticks,
because that is how everything here is written; the JSON says which line the
note points at; and the file is on disk. So the three are put together — the
name from the message, the line from the note, the line from the file — and a
note that names something has to point at a line that has it. The last part of
a qualified name is what is compared, because a note says what the checker
calls a function and the line says what somebody wrote.

A note that names nothing is skipped, because `the first one` is about a place
rather than about a thing and the place is all there is to check. The check
refuses a run where no note named anything at all, so a program that stopped
producing them is not a check that passes by reading nothing.

The hole points the call note at the promise. Both lines are in the same
message, both are real lines of the same file, and the words are the ones a
right note would say.

## D340: a note is read out of the file it says it is in

*Argued.* Every note carries a file as well as a line, and until now nothing
here had made a note that needed one: one file, one diagnostic, and notes about
lines of the same file. The case the field exists for is a promise in one
module broken in another — `world.stepFrame` promises `no.alloc` and the thing
that allocates is in `helper`, so the diagnostic is about one file and both its
notes are about the other.

That works, and it had never been run. It is run now, by a two-file program
beside the one-file one: the rule from the last decision reads each note out of
the file the note says it is in rather than out of the one the diagnostic is
about, and the check refuses a run where no note is about another file, so the
crossing is walked rather than merely allowed.

The hole gives the note its line and not its file, which is what `NULL` for a
source means: it keeps the number and falls back to the file being reported.
The result is a note pointing at a real line of a real file that has nothing to
do with what the note says — the failure this whole rule is shaped around, one
module further out.

## D341: a name with a dot in it is already under its module

*Measured.* `kest call` puts the named file's own module in front of what was
typed, so that `main` finds `world.main`. It did that to everything, so
`shapes.doubled` — a function of a module the file imports, written the way
`check` prints it — became `working.shapes.doubled`, which is nothing, and the
refusal said that name back. A reader was told there is no
`working.shapes.doubled` by a command line they had typed `shapes.doubled` at.

A program is the file that was named and everything it imports, so a function
of an imported module is a function of the program. What tells the two cases
apart is already in what was typed: a bare name is one of the named file's own,
and a name with a dot in it is under its module already. So a dot is the test,
which also settles the message — what cannot be found is said back the way it
was typed.

Nothing else here calls a function of an imported module from outside, and it
was the last thing the command line could not reach. A program of two files
that works is written now as well — a struct made in one and read in the other,
an array grown there and counted here, a piece of text built there and compared
here — because everything else this check writes is a program written to be
refused, and what a module boundary does when nothing is wrong had been left to
the examples.

## D342: the library from a command line

*Measured.* `call` reaches every function of the program, and a program is the
file named and everything it imports, so anybody with a shell can call the
standard library. Nobody had. It works: `math.min 3 7` answers 3 and
`math.min 3.5 7.5` answers 3.5, which is the overload settled by how the number
is written rather than by what it could fit; `text.upper hi` answers `HI`;
`text.number 42` answers 42 and `text.number abc` answers `none`, because an
optional that is nothing is a thing to print rather than a thing to fail at;
and `sort.by 1 2` is refused with what could not be read and where the
functions of that name are.

That last one is most of a library — anything taking an array, a store, a
struct or a function is not something a shell can hand over — and what is left
is what a person would want to try at a prompt anyway.

The sentence in the reference about `min 3 7` and `min 3.5 7.5` had nothing
behind it until now. It is the second pass over what was typed, and the hole
takes that pass away: every number fits every width of its family, four `min`s
take what was typed, and a command line that can reach a library of overloads
can call none of them.

## D343: what a command answers with is alone on standard output

*Argued.* A program says things while it runs. A command that answers with
something of its own — the value `call` gives back, the numbers `tick` counts —
printed both on one stream, in the order they happened, with nothing to say
which was which. A shell reading `kest call x.kest say.greet world` got

    hello world
    5

and no way to tell the answer from the greeting. `--json` had always kept them
apart, by putting the program's writing on standard error so that what is left
on standard output is the object.

That is now the rule and not a thing about JSON: a program's writing goes
beside the answer whenever the answer is something else. `run` is the one
command whose answer *is* what the program said, so there it stays where a
reader looks.

The two holes are the two commands that answer with something of their own: a
call that writes where it answers, and a frame's cost with the program's own
writing between the rows.

## D344: a run asks whether what the program said arrived

*Measured.* `kest run x.kest > /dev/full` wrote nothing and answered nought.
Every write went into a buffer, the buffer went nowhere, and the C runtime
flushes at exit and throws the error away — which is the oldest way there is to
lose somebody's output, and this had it.

`Io.write` gives nothing back. That is the right shape: a program saying
something is the host's to do, and a program that could be told its writing
failed would have to have something to do about it. So the one who finds out is
the host, and here the host is the command line.

What it asks is the stream's own memory of it. A write that failed is
remembered until somebody asks, so this is one question where the run ends
rather than a flag kept by hand at every write — and it has to flush to ask,
because what has not been written yet has not failed yet.

That flush is now the one that empties the buffer before anything is said about
what went wrong, which is what D308 is about. The hole for D308 takes both away
rather than one, because a hole that leaves the other in place changes no
output.

## D345: a read that could not happen is not an empty input

*Measured.* `Io.read` hands a program everything on the standard input as one
piece of text. A stream that will not be read hands over an empty piece, and a
program counting what it was given counts nought — which is what an empty input
gives too. `kest run x.kest < somedirectory` answered nought and said nothing;
so did a run with the stream closed.

It is the shape of D344 the other way round. Text is what comes back, so there
is nowhere in the answer for `this failed`; the host is the one that finds out,
and the command line is the host. It asks the stream, after the run, the same
way it asks about writing.

Two things went with it. A read that runs out of memory halfway used to hand
over what it had, which is a piece of the input passed off as the whole of it —
the quiet truncation this project refuses everywhere else — and it hands over
nothing now and says so. And a read that failed hands over nothing rather than
whatever had arrived before it failed, for the same reason.

## D346: what the command line provides is written down, all of it

*Measured.* The reference said the command line provides three names no module
declares. It provides eight: `Io.read`, `Engine.name` and `Engine.decide`, and
then `Host.sqrt`, `Host.write`, `Host.clock`, `Host.samples` and `Host.sample`,
which the examples and the one instrument declare. Five of them were bound by
this host and named in no document, so the only way to find out that a program
may ask for them was to read the C.

A host is a list of bindings and this one is a host. What it provides beyond
what the library asks for is between it and the program that asks — which is to
say it is written in the reference or it is written nowhere. So every name the
command line binds that no module of `lib/std` declares is held to being named
in a document.

And what two of them answer is held as well. `Engine.name` says `kest` and
`Engine.decide` says 1, which a program can only find out by asking, and
nothing had ever asked. The reference says both now and a run says them back.

## D347: which promises this host can keep, and who says so

*Measured.* A program declares an `extern` and may promise `no.alloc` for it.
That is the one promise in this language somebody else keeps: the compiler lets
a `no.alloc` body call it on the strength of the declaration, and the machine
measures the heap around the call. Under the engine that was walked. Under the
command line it was not, and the command line is a host with eight names of its
own.

Which of them a program may promise for is not a fact about the names. What
crossing back costs is what decides it: `Io.read`, `Engine.name` and
`Host.samples` hand over a piece of text or a run of numbers, which the machine
has to own, so they reach its heap; the other six answer with a number or take
one, and reach nothing. All nine — the eight and `Io.write` — are declared with
the promise and run now, three refused with `K0631` at the call and six clean.

It is the one place where what somebody typed at a shell is checked against
what this compiler's own C does. The hole stops the measuring, and the three
that make text are let through.

## D348: what a lend costs does not grow with what is lent

*Measured.* The last line said the command line hands back a copy where a lend
would take nothing at all. Half of that is wrong twice over. `Host.samples`
already lends — `kest_borrow` over the host's own array — and a lend does not
take nothing: it takes a header, and the header is why that name cannot be
promised `no.alloc`.

What is true is the thing worth writing down. A header is one size whatever it
stands in front of, so lending four bytes and lending forty thousand cost the
same, and a header a lend gives back is the header the next lend gets, so the
one after those costs nothing at all. That is the whole reason a host lends
rather than copies, and it was written nowhere and run by nothing.

`examples/embed.c` lends both now and holds the two costs to each other, and
holds the third to nought. The hole makes a lend allocate what it was lent,
which is the mistake that looks like a kindness — a host's array copied so the
host may free it — and turns a frame budget into something that grows with
somebody else's memory.

The numbers in the last entry were wrong as well: the three that reach the heap
take 1, 5 and 88 bytes, not what was written there. They are measured now.

## D349: the list of spare headers goes with the heap

*Argued.* Ending a lend puts its header on a list of spares, so the next lend
costs nothing. That list lives on the machine's heap, and so do the headers on
it. A reset takes the heap back, so it has to take the list with it: a spare
left behind hands the next lend a header out of memory the machine has given
away, and what is written through it is somebody else's.

The line does that and always has. What nothing did was ask. `examples/embed.c`
ends a lend, throws the heap away, and lends again, and holds the new lend to
costing something — a lend that costs nothing after a reset is one whose header
came from a list that should have gone with the heap. That is the only sign
there is: everything else about the two lends looks the same.

The hole leaves the list behind. The build that ships walks off into memory it
gave back and stops with no words; the sanitised one says `use-after-poison`,
because the arena poisons what it takes back and the header is the first thing
read out of it. So this is one of the few holes here caught by the build that
checks itself rather than by a check saying something.

## D350: a lend refused for want of room says so

*Measured.* A lend costs a header and a place in the list of what is lent, both
on the machine's heap. A host that ends its lends pays for one header ever; a
host that ends none pays for every one of them until the heap goes. With a heap
of sixty-five thousand bytes that is a thousand and twenty-six lends, and the
thousand and twenty-seventh got back a value with nothing in it and no words
anywhere.

That is the same answer `kest_borrow` gives for a name the program has no array
of, and one of those is about the program while the other is about the heap the
host itself gave. So the second one says which now, with the numbers: what is
left of what the host allowed, out of what it allowed, and what to do about it
— end the lends this host is done with, or give the machine more heap.

Two paths reach it, the header and the list, and both are the same sentence
because they are the same thing: room to write down what was lent. When there
is no ceiling at all it says the machine has none, which is the same shape as
every other refusal here that can be either.

## D351: what a host pays for lending is the most it has lent at once

*Measured.* The line said the list of what is lent doubles and never shrinks,
so a host that lends a thousand times keeps a list with room for a thousand,
and a frame budget sees a number that only goes up. Half of it is true and
none of it is a leak: ending a lend takes it out of the list and puts its
header on the spares, so both the list and the headers are used again. What is
kept is the most that was ever lent at once, and a thousand frames of lending
and ending cost what one frame costs — which `examples/embed.c` has held for a
long time, a thousand frames at a time.

So there was nothing to fix and two lines nothing was aimed at. The header
going back to the spares and the lend coming out of the list are what make the
property true, and a hole in either of them is a host paying for every frame it
has ever run. Both are aimed at now, and the thousand-frame probe catches both.

I wrote a probe for it first — a hundred lent at once, ended, and a hundred
lent again for nothing — and took it back out. Neither of the two holes needs
it to be caught, and a probe nothing has been seen to catch anything with is
one more thing to read.

## D352: a handle to a lend that ended names the next lend

*Measured.* A host lends a block, ends the lend, and lends another. The second
lend gets the header the first one gave back — that is what makes lending cost
the most lent at once (D351) — so the handle from the first lend now points at
a live header describing the second block. The program takes it and reads the
new block through it: `heaviest` over a handle to a lend that ended answered
with the tag of the block lent after it.

The machine cannot tell them apart. A lend handle is a pointer, and a pointer
is all of it; a reference into a store is a number carrying a stamp, which is
why D314 and D316 could close the same shape of hole there. There is nowhere in
a `KestValue` to put a stamp beside a pointer — it is a union of eight bytes,
and every host and every slot in the machine is built on that — and refusing to
reuse headers would trade this for a header per lend for ever, which is the
cost D351 is about.

So it is a rule rather than a refusal, and it is written where a host reads:
ending a lend is where the handle is dropped, not something done before using
one once more. `examples/embed.c` shows it happening, which is the only way a
rule like this is worth anything: the host lends, ends, lends again, and holds
the old handle to naming the new block, so the day a lend carries an age this
stops being true and somebody has to come back here.

## D353: what a host kept across a reset is gone until the machine makes something

*Measured.* A host keeps a piece of text by keeping a pointer into the
machine's heap. `kest_heap_reset` takes that memory back, and
`kest_still_holds` says so — which this tree has walked for a while. What
nothing had asked is how long that lasts. The first thing the machine makes on
an emptied heap goes where the last one was, so the pointer is live again,
`kest_still_holds` says true again, and what it reads is whatever the machine
made: text kept across a reset read `the second` when that is what was made
next.

It is D352 one level up and for the same reason: a pointer carries no stamp,
and there is nowhere in a `KestValue` to put one. So it is a rule rather than a
refusal — a host drops what it kept where it throws the heap away, not
afterwards — written where a host reads it, and shown happening by
`examples/embed.c` rather than described.

## D354: the host that shows the rule follows it

*Measured.* `examples/embed.c` made its world once and kept the handle through
everything after, including the part that throws the heap away three times. A
store handle is a pointer like the text D353 is about: the machine takes the
memory back, the next thing it makes goes there, and `kest_still_holds`
answers about the memory rather than about what was in it — so the handle read
as live and the calls after it were working on whatever store had landed at
that address. They passed. They passed by luck.

So the host makes a world of its own after the heap goes, which is the rule it
is there to show, and puts two in it: a store with no places refuses a
reference by its index alone, and two of the probes that come after are about a
reference being refused for its stamp. Both of them stopped catching their
holes when the world was empty, which is how this was found — a probe that
passes for the wrong reason is one that has stopped asking anything.

The order matters as well, and the comment says so: what a host kept is asked
about before anything else is made, because the first thing made goes where it
was.

## D355: the rules a host keeps are a list, and the engine says them

*Argued.* Three of the last four decisions ended at the same sentence: what a
host is handed is a pointer, and a pointer carries no stamp. Each of them is a
rule a host has to keep rather than a thing the machine refuses, and each was
written where it came up — so a host writer met them one mistake at a time.

They are a list now, in the host boundary, beside the refusals so a reader can
see which is which: a handle to a lend that ended, what was kept across a
reset, the block a host lends, the context it bound, and a bound function
taking what the declaration says. Most of what a host can get wrong is refused
where it is done; this is the rest, and the rest is what a list is for.

A list of rules nobody has watched being broken is a paragraph. So the
reference quotes the lines `examples/embed.c` prints when it breaks the two it
can break safely, and `check-docs.sh` runs the engine and holds each of those
lines to being said. The hole changes what the engine prints, which is the
shape the list would rot into: the words in the document and the run drifting
apart with nothing between them.

## D356: one of a host's own rules has a net, in one build

*Measured.* Of the five rules a host keeps for itself, the block outliving the
lend is the one something can check. The machine that ships holds an address
and a count and cannot know when the block went; the build that checks itself
is told where every block a host has ends, and refuses a lend of memory the
host has given back — in the machine's own words rather than the sanitiser's:

    error[K0610]: this host lent 4 `u8` and does not own that many

So a host is worth running against that build once, for exactly this, and the
reference says so where the rule is written. The hole is a host that mallocs,
frees a function away — near enough and the C compiler says it itself, which is
a different thing being held — and lends what it gave back.

The other two rules in the list still have nothing showing them: a context that
does not outlive the machines started with it, and a bound function that reads
past what it was passed. The first is a host's own memory and outside anything
this library can see; the second reads a slot beside the frame, which is the
machine's own arena and looks like every other read to a sanitiser. They are
written down as rules with nothing behind them, which is what they are.

## D357: a lend at no address is refused

*Measured.* `kest_borrow(runtime, NULL, 4, "u8", 1)` handed the program an
array of four bytes at no address, and the program read it. A host gets there
by lending what a failed allocation gave back, or a lookup that found nothing,
or the wrong variable — all of which are how a host arrives at a null pointer
with a count still in its hand.

The machine cannot tell a bad address from a good one: what it holds is an
address and a count, and D356 says so. This is the one address it can tell,
and it costs a comparison. So a lend of nought at no address is a lend — a host
with nothing to lend says so with the count, and the program reads an empty run
— and anything above nought at no address is refused with the count and the
type in the message.

The build that checks itself would have caught the read afterwards, somewhere
else, as a read of memory nobody owns. This is the same thing said where it
happened, in the build that ships, by the machine that was handed it.

## D358: one lend gets one answer, whichever build is asked

*Measured.* Two things about a count are asked at a lend: what the program can
count to, which either build knows, and whether the host owns that many, which
only the build that checks itself can ask. They were asked in that order
backwards. A lend of three thousand million bytes over a block of four was told
it did not own that many in the build that checks itself, and told the program
counts them with an `i32` in the build that ships — one lend, two answers, and
a message a reader cannot repeat to somebody running the other build.

What both builds can say is asked first now. A count no `i32` holds is wrong
whatever the host owns, so that is the answer in both, and the one only the
sanitised build can give is for the case where the count is sayable and the
memory is not there.

The lend refused for its count was already walked — by `examples/embed.c`,
which lends two thousand million and one `Event`s over an aligned block and
says `a lend longer than a count was allowed` when it is let through. I wrote a
second probe for it before finding that, and took it out again: the hole is
aimed at the line, and the probe that was already there catches it.

## D359: a host with bytes copies them into the type it means

*Argued.* The alignment refusal says what a host may not do — lend a byte
buffer as a type read wider than a byte — and said nothing about what to do
instead, which is the whole of the case that brings anybody here. A packet read
off a socket is a run of bytes at whatever address the reading put it, and the
program wants events out of it.

The way through is a copy: into an array of the type itself, which the host's
own compiler aligns, and lend that. It is one copy for the batch rather than
one for each thing in it, and after it every read and write is the host's own
memory again, which is what a lend is for. There is nothing the library can do
instead — the address is the host's alone, and the refusal is the whole of what
a machine can say about somebody else's memory.

`examples/embed.c` does it with a packet a byte out of alignment: it asks for
the refusal, copies the batch out, lends the copy, and the program reads it.
The recipe is in the reference beside the refusal now, where a host writer
meets the problem.

## D360: there is no number to choose between a copy and a lend of bytes

*Measured.* The question was which of the two ways of handing a packet over
costs less: copy the batch into an array of the type and lend that, or lend the
bytes as `[u8]` and let the program read what it wants out of them. I measured
what each costs the machine's heap. Thirty-three bytes lent as `u8` and two
`Event`s lent cost the same thing — a header — and the second of any two lends
costs nothing at all, because it gets the header the first gave back. That is
D348 said again with different numbers, which is what a probe for this would
have been: one more thing to read that catches nothing new.

So the answer is that there is no number here. What the machine charges is one
header either way. The difference is a copy of the batch on the host's side
against a loop over bytes on the program's, and neither of those is the
machine's to charge for or to have an opinion about. The reference says so
where the recipe is, and says which host wants which: one that already has the
type copies, and one whose wire form is bytes anyway lends them.

The measurement is not written down, because the one this project keeps is
`make time` and it measures a frame of a program running.

## D361: a program takes a wire form apart a byte at a time

*Argued.* The other half of D359: a host whose wire form is bytes lends them,
and then the program has to make sense of a run of `u8`. The language has one
way to do it — read a byte, widen it, shift it into place — and the reference
said nothing about it, which left the recipe half written.

It says it now, with the code, and `examples/embed.kest` has the same code so
it is run rather than shown: four bytes a record, least significant first, out
of a buffer the host lends without copying. Which end the bytes start at is the
program's to write down, because a wire form says it and a machine does not — a
program that reads a number out of bytes without saying the order is a program
that works on one computer.

One of the records has a byte above 127 in it, and that is the point of the
numbers rather than decoration: a `u8` widened as though it were signed makes
every record above that byte wrong, and the program still answers with a
number. The hole reads the byte as an `int8_t`, and the batch comes to 244
instead of 500.

## D362: a program answers in a wire form by writing bytes

*Argued.* D361 gave the reading half: a run of `u8` taken apart a byte at a
time. The writing half is the same crossing the other way and had nothing said
about it either — a program that can read a wire form could only answer in a
form the host already knew.

It can answer in bytes, and the language has everything it needs: mask, shift,
narrow, and write into the lend. What a program writes into a lend is the
host's own memory, so nothing is copied in either direction — the same eight
bytes carry the question and the answer in `examples/embed.c`, which reads them
back the way it would read anything off a wire. The order the bytes go in is
written down in the program for the same reason it is when reading.

Two holes are aimed at the byte itself now, one each way: read out of the
host's memory as though it were signed, and written into it out of the wrong
end of the number. The second of those is also where the older hole about a
write going somewhere else is caught now, because this probe is the first to
notice — a batch that comes back unchanged says it before anything else does.

## D363: a program answers with words by writing bytes

*Argued.* The line said a program has nothing to answer with in words, because
text is the machine's own memory and a host keeping a piece of it is holding a
pointer that dies with the heap. It has everything it needs: text is its bytes,
`len` counts them and `what[i]` is one, and a lend is the host's memory to
write into. So words go back the way numbers do — the same loop, one byte at a
time — and what the host has afterwards is its own.

`examples/embed.c` asks for a word that way, throws the heap away, and reads
the bytes after: which is the whole of why a host would ask like this rather
than keep the value.

Writing it turned up something else. `t[i]` is a `u8`, and every word in this
tree is ASCII except one — `hız`, which is in `examples/words.kest` to show
that four bytes are three characters. Nothing asked what those bytes are worth,
so a byte read as though it were signed passed every check in the tree: 196
read as -60 and nobody the wiser. The example asks now, and the hole is that
read.

## D364: what a character is, said once in the library

*Argued.* This tree has said for a long time that `"hız"` is four bytes and
that a program wanting characters says what it means by one — and then left
every program to mean it for itself, which means every program decoding UTF-8
in its own loop with its own mistakes.

`std.text` means what UTF-8 does, and says it in three functions: `charBytes`
is how wide the character starting at a byte is, and nought for a byte in the
middle of one; `chars` counts them; `charAt` is the one at a place, as text of
its own. A character comes back as text rather than as a number because a
character is not a number here — what a byte means is a program's to say, and
what a character means is Unicode's, and there is no third thing for it to be.

Text that is not UTF-8 is still text, so a byte that begins no character counts
as one. A count that stops at the first byte it does not understand is a count
nobody can use, and a library that refuses such text would be refusing what a
socket hands over.

`examples/words.kest` holds all three against the word it has had since it was
written, and the hole reads a two-byte character as one byte — which is how a
program is told a word is longer than it is.

## D365: which walk over characters is the free one

*Argued.* `charAt` cuts, and cutting copies the piece it names. A walk that
asks for every character in turn is therefore one piece of text per character
on the heap — the shape this project has taken out of `join`, out of `repeat`,
and out of the way a string is built a piece at a time. The same walk written
with `charBytes` and an index reaches nothing.

Which of the two a function is doing is already written on it: `chars` and
`charBytes` promise `no.alloc` and `charAt` does not, so a body that walks with
`charAt` cannot keep the promise, and the refusal names the line in the library
that cuts. That is the whole rule, and the reference says it beside the three
functions; `examples/words.kest` walks both ways, and the one that counts wide
characters keeps the promise.

The hole takes `slice` out of the list of builtins that reach the heap, and
what catches it is the other half of the same promise: the tree walk lets the
body through, the emitted code says otherwise, and `K0405` says a promise was
allowed that the code contradicts.

## D366: a character whose bytes run out is the bytes that are there

*Measured.* `charBytes` reads how wide a character is out of its first byte,
and `charAt` cut that many. Text read a piece at a time ends in the middle of a
character — a line off a socket, a file read into a buffer — so the last
character of a half-read piece says it is three bytes wide when two are there.
Asking for it stopped the program: `K0604`, three bytes from one is outside
text of two, at a line in `std.text` the program never wrote.

The machine was right to refuse the read; the library was wrong to ask for it.
A character whose bytes run out is the bytes that are there, which is the same
rule D364 already keeps for a byte that begins no character: text that is not
UTF-8 is still text, and the point of counting it at all is that somebody has a
piece of it in their hands.

So `charAt` takes what is left when the width says more than is there, and
`chars` already counted such a character as one. `check-commands.sh` builds
text that ends in the middle of a character and asks for both — nothing else in
this tree could, because a literal cannot spell one.

## D367: a byte that begins a character ends the one before it

*Measured.* The library read a character's width out of its first byte and
believed it. A three-byte lead followed by a letter therefore ate the letter:
`h`, a lead byte, `i` counted as two characters, and asking for the second one
handed back two bytes with the `i` inside it. One wrong byte in a line took the
next character with it and the count came out short.

What a first byte says is three bytes wide is three bytes only when the two
after it are the middles of one. A byte that begins a character of its own ends
the one before it, and a piece of text that stops sooner ends it too — which is
D366's rule, now one line of the same function rather than two rules in two
places. `charWidth(t, at)` is the width to walk by and `charBytes(b)` is the
question about a byte on its own, which is the shape those two names should
have had from the start.

Neither of them refuses anything. A decoder that stops at the first byte it
does not like is no use to somebody holding half a line off a socket, and the
whole reason this library counts characters at all is that somebody is holding
a piece of text they did not choose the bytes of.

## D368: the walk back lands where the walk forwards started

*Argued.* Everything here read text forwards, so a program in the middle of a
line that wanted the character before it counted from the start again — which
is the walk an editor does most, and the one UTF-8 was designed to make cheap:
the middle of a character says so in every one of its bytes.

`charBack(t, at)` is that walk. At most three steps back over the middles of a
character, and then the one thing that makes it safe on text nobody chose the
bytes of: what it lands on has to reach where it started from. Bytes that
disagree with each other are a byte on its own — which is what the walk
forwards makes of them too, so the two agree about every place in any text.

That agreement is the property worth holding rather than either walk on its
own, and it is what `check-commands.sh` asks: over a word, over a lead byte
followed by a letter, and over a character cut off at the end, every place the
walk forwards starts at is a place the walk back lands on. The hole stops the
walk back using what the middle bytes say, which is a program moving one to the
left and ending up between the bytes of a letter.

## D369: no `for` over characters, and one walk that hands them all back

*Argued.* A walk over characters is a `while` with the width in it, written out
in every program that needs one, and the obvious wish is a `for` that yields
them. There will not be one. What the cheap walk yields is places rather than
values — the width and the index — and a `for` that yielded values would make a
piece of text for every character of every line anybody walked, which is the
cost this project has spent decisions taking out of `join`, out of `repeat`,
and out of building a string a piece at a time. Sugar that hides an allocation
per character is the wrong end of that.

What a program that does want them all should not write is `charAt` in a loop:
`charAt` counts from the start every time it is asked, so asking for every
character is the text walked once per character. `charsOf(t)` is the one walk,
and it pays for one piece of text each, which is what was being asked for.

The heap it takes is measured now. `check-costs.sh` asked the functions that
hand back one piece of text and skipped the two that hand back a run, because a
command line cannot print a `[text]`: they are asked through a wrapper that
answers with how many there are, which takes the same heap and says a number a
shell can read. `split` came with `charsOf` for free.

What none of that measures is the time. The heap says a piece per character
either way — the quadratic version allocates exactly as much — so a walk that
counts from the start every time would pass this check and be slow. `make time`
is the only measurement here, and it measures a frame of a program running.

## D370: a cut that ends where the text ends is a place inside it

*Measured.* Every `slice` copied. `slice(t, 0, len(t))` — the whole of what it
cuts — took eleven bytes for ten, and `slice(t, i, len(t) - i)` took the rest
of the line again every time a walk asked for it.

Text ends at a nought. A piece that reaches the end of what it was cut from
therefore has its nought already: the one that was there. That is exactly what
`rest` is, and the contract file has said so for a long time — `rest` and
`slice` differ in that one of them ends where it was already ending — without
the machine doing anything about it. It does now, and the two spellings cost
the same.

A cut that stops sooner still copies, because it needs a nought of its own and
the one it would have to write is somebody else's byte. The promise is
unchanged: a `no.alloc` body may not cut at all, because which of the two a cut
is is not known until it runs, and a promise that held for some arguments is
not a promise.

It moved a hole. The one that made `charsOf` keep the rest of the text in every
piece was quadratic before and free after, because keeping the rest is exactly
the cut this makes free; it hands back everything up to each character now,
which is the same shape and still copies.

## D371: a cut walks to where it cuts, and measures only to refuse

*Argued.* `rest` walks to the place it is given, one byte at a time, because
what it steps over is what it costs. `slice` measured the whole text with
`strlen` and then cut. The two are the same question about the same text, and
one of them was a walk and the other was a walk and a measurement.

A cut needs three answers: whether the text reaches `from + count`, whether it
ends there, and where `from` is. All three are at `from + count`. So the walk
stops there: it steps until the nought or until `want`, and a text shorter than
`want` is the walk running out. Whether the piece ends where the text ends —
D370's free cut — is `text[want] == '\0'`, asked at the place instead of by
measuring to the end and comparing two numbers. A cut of ten bytes out of a
line of a thousand now reads ten of them; a cut that reaches the end reads the
same bytes it did before. It is never more work.

The length is measured in one place: the refusal. `9 bytes from 8 is outside
text of 10 bytes` has to say how long the text is, and a run that is stopping
can pay for the rest of the walk — `seen + strlen(text + seen)` finishes the
walk the check abandoned rather than starting again. The same is true of what
a cut says when there is no room for it. A refusal without that number says a
cut did not fit and leaves the reader to find out what it would have fitted in,
so there is a hole for it.

## D372: a byte at a place costs the walk to that place

*Argued.* D371 stopped `slice` measuring the whole text before cutting it. Two
places were still doing it: reading a byte at an index, and where `find` was
told to start. Both were `strlen` and a comparison, which is the whole text
read to answer a question about one place in it.

A read at an index needs to know that the text reaches the index and that it
does not end there. Both are answered by walking to it: step until the nought
or until the index, and a text that ended first is the walk running out. Then
`text[index] == '\0'` is the second half, asked at the place. Reading the first
byte of a line now reads one byte rather than the line. That matters because a
loop over text reads one byte at a time: what was the whole text per step is
now the walk to the step, and a scan that cost the length squared costs half of
it.

It is still not linear, and a scan that indexes from the start cannot be: text
here is a run of bytes ending at a nought, and where the fourth byte is is only
knowable by passing the first three. What is linear is the walk this language
already has — `rest` from where the last one stopped, which is what `split`
does — and a `for` over text, which reads without checking because the compiler
knows the place is in range.

The length is measured in the refusals and nowhere else, as in D371, and the
`find` refusal keeps its one difference: looking from where the text ends finds
nothing, which is an answer rather than a mistake, so the walk may stop exactly
where the text does.

## D373: a walk over text asks how long it is once

*Argued.* `while at < len(subject)` reads well and walks the whole text on
every step, because `len` on text is the walk to the nought. Four functions
were written that way — `chars`, `charsOf`, `charAt` and `trim` — so a walk
over a line of a thousand bytes measured a thousand bytes per step for no
answer that had changed. The length of a text does not change while a walk
over it runs, so it is asked once and kept.

`trim` is the one this was found through. Its condition was
`from < len(subject) && isSpace(subject[from])`, which measured the line and
then, before D372, measured it again to read one byte of it: a line with a
hundred spaces in front of it read the line two hundred times. It now measures
once and reads the byte at the place, which is the walk to that place.

Nothing here catches this, and that is worth writing down rather than leaving
as an omission. What this project measures is memory, and none of this changes
what anything allocates; the one measurement there is is a frame, and a
diagnostic-shaped check for "walked more than it had to" is a benchmark
harness, which this project does not have. What holds these four is that they
are four lines of a kind a reader can see.

`charWidth` still asks twice on every call, and every one of these walks goes
through it. That is the next one, not this one: it is a signature question
rather than a line, since what it wants is the length it was called from.

## D374: the width of a character is a question about a piece of text

*Argued.* `charWidth(t, at)` asked how long `t` was twice on every call — once
to see whether `at` was in it, once for the room left — so every walk that
stepped by it read the whole text per character, whatever D373 did to the loop
condition above it. The place is what made that necessary: a question about the
`at`th byte of a text can only be answered by walking to it, and a function
given a place has to do that walk itself, from the front, every time.

So it is given the piece instead: `charWidth(t)` is the width of the character
at the front of `t`, and it reads at most four bytes. The walk keeps what is
left rather than counting from the start —

```kest
while tail != "" {
    tail = rest(tail, text.charWidth(tail))
}
```

— which is the shape `split` has had all along, and it reads the text once
through. `chars`, `charsOf`, `charAt` and `examples/words.kest` are written
that way now.

`tail != ""` is the emptiness question asked without measuring: comparing text
stops at the first byte that differs, so it is one byte, where `len(tail) > 0`
is the walk to the end for an answer the first byte already had.

What this gives up is asking about a place directly. A caller with a byte
offset writes `charWidth(rest(t, at))`, which costs the walk to `at` — the same
walk as before, except that it is written down. That is the point: cost is
visible, and a function that hid a walk over the whole text behind an index was
a cheap-looking call that was not one. `charBack` keeps its place, because
where the character before a place begins is a question about a place and
nothing makes it cheaper than the walk to it.

## D375: forwards is the cheap direction, and it is the only one

*Argued.* D374 made the walk over characters keep what is left instead of
counting from the start. The same thing was written the slow way in three more
places, and they are all one shape: a question asked about the far end of a
piece of text.

`while len(tail) > 0` is the walk to the nought for an answer the first byte
already had. It is `tail != ""` — comparing text stops at the first byte that
differs, so an empty text differs immediately and one that is not differs at
its first byte. `split` and `examples/scan.kest` asked it the long way in five
places.

`trim` walked in from the right by `subject[to - 1]`, which after D372 is the
walk from the front on every step: a line with a hundred spaces after it read
the line a hundred times. There is no cheap way to step leftwards through text
that ends at a nought, so it does not step leftwards. It walks forwards once
and remembers where the last thing that was not a space ended, which is one
pass for both ends.

This is worth saying in the reference rather than only in the library, because
a program written outside this tree walks text too, and the two forms look the
same on the page. What holds `trim` itself is that `examples/parse.kest` and
`examples/lines.kest` trim and then check what they got, so a byte lost at
either end is an example that answers with which line failed.

## D376: a number too big to hold is not a number this reads

*Measured.* `text.number("2147483648")` gave back -2147483648 and said nothing.
`text.number("99999999999")` gave 1215752191. `text.number("-2147483649")` gave
2147483647, which is the wrong number with the wrong sign. `text.real` of forty
digits gave `inf`. All four are a field read out of a line and handed to a
program as a number nobody wrote.

An `i32` given more than it holds wraps rather than refusing, so a reader that
counts in one cannot tell afterwards whether it ran out of room. It counts in
`i64` now and holds the count to one past the largest `i32` on every digit —
one past, because the smallest is one further out than the largest and is
spelled with a sign in front of it. The bound inside the walk is also what
keeps the count itself in range: twenty digits would run an `i64` out of room
the same way, and this stops long before that.

`text.real` narrows once at the end, and more than an `f32` holds narrows to
infinity. That is a value this language has and not one any text spells, so it
is nothing instead. What asks the question is `narrowed - narrowed != 0.0`:
infinity less itself is not a number where every number less itself is nought,
and it needs no constant, which is as well — this language has no exponent in a
literal and `3.4028235e38` cannot be written down.

Both are the promise this project already keeps in the other direction, that a
number written down reads back as the number it was written from. Reading was
the half nothing had asked about. Both have a hole and both are asked for at
the command line, at the largest, one past it, the smallest and one past that.

## D377: a hole that breaks one of ten ways to say something proves nothing

*Measured.* The hole for "a run with no memory left that says nothing" took the
starvation out of one place in `diag.c`. There are ten places that record it,
so the other nine still did, and a run with nothing left still said so. The
hole had stopped breaking anything, and a backstop that breaks nothing reports
`caught` for whatever the check was already catching.

It surfaced by accident: `examples/numbers.kest` is the program the memory
ladder walks, and adding four checks to it moved which rung the ladder lands
on. The hole went from caught to missed without any of the code it is about
changing, which is what a hole that was passing for the wrong reason looks like
when the wind changes.

Broken where it is recorded now — the one line that sets the bit — because
what a check is held by has to be the one thing all of it goes through. A hole
at one of ten callers is a hole in nothing.

Two things this ruled out on the way, both worth writing down because both look
right. Stepping the ladder finer near the bottom: the band where a run has
nothing left to say it with turned out to be 460K wide on this machine, so a
step of a hundred lands in it four or five times and finer steps buy nothing.
And ending the ladder on an exit status of 127 rather than on the loader's own
words: true, and about a rung a hundred below where this ladder ends, so it was
a change with nothing behind it.

## D378: a float has two answers that are not numbers, and nothing could ask

*Measured.* `math.sqrt(-1.0)` gives back not-a-number. A number too big for an
`f32` gives back infinity — which is what D376 had to catch in `text.real`, and
what it caught it with was `narrowed - narrowed != 0.0` under a paragraph of
explanation. Both of those come out of arithmetic without a word said, and
neither can be found by comparing: the one that is not a number is not equal to
itself, and infinity is equal to itself. So the question was written down:
`math.isNumber(x)`, true only of a number a program can go on with.

Every number less itself is nought and neither of those is, which is the whole
of it and needs no constant — as well, since this language has no exponent in a
literal and the largest `f32` cannot be written down.

`sqrt` keeps giving back not-a-number rather than an optional, and that is not
the same inconsistency it looks like beside `asin`. `asin` outside -1 to 1
would have to make an answer up; `sqrt` of a negative already comes back as the
value a float has for exactly this, and its two callers here take the square
root of a sum of squares, where an optional would put a branch that cannot
happen in front of a value that would have to be invented. What was missing was
not a refusal but a way to ask, and there is one now.

`abs` of the smallest `i32` is itself, because its distance from nought is one
past the largest and negating it wraps the way D018 says all arithmetic does at
the end of a width. That is written down rather than changed: a value that
wraps is what its type says happens, and the alternative is an optional on the
most-called function in the module.

`std.text` may not import `std.math` to ask any of this. An import of a module
that declares `extern`s is those `extern`s required of every host of every
program that reaches it, and `std.math` declares seven: adding the import made
`examples/embed` refuse to start with `the program asks for `Math.sqrt` and
nothing is bound`. So the question is written out once more where `text.real`
asks it, with a note saying why. What holds that is the two hosts in this tree,
which is how it was found.

## D379: every function in `math` is written in both widths, and four were not

*Measured.* `math.asin` of an `f32` was refused: `value` expects `f64`. So were
`math.abs` and `math.clamp` of an `i64`, and `math.sign` of anything but an
`i32`. The module's own comment says why that is wrong — a frame works in `f32`
and widening one by hand at every call is the module not doing its half — and
`examples/camera.kest` was doing exactly that, wrapping a dot product in `f64`
to ask for an angle and narrowing the answer back. It asks for the angle now.

The whole-number half is the same asymmetry the other way round: `min` and
`max` took `i64` and `abs` and `clamp` did not, so a program counting in `i64`
could take the smaller of two and not the distance from nought.

`sign` is the one that does not come in pairs. It gives back one of three
answers whatever width it was handed, so it answers in the width those three
fit in — an `i32`, from either. And there is none for a float, on purpose: two
of the values a float has are on neither side of nought, and the shape this is
written in would call what is not a number nought, which is the answer for a
number that is exactly nought. A program that wants it asks `isNumber` and then
compares, which is the two questions it was really asking.

What let all four hide is that nothing asks a library function until something
calls it. `check-dead.sh` holds every one to being named somewhere, which is
why every function that is there is reached — and a function that is not there
is named by nobody and missed by everything. The pairs are asked in both widths
in `examples/numbers.kest` now, which is where the same gap was found the last
time it was looked for.

## D380: a module written in two widths is held to both

*Argued.* D379 found four functions missing their other half, and what is worth
recording is why nothing here could have found them. `check-dead.sh` holds
every library function to being named somewhere, which is what makes every
function that exists reached — and a function nobody wrote is named by nobody.
A check built on what is there can find a leftover and can never find a gap.

What can see a gap is the pairs themselves, which are a list that has to be
complete in the same way the token names and the instruction names are. So it
is in `check-tables.sh`, where the lists of that shape are held: every
declaration the library makes is read out of a run of the checker, and a
function that takes an `i32` in a module written in widths has to have one that
takes an `i64` beside it.

Which modules it is asked of comes out of the library rather than a name
written in the tool. A module that declares one name in two widths is a module
written in widths; one that does not is left alone, which is why `std.text`
writing `fixed(f32, i32)` and nothing else is not a gap. What is declared
`extern` is left out: those are the host's, and the reference says the host
provides them in `f64`.

It reports what it counted — twenty-two pairs in one module — because a check
that has stopped matching finds nothing and nothing agrees with everything.

## D381: the shapes in `vec` are not a list that has to be complete

*Argued.* D380 holds a module written in two widths to both, and the obvious
next thing is to hold `std.vec`'s two shapes to each other the same way. It
would be wrong. `f32` and `f64` are the same question asked at two sizes, so a
function for one is a function for the other and a missing half is a gap. Two
and three components are not: what a vector can be asked differs between them.

`perpendicular` has no three-dimensional half, because in three dimensions
there is a circle of them and no reason to pick one. `cross` has no half that
gives back a vector, because what is perpendicular to two vectors in a plane is
out of the plane. A check by analogy would have demanded both and been argued
with rather than obeyed, and a rule with exceptions written into the tool is a
list of names in a check, which is the thing `check-tables.sh` is written to
avoid.

What the reading did turn up is two real gaps, neither of them about shapes.
`lengthSquared` is there because comparing lengths orders the same way without
the square root, and that is the question a frame asks — but the two-point form
of it, which is the one a frame actually asks, was not there. `distanceSquared`
is now beside `distance` the way `lengthSquared` is beside `length`.

And a cross in two dimensions does exist: it is not a vector but one number,
the `z` the three-dimensional one gives back, and what a program reads off it
is which side of one vector another is on. That is `dot` with one of them
turned a quarter, and this module gave both halves of that and never put them
together. `examples/physics.kest` holds the two forms to each other: the `z` of
the three-dimensional answer is the two-dimensional one.

## D382: a direction is found by dividing before squaring

*Measured.* The `Next:` said `direction` wastes a square root comparing a
length against nought. It does not — the length it compares is the one it then
divides by, so there is nothing spare. What is wrong is what the squaring does
at either end of what an `f32` holds.

`direction(Vec2(1e20, 1e20))` gave back `(0, 0)`, whose length is nought: the
squares overflowed to infinity, one over infinity is nought, and every
component was multiplied by it. That is a vector handed back as a direction
which is not one and does not say so. `direction(Vec2(1e-21, 1e-21))` gave back
a vector of length 0.99973655, because the squares landed where an `f32` keeps
numbers badly. Both are the shape of mistake this project looks for: an answer
rather than a refusal, and wrong.

Each component is divided by the largest of them first. That puts every one
between -1 and 1 with one of them at exactly 1, so what is squared is between 1
and 3 whatever was handed in, and neither end of the width is anywhere near.
`direction(Vec2(1e20, 1e20))` and `direction(Vec2(1, 1))` are now the same
vector, bit for bit, which is the property a direction has and a length does
not.

The cost is two divisions and a `max` per call, and it is paid because the
alternative is a function that is right for the vectors somebody thought of.
This is a bytecode machine: an arithmetic instruction is a fraction of what
dispatching it costs, so the exchange is not the one it would be in C.

What is left is a component that is not a number, and infinity over infinity is
not one either, so the length of the divided vector is the place to ask: it can
only fail to be a number if a component already was not. That is `none`, which
is what a vector with no direction gets.

## D383: a length is found by dividing before squaring, and a square is a square

*Measured.* `vec.length(Vec2(1e20, 1e20))` was infinity. The length is 1.41e20
and an `f32` holds that with room to spare; what does not fit is the square,
which is where the answer went. At the other end the length of (1e-21, 1e-21)
came back a part in five thousand out, because the squares landed among the
numbers this width keeps badly. `distance` is a length and had both.

So `length` does what `direction` does since D382: divides every component by
the largest of them, which puts them between -1 and 1 with one at exactly 1, and
multiplies the answer back. `length(Vec2(3, 4))` is still exactly 5 and
`length(Vec2(5, 12))` is still exactly 13; `length(Vec2(1e20, 1e20))` is
1.4142136e20.

A largest component that is not a number is the answer itself. The length of a
vector with an infinite component is infinite, and dividing by infinity would
have made it nothing instead — which is the same mistake in the other
direction, so that one is answered before any dividing happens.

`lengthSquared`, `distanceSquared` and `dot` are left alone. They hand back a
square, and a square runs off the end of a width where squares do: the square
of 1e20 is infinity because that is what the square of 1e20 is. Making those
answer for vectors their answer does not fit would be making them something
other than what they are called, and they are there to be compared, which works
up to the size where the squares stop fitting.

That is the line this module answers along, and the reference says it now:
every vector whose *answer* an `f32` holds, which is not every vector whose
square it holds.

## D384: where a line may be broken is where a line may end, asked once

*Measured.* `kest fmt` was handed
`if a / b - c > 0.0001 {`, too long for the line, and wrote it as two lines
with the first ending in `>`. That does not parse: a line may end after `>`,
because `ref<Npc>` ends in one and a field ends where its line does, which
D003's list has said since it was written. So the formatter made a program the
compiler refuses, and answered nought having printed it.

The formatter breaks a chain after its operator, and the comment above that
code says why it is legal — a line that ends in an operator carries on. That is
true of every operator but one, and the one it is not true of was the one being
broken. There is no legal break in such a chain at all: before the operator
ends the line on a value, which the same list refuses from the other side. So a
comparison holding a `>` stays on the line it is on however long that is. Being
too wide is a thing a reader can see; not parsing is not.

What made this possible is that the rule was written twice — the lexer's list
of what a line may end after, and the formatter's sentence about what carries
on. It is asked once now: `ends_statement` is `kest_lexer_ends_statement` and
the formatter asks it. Where a line may be broken and where a line may end are
the same question, and a second copy of an answer is a second thing to keep
right.

`check-fmt.sh` holds the formatter to its output parsing, over every file in
the tree — and the tree has no line like this, which is why nothing said
anything for as long as there has been a formatter. It writes one now, and
there is a hole for it.
