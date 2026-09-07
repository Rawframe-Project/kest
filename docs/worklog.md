# Worklog

What was built, in order, newest last. One entry per session. An entry says
what runs, not what is planned.

## 2026-09-07, repository set up

Read the predecessor repository at `Rawframe-Project/kest-research` and took
four results from it: the measured host boundary shapes, the measured cost of
having only managed references, the measured inference boundary for the
allocation contract, and the two `.kest` sketches, whose syntax this project
adopts.

Wrote `CLAUDE.md`, `docs/decisions.md` with D001 to D009, and
`docs/language.md`. Repository builds an empty `kest` binary that reports its
version.

**Runs:** `make`, `./kest --version`.
**Next:** lexer.

## 2026-09-07, lexer

Three modules. `mem` is a block-chained arena, because nothing the compiler
produces before the program runs outlives compilation. `diag` holds spans,
source line lookup, and both renderings; column counts characters rather than
bytes so a caret lands under the right glyph in a UTF-8 identifier. `lexer`
turns source into tokens.

Two things in the lexer are decisions rather than mechanics. A line break is a
statement terminator only when the previous token could have ended a statement
and no bracket is open, so a line ending in `*` or `,` continues without any
rule the author has to remember. And every byte above ASCII starts an
identifier, which makes `let hız = 5` legal without carrying Unicode tables.

`;` and `/* */` are refused with their own codes rather than falling through to
"unexpected character", because D004 makes the parser strict and D008 makes the
message carry the cost of that.

**Runs:** `kest lex <file>`, `--errors=json`. Six errors in one broken file
report in one pass.
**Next:** ast and parser.

## 2026-09-07, parser

`ast` defines the tree and prints it; `parser` is recursive descent with
precedence climbing over six levels. Literals and names keep only their span,
so the tree stays small and the text is read from the source when it is needed.

The whole of `examples/player.kest` and `examples/frame.kest` parses:
`ref<Npc>?`, `[ref<Quest>]`, `extern fn Clock.now() -> u64 no.alloc`,
`while`, `for ... in`, compound assignment, and correct precedence and
associativity.

Recovery has two levels and the difference matters. A broken statement
recovers to the next line, so the rest of a body still reports its own errors.
A broken *signature* recovers to the next declaration, because a body measured
against a signature nobody has produces only noise; the first version of this
reported a spurious "expected a declaration" at the first `let` of the
abandoned body. Three real errors in a four-error file, no cascades.

`no.alloc` is spelled with a dot rather than as a keyword so the namespace can
hold further contracts without spending more keywords on them.

**Runs:** `kest parse <file>`, and `--errors=json` on both commands. Clean
under ASan and UBSan on every example.
**Next:** types.

## 2026-09-07, type resolution

`types` turns the syntax tree's type references into resolved types, collects
what a file declares, and reports what it cannot resolve. Expression and
statement checking is not in this pass.

Structs are registered before any field is resolved, so two of them may name
each other and `struct Quest { giver: ref<Npc> }` may name an `Npc` declared
below it. `ref` is the only generic, and an unknown one says so rather than
failing through a general mechanism that does not exist yet.

Unknown names suggest the nearest declared one by capped edit distance:
`f33` suggests `f32`, `Playr` suggests `Player`, and `Playr` in a file with no
`Player` suggests nothing, which is the case that matters. A suggestion that
is wrong costs more than no suggestion.

Diagnostics are now sorted by source position before rendering. Stages find
problems in the order that suits the stage, and a reader scans in the order of
the text; the duplicate-struct error was arriving before a field error four
lines above it.

Two build fixes. Objects live under `build/release` and `build/debug`, because
`make debug` followed by `make` was linking sanitizer objects without the
sanitizer flags.

**Runs:** `kest check <file>`. Eight errors in one broken file, in source
order. Clean under ASan and UBSan.
**Next:** expression and statement checking, with locals and scopes.

## 2026-09-07, body checking

`check` is its own module: `types` was at the 600-line signal and the two jobs
are different, one resolving declarations and one measuring bodies against
them. `CLAUDE.md`'s pipeline lists both now.

Fifteen errors from one broken file in one pass: operand mismatch, unknown
field, redeclaration, non-`bool` condition, assignment to a constant,
`continue` outside a loop, `for` over a non-array, argument type and count,
calling a non-function, returning the wrong type, and a function that can end
without returning.

Three decisions are in here rather than in a document, because the code is
where they became real.

