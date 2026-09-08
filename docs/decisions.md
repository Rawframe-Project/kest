# Decisions

Append-only. A decision that turns out wrong is superseded by a later entry,
not edited or deleted. Each entry says what was decided, why, and what the
claim rests on: a measurement, or an argument.

Evidence marked *measured* comes from the predecessor research programme at
`Rawframe-Project/kest-research`, which produced no language but did close two
comparative workloads and one prototype. Everything else is marked *argued*.

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
