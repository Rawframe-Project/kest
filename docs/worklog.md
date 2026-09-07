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

## 2026-09-07, the outward direction

The host could not call into a Kest program at all, which is half of the
boundary D007 specifies and the half W11 measured as the wider one.

The machine outlives a call now. `kest_runtime_new` builds one, resolves what
the host provides, and holds the stack, the frames and the heap;
`kest_call(runtime, name, frame)` runs a function with the arguments in
`frame` and writes the result over them. That is the same convention a host
function is handed in the other direction, so nothing is marshalled either
way. `kest run` is that call, made once, on `main`.

`kest tick <file> [n]` drives a program from the host both ways:

```
$ ./kest tick examples/events.kest 1024
onEvents  1 crossing   returned 174933
onEvent   1024 crossings returned 174933
```

The same answer from one crossing carrying a batch the host lent, and from a
thousand and twenty-four crossings carrying one event each. The language makes
the first easy and keeps the second expressible, which is what D007 decided
from W11's figure of 19.28 times for the per-item outward path.

Both are `no.alloc` and the compiler proves it, so walking a lent batch is a
frame-budget operation.

**A regression the sweep caught.** A diagnostic about the program rather than
about a file now had no file, and the renderer dereferenced it. Both renderers
handle that: a message with nowhere to point at prints without a location
rather than inventing one. The run path sets the file the command named before
running, so "this file has no `main`" still points at the file it is about.

**Runs:** nine of ten examples, and `kest tick` on the tenth. Clean under ASan
and UBSan across 76 files and six commands.
**Next:** the `f32` gap in the other direction. A `f32` local is a double in a
slot and rounds correctly, but nothing rounds a `f32` read from an array
before it is used, and nothing needs to; what has no answer yet is `i8` and
`u8` arithmetic, which wraps at 64 bits where the type says 8.

## 2026-09-07, narrow integers

The same fault as the `f32` one, in the other half of the type system. `u8`
plus `u8` was computed at sixty-four bits and kept the answer, so two hundred
plus one hundred was three hundred and `i32` at its maximum plus one was two
thousand million and something. Five cases against a C program compiled beside
them, all five wrong.

An instruction cuts a result to the width its type declares. Every integer in
a slot is kept at that width, sign extended or zero extended, which is what
lets a comparison and a division stay one instruction each rather than one per
width. All five agree with C now:

```
u8 200 + 100 = 44        i8 127 + 1 = -128
i32 max + 1 = -2147483648   i16 300 * 300 = 24464
u32 max + 1 = 0
```

And a literal that does not fit its type is refused rather than wrapped:

```
error[K0326]: 256 does not fit in `u8`
error[K0326]: `u8` holds no negative numbers
error[K0326]: 128 does not fit in `i8`
```

`-128` in an `i8` is accepted and `128` is not, which needs the sign to be
part of the question rather than applied to the answer: the checker knows it
is inside a negation while it reads the number.

D018 records the cost as well as the rule. `i32` is the default integer type,
so a loop counter now pays an instruction twice a turn:

```
  0038  const       0  ; 1
  0041  add.i
  0042  narrow      2
  0045  store       2
```

Folding that into the arithmetic is six more opcodes and is the obvious thing
to do when something measures it mattering. Nothing has, and D010 is the
precedent for saying so rather than doing it.

**Runs:** nine of ten examples and `kest tick` on the tenth. Clean under ASan
and UBSan across 76 files and six commands.
**Next:** `i64` and `u64` division, which is the one width where the sign
still has to be asked about, and then what the type system does about
conversion: there is no way to turn an `i32` into an `f32` at all.

## 2026-09-07, conversion

`i64` and `u64` were the one width left to check. Division, modulo and
comparison were already right, because every integer is kept at its declared
width and an unsigned one is therefore never negative. Printing was not: `u64`
at its maximum printed as minus one, because the only way to write a number
was the signed one.

The larger gap was that there was no way to turn an `i32` into an `f32` at
all. There is now, and it is written the way a struct is built:

```kest
return total / f32(len(w.enemies))
```

D019 records why that is the same syntax and not a cast operator: D011 chose
call syntax for a struct to avoid a grammar rule, and the rule that came out of
it turned out to be worth more than the reason. Naming a type makes one of it.

The two conversion rules are deliberately different and it is worth saying so.
An integer into a narrower integer wraps, because that is what C does and D018
is the argument for why that matters. A float into an integer stops at the end
of the range, because C has no answer there and an undefined answer is one that
differs between machines.

```
i32(3.5) = 3            i32(-3.7) = -3
u8(300.0) = 255         i8(-1000.0) = -128
u8(300) = 44            i32(true) = 1
```

**Two bugs the sweep found.** The compiler had its own integer reader,
accumulating in a signed sixty-four bit value, so a `u64` literal at its
maximum overflowed while being compiled. It uses the lexer's now, which the
checker was already using, and there is one reader rather than two.