**Literals take their type from context, and nothing else converts.** `let d:
f64 = 1.5`, `let e: u8 = 200` and `2.0 * v.x` all work, while `v.x + n` with
an `f32` and an `i32` is refused. Silent numeric conversion is the class of bug
this language is for avoiding; a literal has no type of its own to lose.

**Shadowing a visible local is refused.** At any point in a body one name means
one thing. A sibling block may reuse a name, because the first is gone by
then, so two loops may both use `i`.

**A one or two character name gets no suggestion.** Every such name is one
edit from every other, so `v.z` was suggesting `x`. A suggestion that carries
no information is worse than none, which is the same rule that stops `Playr`
suggesting anything in a file with no `Player`.

Known hole: `import` binds a name and nothing else, so a member of an imported
module resolves to the error type without a diagnostic. It is not wrong, it is
unknown, and reporting it would be a guess. Modules are a later stage.

**Runs:** `kest check` on both examples, clean. Clean under ASan and UBSan.
**Next:** compile, the bytecode emitter.

## 2026-09-07, bytecode and the compiler

`value` holds the runtime value, the instruction set and a disassembler.
`compile` walks the checked tree and emits.

A runtime value carries no tag. The language is statically typed, so `add.i`
and `add.f` are different instructions and the compiler picks between them by
reading the type the checker left on the expression. Tagging every value would
pay at runtime for a question that was already answered. This is recorded as
the durable half of D010, which also settles the stack machine and says what
would reverse it.

The checker now records a resolved type on every expression node. Slot
allocation stays in the compiler: the checker decides what a name means, the
compiler decides where it lives.

What runs: locals, arithmetic on both storage classes with unsigned variants,
comparison, short-circuiting `&&` and `||` compiled as control flow, `if` and
`else if` chains, `while` with `break` and `continue`, calls with forward
references, and `print`. `examples/math.kest` compiles to bytecode whose jumps
and back-edges resolve.

What does not, and says so: struct fields, arrays, `for`, module-level
constants, and calls to `extern`. Each refuses with its own message rather
than emitting something that does not mean the same thing.

Two fixes found while testing. The compiler was stopping at the first refusal,
which breaks D008; a reported problem no longer stops the walk and only
running out of memory does. And the disassembler was printing a text constant
as the integer its pointer happens to be, so a constant now carries the class
the compiler knew when it wrote it. The machine never reads it.

A correction to the three entries above this one. They each claim the
sanitizer build was clean, and the check behind that claim read the exit
status rather than the output, which is not what a sanitizer reports through.
Checked properly, every one of those runs was reporting `memcpy` with a null
source: a growable array starts out NULL and the first growth copies nothing
from it, which is undefined however harmless it looks. Guarded in all six
places that grow one. The claims are true now and were not before.

**Runs:** `kest emit <file>`.
**Next:** vm, and `kest run`.

## 2026-09-07, the machine, and Kest runs

`kest run examples/math.kest` prints `positive`. It computes `factorial(5)`,
takes `gcd` of that and 84, and classifies the result, across four functions
with forward references, a loop, an `else if` chain and a text return.

The larger check is a file that verifies itself: `fib(20)` at 6765 through
recursion, Collatz of 27 at 111 steps, a short circuit that would divide by
zero if it did not short circuit, and float arithmetic landing between 0.29
and 0.31. It exits zero, and each failure would have exited with its own code.

Two things in the machine are decisions.

**The operand stack is sized at compile time.** The compiler follows its own
emit sites and records the exact depth each function reaches, so the machine
checks for room once per call rather than once per push. A per-push bound
check is the kind of cost that is easy to add and hard to remove later, and
this language's first goal is speed.

**A runtime failure is a diagnostic.** Division by zero, call depth and a
missing `main` report with a code, the source line and a caret, and
`--errors=json` covers them. Nothing repairing a program has to know whether
it is reading a compile failure or a runtime one, which is the property that
matters for the fix loop D008 exists for.

Two fixes. A diagnostic about the file rather than a place in it used to point
a caret at line one; a zero-length span now renders without one. And frames
moved into the arena, so the call depth limit is Kest's own number rather than
whatever fits on the host's stack.

**Runs:** `kest run <file>`. `make debug` clean across 26 files and five
commands, checked on the output rather than the exit status.
**Next:** value structs and arrays, in the checker, the compiler and the
machine together.

## 2026-09-07, value structs

