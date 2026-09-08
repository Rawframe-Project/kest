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