And the example that was meant to demonstrate the conversion asserted an
average of 1.5 for three heights that average 1.8333. The language was right
and the expectation was not, which is the second time an example has been the
thing that was wrong.

**Runs:** nine of ten examples, `kest tick` on the tenth. Clean under ASan and
UBSan across 81 files and six commands.
**Next:** `while` is the only loop over anything that is not an array, and
there is no way to walk a `store`. A frame step that iterates the object graph
cannot be written.

## 2026-09-07, walking a store

A frame step over the object graph could not be written: `for` walked arrays
and nothing else, so there was no way to visit what a store holds.

It walks a store now and binds a `ref<T>`, because a reference is what
removing and writing take, and a frame step over an object graph does both.
Removing during the walk is allowed and the slot goes dead behind the cursor.
Because slots go dead, the walk searches for the next live one rather than
counting to it, and where it stopped is where it resumes.

`examples/quests.kest` has the step the object graph exists for:

```kest
fn decay(world: store<Npc>, amount: i32) -> i32 no.alloc {
    let removed = 0
    for r in world {
        if let npc = get(world, r) {
            if npc.name == "guard" {
                set(world, r, Npc(npc.name, npc.escort, npc.quest))
            } else {
                remove(world, r)
                removed += amount
            }
        }
    }
    return removed
}
```

It walks every live character, reads through the references it finds, writes
some back and removes the rest, and it promises to allocate nothing. The
compiler proves it, because walking, reading, writing and removing all touch
no heap and only `add` does.

D020 records the part that is not comfortable. That `get` returns an optional
it cannot fail: the reference came from the store's live set this turn. The
predecessor's probe 4 asks whether the failure arm reads as noise at ten
thousand call sites and records it as untested; it is written now rather than
asked about. Making the arm go away means telling the type system the
reference is live, which is a claim about lifetime that D014 gives no way to
make, so the noise stays visible until something can measure it.

**A slip while editing.** The edit that replaced the `for` case took `return`
with it, because the case after `for` in that switch is not the one the
replacement stopped at. Caught by the compiler in the same second, which is
what `-Werror=switch` is for.

**Runs:** nine of ten examples, `kest tick` on the tenth. Clean under ASan and
UBSan across 82 files and six commands.
**Next:** `while` and `for` are the only loops, and neither carries an index
when walking. `for i, x in a` does not exist, so anything needing the position
falls back to a `while` with a counter the author maintains.

## 2026-09-07, the position

Anything that needed where it was in a walk fell back to a `while` with a
counter the author kept. `for i, x in a` binds the position too, and
`examples/world.kest` is the case it was needed for: the loop's `e` is a copy
so writing back needs `w.enemies[i]`.

```kest
fn damageAll(w: World, amount: i32) {
    for i, e in w.enemies {
        if e.health > 0 {
            w.enemies[i].health -= amount
        }
    }
}
```

The name is a copy of the walk's own count rather than the count itself, so
assigning to it cannot make the walk go wrong. It also cannot do anything,
which is the same trap the element copy is, and the warning covers both with
the reason each needs:

```
warning[K0321]: `i` is the loop's own, so this is discarded
  |         ^ the walk keeps its own count, which this is a copy of
warning[K0321]: `p` is the loop's own, so this is discarded
  |         ^^^ index the array to write to it: `a[i]` names the element
```

A store has no position to give: its slots are an implementation and its
reference is what names one. `for i, r in world` is refused and says that.

**Runs:** nine of ten examples, `kest tick` on the tenth. Clean under ASan and
UBSan across 84 files and six commands.
**Next:** every diagnostic in this compiler points at one span, and some of
them are about two places: a duplicate names the line of the first in prose,
and a contract failure names a call path in a sentence. Both would be clearer
as what they are, which is a second span with its own line.

## 2026-09-07, a diagnostic about two places

Every diagnostic pointed at one span, and several of them were about two. A
duplicate declaration named the first one's line number in prose. A broken
`no.alloc` promise named the whole call path in one sentence, which was a
sentence that got longer the deeper the path went and never said where any of
it was.

A diagnostic carries notes now: a span, a file and a label, each rendered as
its own frame. The contract failure is the one it does most for, because the
path is what it is about:

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
  --> chain.kest:14:12
   ...