D006 is the decision this project took from a measurement it did not make:
with only managed references, a frame step written with vectors took six
allocations a frame inside a promise not to allocate, and no annotation could
fix it. Value structs are what fixed it there. They work here now.

A struct is laid out flat. `Body` holding two `Vec3` is seven slots, not two
pointers, and a nested field is reached by adding offsets. `examples/physics.kest`
is that frame step: helpers that take and return vectors, called from a loop.
`step` compiles to six instructions.

```
fn step  7 parameter slots, 7 slots, 7 deep
  0000  load.n      0  6
  0005  load        6
  0008  call        3  7
  0013  load        6
  0016  call        4  7
  0021  return      6
```

What `p.position.y = 9.5` costs is `store 1`. The offset is arithmetic the
compiler did, so a field access is not a load and a struct is not an
indirection.

Construction is call syntax, `Vec3(1.0, 2.0, 3.0)`, and D011 records why: a
braced literal would need a rule about where a brace may start an expression,
which is the rule `if p.y < 0.0 {` currently needs none of. It also emits
nothing, because the fields were pushed in layout order.

A struct that contains itself is refused with its size, and told to hold
itself through `ref`, which is a handle.

Module constants compile now, written into each use rather than loaded, which
is what makes them constants. Only literal ones; anything else says so.

**Runs:** `examples/physics.kest`, a hundred steps of falling with the vectors
intact. Clean under ASan and UBSan across 30 files and five commands.
**Next:** arrays, which is what `examples/frame.kest` and
`examples/player.kest` are still waiting on.

## 2026-09-07, arrays

Literals, `len`, indexing on both sides, `for ... in`, and arrays of structs.
An array of `Vec2` has a stride of two slots, `points[1].x` reads through it,
and `points[1] = Vec2(0.0, 0.0)` writes two slots into place.

`for x in a` is written as an index walk in the compiler rather than in the
parser, so the counter and the array handle sit in slots nobody can name or
assign to. That exposed something the `while` loop had not: `continue` was
compiled as a jump to the top of the loop, which in a `for` skips the step and
runs forever. Both loops now land their `continue` on a pad between the body
and the step.

`==` on an array or a struct is refused. It has more than one reasonable
answer and the one a handle comparison gives is the wrong one.

D012 records the two decisions: bounds checks are on, and nothing frees an
array yet, which is the absence of a memory model rather than a choice of one.

**A bug that only a running program could find.** `examples/player.kest`
returned the wrong answer, and the cause was that a constant's value was never
type checked. Nothing had a type on it, so the compiler's test for "is this a
float" said no, and `const GRAVITY: f32 = -9.81` compiled to an *integer*
negate over the bits of a double. `examples/physics.kest` had the same bug and
passed, because its assertions were loose enough not to notice. Constants are
checked against their declared type now, which is both the fix and a class of
error the file could not report before.

**And a program that was wrong, where the language was right.**
`examples/player.kest` had `update` changing its parameter and the caller
reading the change. A struct is a value, so it was changing its own copy. The
example was written before anything could run. It returns the player it
produced now, which is what D006 means in a program rather than in a decision
record. There is no way to pass a struct by reference yet, and that is the
next real gap.

**Runs:** three of the four examples. `examples/frame.kest` is a checking
example and says so. Clean under ASan and UBSan across 35 files.
**Next:** `ref<T>`, which is what a by-reference parameter and
`examples/frame.kest` both need.

## 2026-09-07, places

`enemies[i].health -= 10` is the most ordinary line a game script has and it
did not compile. Assignment only reached slots a name could be added to, so a
path that went through an array had nowhere to put the result.

Paths now compute an address. `elem.addr` bounds-checks an index and leaves
the address of the element, and `load.at` and `store.at` read and write a run
of slots at an offset from it. The address lives for one statement, during
which nothing can move what it points at. `w.enemies[i].health -= amount` is
nine instructions with one bounds check, and a path that touches no array
still reaches its slots by arithmetic and takes no address at all.

This is the machinery `ref<T>` needs. What `ref<T>` needs beyond it is a
decision about what happens when the thing it points at is gone, which is the
memory model D012 defers. The address is not that: it cannot be stored, cannot
outlive its statement, and is not a value the language has a type for.

`examples/world.kest` is what a struct is and what an array is, in one
program. Copying a `World` copies its fields, and one of those fields is a
handle, so the copy and the original name the same elements.

