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