```

The promise comes first and the calls follow in the order they are made, so
the chain reads forwards from what was promised to what breaks it. Across
files it says which file each hop is in, which the sentence could not.

Duplicate declarations, duplicate fields, duplicate parameters, a shadowed
local and a name reached from a module that was never imported all point at
the first one now instead of describing it. And the JSON carries the notes, so
what a model reads is what a person reads.

One detail that mattered more than expected: every frame of one diagnostic
shares a gutter width, computed across the primary and all its notes. Without
that, a note on line 5 and a note on line 17 print their source lines one
column apart and the whole thing looks broken.

**Runs:** nine of ten examples, `kest tick` on the tenth. Clean under ASan and
UBSan across 86 files and six commands.
**Next:** `kest fmt`. CLAUDE.md has said since the first commit that a
canonical form is what makes the strict parser bearable, and nothing writes
one.

## 2026-09-07, one form

`CLAUDE.md` has said since the first commit that a canonical form is what
makes a strict parser bearable, and nothing wrote one. `kest fmt` does.

It prints from the tree and not from the tokens, and that is the whole reason
it works: `ref<Npc>` and `a < b` are the same three tokens with the same
spacing, and no amount of looking at the characters tells them apart. The tree
knows which is a type.

The same choice fixes brackets. `(1 + 2) * 3` keeps its brackets, `1 + 2 * 3`
never had any, and `1 - (2 - 3)` keeps its because taking them away would
regroup. The formatter puts one back exactly where the tree says one is
needed, rather than preserving what was there.

Comments are kept, scanned out of the source separately and emitted before the
first thing that starts after them, at that thing's indent. A `//` inside a
string is not a comment and the scanner knows it.

Every example is in the form `kest fmt` prints now, and each one still parses
to the same tree and still does the same thing.

**Two bugs while writing it.** The `else if` arm printed at indent zero,
because the trick for keeping a chain on one line was to zero the indent
rather than to skip printing one. And every blank line after a closing brace
disappeared, because the block printer forgot where it had got to instead of
remembering the brace's line.

**What it does not do.** It does not break long lines. An expression comes out
on one line however long, which turned one wrapped constructor in
`examples/world.kest` into a hundred and seven characters. The example was
rewritten to be short rather than the limitation hidden.

**Runs:** nine of ten examples, `kest tick` on the tenth. Clean under ASan and
UBSan across 89 files and seven commands.
**Next:** that limitation. A formatter that makes lines longer is one people
turn off, and the rule is the usual one: if the arguments do not fit, each
goes on its own line.

## 2026-09-07, breaking a long line

A formatter that makes lines longer is one people turn off. This one did:
a wrapped constructor came back as a hundred and seven characters.

Deciding to break needs the width before printing, and the way not to get it
is a second function that describes what a thing looks like, because two
descriptions drift. Everything the printer writes goes through one call now
that tracks the column, and measuring is printing with the writing turned off.
There is one description.

A list that does not fit in eighty columns goes one item to a line, all of
them or none. Half on one line and half on the next is the arrangement nobody
asked for, and packing as many as fit makes a diff churn every time one
element changes length.

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

The inner array breaks too, because after the outer one broke it starts at
column eight and still does not fit. That falls out of measuring from where
the printer actually is rather than from column zero.

**Two things went wrong.** Measuring called the printer, which asked whether
it fit, which measured: it went round until the stack ran out. While counting,
the answer is the flat width, which is the thing being measured.

And every statement got a blank line after it on the second pass, because the
printer remembered where a statement *began* and a broken argument list makes
that a different line from where it ends.

**Runs:** nine of ten examples, `kest tick` on the tenth, every one of them
already in the form `kest fmt` prints. Idempotent on sixteen files including
the deliberately ugly one. Clean under ASan and UBSan across 93 files and
seven commands.
**Next:** two lines in the examples are over eighty columns and both are
`print` with a long string in it, which nothing can break. What can be broken
and is not is a long chain of operators.

## 2026-09-07, breaking a chain, and a formatter that wrote invalid code

A long chain of operators broke the wrong way twice before it broke the right
way.

The tree nests to the left, so `a || b || c` is two nodes and breaking the top
one alone puts `(a || b)` on a line by itself. Everything at one precedence is
one chain and has to break as one, which means flattening the left spine
first.

Then it emitted this:

```kest
let total = alpha * 1000
    + beta * 2000
```

which does not parse. A line ending in `1000` ends a statement, and D003 is
the decision that says so. The operator ends the line:

```kest
let total = alpha * 1000 +
    beta * 2000
```

Leading-operator style is not available in this language, and that is a
consequence of the newline rule rather than a preference.

**The verification was not verifying.** Idempotence was being checked and
faithfulness was not, so a formatter emitting code that does not parse passed.
`tools/check-fmt.sh` checks what a formatter has to be true of: the output
parses, it produces the same tree, and formatting it again changes nothing.
Ninety-seven files pass it, and it found the last bug on its own: a broken
condition put a blank line between `{` and the first statement, because that
statement was being compared against the line the `if` started on. Nothing is
separated from the brace that opened it now.

**Runs:** nine of ten examples, `kest tick` on the tenth. Clean under ASan and
UBSan across 97 files and seven commands.
**Next:** `kest fmt` prints to standard output and nothing writes a file, so
using it means a shell redirect that truncates the file it is reading.

## 2026-09-07, writing the file