**A warning, which is the first one this compiler emits.** `for e in a` binds
a copy, so `e.health = 0` is legal, does nothing to the array, and used to do
it silently. It now says so and suggests indexing instead. The rule is about
the path rather than the name: `e.items[0] = 9` crosses an array, so it is
visible and is not warned about, while `e.count = 0` beside it is.

The first version of that rule warned about both, because it looked at the
field steps and not at the whole path. Written the short way, walk the path and
stop at the first index, it is right and is four lines.

**Runs:** four of five examples, `examples/frame.kest` still checking only.
Clean under ASan and UBSan across 39 files.
**Next:** `ref<T>` itself, which now needs the memory model rather than the
machinery.

## 2026-09-07, optionals

`ref<T>` was the next thing, and it turned out to need this first: reading
through a reference that may be stale is a lookup that can fail, and the
language had no way to receive a failure.

`T?` is what it holds with a tag after it, so it is a run of slots like any
other value and a miss costs no allocation. A value standing where an optional
is wanted becomes one, which is the only implicit conversion in the language.
`none` takes its type from where it is written and says so when there is
nowhere to take it from.

`if let x = e { } else { }` is the only way to open one. The property that
matters is not that it is checked but that `x` is scoped to the arm: code that
uses the result cannot be written where the result might not exist, because
the name is not there. `return x` after the arm is `unknown name x`, which is
the test in `/tmp/opterr.kest` and the reason D013 says there is no unwrap
operator.

Optional structs work: `Vec2?` is three slots, `if let p = pick(points, 3.0)`
binds two of them and the failing arm drops them.

**Runs:** five of six examples, `examples/lookup.kest` added. Clean under ASan
and UBSan across 41 files.
**Next:** `ref<T>` again, now with somewhere for its failure to go. What it
still needs is a store that can delete, which is the first thing in this
project that has to say something about memory.

## 2026-09-07, the contract

`no.alloc` has been in this language since the first commit, in the syntax, in
the examples, and in three decision records. The compiler accepted it and did
nothing with it, which is worse than not having it: a promise nobody checks is
a comment that looks like a guarantee. `hot(n) no.alloc { let scratch = [0, 0,
0] }` compiled and ran.

`contract` is a pass over the checked tree. It finds where each body allocates,
builds the call graph, spreads allocation up it to a fixed point, and refuses
every promise that does not hold.

Two things about it are from the predecessor's measurements rather than from
taste.

**It infers inside the file and reads declarations at the boundary.** A callee
defined here is judged by its body, transitively, so only entry points carry
the annotation. ADR-0008 measured that: two annotations where the strict
reading costs sixteen, on an eighteen-function frame step four hops deep. A
foreign function is judged by what it declares, because its body is not here.

**The refusal lands on the allocation, not on the promise.** The same
measurement found that naming the promise leaves the author a mean of 2.17
hops to walk, worst case 3, and that reporting the path takes both to 0. So:

```
error[K0401]: this allocates, and `stepFrame` promises `no.alloc`
 --> chain.kest:5:17
  |
5 |     let trail = [n, n, n]
  |                 ^^^^^^^^^ reached through `second` -> `third` -> `leaf`
```

Building an array is the only thing in this language that reaches the heap, so
the contract has exactly one direct source and one indirect one. That is a
small claim today and it is the true one.

All six examples keep their promises, including the frame step in
`examples/physics.kest` that D006 exists for. Mutual recursion terminates, and
recursion that allocates is caught.

**A gap found on the way.** `extern fn Clock.now()` could be declared and not
called: `Clock.now()` parsed as a field of a `Clock` that does not exist. An
extern is named for the host type it belongs to, so the receiver is part of
what it is called, in the checker and in the call graph both.

**Runs:** everything that ran before, plus the contract holding over it. Clean
under ASan and UBSan across 47 files.
**Next:** `ref<T>` and the store it needs, which is still the first thing here
that has to say something about memory.

## 2026-09-07, references and the store

`ref<T>` needed something to say about memory, and the thing it says is
neither collection nor counting nor regions. `store<T>` is a slot map: it owns
its elements, hands out a reference that is an index with the generation it
was handed out at packed above it, and `remove` marks the slot dead and steps
the generation. `get` returns `T?`, which is why optionals came first.

The tests are the two that matter. A slot freed and filled again does not
resurrect the handle that named what was there before, because the generation
moved. And two characters pointing at each other is one line, because nothing
owns anything and there is no cycle to break.

```kest
set(world, guard, Npc("guard", smith, none))
set(world, smith, Npc("smith", guard, none))
```

