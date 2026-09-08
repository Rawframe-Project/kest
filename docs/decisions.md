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
        ...
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