`kest fmt` printed to standard output and nothing wrote a file, so using it
meant `kest fmt f > f`, which truncates `f` before reading it. The formatter
was unusable on the thing it was for.

It returns the text now rather than writing it, which is what lets the caller
compare it against what is on disk. A file already in the form it prints is
not written at all and nothing is said about it.

`-w` writes each file it is given and names the ones it changed. `--check`
names them without writing and exits non-zero, which is the question "is this
already right" and the one a build asks. Writing goes through a file beside
the target and renames over it, so a program that stops half way leaves the
file rather than half of it.

Each file is its own answer: one that does not parse is reported, left exactly
as it was, and does not stop the rest.

**Runs:** `kest fmt -w` over three files rewrites the two that needed it,
leaves the third, and leaves no temporary behind. Clean under ASan and UBSan
across 102 files and seven commands, and `tools/check-fmt.sh` passes on all of
them.
**Next:** `kest` reads one file per command and every command but `fmt` takes
exactly one. Checking a project means checking its entry point and hoping
everything is reachable from it, and a file nothing imports is never looked at.

## 2026-09-07, a project rather than an entry point

Every command read one file and followed its imports, so a file nothing
imports was never looked at. `kest check *.kest` reads them all now, and the
difference is not theoretical: a file with a type error in it passes when only
the entry point is checked and fails when the project is.

Two things came out of pointing it at the examples.

**Two modules were both called `world`.** Names live under the last part of a
module's name, so `examples.world` and `game.world` would have put their names
under one. Nothing tells them apart, and that is refused now with both `module`
lines shown. One of the examples was renamed. An alias on the import is the
real answer and there is no syntax for one.

**The root was the wrong directory.** Imports resolved from the directory of
the file named, so a file inside a package directory looked for its imports one
level too deep. The root is worked out from what the file calls itself: `module
a.b.c` at `x/y/a/b/c.kest` means the root is `x/y/`. A module's name is where
its file is, and the examples were made consistent with that.

**And `tools/check-fmt.sh` was comparing the wrong thing.** It formatted into a
copy beside the original, which has a different path, which under the new rule
is a different module, whose imports no longer resolve. It formats the file in
place and puts it back now, which is the only way the comparison is of the same
file.

**Runs:** nine of ten examples, and the whole `examples` directory checks as
one project. Clean under ASan and UBSan across 106 files and seven commands.
**Next:** `kest fmt` is the only command that reads a file without following
its imports, and it is right to. Everything else is one pipeline with a command
name in front, which is fine until the first thing that wants two of them.

## 2026-09-07, an array whose size nobody wrote down

The last entry's "next" was an observation about `main.c` rather than a job,
and building for the first thing that might want two pipelines is the kind of
work this project exists not to do. So: the real gap.

Every array was a literal. There was no way to make one of a size worked out
while running, and no way to put anything on the end of one, so every program
had to know all its sizes when it was written.

`array(n, v)` makes one, and `push` adds to it. What it holds comes from what
it is filled with, so `array(0, Sample(0, 0.0))` needs no type written down.
Growing allocates a bigger block and copies, and the handle is the header
rather than the block, so every reference to the array sees the growth.

**An array the host lent cannot grow.** The block is not Kest's to move, and
growing one would write past what was lent or silently stop sharing it. It
fails with a message:

```
error[K0608]: this array is the host's, so it cannot grow
 --> borrowpush.kest:5:5
  |
5 |     push(lent, 1.0)
  |     ^
```

Both `array` and `push` reach the heap and the contract charges for them, so a
frame step can read an array and cannot build one. Reading and indexing still
cost nothing.

`examples/grow.kest` builds a list of samples from nothing, sums it inside a
`no.alloc` promise, and finds the highest as an optional, also inside one.

**A code was used twice.** The borrowed-array failure was given `K0607`, which
already meant "this program has no such function to call". `CLAUDE.md` says
codes are never reused and it was right to.

**Runs:** ten of eleven examples, `kest tick` on the eleventh. Clean under ASan
and UBSan across 110 files and seven commands.
**Next:** `len` counts an array and a store and not a piece of text, and
nothing compares two pieces of text except for equality. A program can build a
string and then do nothing with it.

## 2026-09-07, text is its bytes

A program could build a string and then do nothing with it. `len` counts its
bytes now, `t[i]` reads one as a `u8`, and two pieces compare by them.

D021 records what that means and what it costs. A piece of text is a pointer
and nothing else, because the host hands one over as a `const char *` and D016
says a value shared with the host is what the host has. So `len` walks the
string rather than reading a field, `"hız"` is four bytes and not three
characters, and there is no character type.

That last one is a decision not to decide. A character is a byte, a code
point or a grapheme, and every language that picked before it had programs
picked wrong for somebody. This has one program's worth of evidence and picks
the byte, which is the thing that is actually there.

`examples/words.kest` sorts an array of names, which needs the ordering and
nothing else, and counts vowels by walking the bytes.