`examples/quests.kest` is the object graph the predecessor kept measuring.
The smith dies, and the guard's escort reference and the quest's giver
reference both read as gone rather than as a pointer that lies.

D014 records the trade and where it came from: W03 found every idiomatic
implementation across five languages and three memory models converging on
exactly this, and a deletion costing four to six reads. That batch was
rejected, so it is a direction and not a result, and the record says so.

**What it does for the contract.** `get`, `set` and `remove` allocate nothing,
so a frame step can walk the graph, follow references and delete inside a
`no.alloc` promise. `add` can grow the store and cannot. The compiler draws
that line:

```
error[K0401]: this allocates, and `spawn` promises `no.alloc`
 --> storecontract.kest:3:12
  |
3 |     return add(w, N(1))
  |            ^^^^^^^^^^^^
```

The builtins are checked in one place now rather than branched at each site,
and a file that declares its own `len` or `get` gets that one, so none of them
is a reserved word.

**Runs:** six of seven examples. Clean under ASan and UBSan across 51 files.
**Next:** text. `print` takes a literal and nothing builds one; `"{x}"` parses
and is emitted with the braces still in it, which is the last place the
language does something other than what it says.

## 2026-09-07, text, and a type that was not what it said

`"{x}"` parsed and printed with the braces still in it, which is the last
place this language did something other than what it said. It builds a string
now. The holes are parsed from the source they were written in, using a lexer
started at an offset, so a mistake inside one reports at the character it is
at rather than at the string.

Building text reaches the heap, so the contract charges for it and a `no.alloc`
function may hold a string and may not build one. That is the first place the
contract says something a reader would not have guessed.

**And the feature found a bug in the type system.** Printing `0.1 + 0.2`
honestly, by the shortest spelling that reads back as the same number, printed
the `f64` answer for two `f32` values. The types said `f32` and the machine
computed in `f64` throughout. That is not a rounding detail: the whole point
of matching an engine's layout is getting the engine's answer, and a `f32`
that is secretly a `f64` gets a different one.

Five instructions fix it. A slot still holds a double, and `f32` arithmetic
rounds the double result to `f32`, which is exactly right for add, subtract,
multiply and divide because a double has more than twice the precision. An
`f32` literal is now the nearest `f32` rather than the nearest double spelled
the same way.

```
f32: 0.3                      via add.f32
f64: 0.30000000000000004      via add.f
f32 third: 0.333333343        via div.f32
f64 third: 0.33333333333333331 via div.f
```

**A second bug behind that one.** When a literal takes its type from the other
side of an operator, the checker was fixing its own copy and leaving the wrong
type on the tree. The compiler reads the type from the tree to choose between
`div.f32` and `div.f`, so `1.0 / e` with an `f64` `e` was dividing as `f32`. A
type only the checker knows is one nobody applies.

**Runs:** every example prints something it computed.
`player.kest` says `dead after 94 ticks, at y 0.0 with 0 health`. Clean under
ASan and UBSan across 58 files.
**Next:** modules. `import` binds a name and nothing else, which is the last
declaration in the language that does not mean anything.

## 2026-09-07, modules

`import` bound a name and did nothing. It reads a file now: `import game.world`
is `game/world.kest` beside the importing file, followed transitively, and a
file read twice is read once.

Every name lives under the last part of its module's name. A struct `Npc` in
`module game.world` is `world.Npc`, and inside that file it is also `Npc`.
Nothing is brought in unqualified, so where a name came from is written at
every use of it, which is the property a reader and a model both want. The
lookup is one rule: try the current file's own module, then the name as
written.

That rule made most of the change small. `world.Npc` and `Clock.now` are the
same shape, one name with a dot in it, so calling into a module and calling a
host function resolve through the same path, and a dotted name works as a type
because `parse_type` reads a path rather than an identifier.

**The structural part was diagnostics.** A span said where in a file something
was and nothing said which file, which was true while there was one. Each
diagnostic carries its source now, and every stage says which file it is
working on before it reports. That is what lets a contract broken in one file
by a body in another point at the body:

```
error[K0401]: this allocates, and `app.tick` promises `no.alloc`
 --> /tmp/lib/heap.kest:3:13
  |
3 |     let t = [n]
  |             ^^^ reached through `heap.scratch`
```

The first version of that reported the right line number against the wrong
file, because the path down to the allocation carried a span and not the unit
it was in. A span is not a place until something says where.

**Known gap.** The namespace is flat, so a file can reach a module that
something else imported without importing it itself. The names are still
qualified and still say where they came from, but the check that you asked for
them is missing.