**A parser bug the example found.** A string inside an interpolation hole did
not work at all: `"{vowels("herald")}"` ended the outer string at the inner
quote. The lexer counts braces while it reads a string now, so the quote that
closes one is the one found outside every brace. The formatter's comment
scanner had the same fault and the same fix, so a `//` inside a nested string
is still not a comment.

**Runs:** eleven of twelve examples, `kest tick` on the twelfth. Clean under
ASan and UBSan across 114 files and seven commands.
**Next:** the missing half of text, which D021 names: there is no way to take
a piece of text apart into another piece of text. No slice, no substring, and
so no parsing anything.

## 2026-09-07, taking text apart

D021 named the missing half and this is it. `find(t, needle)` gives where
something is or nothing, and reads without allocating. `slice(t, from, count)`
makes a new piece of text, which reaches the heap, because a piece of text is a
pointer to something that ends in a nought and a window into the middle of one
is not that.

That is the whole addition, and it is enough to write a parser.
`examples/parse.kest` reads `health=30,armour=12,speed=7` into an array of
structs: it finds the separators, cuts at them, and reads the numbers out of
the bytes, because there is no conversion from text and this is what one would
be.

The contract draws the line where it should: `find` is free and a frame step
may use it, `slice` is not.

```
error[K0401]: this allocates, and `cut` promises `no.alloc`
 --> slicecontract.kest:2:12
  |
2 |     return slice(t, 0, 1)
```

**Runs:** twelve of thirteen examples, `kest tick` on the thirteenth. Clean
under ASan and UBSan across 116 files and seven commands, and the whole
`examples` directory checks as one project.
**Next:** `number` in that example returns `i32?` and every program that reads
a number will write it again, which is what a standard library is. There is no
way to write one: nothing can be imported that is not beside the program.

## 2026-09-07, a standard library

`examples/parse.kest` wrote a number reader and a splitter, and every program
that reads text would have written them again. `lib/std/text.kest` has them:
`number`, `split`, `trim`, `starts`, `ends`, `contains`.

It is Kest source, not builtins. `number` reads bytes and `split` cuts, and
both are things a program can already do, so making them builtins would say the
language could not do what it can. It also holds the library to the same rules:
`number` promises `no.alloc` and the compiler proves it.

`std` is reserved. A module named that always comes from the library, which is
at `$KEST_LIB` or `lib/` beside the compiler. The alternative is a search order,
and a search order means a file can quietly shadow a library module and a
reader has to know the order to know what they are looking at.

`examples/parse.kest` is half the size and handles `health=30, armour=12`
with the spaces, because `trim` was there.

D022 also records what is missing on purpose. `min`, `max`, `abs` and `clamp`
are what a game program asks for next, and with no generics they would have to
be `minInt` and `minFloat`. An ugly name in a standard library is permanent and
the thing that fixes it is a decision nothing has made, so they are absent
rather than named badly.

**Runs:** twelve of thirteen examples, `kest tick` on the thirteenth, and the
library resolves from another directory and from `KEST_LIB`. Clean under ASan
and UBSan across 120 files and seven commands.
**Next:** the decision D022 is waiting on. Without generics or overloading a
library cannot have `min`, and a program cannot write one function that works
on two types.

## 2026-09-07, one name, two functions

D022 was waiting on a decision and this is it. Two functions may share a name
when they take different things, and which is meant is settled by what is
passed. `lib/std/math.kest` has one `min`, one `max`, one `abs` and one
`clamp`, over four number types between them.

D023 records why this and not generics, and it is not that generics are worse.
It is that in this language overloading is almost nothing: no subtyping, no
implicit conversion and no ranking, so resolution is "find the one whose
parameters are exactly these" and there is no second rule. Generics need
constraints or they need errors reported inside a body the caller did not
write, and that is a design taken on an argument. A program still writes `min`
twice, which is the gap and is still open.

The one rule that was needed is about literals. `min(3, 7)` fits four
candidates because a literal has no type of its own to lose. It is settled
twice: once letting a literal match any width of its family, and again
requiring the width it would have had on its own.

**Three things broke and each was the same shape.** A function is now compiled
under a symbol that includes what it takes, and three places were still
looking one up by name. The compiler stopped finding a user's `find` and used
the builtin, so `examples/lookup.kest` segfaulted inside `strstr`. The checker
checked all three `abs` bodies against the first one's signature. And the
contract's call graph matched by name and printed the symbol in its messages.

Each is the same lesson: a name stopped identifying a function, and everything
that had been using it as an identifier had to start using the thing that
still is. What made them findable was that the examples run and the sanitiser
runs on every one of them.

**Runs:** twelve of thirteen examples, `kest tick` on the thirteenth. Clean
under ASan and UBSan across 123 files and seven commands.
**Next:** `std.math` has no `sqrt`, `sin` or `floor`, and cannot: they are
what the host has and there is no way for the library to declare an `extern`
that a program's host is required to provide.

## 2026-09-07, what the library asks the host for

`std.math` had no `sqrt` and could not have one: it is what the host has, and
there was nothing saying a program's host has to provide it.

There is now. `std.math` declares `Math.sqrt`, `Math.floor`, `Math.ceil`,
`Math.sin`, `Math.cos` and `Math.pow`, and wraps each in a function a program
calls. The `f32` ones go through the `f64` ones and come back, which is exact
for these because a `f64` has more than twice the precision.

**Importing a module means providing what it needs, all of it.** A program
that imports `std.math` for `min` alone still requires all six. That is not an
oversight: the host may call any function in a program through `kest_call`, so
nothing can be left out on the grounds that this program does not reach it.
Leaving it out would be a program that runs until the host calls the function
that was dropped.

`examples/physics.kest` has a `length` now, which is a square root of a sum
that was already there, and it comes from the host.

**Runs:** twelve of thirteen examples, `kest tick` on the thirteenth. Clean
under ASan and UBSan across 125 files and seven commands.
**Next:** `print` is the only way a program says anything, it takes text and
nothing else, and it is a builtin rather than something the host provides.
A host that is a game engine has nowhere to send it.

## 2026-09-07, the language says nothing

`print` was a builtin that wrote to standard output, which is a decision taken
on behalf of a host that has no standard output. An engine writes to its
console and a server to its log.

There is no `print` now. `std.io` declares `Io.write` and wraps it, and a
program that wants to say something imports it. That leaves the language with
no input and no output at all, which is what an embedded language should have,
and it is D022's rule applied to the last place it had not been: a builtin is
a thing the language cannot express, and this was a thing the host does.

Every example says where its output goes now, and every one still says the
same thing.

The one concession is a diagnostic. `print` is the first thing anybody reaches
for, so calling it without the import is told where it lives rather than told
the name is unknown:

```
error[K0306]: unknown name `print`
 --> noio.kest:2:5
  |
2 |     print("hello")
  |     ^^^^^ `import std.io` and call `io.print`
```

**Runs:** twelve of thirteen examples, `kest tick` on the thirteenth. Clean
under ASan and UBSan across 127 files and seven commands.
**Next:** the machine has one stack of a fixed size, one heap that is never
freed, and a call depth of a thousand, and all three are numbers written into
`vm.c` rather than anything a host can choose.

## 2026-09-08, the machine's numbers

The stack was sixty-five thousand slots, the call depth a thousand, and both
were written into `vm.c` where a host could not reach them. `KestLimits` says
what a machine may use and `NULL` says the host has no opinion, which is what
the command line passes.

The heap is the more interesting one. D012 says nothing frees it and D017 said
that was "now a thing a host can observe rather than a thing this project can
only argue about", which was not true, because nothing reported it.
`kest_heap_used` does:

```
$ ./kest tick ticky.kest 100
onEvent   100 crossings returned 4950
heap      6385 bytes, none of it freed
$ ./kest tick ticky.kest 1000
onEvent   1000 crossings returned 499500
heap      63985 bytes, none of it freed
```

A program that allocates every event grows with the events, and the number
says by how much. That is the deferred decision made measurable rather than
argued about, which is what D012 said it was waiting for.

**Two things broke and both were the same shape as last time.** A function is
compiled under a symbol that includes what it takes, and `kest_call` and
`kest_defines` were still looking one up by name, so `kest tick` found no
`onEvents` at all. A host calls by name and should not have to know about the
symbol, so a lookup that finds nothing exactly now finds the one function of
that name, and refuses when there are several. And `kest tick file 1024` read
`1024` as a file, because the paths were a slice of `argv` rather than a list
of what was actually a path.

**Runs:** twelve of thirteen examples, `kest tick` on the thirteenth. Clean
under ASan and UBSan across 129 files and seven commands.
**Next:** the heap number can be watched and nothing can be done about it. A
host that sees a script growing every frame has no way to say "start again",
which is the smallest useful thing short of deciding how memory is reclaimed.

## 2026-09-08, starting the heap again

The heap could be watched growing and nothing could be done about it.
`kest_heap_reset` throws it away and starts again.

What matters about it is what it invalidates, which is written on the
function. Nothing of a program's survives a call: there are no mutable
globals, and the stack and frames are set up per call, so between two calls
nothing in the machine points at the heap. What a reset invalidates is what
the *host* is holding — an array or a store that came out of `kest_call` is
gone, and passing one back in is reading freed memory.

`kest tick --reset` shows what it is for:

```
$ ./kest tick ticky.kest 1000
onEvent   1000 crossings returned 499500, peak 63985 bytes

$ ./kest tick ticky.kest 1000 --reset
onEvent   1000 crossings returned 499500, peak 49 bytes
```