**Runs:** `examples/game.kest` imports `examples/game/world.kest`. Clean under
ASan and UBSan across 65 files.
**Next:** that gap, and then the host boundary, which is the last of the
measured decisions with nothing behind it: `extern` declares and nothing links.

## 2026-09-07, the host, and a namespace that meant it

Two things, and the first one is small. A file could reach a module something
else had imported, because the namespace is flat and a qualified name resolves
wherever it was declared. The check is not about how a name is spelled but
about where it was declared: a name from another file needs its module named
in this one. A host receiver has a dot in it and is not a module, and it was
the first version of this check that said so, wrongly, about `Engine.spawn` in
a file with no module at all.

Import paths also moved. They resolved relative to the file that wrote them,
so `mods/b.kest` importing `mods.a` looked for `mods/mods/a.kest`. They
resolve from the root, which is the directory of the file the command named,
so a module path names one file however it is reached.

**And `extern` means something now.** `include/kest.h` is an embedding API: a
host binds C functions by name, and the program says what it needs.

```kest
extern fn Host.sqrt(value: f64) -> f64 no.alloc

fn hypotenuse(a: f64, b: f64) -> f64 no.alloc {
    return Host.sqrt(a * a + b * b)
}
```

The command line binds three, which is not a standard library, it is enough
for `examples/host.kest` to run. A declaration nobody provides refuses the
program at the line that declared it:

```
error[K0606]: the host does not provide `Engine.spawn`
 --> nohost.kest:1:11
  |
1 | extern fn Engine.spawn(n: i32)
  |           ^^^^^^^^^^^^
```

A host function receives its arguments where the slots are and writes its
result over them, which is exactly what a Kest call does, so there is no
marshalling and nothing to convert.

D015 records what this is and what it is not. It is the inward direction. The
outward one is a separate specification by D007 and is not built, and neither
is the bulk borrowed crossing D007 makes the default, because borrowing host
storage means reading the host's layout and this machine's slot is eight bytes
whatever the type. What exists is the shape W11 measured as the expensive one,
and it is written down that way.

**Runs:** eight of nine examples. Clean under ASan and UBSan across 73 files.
**Next:** the value representation the bulk crossing needs, which is the last
thing between this and the decision the whole predecessor programme pointed at.

## 2026-09-07, two layouts

This is the one the predecessor's whole measurement programme pointed at, and
what it needed was not a decision about memory management but a decision about
what a value *is*.

Every value was an eight-byte slot. An array of `Vec3` was twenty-four bytes an
element where a C engine has twelve, so nothing could be shared across the
boundary and everything had to be converted at it, which is the shape W11
measured at 9.63 times.

There are two layouts now. On the stack a value is slots. In an array it is
what a C compiler would give it, and the checker prints both:

```
struct Particle  6 slots, 32 bytes aligned 8
  slot +0  byte +0   position: Vec3
  slot +3  byte +12  alive: bool
  slot +4  byte +16  id: i32
  slot +5  byte +24  mass: f64
```

A C program compiled beside it reports 32 bytes aligned 8 with the members at
0, 12, 16 and 24. An optional is what it holds and a byte saying whether it
does: `i32?` is eight bytes, which is what `struct { int32_t; bool; }` is.

So the block behind an array is the block a host already has:

```kest
extern fn Host.samples() -> [f32] no.alloc

fn totalOf(samples: [f32]) -> f32 no.alloc {
    let total: f32 = 0.0
    for s in samples {
        total += s
    }
    return total
}
```

`Host.samples` hands over a `float[1024]` the command line owns, without
copying it. One crossing, and reading `samples[i]` reads the host's float
because a Kest `f32` is the same four bytes. Writing `samples[0] = 99.5` and
asking the host what its own array holds gets 99.5 back, which is the test
that there is one copy of it and not two.

`totalOf` promises `no.alloc` and the compiler proves it, because reading a
borrowed array allocates nothing.

D016 records the split and where it came from: W11 found that what costs is
the crossing and not the load, so the crossing shares memory and the
arithmetic uses slots. What it costs is that a local is still eight bytes
whatever it holds, which nothing has measured and nothing can share.

**Runs:** eight of nine examples, and `examples/host.kest` now crosses the
boundary both ways it can. Clean under ASan and UBSan across 75 files.
**Next:** the outward direction, which D007 says is its own specification and
which nothing here has built: the host cannot call into a Kest program at all.