The same answer, and the difference between a script that grows with the frame
count and one that does not.

D025 says what this is and is not. It is not an answer to the question D012
defers; it is the smallest thing that lets that question wait honestly. A host
that resets between frames has bounded memory and no collector, which is the
arena an engine already uses. A host that carries values between frames cannot
use it and still has no answer, and which of those a real program is remains
the thing nobody here has measured.

**Runs:** twelve of thirteen examples, `kest tick` on the thirteenth with and
without `--reset`. Clean under ASan and UBSan across 129 files and seven
commands.
**Next:** `store<T>` is the one thing a program cannot hold across a call, and
it is the one thing a game script most wants to. A host has no way to keep a
world between frames except by passing it in every time.

## 2026-09-08, a library, and a host that is not this one

The last entry said a host had no way to keep a world between frames. That was
wrong, and checking it first was worth more than the work it would have saved:
a store handle comes out of `kest_call` and goes back in, and nothing stops it.

What was actually missing is bigger. There was no library. `include/kest.h`
described an embedding API and nothing built one, and a host that wanted to
compile a file had to include `src/loader.h`, `src/check.h`, `src/compile.h`
and know what order to call them in.

`libkest.a` is the language now. `kest_build` compiles a file and everything it
imports, `kest_start` makes a machine, and `kest_build_name` says what
something is called in the file that was compiled. The stages still exist for
the command line, which stops between them, and `src/build.c` is where both
views meet, so there is one pipeline rather than a public one and a private
one.

`examples/embed.c` is a host that is not this command line. It compiles
`examples/embed.kest`, binds one function, sets its own limits, and holds a
`store<Npc>` across ten frames:

```
frame 4: spawned, 5 alive
frame 5: stepped, 4 alive, 256 bytes
...
frame 9: stepped, 0 alive, 256 bytes
```

The world is the program's to make and the host's to keep, and the heap does
not move, because `step` walks, reads, writes and removes and allocates
nothing.

**Two things it found.** A frame passed to `kest_call` has to be wide enough
for whichever is larger, what goes in or what comes back, because they are the
same slots; the compiler caught that as an out-of-bounds write and the header
says it now. And `kest_vm_run` had no callers left once the command line went
through the same door a host does, so it is gone.

**Runs:** thirteen of fourteen examples, `kest tick` on the fourteenth, and
`examples/embed`. Clean under ASan and UBSan across 131 files and seven
commands.
**Next:** `libkest.a` is built and nothing installs it. There is no way to put
the compiler, the header and the library where another project would look.

## 2026-09-08, putting it somewhere

`libkest.a` was built and nothing put it anywhere. `make install` puts the
program, the header, the archive and the standard library where another
project looks, and `make uninstall` takes them back out.

The part that needed thinking about is that installing breaks the rule the
compiler used to find its own library. "Beside the program" is `lib/` in a
source tree and is nothing at all next to `/usr/local/bin/kest`. It is looked
for at `$KEST_LIB`, then beside the program, then a directory up and into
`lib/kest`, then where the build was told it would be installed, and the first
one that is *actually there* wins rather than the first one that is plausible.

That last one is a path compiled in from `PREFIX`, which means the prefix
belongs to the build and not to the install, and building for one place and
installing to another gets a compiler that looks in the first. Checked by
doing exactly that and watching it fail before doing it the right way round.

An embedder is the case that needed it: `examples/embed.c` compiles against
the installed header and archive, passes NULL for the library, and finds it.

```
$ cc -I/prefix/include -o embed examples/embed.c /prefix/lib/libkest.a -lm
$ cd /tmp && ./embed /path/to/embed.kest
frame 9: stepped, 0 alive, 256 bytes
```

**Runs:** thirteen of fourteen examples, `kest tick` on the fourteenth,
`examples/embed` from the source tree and from an install. Clean under ASan
and UBSan across 131 files and seven commands.
**Next:** `kest` has eight commands and no way to ask what they are except a
usage line, and `--errors=json` is the only thing a tool can rely on. There is
no `--version` on anything but the compiler itself.

## 2026-09-08, what a tool can ask

Eight commands and a usage line on standard error, which is where a thing goes
when it is a complaint and not where it goes when somebody asked for it.
`kest help`, `-h` and `--help` write to standard output and exit zero, and the
help says what every command and every flag does and what the exit status
means.

The larger half is that a tool could only ask what was *wrong* with a program.
`--errors=json` is `--json` now, and it means everything a command says rather
than only its complaints. For `check` that is what the program holds:

```json
{"diagnostics": [],
 "types": [{"name": "grow.Sample", "slots": 2, "bytes": 8, "align": 4,
            "file": "examples/grow.kest", "line": 9,
            "fields": [{"name": "at", "type": "i32", "slot": 0, "byte": 0}]}],
 "functions": [{"name": "grow.total", "parameters": ["[grow.Sample]"],
                "result": "f32", "noAlloc": true, "foreign": false,
                "file": "examples/grow.kest", "line": 24}]}
```

Every function including the library's, with what it takes, what it gives,
whether it promises `no.alloc`, and where it was declared. That is the
question "what can I call here" answered without reading the source, which is
what an editor asks and what a model asks.

One flag replaced two rather than being added beside them: `--errors=json`
would have been the flag for one of the two things `--json` says, and a tool
would have had to know which.

**Runs:** thirteen of fourteen examples, `kest tick` on the fourteenth. Clean
under ASan and UBSan across 132 files and eight commands, and every file's
`--json` parses.
**Next:** `kest help` says a command takes more than one file, and `lex` takes
exactly one and says nothing about it. Five of the eight commands ignore every
path after the first in some way, which the help does not say.

## 2026-09-08, two kinds of command

The help said every command takes more than one file, and asking which ones
actually did found two things.

`lex` ignored every path after the first. And `parse` did nothing at all: the
refactor that put the command line through the same door a host uses dropped
its branch, and nothing noticed, because the sweep ran `parse` on every file
and only looked at whether the sanitiser complained. A command that prints
nothing and exits zero is invisible to a check for crashes.

Both are fixed, and the fix is a distinction the help now makes. `check`,
`run`, `emit` and `tick` read a *program*: the files named and everything they
import. `fmt`, `parse` and `lex` read each file on its own and follow nothing,
because what a file is does not depend on what it imports.

That also changes what `parse` prints. It used to walk the imports and dump
the standard library beside the file that was asked about; now it dumps the
file. And a file that cannot be read is reported and does not stop the rest,
which the formatter already did and the other two now do.

**What would have caught it.** The sweep checks that nothing crashes and that
formatting is faithful. It does not check that a command does anything, and
`parse` printing nothing passed everything.

**Runs:** thirteen of fourteen examples, `kest tick` on the fourteenth.
**Next:** that gap in the sweep. There is nothing that says what a command
should print, so a command that prints nothing looks the same as one that
works.

## 2026-09-08, holding the commands to something

The sweep could not tell a command that prints nothing from one that works,
which is how `parse` doing nothing at all went unnoticed for a turn.
`tools/check-commands.sh` says what every command has to be true of: it
produces output of the right kind, or it says why and exits non-zero, and what
it says as JSON is JSON. The examples check their own answers by returning a
number, so `run` exiting zero is already the answer being right; this adds the
part that was missing.

It found something on the first run. `kest run --json` printed the program's
own output and then the JSON, on the same stream, so nothing could read it.

**Fixing that needed something the host API was missing.** Where output goes is
the host's to decide, but a host function had no way to reach the host's own
state: a native was handed the frame and the machine and nothing else. It is
handed whatever was given when it was bound now, so the command line binds
`Io.write` to the stream it means and passes the other one when the caller
asked for JSON.

That is a gap an embedder would have hit immediately and this one hit first: an
engine binding `Engine.spawn` needs its engine, and there was nowhere to put
it.

```
$ ./kest run examples/math.kest --json 2>/dev/null
{"diagnostics":[],"errors":0}
```

**Runs:** thirteen of fourteen examples, `kest tick` on the fourteenth,
`examples/embed`. Every command does something on eighteen files, formatting is
faithful on eighteen, and the sanitisers are clean across 129 files and seven
commands.
**Next:** `kest tick` is the only way to call into a program from the command
line and it calls two names nobody chose. There is no way to say "call this
function with these arguments", which is what a host does and what a person
debugging one wants.

## 2026-09-08, calling one function

`kest tick` was the only way into a program from the command line and it
called two names nobody chose. `kest call <file> <function> [argument]...`
calls one and prints what it gives.

```
$ ./kest call lib/std/math.kest min 3 7
3
$ ./kest call lib/std/math.kest min 3.5 7.5
3.5
$ ./kest call lib/std/text.kest number -42
-42
$ ./kest call lib/std/text.kest number 4x2
none
```

The arguments are read the way the language reads a literal, and settling
which `min` was meant needed the same two passes the checker uses: any width
of the right family, then the width it would have had on its own. Without the
second, `min 3 7` matched all four, because `3` parses as every one of them.

A parameter that cannot be typed at a shell is refused with every signature
of that name listed, which is the same answer the language gives and the same
shape of message.

Float printing moved out of the machine and into `value.c`, because the
command line needs exactly what a program's own `"{x}"` gives and two of those
would have drifted.

**Runs:** thirteen of fourteen examples, `kest tick` on the fourteenth, and
`call` against the library and the examples. Every command does something on
eighteen files and formatting is faithful on eighteen. Sanitisers clean.
**Next:** `kest call` reaches a function and cannot make anything to pass it,
so every function that takes a struct, an array or a store is out of reach
from the command line. What reaches those is a program, which is what
`examples/embed.c` is.
