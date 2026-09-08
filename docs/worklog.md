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

## 2026-09-08, a thing that is one of several

`enum` and `match` were reserved words that did nothing, which is a promise the
language had not kept. They work now, and `examples/state.kest` is a door that
is shut, locked with a key, or open by an amount.

What they replace is a struct with a number in it and a chain of `if`s, and
what is wrong with that is not the verbosity: adding a case changes nothing
anywhere, and every place that forgot it keeps compiling. A `match` that leaves
a case out is refused and the message points at the case in the declaration.

D026 records the two choices under it. What a case carries is positional,
because D011 chose that for a struct and D019 kept the rule: naming a type
makes one of it, by position, and the names are given in the arm that answered
the case. And the tag is a four byte integer at offset zero with the payload
after, which is what a C tagged union is, so an enum crosses the boundary D016
makes crossable. An optional's tag is last, which is the other way round, and
the difference is only that an optional was built before anything crossed
anywhere.

**One instruction was needed.** A case is written payload first and tag last,
because that is the order the source is in, and is laid out tag first, because
that is the order it is read in. `rotate` rolls the run by one.

**What it refuses, which is most of the value:**

```
error[K0333]: this `match` does not answer `Damage`
error[K0332]: `Quit` is already answered here
error[K0330]: `Event` has no case `Nope`
error[K0309]: `Damage` carries 1 thing, and 2 names were given
error[K0309]: `Quit` carries 0 things, found 1
error[K0331]: `match` chooses between the cases of an enum, found `i32`
```

**Runs:** fourteen of fifteen examples, `kest tick` on the fifteenth. Every
command does something on eighteen files, formatting is faithful on nineteen,
sanitisers clean.
**Next:** an enum cannot hold itself, even through a `ref`, because nothing
resolves a case's payload against a type that is still being measured. A tree
is the first thing anybody writes with one.

## 2026-09-08, a size is what a thing holds

Checking whether an enum could hold itself found three faults rather than the
one the last entry named.

A struct holding an enum was silently wrong. Structs were measured, then
enums, so a struct measured first saw an enum of no size: `struct Holder {
what: Tree, count: i32 }` came out as one slot with both members at offset
zero. It compiled, and everything written against it would have been wrong.

An enum holding itself by value was not refused. `enum Loop { Only(Loop) }`
took a size and the size was nonsense.

And an enum holding itself through a `ref` did work, which was the thing being
asked about, and was the only part that was already right.

All three are one fault: a size is what a thing holds, and structs and enums
hold each other, so measuring them in two passes cannot be right whichever
order the passes are in. There is one pass now that follows what a type holds
and stops where it comes back to itself, and a `ref` is where it stops because
a reference is one word whatever it points at.

Against a C program compiled beside it: `Tree` is 24 bytes aligned 8 with its
payload at 8, and `Holder` is 32 aligned 8 with its count at 24. Both agree.

`examples/tree.kest` is the thing this was for. A branch is a case that holds
two references, the store holds the tree, and walking it is a walk of things
that might not be there, which allocates nothing and the compiler proves it.
Removing a leaf makes the total drop rather than making the walk follow a
handle to somewhere that is gone.

**Runs:** fifteen of sixteen examples, `kest tick` on the sixteenth.
Formatting is faithful on twenty, every command does something on nineteen,
sanitisers clean.
**Next:** `match` is a statement, so every arm has to `return` or assign, and
the value it chose cannot be the value of anything. `let name = match door {
... }` is what half of these arms are working around.

## 2026-09-08, a match that answers everything returns

Half of what the last entry asked for turned out to be cheap and the other
half turned out not to be a plumbing job.

The cheap half: a `match` that answers every case and returns from every arm
now counts as returning, so the dead line after it is gone. `describe` in
`examples/state.kest` ends at its last arm, and a `match` that leaves an arm
without a return still gets `can end without returning`.

**The other half is a design question and this entry is where it stops rather
than being rushed.** Making `match` a value means blocks have values, which
means a block's value is its last statement when that statement happens to be
an expression. That is invisible: nothing at the top of a block says whether
it gives one, and the difference between a block that yields and a block that
does not is a line that looks the same either way.

D011 refused a braced struct literal for exactly that shape of reason, that
the rule is invisible until it bites and it bites where nobody is thinking
about grammar. Rust has this rule and its trailing semicolon is the thing
people trip on; Kest has no semicolon, so the distinction would have nothing
to show for it at all.

So the question is not "how" but "what", and it has at least three answers:
blocks with values; arms written `-> expression` where every arm gives one or
none does; or nothing, and a `match` stays a statement. The first is the most
familiar and the least visible. The second is explicit and adds a second shape
of arm. The third is what there is.

**Runs:** fifteen of sixteen examples, `kest tick` on the sixteenth.
Formatting is faithful on twenty, every command does something on nineteen,
sanitisers clean.
**Next:** that question, answered rather than deferred: whether a `match` can
be the value of something, and if so in which of those three shapes.

## 2026-09-08, a match that is a value

The question the last entry stopped at, answered. An arm is written
`Case -> expression` when it gives a value and as a block when it does
something, every arm of one match is the same kind, and a match whose arms
give values is a value.

D027 records why not the familiar answer. Blocks with values means a block's
value is its last statement when that statement happens to be an expression,
and here there is no semicolon, so a block that yields and one that does not
would look the same. D011 refused a braced struct literal for the same shape
of reason and this got the same answer.

`match` is parsed once, as an expression, and a statement that is a match is a
match that was not used for anything. That removed a statement kind rather
than adding one, and it is what lets the same thing appear in a `return`, in a
`let`, and in the middle of an arithmetic expression.

```kest
return match door {
    Shut -> "shut"
    Locked(key) -> "locked with {key}"
    Open(width) -> "open {width} wide"
}
```

What it refuses is most of the value: an arm that disagrees with the others
about what it gives, a value match that leaves a case out, a statement match
used as a value, and a match with one arm of each kind.

D027 also writes down what this does not do. An arm that needs several
statements and a value cannot be written, and there is still no conditional
expression: a value chosen by a `bool` takes a `let` and an `if`.

**Runs:** fifteen of sixteen examples, `kest tick` on the sixteenth.
Formatting is faithful on twenty, every command does something on nineteen,
sanitisers clean.
**Next:** that conditional. `if` is a statement, there is no ternary, and
`let x = if c { 1 } else { 2 }` is the shape half the remaining `let`
declarations are working around.

## An `if` that gives a value

D027 settled the principle a turn ago: whether something gives a value is
written in it, not worked out from where the reader happens to be looking.
This turn applies that to `if`, which is D027's one loose end.

`if` moved out of `parse_statement` and into `parse_primary`, so it is parsed
once and `KEST_STMT_IF` is gone. An arm gives a value with `-> expression` and
does something with a block, both arms of one `if` are the same kind, and an
`if` that gives one needs an `else`. Recorded as D028, with why a ternary was
not the answer: `?` already means "optional" and would have had to mean two
unrelated things.

The checker types both arms against each other, so a literal in one takes the
shape the other settled on. The compiler emits the same jumps the statement
form emitted, plus the stack accounting `match` already needed. The formatter
prints a chain on one line, which it was already doing for `else if`.

Found while wiring this: the cost contract's `walk_expr` ended in
`default: break`, so it never walked into a `match` at all. A `no.alloc`
function whose arm allocated was not being caught. Both `match` and `if` are
walked now.

`lib/std/math` is where the shape was actually costing something: `min`, `max`
and `abs` across four widths, and `sign`, went from four lines each to one.
`examples/state` uses it where it had an `if` and a fall-through `return`.

Refusals verified: an `if` giving a value with no `else` (K0334), arms that
disagree about what they give (K0310), one arm of each kind (K0208), and a
block-form `if` used as a value, which is `void` and says so.

**Runs:** fifteen of sixteen examples, `kest check` on the sixteenth.
Formatting is faithful on twenty, every command does something on nineteen,
sanitisers clean across every file and every command.
**Next:** `while` and `for` are the last statements that cannot be reached
from an expression, and that is fine. The gap now is that `text` has no way to
be built a piece at a time: `split` in `lib/std/text` walks bytes and pushes
whole strings, and there is no `push` for a character.

## Text built a piece at a time

`std.text` could cut text apart and could not put it back together. There was
no `join`, because there was no way to build a string except interpolation,
which builds a whole one every time: a loop growing a string of *n* bytes out
of *k* pieces did O(nk) work and allocated *k* times.

`text(bytes)` is the answer, recorded as D029: a `[u8]` becomes one piece of
text, one copy, one allocation. Nothing new had to be invented for the
gathering, because arrays already grow and already have `push`. A new opcode,
`text.from`, and the checker branch that lets `text` be named as a conversion
the way `i32` already is.

`std.text` grew `bytes`, `append`, `join`, `repeat`, `upper` and `lower`, all
written in Kest out of that one builtin. `upper` and `lower` are ASCII and say
so: what an upper case `ı` is depends on a language rather than a table.

`examples/pieces` is the new example. It lays out a padded table on one array
of bytes, which is one allocation instead of one per column, and counts vowels
inside a `no.alloc` promise to show that reading bytes stays free.

The contract graph learned that `text` allocates, so `no.alloc` may gather
bytes and may not finish. A zero byte is refused at run time rather than
silently cutting the string, since text ends at its first zero.

Also corrected: the sanitiser sweep had been running `kest build`, which is
not a command, so one of its five columns had been checking the usage message
for several turns. It runs `parse`, `check`, `fmt`, `run`, `emit` and `tick`
now.

**Runs:** sixteen of seventeen examples, `kest check` on the seventeenth.
Formatting is faithful on twenty-one, every command does something on twenty,
sanitisers clean across every file and every real command.
**Next:** `array(0, u8(0))` is how an empty array is spelled, and it reads
like a bug. Every builder in `std.text` opens with it. An array's element type
is known from where it is going in every one of those places.

## An empty array that says what it holds

`array(0, u8(0))` was the spelling for an empty array: a nought count and a
fill the instruction never looks at. Four functions in `std.text` and three
examples opened with it, and it reads like a bug rather than a declaration.

`array()` now takes what it holds from where it is going, which is what
`store()` already did, down to the shape of the diagnostic. Recorded as D030,
with why an empty `[]` literal was not the answer: it is a second spelling of
one thing and needs the same rule regardless.

The compiler pushes a nought of the right width for the fill, because the
instruction reads one whether the count uses it or not. `K0335` is the refusal
where there is nothing to take a type from, and it writes out the fix.

Every empty array in the library and the examples is written that way now:
`let out: [u8] = array()`, `let rows: [Row] = array()`. It works as an
argument and as what a function returns, both of which have a type to take.

**Runs:** sixteen of seventeen examples, `kest check` on the seventeenth.
Formatting is faithful on twenty-one, every command does something on twenty,
sanitisers clean.
**Next:** `push` is the only way to grow an array and there is no way to take
anything out of one. `remove` exists for a store and not for an array, and
`examples/parse` works around it by rebuilding.

## Taking things out of an array

`push` was the only thing that changed an array's length. Anything that needed
to filter rebuilt the array into a second one.

Three ways out now, recorded as D031, each with its cost on the call: `pop`
off the end for nothing, giving a `T?` because an empty array has no last one;
`remove` at a position, giving what was there and keeping the order of what is
after it; `clear` to nought. Three opcodes, `pop.last`, `take` and `clear`.

`remove` is the same word a store uses, and which one is meant is settled by
what is handed in — a store and a reference, or an array and a position. That
is how `len` already works across three things. Wiring it caught a real bug in
the shape of the compiler: returning `false` from the array branch left
`compile_builtin` entirely rather than falling through to the store branch, so
`remove(world, r)` stopped compiling. The branch asks what it was handed
instead of only what it was called.

There is deliberately no swap-remove. Taking the middle out by moving the last
one into the hole is O(1) and loses the order, and that is a store: D014's
references exist so a thing whose position does not matter can be removed for
nothing and still be named. Adding one would make an array a worse store.

A borrowed array cannot shrink, for the reason it cannot grow: the length is
the host's, and so is the extent it lent.

`examples/queue` is the new example. It affords tasks in order out of an array
and says in as many words when a store would have been the right container.

**Runs:** seventeen of eighteen examples, `kest check` on the eighteenth.
Formatting is faithful on twenty-two, every command does something on
twenty-one, sanitisers clean.
**Next:** `for e in events` walks a copy of each element, so a loop cannot
change what it walks. `examples/queue` works around it with a `while` and an
index, and a frame step over `[Npc]` would have to do the same.

## Bits

Last turn's `Next:` was wrong, and checking it first is what found the real
gap. `for i, x in a { a[i].health -= 1 }` already works, and writing to the
loop's own name already warns with the fix in it — there was nothing to build.

What was actually missing was bits. A language for games and engine embedding
could not say what a byte of state is: flags were eight `bool` fields, a
packed handle could not be taken apart, and the runtime's own `ref<T>` —
a generation and an index in one number (D014) — was a shape the language
could not write.

`&`, `|`, `^`, `~`, `<<` and `>>`, recorded as D032. Six tokens, seven
opcodes, and a precedence table that puts the bitwise operators tighter than
the comparisons, because C's is the one that is known to be wrong:
`flags & MASK == 0` means `flags & (MASK == 0)` there. Shifts keep C's place.

A shift takes a value and a count rather than two operands, so the count is an
integer of any width the way an index is. What C leaves open is defined here:
a left shift wraps at the declared width, which is D018 and not a new rule;
a right shift brings the sign in on a signed type and nought on an unsigned
one; a count past the slot shifts everything out; a negative count fails.

`>>` and the end of `store<ref<Npc>>` are the same two characters. Closing a
generic splits the token and leaves the second half where it is, which is
tested and works.

`examples/flags` is the new example: setting, clearing, toggling and counting
bits of a `u8`, and packing a generation and an index into an `i64` the way
the runtime does it.

**Runs:** eighteen of nineteen examples, `kest check` on the nineteenth.
Formatting is faithful on twenty-three, every command does something on
twenty-two, sanitisers clean.
**Next:** `const MOVING: u8 = 1` is the only way to name a set of related
values, and `examples/flags` declares four of them loose at the top of the
file. An enum cannot carry a number the program picked, so a flag set has no
type of its own.

## A set of named bits

D032 gave the language bits and left `examples/flags` declaring four `const`
values loose at the top of the file. Nothing tied them together, nothing
stopped one being passed where another belonged, and the powers of two were
written out by hand.

`flags State: u8 { Moving Airborne Hurt Armed }`, recorded as D033. Which bit
a name stands for is where it was written, so the one thing a reader could get
wrong is the one thing they no longer write. The width is written rather than
counted, because it is what a host sees and a ninth flag must be a decision
rather than a silent widening under a host already reading the bytes.

A set is a type: `&`, `|`, `^` and `~` give the same set back, `==` compares,
and everything else is refused — arithmetic, mixing two sets, and `match`,
which cannot apply because every combination is a value and nothing exhausts
it. `State()` is the empty one, which is what `array()` and `store()` already
read as. `u8(state)` and `State(bits)` cross at the declared width only.

Not an enum with numbers: an enum is a tagged union whose cases `match`
answers, and giving it numbers would have made one word mean two things and
quietly ended the exhaustiveness D026 is built on.

`flags` is a word rather than a keyword. Making it one broke
`module examples.flags` on the first build, which is the whole argument: a
keyword takes the name from every field and every module, and `npc.flags` is a
thing people write. It is read as a declaration only where a declaration
begins, the way `no.alloc` already is.

`examples/flags` is rewritten on it. It works in arrays and in struct fields,
with the byte layout the declared integer has.

**Runs:** eighteen of nineteen examples, `kest check` on the nineteenth.
Formatting is faithful on twenty-three, every command does something on
twenty-two, sanitisers clean.
**Next:** `count(state)` in `examples/flags` walks eight bits by hand because
a set cannot be walked. `for flag in state` is the shape, and the store and
the array both already answer `for`.

## Walking a set of bits

`count(state)` in `examples/flags` walked eight bits by hand, through a `u8`
conversion, because a set could not be walked — which meant D033 refused
arithmetic on a set and then the example did the arithmetic anyway, one
conversion to the side.

`for flag in state` now gives the flags that are there, in declaration order,
each one a value of the set. Recorded as D034. There is no position form, for
the reason a store has none: a bit position is a number with no way back to
the flag it names, since shifting is not defined on a set.

No new instruction. The walk is written out of what exists: a counter to the
number of names, the bit at that counter, and a jump past the body when the
set does not hold it. The flag goes into its name before the test rather than
after, so the path that skips a bit leaves nothing on the stack — the first
version left one slot per absent flag.

The loop-copy warning learned what it is walking. `flag = A.One` used to
suggest indexing an array, which a set has none of; it now says the walk gives
one bit at a time and to build the set you want.

`count` is four lines with no conversion in them, and `firstOf` is what a walk
that returns from inside looks like. `break` and `continue` are tested.

**Runs:** eighteen of nineteen examples, `kest check` on the nineteenth.
Formatting is faithful on twenty-three, every command does something on
twenty-two, sanitisers clean.
**Next:** `"{state}"` is refused, and now that a set can be walked there is
one obvious spelling for it: the names that are there. D021 only writes what
has one, and this now has one.

## The text of a set of bits

D021 writes a value into a string only where it has one obvious spelling, and
until a set could be walked it had none. D034 made the names reachable, so
this turn picked the spelling: the source that builds the value.

`"{state}"` gives `State.Moving | State.Armed`, and `State()` for a set that
holds nothing. Recorded as D035. That is the rule every other type already
follows — `"{3}"` is `3`, `"{true}"` is `true` — and text is the exception
rather than the pattern, because text in a hole is content and not a name.

One instruction, `text.flags`, taking a layout index. A layout already carries
the type it was made for, so the names came with it and a module stores
nothing new for this.

The name it prints is the last piece of the one the type is registered under:
the first version printed `flags.State.Moving`, because that is what a type
declared in `examples/flags` is called inside the compiler, and it is not what
a program writes. That limit is written into D035 rather than hidden.

An enum's suggestion said to write the fields it wants to see, which an enum
does not have. It names `match` now.

**Runs:** eighteen of nineteen examples, `kest check` on the nineteenth.
Formatting is faithful on twenty-three, every command does something on
twenty-two, sanitisers clean. `no.alloc` still refuses a set in a hole,
because building text reaches the heap whatever is in it.
**Next:** an enum in a hole is refused and `examples/state` writes `describe`
by hand to answer it. A case that carries nothing has the same obvious
spelling a flag does; one that carries something needs text for what it
carries, and that is the decision to take.

## The text of an enum case

D035 settled that a value's text is the source that builds it and left the
enum, which had a spelling waiting for it and no way to reach the names.

`"{Door.Locked(7)}"` is `Door.Locked(7)` now, recorded as D036. A string
inside a case is written with its quotes and escapes, because there it is
being named rather than pasted — the exception D035 carved out for text is
about a hole holding text on its own.

An enum has text exactly when everything its cases carry has text, which the
checker works out by walking the payloads. A case carrying a struct or an
array has none, and the refusal names what it was rather than only the enum:
"`E` carries a `P`, which has none".

One instruction, `text.enum`, and one recursive formatter in the machine,
which `text.flags` now shares. It measures with room of nought and writes on
the second pass, so a value of any depth costs one allocation.

Structs stay refused and D036 says why: `P(1, 2)` is source too, but a struct
names its fields and that form does not, so a struct of ten fields in a log
line is ten numbers in a row. An enum has no such second reading.

`examples/state` keeps `describe`, because prose and the source form answer
different questions, and now asks both: `Door.Shut is shut`.

**Runs:** eighteen of nineteen examples, `kest check` on the nineteenth.
Formatting is faithful on twenty-three, every command does something on
twenty-two, sanitisers clean.
**Next:** `examples/state`'s `next` is four levels of nested `match` because
a `match` chooses one subject. Two enums answered together is the shape, and
whether that is a tuple, a second subject or nothing at all is the decision.

## A `match` that chooses between two things

`examples/state` answered two enums with four levels of nested `match` —
thirty two lines for a nine cell table, each inner match with its own `else`,
and nothing checking that the nine combinations were covered.

`match door, move` now, recorded as D037. An arm answers a case per subject,
`else` in a position answers any case there, and an `else` on its own stands
for every position. The example is ten lines and the checker asks about all
nine combinations.

Not a tuple: that would be a new type, new values, new patterns and a new way
to write a return, all to be taken apart again at the top of every arm.
Several subjects is a list where there was one, and the exhaustiveness check
becomes the product it already wanted to be.

An arm is now a list of parts rather than one name and its bindings, which
touched the parser, the dump, the checker, the compiler, the formatter and
the contract graph. The compiler emits one tag test per position that names a
case and none for a position that says `else`.

The first version refused any overlap between arms, and rejected the very
example that motivated the feature: `Open(width), Pull` then
`Open(width), else` is a fallback, not a mistake. Arms are tried in order, so
what is refused is an arm nothing can reach — every combination it answers
already answered. A blanket `else` marks them all, so a case written after one
is reported as unreachable.

Limits are written down: eight subjects and 256 combinations, past which an
`else` is asked for and the refusal says the number and the limit. Refusals
about the whole match point at the word and what it chooses between rather
than at every line of every arm.

**Runs:** eighteen of nineteen examples, `kest check` on the nineteenth.
Formatting is faithful on twenty-three, every command does something on
twenty-two, sanitisers clean.
**Next:** the giving form of `if` has to fit on one line, which is why the
`Unlock` arm above renames its binding to make room. D028 wrote that cost
down; what would remove it is a line that continues after `->` the way one
continues after a binary operator.

## A counted walk

The `Next:` line was wrong again, and checking it first is what found the real
work. A line already continues after `->`, because the newline rule is a list
of tokens that can *end* a statement and `->` is not one of them. The thing
that genuinely cannot be written is a break before `else` in the value form,
and that cannot be added: inside a `match`, `A -> if c -> 1` followed by a
line starting with `else` is ambiguous between the `if`'s else and the match's
`else` arm, which is exactly what D004 refuses to resolve by precedence.

What was actually missing was a counted walk. Every counted loop in the
language was three statements — a `let`, a condition, and an `i += 1` at the
bottom that nothing checked was there. `lib/std/text` had seven and the
examples six more.

`for i in 0..len(a)`, recorded as D038. Exclusive, so a walk of an array's
positions and an index into it are the same numbers. Not a type: a range is a
way to write a walk and `let r = 0..n` is a syntax error, because a range
value would need a range type and a decision about walking one twice, neither
of which is needed to remove the three-statement loop.

The end is worked out once into a slot nobody can name, so a `push` in the
body cannot make the loop run longer. A literal at one end takes the type of
the other, which is the rule an operator already follows.

The first version made the named counter the loop's own slot, so `i = 9` in
the body would really have moved the count — while the warning said the
assignment was discarded. The name is a copy of a hidden slot now, the way a
walk of an array already worked, and the warning is true.

`while` went from twenty-five uses to ten across the library and the examples,
and the ten that are left are conditions rather than counts.

**Runs:** eighteen of nineteen examples, `kest check` on the nineteenth.
Formatting is faithful on twenty-three, every command does something on
twenty-two, sanitisers clean.
**Next:** `examples/words` sorts by hand because there is no way to pass a
function. `sort(a)` on a comparable type is one answer and a function value is
the other, and which one the language takes is the decision.

## A function as a value

`examples/words` sorted by hand because there was no way to say what comes
first. The two answers were a `sort` over what the language can already
compare, and a function value; the second is the one that also answers
callbacks, rules and systems, so it is the one taken, as D039.

The objection was real: the `no.alloc` contract is proved by a call-graph
fixed point, and a function value is exactly what makes the call graph
unknown. So the promise goes into the type. `fn(text, text) -> bool no.alloc`
is a function that promises, checked where the value is made, and the contract
is read off the type at the call. A `no.alloc` function can call one.

A value that promises fits where one that does not is wanted, and not the
other way round. An overloaded name takes the shape of the place it is going,
which is the rule a literal already follows and the other half of D023. An
extern is called and not named, refused with the fix in it.

One instruction, `call.value`, taking which function it is off the top of the
arguments. A function value is one slot holding a module index.

Two things fell out of doing it. The contract graph had never looked at a call
it could not name, so an indirect call would have been counted as free — the
hole was only theoretical until there were function values, and it is closed.
And the first version of the compiler's test for "is this a value or a
declaration" used the shape of the callee rather than whether its type carries
a symbol, which sent `io.print` down the indirect path and broke every example
that prints.

`lib/std/sort` is new: insertion, `no.alloc`, told what comes first, with
`ascending` and `descending` for the three types that compare.
`examples/words` uses it three ways, the third with a comparison of its own.

**Runs:** eighteen of nineteen examples, `kest check` on the nineteenth.
Formatting is faithful on twenty-four, every command does something on
twenty-three, sanitisers clean.
**Next:** `lib/std/sort` is the same insertion sort written three times
because there are no generics. `sort(items: [T], before: fn(T, T) -> bool)`
is the shape, and whether the language takes generics at all is the decision.

## Types are taken, and a copy is compiled for each set

`lib/std/sort` was the same insertion sort three times. The question was
whether the language takes generics at all, and the value model answered it:
a `KestValue` has no tag and a struct is flat in slots, so erasure would need
a box and D016's free crossing would stop being free. Monomorphisation, then,
recorded as D040.

`fn sort<T>(items: [T], before: fn(T, T) -> bool no.alloc) no.alloc` is one
body now, and `lib/std/sort` is a third of its size. What each type name
stands for is worked out from what was passed; a function argument is settled
after the others, because which overload it is depends on what they settled,
which is how `sort(words, ascending)` picks the `text` one.

Each copy is checked against its own types, so `no.alloc` can hold for one and
not another — `kept<T>` in the new `examples/shapes` promises nothing because
it builds, while `count<T>` beside it promises and keeps it.

The bug worth recording: the tree is shared between copies and the checker
writes types onto it, so a tree carries one copy's types at a time. The first
version compiled every copy from whatever the last check left, and a
`count<T>` over `[Vec]` was emitted with the `[i32]` copy's layout — it moved
one slot where a `Vec` is two, and the count came back wrong rather than
crashing. The compiler asks the checker to put a copy's types back before
emitting it.

Two smaller things: the formatter dropped `<T>` and the tree dump did not
print it, so `check-fmt` could not see the loss — it compares trees, and the
tree was missing the thing that changed. Both print it now.

`examples/shapes` is the new example: three generic functions over `[Vec]` and
`[text]`, a value struct and a one-slot type through the same bodies.

**Runs:** nineteen of twenty examples, `kest check` on the twentieth.
Formatting is faithful on twenty-five, every command does something on
twenty-four, sanitisers clean.
**Next:** `store<T>`, `ref<T>` and `[T]` are the language's own generics and a
program cannot write anything like them. `struct Pair<A, B>` is the shape, and
whether a generic struct is worth its measuring pass is the decision.

## A struct takes types too

D040 gave functions types and left the containers: `[T]`, `store<T>` and
`ref<T>` were shapes a program could use and not write. `struct Table<K, V>`
now, recorded as D041, with a copy per set of types measured like any other
struct.

A shape is not a type. `Pair` has no size and is never measured;
`Pair<i32, text>` is a struct with a layout. Which copy is being built comes
from what it is built with, so `Pair(1, "a")` is a `Pair<i32, text>`, and the
written type wins when there is one.

A copy remembers its shape and what it was made with, which is what a
`Grid<T>` written inside a generic function needs: without it, substitution
had nothing to rebuild from and the copy stayed `Grid<T>` while the function
became the `i32` one. That was the first thing to go wrong and it is why the
fields of a shape are resolved with its names standing for themselves rather
than left unresolved.

`lib/std/table` is new and is the point of the whole thing: a table of pairs
searched by walking it, written in Kest on a generic struct, which is what
`store<T>` and `[T]` were and a program could not be. Its `table()` takes what
it holds from where it is going, which needed one more thing — a type name
that appears only in what a function gives is now taken from where the value
goes, the way `array()` and `store()` already read.

Writing it turned up a real hole: a file that declares a function shadowed the
builtin of the same name completely, so `std.table` could not call the array's
`remove` from inside its own `remove`. A builtin is one more thing a name
could mean now, settled by what is passed, which is D023's rule applied
somewhere it never had been. A parameter mentioning a type name is asked about
its shape rather than compared exactly.

`kest emit` on a file of nothing but generic functions printed nothing, which
`check-commands` caught. It says why now.

`examples/inventory` is the new example: a table of items keyed by name, and
the same table over two other types.

**Runs:** twenty of twenty-one examples, `kest check` on the twenty-first.
Formatting is faithful on twenty-seven, every command does something on
twenty-six, sanitisers clean.
**Next:** `lib/std/table` walks its keys to find one, which is right for a few
dozen and wrong for a few thousand. What is missing before that can change is
a way to ask a type for a number that stands for it.

## A number that stands for a value

`lib/std/table` walked its keys, which is right for a few dozen and wrong for
a few thousand. `hash(x) -> u64`, recorded as D042, applies to exactly what
`==` applies to: two values that are equal have to hash the same, so defining
it anywhere `==` is not defined would be defining it where nothing says what
equal means. A struct key is refused with the fix in the message.

Not told the way `sort` is told what comes first: `sort` is told because there
is more than one right order, and there is one right hash for an `i32`.

`lib/std/table` is a hash table now — open addressing, linear probing, written
in Kest on the generic struct D041 gave it. The keys and values sit packed in
two arrays and `slots` says where each one is; a removal moves the last pair
into the hole and marks the slot, because a hole would end a probe that has to
carry on past it.

The bug worth recording is D006 biting. A struct is a value, so a table handed
to a function is a copy: `t.slots = bigger` inside `grow` replaced the copy's
handle and left the caller's table where it was, and `t.live += 1` counted on
a copy that was thrown away. Everything that changes is behind a handle now —
`slots` is emptied and refilled rather than replaced, and how many pairs there
are is `len(keys)` rather than a number beside it. It is written into the
file, because anyone writing a container in this language meets it.

Also: `const TAKEN: i32 = 0 - 1` did not compile, because only a literal
constant is written into its uses and `0 - 1` is not one. `-1` is, and the
examples had been writing `0 - 1` out of habit rather than need.

Verified on five hundred integer keys through two growths, two hundred and
fifty removals, a refill over the marks, and two hundred text keys.

**Runs:** twenty of twenty-one examples, `kest check` on the twenty-first.
Formatting is faithful on twenty-seven, every command does something on
twenty-six, sanitisers clean.
**Next:** `hash` and `==` agree on which types they cover, and nothing checks
that they keep agreeing. An enum compares and does not hash, which is the one
place they are already apart.

## An enum compares

The `Next:` line said an enum compares and does not hash. It did neither, so
the two agreed — but checking that is what showed the real gap:
`examples/state` compared doors by building text out of them, because `==` on
an enum was refused and a `match` was the only other way to ask.

`door == Door.Locked(7)` now, recorded as D043: the same case carrying the
same things. A case carrying something that does not compare makes the enum
not compare either, and the refusal names what it was. `hash` covers the same
ground over the same parts, so the two cannot come apart.

A struct still does not compare, and D043 says why: a value of an enum is its
case and its payload and nothing else, while "are these two `Npc`s the same"
has two common readings and the language does not pick one.

What this turned up is the largest thing: an array of enums had never worked,
and nothing had tried one. `KestLayout` is one scalar per slot, which a tagged
union is not — which type a payload slot holds depends on the tag. An enum's
piece list was one entry short and the rest was whatever the arena held, so
`push(ks, Kind.Rope)` stored a `Sword` and the third element was nonsense. A
layout says whether it holds a tag now, and a value that does is moved by
reading the tag and using that case's byte offsets. Verified in an array, in a
struct field, in an optional and in a store.

Then a second one: the instruction name table had drifted from the opcodes,
because the new hash instructions went in one place in the enum and another in
the table. `emit` read an operand for an instruction that has none and walked
off the end of the code — ASan caught it, which is the third time the sweep
has earned itself.

`tools/check-tables.sh` is new and holds both parallel arrays in step: the
instruction names against the opcodes by name, and the token names against the
token kinds by count. Neither drift is something C says anything about.

`examples/state` compares doors, and `examples/inventory` keys a table by an
enum, which is what D042 and D043 together make possible.

**Runs:** twenty of twenty-one examples, `kest check` on the twenty-first.
Formatting is faithful on twenty-seven, every command does something on
twenty-six, the tables are in step, sanitisers clean.
**Next:** the byte layout of an enum is what a C tagged union is, and nothing
has ever handed one across the host boundary. `examples/embed.c` lends an
array of structs; an array of tagged unions is the shape that would prove D016
still holds after this.

## A host lends an array of tagged unions

D026 said an enum is a C tagged union and D016 said an array is the host's
bytes. Nothing had put the two together, and last turn found that an array of
enums did not work at all, so this turn is the proof that the layout is real.

`examples/embed.c` declares the struct and union that `examples/embed.kest`
declares as an enum. They are the same sixteen bytes with the payload at
eight, which `kest check` and a C program printing `sizeof` and `offsetof`
agree on. The host lends an array of four, `onEvents` walks it in place and
allocates nothing, and `silence` writes a tag the host reads back.

Recorded as D044, which is less a decision than the first thing to hold both
of them at once.

`make embed-debug` is new. `examples/embed` is the only thing that crosses the
public boundary in both directions and it had never been run under the
sanitisers; it is one command now, and it is clean.

**Runs:** twenty of twenty-one examples, `kest check` on the twenty-first, and
the host beside them in both builds. Formatting is faithful on twenty-seven,
every command does something on twenty-six, the tables are in step, sanitisers
clean.
**Next:** `kest_borrow` takes a length and a stride and trusts both. A host
that lends a stride that is not what the program's element is gets whatever
that produces, and the program cannot ask.

## A lend that cannot be the wrong shape

`kest_borrow` took a stride and trusted it. The stride is now the program's
own, and what the host passes is `sizeof` of its own struct — the number that
is there to be disagreed with. Recorded as D045.

```c
frame[0] = kest_borrow(runtime, events, 4, "Event", sizeof(Event));
```

A host whose `Event` has come apart from the program's gets
"the program lays `Event` out in 16 bytes and this host has 8" and a value
whose `object` is NULL. A name the program holds no array of is refused, and
so is one that means two types in two modules.

`kest_report` is new and was the other half of the gap: a host that got
`false` from `kest_call` had no way to find out why. The diagnostics existed
and only the command line could reach them. It writes what is new since the
last time it was asked, so a host that asks twice is told each thing once.

The command line turned out to have the same hole. `kest tick` hands a batch
of `i32` to `onEvents`, and `examples/embed` gained an `onEvents` over an enum
last turn — so `kest tick examples/embed.kest` read sixteen byte events as
four byte ones and crashed. The sanitiser sweep found it, which is the fourth
time. `tick` asks what the entry takes now and says what it has instead.

**Runs:** twenty of twenty-one examples, `kest check` on the twenty-first, and
the host beside them in both builds. Formatting is faithful on twenty-seven,
every command does something on twenty-six, the tables are in step, sanitisers
clean across every file and every command including `tick`.
**Next:** `kest_call` writes the arguments into the stack and reads the result
back, and the host is trusted about how many slots it laid out. A struct
argument written one field short is the same class of mistake a lend was.

## A call that cannot run off the end of the frame

`kest_call` copied what a function takes out of the host's array and what it
gives back over it, and neither number came from the host. A frame one slot
short was read past on the way in and written past on the way out — the same
class of mistake a lend was before D045, at the other end of the same
boundary.

The host says how wide its frame is and the program says how wide it has to
be. Recorded as D046, with `kest_frame_slots` so a host can size the frame
rather than guess: `examples/embed` had a comment saying "wide enough for the
most any of these calls passes or returns" and now asks.

```
error[K0611]: `embed.spawn` takes 2 slots and this frame holds 1
      `kest_frame_slots` says how wide it has to be
```

What it does not check is written into D046: the right number of slots holding
the wrong things is still the host's to get right, because the runtime carries
no types and giving it some would cost every call to save a host writing
`sizeof` wrong.

**Runs:** twenty of twenty-one examples, `kest check` on the twenty-first, and
the host beside them in both builds. Formatting is faithful on twenty-seven,
every command does something on twenty-six, the tables are in step, sanitisers
clean.
**Next:** the public header is eighteen functions and nothing checks that a
host can be written against it alone. `examples/embed.c` includes only
`kest.h`, but nothing says so and the day it stops being true nothing will
notice.

## The header stands on its own

`include/kest.h` is the only header a host includes and `libkest.a` needs libc
and nothing beyond it. Both were true, and the only reason they were true is
that the one example beside them happens to be written that way.

`tools/check-header.sh` writes a host that includes the header and nothing
before it, names every function the header declares, and links against the
library alone. Recorded as D047. It catches both breaks it is for: a
declaration with nothing behind it fails to link, and an include from `src/`
is refused outright. Both were tried.

Naming rather than calling is what makes it work — an address forces the
linker without anything needing arguments that mean something — and
`-pedantic` is what pointed out that the array has to be of function pointers,
because a function pointer converts to another function pointer and to no
object pointer.

It turned up that `libkest.a` never needed the maths library: only the command
line's host functions reach for it. Two link lines were carrying `-lm` for a
library with no floating point call in it, and they do not now.

**Runs:** twenty of twenty-one examples, `kest check` on the twenty-first, and
the host beside them in both builds. Formatting is faithful on twenty-seven,
every command does something on twenty-six, the tables are in step, the header
stands alone, sanitisers clean.
**Next:** `docs/language.md` describes a language and nothing checks that the
programs in it run. Every fenced `kest` block is either a fragment or a thing
that should compile, and neither is marked.

## The documented programs parse

`docs/language.md` described a language and nothing checked that the language
it described was this one. `tools/check-docs.sh` takes every fenced `kest`
block in the reference and the decisions and parses it, recorded as D048.

Parsing rather than checking: a fragment names things that are not in it, so
asking whether it means anything would need scaffolding invented per block,
and invented scaffolding is a second thing to keep true. Parsing needs nothing
and catches the mistake documentation makes, which is showing a shape the
parser would refuse.

A block is declarations, statements, or a declaration and a use of it, which
is how the reference is written; the tool splits at the first line that is not
part of a declaration and puts the rest in a function.

Forty-seven blocks, and four were wrong. Three used `...` for a body nobody
wanted to write out — not an elision this language has — and each is the code
it stood for now. Two decisions fenced a signature on its own as `kest`, which
is not a program: `fn sort(...) no.alloc` with no body is only ever written as
`extern fn`, so they are fenced plainly.

The worklog is deliberately not held to it. It records what went wrong, so it
holds code the parser refuses on purpose: the leading-operator break is in
there because refusing it was the entry.

**Runs:** twenty of twenty-one examples, `kest check` on the twenty-first, and
the host beside them in both builds. Formatting is faithful on twenty-seven,
every command does something on twenty-six, the tables are in step, the header
stands alone, forty-seven documented blocks parse, sanitisers clean.
**Next:** five tools each check one thing and each is run by hand. Nothing
runs them together, so "everything passes" is a claim rather than a command.

## Everything is one command

Five tools each checked one thing and each was run by hand, so "everything
passes" was a sentence rather than a command — and twice a column had quietly
gone missing from a sweep run that way, both times reporting success.

`make check` now: both builds, both hosts, every `.kest` file run or resolved,
every command against every file under the sanitisers, and the five tools.
Recorded as D049. It takes no list of files, because a list is what goes
stale, and it found that out at once: one file had been outside every by-hand
run of `check-commands.sh`.

What a file has to do comes from the file. One with a `main` runs and answers
nought; one without resolves, which is read off the refusal rather than off a
name written into the script.

Proved by breaking it three ways — an example answering wrong, a documented
block that does not parse, a header promise with nothing behind it — each
reported with the file named. The first attempt at the first probe changed a
line nothing reached, which is its own small lesson about what a probe has to
touch.

**Runs:** `make check`, which is now the whole of it: twenty ran, seven
resolved, both hosts, 189 sanitised runs over twenty-seven files, formatting
faithful, every command doing something, tables in step, header standing
alone, forty-seven documented blocks parsing.
**Next:** `make check` is thorough and slow, and the only thing it cannot say
is whether the language got faster or slower. That is a measurement, and the
predecessor died of measurements; what would earn its place is one number the
frame budget cares about, taken the same way every time.

## One number

`make check` could say everything about the language except whether it got
faster or slower. That is a measurement, and measurement is what the
predecessor died of — so the question was how to take one and only one.

`make time` prints how long a frame step takes per entity: an array of value
structs walked in order, read, computed on and written back, inside a promise
that nothing reaches the heap. That is the one thing this language claims.
Recorded as D050, with why there is one of them, why `check` does not run it,
and why no file records what it said: a recorded number becomes a series and a
series becomes the work.

It is written in Kest. The host already provides a clock, so the instrument
that measures the language is a program in it.

Getting it to mean anything took two goes. Reporting one timed run gave 161 to
205 nanoseconds across five invocations, which would hide any regression worth
finding; taking the best of seven rounds puts the floor at 160 and holds it,
because anything else sharing the machine only ever adds time. What is left is
that the first run or two on a cold processor read about a fifth high, which no
amount of rounds fixes and which the file says outright: run it twice, believe
the second, and it will show a change of a quarter and not one of a tenth.

The number today is about 160 ns per entity per step, which is not written
down anywhere but here, once, because this is the entry that introduced it.

`make check` holds Kest under `tools` to resolving and to formatting and not
to running, because what an instrument does takes a while on purpose.

**Runs:** `make check`, everything passing: twenty ran, seven resolved, one
instrument resolved, both hosts, 189 sanitised runs, formatting faithful on
twenty-eight, tables in step, header standing alone, forty-seven documented
blocks parsing.
**Next:** `step` in `tools/frame.kest` reads a struct out of the array and
writes a whole one back to change two fields. `world[i].x = x` exists and is
what a frame would write; whether the two produce the same instructions is not
something anything has looked at.

## A field of an element is read from its address

`a[i].health = 0` already wrote four bytes. `a[i].health` took the whole
element out of the host's layout, kept one piece and dropped the rest, because
a field goes through one path whether it belongs to a local, a call's result
or an array element, and only the local had a shortcut.

It reads from the address now, recorded as D051. A pass over ten thousand
entities touching two of five fields went from 79 nanoseconds an entity to 66,
which is above the noise `make time` admits to.

`make time` itself did not move, and that is the honest part: what it times
reads the element whole, which is the right shape for a step that touches
every field and the wrong one to notice this. Having one number means it
answers one question, which is what D050 said it would.

`compile_address` emits as it walks, so it cannot be used as a test by a
caller with somewhere else to fall back to — halfway through it has already
put an array and an index on the stack. There is a predicate beside it now
that answers the same question without emitting, and the read path asks that
first.

The shape is already covered by `make check`: `examples/world` writes
`w.enemies[i].health`, `examples/queue` reads `queue[i].cost`, and
`examples/pieces` reads two fields of a row.

**Runs:** `make check`, everything passing.
**Next:** `for one in world` copies each element out of the array, so a walk
that touches one field of a wide struct pays for all of it. `for i in 0..len`
with a field read is what a frame writes instead, and the two should not be
different in cost for the same work.

## A walk that only reads fields does not copy the element

`for one in world` copied all seven fields of an `Npc` to look at one, while
`for i in 0..len(world)` with `world[i].health` read four bytes. The readable
spelling was the expensive one, which is backwards for a language about frame
budgets. Measured first: 66 nanoseconds an entity against 43.

The compiler asks the body. If every use of the walked name is a read of a
field of it, the name holds where the element is rather than the element.
Anything else — passed, returned, compared, assigned to, a field of it written
— and it holds the element, because that is what such a body asked for.
Recorded as D052. It is 46 nanoseconds now, which is what the counted form
costs, so the two spellings are two spellings and not two prices.

The address is worked out every turn, so an array that grows under the walk is
followed rather than remembered — the same answer the copying form gave, and
tested.

The bug worth recording: `Local` gained two fields and neither binder zeroed
them. Slots are reused between scopes and between functions, so a stale "this
holds an address" left over from one function made `best = took` in another —
an assignment to a plain `i64` local — compile into a store through a null
pointer. It segfaulted immediately, which is the good case; the sanitiser
named the instruction. Both binders clear a name before they write it now.

**Runs:** `make check`, everything passing. `make time` is unchanged, because
what it times reads the element whole.
**Next:** the analysis is per name and per loop, and `let one = world[i]`
inside a counted walk is the same shape with the same answer and does not get
it. Whether that is worth a second place to look, or whether the two should be
one, is the question.

## The walked name is what the element was

Measuring the question the last entry left — whether `let one = a[i]` deserves
the same treatment as a walk — turned up something worse: D052 had changed
what a walk means.

```kest
for i, e in w.enemies {
    w.enemies[i].health = 0
    seen += e.health
}
```

That added up the healths before D052 and noughts after, because the name had
become a view of memory the same turn was writing. I shipped that last turn.
Nothing caught it: every example that wrote what it walked happened to read
before it wrote, so the answer came out the same either way.

The rule is recorded as D053 and it is not negotiable: the walked name is the
element as the turn began. The address is taken only when nothing in the body
could write the array — it must not name what is being walked, must write
nothing through an index, and must hand no array, store or reference to a
call. That is coarse on purpose. Two handles cannot be told apart here, and
there is no global mutable state in this language, so a write reaches an array
through a name in scope or through a call that was given one; refusing both is
sound and telling them apart is a question this compiler does not ask.

The shapes that matter keep the speed: 46 nanoseconds an entity against 66.

`examples/world` gained a walk that writes first and reads after, and the
compiler as it was yesterday answers 6 on it. That is the part that mattered:
the hole was in the check, not only in the compiler.

The question that started this — `let one = a[i]` — is answered no. It is the
same analysis for a smaller gain, 77 nanoseconds against 67, and it would need
the same aliasing rule to be sound. It is not worth a second place to look.

**Runs:** `make check`, everything passing. `make time` is unchanged.
**Next:** three of the last four entries found their work by measuring first
and two found a bug that way. What has never been measured is the thing the
language is named for: how long `kest_call` takes to cross into a program and
back, which D007 says is the wider of the two directions.

## A crossing costs what a crossing costs, not what the program is

D007 says the crossing into a program is the wider of the two directions and
the bulk-first shape rests on it, and nothing had ever put a number on it.

A call into `return n + 1` was 62 nanoseconds in a program of six functions
and 430 in a program of sixty-one. `kest_call` took a name and searched for
it every call, over everything the program defined, twice — a host writes
`spawn` and a function is compiled under `spawn#i32`, so the exact pass fails
before the prefix pass runs. The cost of calling into a program grew with the
size of the program.

`kest_entry` finds a name once and `kest_call` takes what it found, recorded
as D054. It is 21 nanoseconds now, in both programs. `kest_defines` is gone:
`kest_entry` gives -1 for a name that is not there, which is the same question
with one function fewer.

A handle rather than a faster search, for the reason the stride and the frame
width are where they are: finding what a name means is a start-up question and
the API should look like one.

What it says about D007: a crossing at 21 nanoseconds against two or three for
an element of a batch keeps bulk-first right, by about the margin the
predecessor measured. The design stands and the number behind it was taken
here rather than inherited.

Nothing was kept. The host that measured this is not in the repository, which
is what D050 said would happen to a second measurement that had not earned a
place: it answered its question and it is gone.

**Runs:** `make check`, everything passing. `make time` is unchanged.
**Next:** `kest_entry` is resolved against the runtime and `kest_frame_slots`
against the build, so a host holds both to call one function. Whether those
two questions belong to the same thing is worth a look.

## One thing to ask, and it is the runtime

A host held two objects to prepare one call: `kest_entry` against the runtime,
`kest_frame_slots` against the build, and `kest_build_name` to make the name
both of them wanted.

Both are questions about the compiled program and the runtime is what a host
has while it is running, so `kest_frame_slots` takes what `kest_entry` gave
and the name is resolved once for both. Recorded as D055. A chunk carries what
it gives back now, which is what let the answer move off the build.

`kest_entry` also leaves the module off: it tries the bare name and then the
one the file registered under, so a host writing `spawn` does not have to know
about `embed.spawn`. That is a fact about the program's files and not about
the boundary.

Not one call returning both. A `{where, slots}` would tempt a host to hand the
program's own answer back as its frame width, and D046's check is the host
saying how wide *its* array is.

`kest_build_name` left the public header. The command line still uses it to
name into the program's symbol table, which turns out to be a different
question that had the same spelling. The header is sixteen functions where it
was seventeen two entries ago, and `examples/embed` no longer touches the
build to call anything.

**Runs:** `make check`, everything passing.
**Next:** the header is sixteen functions and every one of them is about
running a program that is already compiled, except the two that compile it.
Whether a host that only wants to run something should have to know about
building it at all is worth asking.

## While a program runs, the runtime is the only thing to ask

`kest_report` took the build, so a host that got `false` from `kest_call` had
to reach back to the object it compiled with to find out why. It takes the
runtime now, recorded as D056 — the same move D055 made for
`kest_frame_slots`, one function over.

A runtime records how much had been said when it started, so it never reports
what failed to compile: that went to the `errors` stream `kest_build` was
given, and would have been said twice otherwise. Asking twice still says each
thing once.

The build is for building. Four functions touch it — compile, free, start, and
the one the command line uses internally — and everything a host does while
its program runs is the runtime.

The question that started this was whether a host that only wants to run
should have to know about building at all, and the answer is that it has to: a
Kest program is source and somebody has to turn it into one. Inventing a
compiled artefact to save two lines would be a file format, a version, and a
way for the two to disagree. What the boundary can do is make the compiled
thing something handed over once and then forgotten, and that is what it is
now.

Found while checking it: a refusal about a frame named the function as it was
compiled, `embed.spawn#store<embed.Npc>,i32`, which is a name no host ever
wrote. It says `embed.spawn`.

**Runs:** `make check`, everything passing.
**Next:** `kest_start` takes limits and a machine that runs out of stack says
so, but nothing says what the limits should be. A host picks numbers and finds
out at the worst moment whether they were enough.

## The program says how much room it needs

`kest_start` took a stack size and a call depth and a host had nothing to base
them on. `examples/embed` said "fifty frames of a hundred and twenty slots"
and neither number came from anywhere.

`kest_needs` works it out, recorded as D057: every chunk carries the slots it
needs and the depth its own stack reaches, and the bytecode carries who calls
whom, so the deepest run of frames and what they take together is a walk of
the call graph. It is enough for every function a host could call rather than
the least for one, because a host wants one number and not one per call site.

No answer is an answer. A program that can reach itself has no deepest run of
frames, and neither has one that calls through a function value: D039 put the
promise in the type and did not put the target there. `examples/tree` and
`examples/shapes` are each one of those, and they say so.

Two bugs on the way, and both were found by running at exactly the number.
The first was mine reading badly: the recursion was handed the address of one
number where it wanted the array of them, so it wrote past the end of a stack
buffer — the sanitiser named the line. The second is the one worth recording:
the walk sized a jump at seven bytes, because `JUMP` and `BACK` are their own
operand kinds beside `U16` and the default case caught them. Decoding went out
of step after the first `if`, so half the calls in a program were never seen
and the numbers came out too small to start with.

`examples/embed` asks now, and says what it was told: twenty-six slots and two
frames.

**Runs:** `make check`, everything passing.
**Next:** `kest_needs` walks the bytecode to find who calls whom, and
`contract.c` walks the tree to find the same thing for `no.alloc`. Two call
graphs of one program, built from two things, and neither knows about the
other.

## A promise proved against what was emitted

Two call graphs of one program, one from the tree and one from the bytecode,
and the question was whether they should be one. They cannot be: the tree walk
runs before anything is emitted and reports against source spans, and the
instruction walk runs after and knows what the machine does. What they can be
is checked against each other.

So `no.alloc` is proved twice now, recorded as D058. The second proof walks
the instructions of every function that promised, follows its calls, and asks
the machine's own list of which opcodes reach the allocator.

It is a backstop and its message says so: it points at an instruction and
calls the disagreement a fault in the compiler rather than in the program.
That is the right shape, because the tree walk has had two silent holes — it
never looked inside a `match` arm, and it never looked at a call it could not
name — and both were found by accident.

Proved by making the hole on purpose. With `walk_expr` skipping an `if`, a
`no.alloc` function that builds an array inside one is allowed by `check` and
refused by `emit`, at the line that builds it. `check` cannot catch it, which
is worth knowing: there is no bytecode at that point, and the second proof is
a property of emitting.

It does not follow a call through a function value, because the bytecode does
not say what the value is. That is the one place the type is the only
evidence, and D039 is what makes it evidence.

**Runs:** `make check`, everything passing.
**Next:** `kest_module_prove` and `kest_module_needs` both walk every
instruction of every chunk, one after the other, and both were written with
the same stepping loop copied. A third thing that wants to walk the code will
copy it again.

## One answer to how wide an instruction is

Two walks over the code shared a stepping function and the disassembler had
its own, which is two answers to how many bytes an instruction takes. That
disagreement is exactly what D057's bug was.

`kest_op_width` is the one answer now, recorded as D059. The disassembler
prints operands its own way and moves by it.

One answer is not enough on its own, because nothing says it is the right one.
So building a program walks every chunk to the end and requires it to land
exactly there — every chunk ends in a return, so a wrong width overshoots or
stops short. Proved by declaring a jump seven bytes wide: building
`examples/state` then says `state.next` has 170 bytes of code and a step that
lands on 171, and refuses.

The two graph walks are not merged and should not be: one computes two numbers
over a call graph and the other looks for the first allocation in one. What
they share is the stepping, and that is what is shared.

A note on how this went: `git checkout src/value.c` to undo a deliberate break
also undid the turn's work, which had not been committed. The pieces went back
in by hand. Nothing was lost and nothing about the language changed, but it is
the kind of thing worth writing down once.

**Runs:** `make check`, everything passing.
**Next:** `kest_module_prove` reports a fault in the compiler and the only way
to see one is to break the compiler on purpose. Every other check in `tools`
is run against the language; nothing runs the compiler against itself.

## The compiler's checks on itself are checked

D058 and D059 added two refusals that only speak when the compiler is wrong,
and the only way anyone had seen either speak was by breaking the compiler by
hand and undoing it. A net nobody has seen catch anything is indistinguishable
from no net.

`tools/check-backstops.sh` puts each out of order in a copy of the tree and
requires it to fire. Recorded as D060. Two named breaks, each one a hole this
compiler has actually had: a tree walk that does not look inside an `if`, and
a jump that says it is a different width. No framework and no random
mutation — each break carries the program that should be refused and the code
that should refuse it.

It works in a copy, so it cannot leave the repository broken. That matters
more than it sounds: the last entry lost a turn's work to a `git checkout`
used for exactly this.

It found a real weakness in what it was checking. The first version of the
break did nothing at all — it moved a `return 3` above a label that already
returned 3 — and the second version was caught on a branching program and not
on a four instruction one, because a wrong step can land back on the end by
luck. The walkability check asks more now: every step has to land on something
that is an instruction, and the last one has to be the return every chunk ends
with.

Also fixed while here: `check-docs.sh` made a temporary directory every run
and never removed it.

**Runs:** `make check`, everything passing, nine seconds.
**Next:** `tools/check.sh` builds twice, walks every file with every command
under the sanitisers, and builds the compiler twice more for the backstops.
Nothing says which of those is worth what it costs, and the first one to be
skipped will be skipped quietly.

## `defer`, and a reservation that was not one

The reference said `type` and `defer` were reserved, and `let defer = 5` in
the compiler worked. A claim about the language that the language did not keep.

`defer f(x)` runs when the block it is in ends, however it ends, recorded as
D061. Several run in the reverse of the order they were written. A `return`
runs everything outstanding, a `break` runs what the loop it is leaving added,
a `continue` the same, and a block runs what it added itself unless it left
through one of those.

There is no new instruction: the compiler holds what was deferred and writes
the calls out at each way out. A `return` works out its answer first and then
runs them, so a deferred call sees what the function decided.

It takes a call and nothing else, and it counts against a `no.alloc` promise,
because what is deferred still runs.

`type` is refused as a name now with nothing promised about what it will mean.
That is the honest half of a reservation: nothing has to be renamed the day it
means something, and nothing is claimed about the day.

`examples/host` is where it belongs — a function that takes something from the
host and has three ways out, with the bracket closing on all of them. The
keyword table in the reference was wrong in the other direction too: it listed
neither of the two new words and it did not say that `flags` is not one.

**Runs:** `make check`, everything passing.
**Next:** `defer` is written out at every way out, so a function with three
returns and two defers emits six calls. Nothing measures whether that matters,
and the shape that would — one place to jump to on the way out — is a
different compiler.

## What `defer` costs, measured

A function with three deferred calls and five ways out compiles to 223 bytes
and fifty-seven instructions. The same function with the three calls written
out before each of the five returns compiles to 223 bytes and fifty-seven
instructions. `defer` costs exactly what writing it out costs, which is D062
and the whole answer to the question the last entry left.

The cheaper shape — an inner function and a wrapper that cleans up once — is
123 bytes, and costs a call on every invocation and a function that now
exists. That is a decision about where the boundary of a function is, and not
one a compiler should make on anyone's behalf. Keeping a list at run time
would trade code size for bookkeeping on every call that defers anything,
which is the opposite of cost being visible.

Nothing to fix, so the turn went into the paths a feature added last turn had
one use of. `defer` was tried in a `match` arm, in nested loops leaving
through `break` and through a `return` from the inside, and in a `while`; the
orders came out right when worked through by hand. `examples/host` now has a
walk that defers per turn, falls off the end twice and leaves through a
`return` on the third, so `make check` covers it rather than a scratch file.

**Runs:** `make check`, everything passing.
**Next:** four examples each declare their own two-component vector and write
their own length and scale. A language for games ships no vector in its
library, and `std.vec` is the thing every one of them is missing.

## The library has vectors

Three examples each declared their own vector and wrote their own length and
scale. `std.vec` is `Vec2` and `Vec3` of `f32` with the dozen things that go
with them, recorded as D063, and `examples/physics`, `examples/world` and
`examples/shapes` use it instead.

`vec.add(a, b)` and not `a + b`, because there is no operator overloading and
this is not the module to want one. `direction` gives an optional, because a
vector of nought length has no direction and that is a question with no answer
rather than a zero returned quietly.

`length` reaches the host for a square root, which means importing `std.vec`
means binding one. `lengthSquared` is there for everything that does not need
it, and it is the one a frame budget reaches for: comparing two of them orders
the same way comparing lengths does.

`examples/frame` keeps its own `Vec3`. That file is about boundary
declarations and mutual struct references and declaring is what it is for.

`examples/physics` is sixty-one lines where it was eighty-odd, and what it
lost was the part that was not about physics.

**Runs:** `make check`, everything passing. `make time` is unchanged at about
160 nanoseconds, which is `tools/frame.kest` and not one of the three.
**Next:** `std.vec` has ten functions written twice, once for two components
and once for three, and the bodies are the same shape with one line more.
Generics take a type and not a count, so nothing in the language says how to
write it once.

## `[T; N]` is that many where it stands

The question was `std.vec` writing ten functions twice, and the answer is that
this is not what generics over a count would be for. Writing `Vec2` and `Vec3`
separately is fine. Underneath it was something worse: a Kest struct could
hold `[f32]`, which is eight bytes and a handle, so a C struct with an array
inside it could not be described at all — and that is the shape a host lends.

`[T; N]`, recorded as D064. `struct Transform { m: [f32; 4] tag: i32 }` is
twenty bytes with the floats inside, which is what a C compiler gives the same
declaration, and `/tmp` proved it by lending an array of three and having Kest
total them in place.

It is a value: copying one copies all of it. `[T]` stays the other thing and
`push` is refused on this one. `len` is a constant and an index is checked
while running.

Three instructions — `load.slots`, `store.slots` and `offset.addr` — for
reading and writing one of them in slots, and for stepping an address by an
index in memory the host laid out.

`;` moved from the lexer to the parser. It was refused where it was read,
which is the lexer deciding statement structure, and `[f32; 16]` has no
statements in it. The message is the same and it now comes from where a
statement ends.

Two things went wrong and both were caught immediately. The literal `[1, 2, 3]`
compiled to a heap array and was then stored as three slots, and `len` of one
read a handle out of the middle of it; both gave right answers by accident,
which the bytecode showed and the tests did not. And `kest_fixed_of` registered
its type, which composed types are not: `kest_find_type` walks every registered
name and a composed type has none, so it took `strlen` of NULL. The sanitiser
named the line.

**Runs:** `make check`, everything passing, with `examples/inline` new.
**Next:** `std.vec` could be `[f32; 2]` and `[f32; 3]` underneath, which would
make `vec.add` one body over a count rather than two over a type. Whether that
reads better than `x` and `y` is a real question and not obviously yes.

## Two answers about that many

The question was whether `std.vec` should be `[f32; 2]` and `[f32; 3]`
underneath, so that `add` could be one body over a count rather than two over
a type. The answer is no, recorded as D065: it would put `v.parts[0]` where
`v.x` is, in the code everybody reads, to save ten short bodies in the code
almost nobody does.

Asking it turned up something real beside it. `[T; N]` shipped indexed and
counted and not walkable — `for i in 0..len(m)` worked and `for one in m` did
not — which is an inconsistency in the feature rather than a decision anybody
took. It walks now.

The walk is over a copy, because that many of something is a value. Walking it
where it stands would let a write to the run inside the body change what the
walk reads, which is what D053 refused for an array, and there is no reason
for the two to differ. `examples/inline` writes the run inside a walk of it
and checks that the walk did not notice.

**Runs:** `make check`, everything passing.
**Next:** `examples/inline` walks a `[f32; 4]` and `tools/frame.kest` walks a
`[Npc]`, and only the second has a number beside it. What a fixed run costs
against a struct with the same fields has never been asked.

## An index written down is not an index

The question was what a fixed run costs against a struct with the same fields.
It cost more, and it should not have: `m[2]` went through the path a
worked-out index goes through — a constant pushed, a bounds check that could
not fail, an instruction taking a base and a stride. Ten bytes where `a.z` is
three.

It folds now, recorded as D066: into a slot where the run is in slots and into
a byte offset where the run is memory the host laid out, for reading and for
writing alike. Four paths and one rule. An index written past the end is
refused where it is written, with the number and the count, because both are
written down.

The measurement after: 93 nanoseconds against 94 over ten thousand of them,
which is the same number, and the bytecode says why — it is the same bytecode.

The other half of the question answered itself. Walking a run came out at 251
nanoseconds against 263 for the same loop written by hand with a count and a
worked-out index. The same, which says the copy D065 makes costs nothing
measurable and what separates a walk from four unrolled reads is the loop.
Nothing to fix, and better to know than to guess.

**Runs:** `make check`, everything passing.
**Next:** `[T; N]` is a value and `[T]` is a handle, and a program that wants
the first inside the second writes `[[f32; 4]]`, which nothing has tried. What
a host lends when the element is itself a run has never been crossed.

## What is lent has a name

`[[f32; 3]]` turned out to already work inside a program — built, indexed,
written and walked, with the nesting right — so the only untried part was
lending one. It cannot be lent, and that is the rule rather than a gap:
a lend names a type and a run is spelled out of other types and has no name.

Recorded as D067. The alternative would be a host spelling `[f32; 3]` and the
runtime matching that string, which puts a check whose whole point is catching
a disagreement on getting a space right. A struct around it costs one line,
gives both sides a name, and is the same twelve bytes.

The refusal says that now: a name with a bracket, an angle or a question mark
in it gets the struct written out for it.

`examples/embed` lends an array of `Point`, which holds a `[f32; 3]`, and the
host declares `struct { float at[3]; }` beside it. Twelve bytes on both sides,
walked in place, and it runs in both builds — the shape D064 exists for, which
until now had only been crossed in a scratch file.

**Runs:** `make check`, everything passing.
**Next:** `kest_borrow` looks a name up among the layouts a module happens to
have made, so a type the program declares and never puts in an array cannot be
lent even though it has a name. What can be lent depends on what the program
compiled to rather than on what it declared.

## What can be lent is what the declarations say

`kest_borrow` looked a name up among the layouts a module happened to have
made, and a layout is made where a body reaches into an array. A program
declaring `fn how(all: [Point]) -> i32` and only counting them could not be
handed any: the signature said `[Point]` and the boundary said no.

Every element type a signature mentions gets a layout now, recorded as D068,
whether or not a body ever reached one. What a host can be handed is a
question about what the program takes, and the declarations are where that is
written.

A type in no array and no store is still refused, because there is nothing to
lend an array of, and the message for that was already right.

It went a long time without being noticed because a program that takes
`[Point]` almost always indexes one somewhere and one function doing so is
enough for every other. It takes a program that can only count what it was
given. That is rare enough that a contrived example covering it would be worse
than saying so here; what `make check` covers is the lend a host actually
does, in both builds.

**Runs:** `make check`, everything passing.
**Next:** the reference says a `store<T>` can be lent and nothing has tried
that either. A store is a slot map with generations, not a run of elements, so
the answer is probably that it cannot — and the boundary says otherwise.

## A handle says what it is

The question was whether a `store<T>` can be lent. It cannot — a store is a
slot map with generations, live flags and a free list, and nothing a host has
is one — and asking turned up something worse.

A host could lend an array of `Npc` and hand it where `store<Npc>` was
wanted. The program read the array header as a store header and counted
nought, with no complaint. The previous entry made that reachable by laying
out store element types, which was wrong on D068's own terms: a signature
saying `store<Npc>` does not say it takes an array of them. That is undone.

But the confusion was possible before that too, wherever a type was both a
store's element and an array's. So both headers begin with a word saying which
they are, and every instruction that takes one checks it. A store handed where
an array was wanted is `K0612` now, at the instruction that noticed.

Measured, because the argument against it was cost: `make time` was 160 to 172
nanoseconds an entity before and 142 to 153 after. The compare is free, and
the header growing by a word did not hurt. That is the whole reason to measure
rather than argue.

D046 said the right number of slots holding the wrong things is the host's to
get right, and that still holds for everything that is not a handle. A handle
is the case where getting it wrong is a wrong memory read rather than a wrong
answer, which is why it is the one thing the machine checks.

**Runs:** `make check`, everything passing.
**Next:** `kest_borrow` hands back an array header the program will read, and
nothing stops a host calling it twice for the same block and keeping both. Two
headers over one block is two lengths that can disagree, and the second one to
grow would be writing where the first still points.

## `'a'` is one byte

The question was whether two `kest_borrow` calls over one block can disagree.
They cannot: a borrowed array grows and shrinks nowhere, so both headers hold
the length the host gave, and writing through one is visible through the other
because it is the host's memory. Demonstrated, and `examples/embed` has been
doing it since it lent events twice.

So the turn went to something the library made obvious. `std.text` asked
whether a byte was a space by writing `byte == 32 || byte == 9 || byte == 10
|| byte == 13`, and lower case by `>= 97 && <= 122`. Nobody reads that.

`'a'` is a `u8` whose value is that byte, recorded as D070. Not a character
type: `'ı'` is two bytes and is refused with the count. The escapes are a
string's, because two spellings of one byte is what D004 refuses.
`std.text`, `examples/words` and `examples/pieces` say what they mean now.

A new token kind has to be added to the list of what a newline may end a
statement after, and nothing said so: a line ending in a byte literal swallowed
the next one, and the refusal pointed at the line after. That list is a third
parallel thing beside the two `check-tables.sh` holds, with no mechanical rule
to check it against, so it has a comment saying what it is.

**Runs:** `make check`, everything passing.
**Next:** `while len(a) > 0 { if let one = pop(a) { } }` is two levels for one
idea, and `while let one = pop(a)` is the shape. `if let` exists and the loop
form does not.

## `while let`

`while len(a) > 0 { if let one = pop(a) { } }` was two levels for one idea,
with a count and a lookup that cannot fail having to agree. `while let one =
pop(a)` asks the question once, where it is answered. Recorded as D071.

It is `if let` in a loop and nothing about it is new: what the optional held is
named for as long as there was something to name, and the name exists only
inside the loop.

The one thing to get right is where the turn that stopped leaves its value. An
optional is what it holds with a tag above it; the jump takes the tag, the turn
that ran stores what is below it into the name, and the turn that stopped has
to drop it. So the way out of the loop is not where a `break` lands — a `break`
happens after the store, with nothing left to drop. Both were tried, along with
`continue`, a `return` from inside, a value wider than one slot, and one of
these inside another.

`examples/queue` drains newest first and stops at the one it cannot afford,
which is the shape the loop is for.

**Runs:** `make check`, everything passing.
**Next:** `pop` gives an optional and `remove` gives the element or fails at
run time, which are two answers to the same kind of question. Nothing says why
one asks and the other insists.

## A host may call in from inside a call

The question was why `pop` asks and `remove` insists, and D031 answered it two
months of entries ago: an empty array has no last element and that is a case,
while a position out of range is a claim that was wrong. The reference said
half of that, and says both now.

The turn went to something worse, found by asking what a host can do that has
never been tried. A bound function that calls `kest_call` wrote over the frame
it was called from — every run started at the bottom of the stack and at frame
nought — and segfaulted, which is the good case.

It starts above what is already running now, recorded as D072. Where the
machine is, is written down before a bound function is invoked, and a run
started from inside one puts its frame and its stack there. Tried one level
deep, forty, and five thousand: the last stops at a thousand and twenty-four
with `K0602`, which is the frame limit, and unwinds clean under the sanitiser.

Room is the host's to ask for, because how many times a bound function will
call back is the host's to know. `kest_needs` answers for one call in and says
so.

`examples/embed` has it now: the engine decides what a step costs and asks the
program, which is an engine whose rules live on both sides. The command line
binds the same name to answering one, because it is a host with no engine —
two hosts, two answers, one program.

**Runs:** `make check`, everything passing.
**Next:** the machine now writes down where it is before a bound function
runs, and a bound function may also lend, reset the heap, or free the runtime.
Only the first of those was thought about.

## What a bound function may not do

D072 taught the machine where it is while a bound function runs. The same
knowledge answers what else that function may do, and three things were
reachable from there with only one thought about.

Resetting the heap from inside a call read what it freed: a program holding an
array called a bound function that reset the heap, and read the freed block on
the next line. Freeing the machine from inside is worse and quieter, because
the frames and the stack are what the interpreter is standing on.

Both are refused now and both say so, recorded as D073. Freeing leaves the
heap for `kest_build_free`, which is a leak rather than a read of what was
freed and is the right way round.

Lending from inside is fine and stays fine, which is the distinction: a bound
function may put something on the heap and may not take the heap away.

Nothing was added to find out whether the program is running — it is running
exactly when a bound function is on its stack, which D072 already wrote down.

**Runs:** `make check`, everything passing, and all three shapes tried under
the sanitiser: the reset refused, the free refused, the lend allowed.
**Next:** `kest_host_bind` can be called after `kest_start`, and what a
runtime resolved at start is what it keeps. Binding something after the fact
looks like it worked and does nothing.

## Binding a name twice

`kest_host_bind` used to replace an existing binding and return `true`. A
machine takes what the host held when it started, so after `kest_start` that
answer was a lie: the table changed and the machine kept the function it had
resolved. A host swapping in a stub clock got the real one and no sign of it.

The second binding is refused now, recorded as D074. The alternative — having
the host table repoint every runtime it has produced — makes a name something
that changes underneath a running program, and a host that wants to swap a
function can bind one that decides, which is C it was going to write anyway and
which works mid-run.

The header said the old behaviour and now says this one, and the reference says
it beside the rest of the boundary. Both hosts bind distinct names, so nothing
in the tree changed shape.

**Runs:** `make check`, everything passing, plus a throwaway host that binds a
name twice: first bound, second refused, third name bound, and the function the
table holds is still the first one.
**Next:** `kest_entry` is asked for a name and answers an index, and a host
that asks for a name the program does not have gets -1. Nothing says whether it
is missing or merely not a function the boundary can call.

## What -1 was not saying

`kest_entry` answered -1 three ways: the program does not define that, the name
is generic and is several functions, and the name is an extern the host itself
provides. The first is a question a host is allowed to ask. The other two are
mistakes, and both looked like a typo in a name that was spelled correctly.

The two now say why, into the diagnostics the host already reads with
`kest_report`, recorded as D075. `K0614` points at the `extern fn` line that
asked for the function. `K0615` lists the copies, which matters because the
name to pass instead is not written anywhere in the program:

```
error[K0615]: `pick` is generic and is compiled once for each set of types it is used with
      ask for one of them: `pick#T,T$i32`, `pick#T,T$f32`
```

`kest_module_find` already knew how a copy is named and was the only thing that
did; the counting moved to `kest_module_copies` and `find` asks it for one, so
looking a name up and saying why the lookup could not answer cannot come apart.

**Runs:** `make check`, everything passing, plus a throwaway host over a file
with a generic used at two types: `main` found, `pick` -1 with the list,
`Io.write` -1 pointing at `std.io`, a name nothing knows -1 and silent, and the
name the suggestion gave resolving to a function.
**Next:** `kest_frame_slots` answers 0 for an index that is not a function, and
0 is also the honest width of a function that takes nothing and gives nothing.
A host that asks about a name it never checked gets a number that means both.

## Zero meant two things

`kest_frame_slots` answered zero for an index that is no function, and zero is
also the honest width of a function that takes nothing and gives nothing. A
host that passed `kest_entry`'s -1 straight through sized nothing, called, and
heard about it from `K0607` — a message about the call rather than about the
mistake.

Zero stays the answer and `K0616` now says which zero it is, recorded as D076.
Changing the signature to a status and an out-parameter was the other way, and
it makes every host carry a temporary and a branch for a failure the report
already shows.

```
tick    0
twice   1
missing 0
error[K0616]: there is nothing at -1 to ask the width of
```

`examples/embed.c` checks the index before it asks the width, so it stays
silent, which is the shape the header now tells a host to write.

**Runs:** `make check`, everything passing, plus a throwaway host over a file
with a `fn tick()` and a `fn twice(n: i32) -> i32`: zero, one, and zero with a
message on the third.
**Next:** `kest_report` writes to a `FILE *` and nothing else. A host that
wants what the program said as JSON — which every other command can produce —
has no way to ask for it.

## What the boundary says, as JSON

The commands have had `--json` from the start and a host embedding the library
had prose and nothing else, so the third goal was true of the CLI and false of
the library the CLI is one host of.

`kest_build` and `kest_report` take a `KestForm` now, recorded as D077. Both of
them, because a host with JSON for what failed while running and prose for what
failed to compile has to parse both.

```
{"diagnostics":[{"severity":"error","code":"K0604","file":"boom.kest","line":4,
"column":13,"offset":60,"length":1,"message":"index 9 is outside an array of
length 3"}],"errors":1}
```

The count in a report was the whole run's rather than the written tail's, which
nothing showed while only prose was written and JSON says out loud. Two asks
now say 2 and then 1, rather than 2 and then 3.

`examples/embed.c` passes `KEST_FORM_TEXT`, which is what it wants and now says
so at the call.

**Runs:** `make check`, everything passing, plus a throwaway host over a file
that fails to compile and one that fails while running, in both forms, with the
JSON parsed by something that is not this project.
**Next:** `kest_start` takes limits and refuses a program whose externs the host
does not provide, one diagnostic per missing name. A host embedding a program
it did not write has no way to ask what those names are before it starts, so it
learns them one failed start at a time.

## Asking before starting

`kest_start` refuses a program whose externs are not all bound and names each
one, which is the right refusal and the wrong way to find out. A host embedding
a program it did not write bound what it guessed, started, read the names out of
the refusal, and started again.

`kest_build_extern(build, at)` is that list before the refusal, recorded as
D078. It answers a name by position and NULL past the last, so a host walks it
from zero; a count beside an accessor is two things that can disagree.

```
Io.write  (binding a stub)
Math.sqrt  (binding a stub)
...
started yes
```

The list is what the program declares rather than what it calls — a file
importing `std.math` for one function asks for all seven — because that is what
starting holds a host to, and a list that did not match the refusal would be
worse than none.

**Runs:** `make check`, everything passing, plus a throwaway host that binds a
stub for every name the program asks for without knowing the program, and
starts on the first try.
**Next:** `kest_needs` answers false for a program that can reach itself or
calls through a value, and a host then picks a number. Nothing says which of
the two it was, so a host cannot tell a program it could size from one it
never can.

## Which kind of no

`kest_needs` answered false for a program that can reach itself and false for
one that calls through a function value, and a host could not tell them apart.
They are different news: the first is a shape a host can go and look at, the
second is the language working and leaves nothing to do but pick a number.

It takes a `KestReason *` now, recorded as D079, holding which of the two and
the function it was found in:

```
self.kest:  no least, `down#i32` can reach itself
value.kest: no least, `apply#fn(i32) -> i32,i32` calls through a value
w.kest:     3 slots, 2 frames
```

The return stayed a `bool` rather than becoming the enum, because every
`if (kest_needs(build, &limits))` already written would keep compiling and mean
the opposite. A new parameter is a compile error at every call site instead.
`KEST_REACH_UNASKED` covers a build that did not compile and a run out of room,
so that every false has a reason rather than one of them meaning two things
again.

`examples/embed.c` prints which it hit, so the second host exercises the
parameter rather than passing NULL past it.

**Runs:** `make check`, everything passing, plus a throwaway host over three
files: one that recurses, one that calls through a value, and one that does
neither.
**Next:** `kest_borrow` takes the element type by name and the size of one, and
answers a value whose `object` is NULL when they disagree. The disagreement is
reported, but a host that lends in a loop finds out at the first one and has
no way to ask beforehand what the program thinks a `Point` is.

## Asking what a `Point` is

`kest_borrow` compares the host's `sizeof` against the program's stride, which
is the check that matters and was the only way to run it: a host lending in a
loop found out at the first lend, and one that lends at frame nine found out at
frame nine.

`kest_build_layout` answers it beforehand, recorded as D080. It gives a count,
because a name fails to mean one type in two ways, and hands over the whole
`KestLayout` rather than the size, because the pieces let a host check field by
field:

```
Point          2 of them
other.Point    one, 8 bytes, 2 slots, aligned 4
Nothing        0 of them
```

The lookup moved out of `kest_borrow` into `kest_module_layout_of` and the lend
asks it, so what a host is told and what a lend refuses for cannot come apart.
All three of the lend's refusals still read the same, checked by lending with a
wrong size, an unknown name, and an ambiguous one.

`examples/embed.c` now checks `Point` and `Event` once before it starts, and
says what the program thinks they are:

```
`Point` is 12 bytes in 3 slots, aligned to 4
`Event` is 16 bytes in 3 slots, aligned to 8
```

**Runs:** `make check`, everything passing, plus a throwaway host over two
files that both declare a `Point`, asking about a name with two types, one with
none, and a spelled-out type that can have none.
**Next:** a lend disagreeing about size says the two declarations have come
apart, and now that the layout is readable it could say where: the first piece
whose offset or scalar the host cannot have meant.

## The notes were there and nobody saw them

A refused lend now points at the declaration it is about: a note at each of two
types sharing a name, and the fields with their byte offsets when a size
disagrees.

```
error[K0610]: the program lays `other.Point` out in 8 bytes and this host has 12
      the two declarations have come apart
 --> other.kest:3:8
  |
3 | struct Point {
  |        ^^^^^ `x: f32` at 0, `y: f32` at 4
```

Adding the notes showed that nothing rendered them. `kest_diags_render` printed
the message and the suggestion for a diagnostic with no span of its own and went
to the next one, notes and all — and a lend is at a host rather than in a file,
so it never has one. The JSON form had been keeping them the whole time, which
is the worse half: the two renderings are the same set by rule, and one was
losing what the other showed. Recorded as D081.

Which field moved is not said, because this side never sees the host's struct.
The suggestion for two of a name now names the one that can be asked for, since
a name with its module in front is the only one of the two a host can say.

**Runs:** `make check`, everything passing, plus a throwaway host over two files
that both declare a `Point`: the ambiguity with a note at each declaration, the
size disagreement with the fields, and the same three read back out of the JSON
form by something that is not this project.
**Next:** a lend of a name the program does not hold in an array says only that
it does not. The diagnostics rule says an unknown name reports the nearest
match, and every other stage does; this one does not.

## The nearest name that can be lent to

A lend that did not know the name it was given said only that. Every other
stage in the language answers an unknown name with the nearest one it has, and
this was the one place that did not:

```
error[K0610]: the program has no array of `Smaple` to lend to
      the nearest one that can be lent to is `Sample`
```

Recorded as D082. Only what the program holds in an array is offered, because a
name it has and cannot lend fails the same way the first one did. What is
offered is what a host can write and get: `other.Point` rather than `Point`
where two types share the written name, worked out by `kest_module_askable`,
which is the same answer the refusal for two of a name gives — one function
rather than two rules that could disagree.

`kest_edit_distance` and `kest_type_written` moved out of the two files that
had them privately. What a program calls a type is matched on, printed and now
suggested, and three copies of one `strrchr` is how those stop agreeing.

**Runs:** `make check`, everything passing, plus a throwaway host lending four
names it does not have: a plural, a transposition, something unrelated, and a
spelled-out type — the middle two suggested, the others not.
**Next:** `kest_call` reports a frame too narrow and a bad entry, but a host
that passes a frame wider than the program needs is told nothing, and the extra
slots are read as arguments when the function takes fewer.

## A call with no frame at all

The line this turn came from said a frame wider than the program needs is read
as arguments. It is not: the call copies exactly what the function takes and
what is past that is the host's array being larger than this call. There is
nothing to report and nothing was changed for it.

What is next to it is real. Every check in `kest_call` was written
`frame != NULL && ...`, which reads as carefulness and was permission: a call
with no frame skipped the width check, skipped the copy in, and ran on whatever
the stack floor was still holding.

```
with a frame: 42
with none:    said it worked
```

`fn twice(n: i32)` called with no frame doubled the number the call before it
had left there, and said it had worked. A null frame is now a frame of no
slots, recorded as D083, and the message is the one a too-narrow frame already
gets:

```
error[K0611]: `twice` takes 1 slot and this frame holds 0
```

It stays allowed, because a function that takes nothing and gives nothing needs
no array and making a host write `KestValue frame[1]` for it would be ceremony.
The guards below are gone rather than kept: a guard that can never fire reads
as a case that can happen.

**Runs:** `make check`, everything passing, plus a throwaway host calling three
ways: no frame at a function that takes one slot (refused), no frame at one
that takes nothing (works), and a frame wider than needed (works, which is the
answer to what this turn was asked).
**Next:** a frame too narrow for what comes back is found out after the program
has run and its result thrown away. The chunk says `result_slots` before it
starts, so that call could be refused before anything it does happens.

## Refusing before the program runs

A frame too narrow for what comes back was found out after the call: the
program had done whatever it does and the answer was thrown away.

```
it needs 2 slots
narrow: refused
make ran
wide:   worked
```

`make` prints as it runs, and the narrow call no longer prints anything —
recorded as D084. The width comes from the declaration, which is what
`kest_frame_slots` already answers from, so the refusal and the number a host
sized by are the same number.

That trusts the emitted code, so the emitted code is now held to it. `K0407` is
a third invariant beside walkability and `no.alloc`: no `return` carries a width
greater than the declaration. Less is allowed and happens, because the `return`
written past the end of a body gives nothing; more is what would be read back
into a host's frame past the end of it.

`check-backstops.sh` breaks it on purpose — a compiler emitting `size + 1` — and
catches it, which is the whole reason to have written the invariant down rather
than assumed it.

**Runs:** `make check`, everything passing, all three backstops catching what
they are for, plus a throwaway host calling a function that gives two slots
with a frame of one and then of two.
**Next:** `kest_heap_used` says what the program has allocated and nothing says
what it is allowed. A host that set `KestLimits` can watch one number climb
towards a limit it has to remember on its own.

## A ceiling for the heap

`KestLimits` had the stack and the depth of calls, which are what `kest_needs`
works out, and nothing for the heap — which is the one that grows while a
program runs. A host in a frame budget had `kest_heap_used` to watch and nothing
to hold it to.

`heap_bytes` is that ceiling, recorded as D085. Zero is none, which is what
every host had before:

```
given 0 bytes: ran
  used 262327
given 65536 bytes: stopped
  used 32929
error[K0617]: the program has used the 65536 bytes it was given
 --> hungry.kest:7:9
  |
7 |         push(rows, i)
  |         ^
```

`K0605` stays what it is for: a machine that has actually run out is nobody's
mistake and a ceiling is the host's own number coming back, so the ten
instructions that allocate ask one function which of the two they hit.

The check is in the arena, before a block is taken from the host, so a program
held to its ceiling does not allocate to find out it may not. The arena keeps a
running total now rather than walking its blocks: that walk was fine for a
number asked now and then and not for one asked at every allocation.

**Runs:** `make check`, everything passing, plus a throwaway host running the
same program twice, once with no ceiling and once with 64 KB, and
`examples/embed.c` with the third zero it now needs.
**Next:** `kest_heap_reset` gives the heap back and keeps the ceiling, but a
host cannot ask what the ceiling is. Everything else a machine was made with
can be asked of it afterwards; this one the host has to have kept.

## What the machine is running with

None of the three limits could be read back. A host that kept its own copy was
fine; one that did not had nothing to ask, and `kest_heap_used` is a number
without a scale until the ceiling beside it is readable. A host that passed
nothing had it worse: the stack and depth it got are constants in a source file
it does not have.

`kest_allowed` fills the struct `kest_start` was given, recorded as D086:

```
asked for nothing: 65536 slots, 1024 frames, 0 bytes
asked for some:    32 slots, 4 frames, 4096 bytes
```

All three rather than the heap alone, because the question is what this machine
is running with and answering a third of it invites two more functions later.
Zero for a heap with no ceiling, because that is what the host would pass to ask
for the same machine.

`examples/embed.c` now sets a heap ceiling of a megabyte — a frame budget is a
ceiling as well as a floor — and says what it used of it:

```
used 360 of 1048576 bytes, in 62 slots and 4 frames
```

**Runs:** `make check`, everything passing, plus a throwaway host starting the
same program twice, once with no limits and once with all three, and reading
both back.
**Next:** `kest_start` can be called more than once on one build, and each
machine gets its own heap and stack. Nothing says whether two machines from one
build may run at the same time, and the module they share is written to when a
generic is instantiated.

## Two machines, one build

The line this turn came from worried that the module two machines share is
written to when a generic is instantiated. It is not: a generic is copied per
set of types while compiling, and nothing in the machine adds a function, an
extern or a layout to a module. What they shared and should not have was the
diagnostics.

```
a runs: failed
what b says about itself:
error[K0604]: index 9 is outside an array of length 3
what a says about itself:
error[K0604]: index 9 is outside an array of length 3
```

`b` had never run. It started before `a` failed, so `a`'s failure was inside
its own range and it reported it as its own — and then `a` reported it again.
Both of the header's promises about `kest_report`, that nothing is written
twice and that nothing from before this machine started is written at all, were
false as soon as there were two machines.

Each machine holds its own now, recorded as D087, and `b` says nothing. The
other way was a field on every diagnostic saying which machine raised it,
stamped at eight sites and filtered by a renderer that skips most of what it
walks.

The command line still starts a machine the way a host does and takes the set
rather than a rendering, because it sorts what running found together with what
compiling did: `kest run` on a program that reads past an array still says so
and still exits 1, in both forms.

**Runs:** `make check`, everything passing, plus a throwaway host running one
of two machines into a failure and asking both what they have to say.
**Next:** `kest_vm_run` has no callers. The worklog says it was removed when the
command line started going through the same door a host does, and it is still
in `vm.c` and `vm.h`.

## What the headers were promising

`kest_vm_run` had no callers. The worklog says it went when the command line
started going through the same door a host does; it did not, and it has been
sitting in `vm.c` ever since. Looking for it found two more: `kest_ast_dump_all`,
which prints every file's tree and which nothing asks for, and `kest_load`,
declared in `loader.h` and never written at all.

Five more were called only from the file that defines them, which makes a
header an announcement nobody answers: `kest_lexer_init`, `kest_lexer_next`,
`kest_fn_of`, `kest_nearest_type` and `kest_op_width`. All static now. The last
keeps its rule and says what happens the day something outside walks a chunk:
it stops being static rather than being copied.

Two public functions had no host in this tree using them. `examples/embed.c`
now walks what the program asks for and checks each name against what it bound,
which is `kest_build_extern` and `kest_host_find` doing the thing they were
written for:

```
the program asks for `Engine.decide`, which this host provides
the program asks for `Io.write`, which this host provides
```

`tools/check-dead.sh` is what keeps it, recorded as D088, and is the sixth
thing `make check` runs: every header declares what is there, and something
other than the file it lives in calls it. It reads the symbols out of the
objects, because a name in a comment is not a call.

**Runs:** `make check`, everything passing, with the new tool reporting 105
declarations all present and called, and both hosts still doing what they did.
**Next:** `make check` builds `examples/embed` and the tool builds it again to
read its symbols. One of the two is a copy of the other.

## One compile of the second host

`check-dead.sh` was compiling `examples/embed.c` a second time to read its
symbols, with its own flags, which is a copy of what the build does. The build
makes `build/release/embed.o` now and links the host from it, and the tool
reads that.

What a host calls is readable in an object and gone once it is linked: after
the link every name is defined and nothing says who wanted it. That is the
whole reason the object is kept.

The tool no longer makes what it cannot find, either. It says to build first,
because a tool that quietly builds a missing host passes while saying nothing
about the thing it is for.

Proved by breaking it: in a copy of the tree with the extern walk taken out of
`examples/embed.c`, it says

```
include/kest.h: nothing outside build.o calls `kest_build_extern`
```

**Runs:** `make check`, everything passing, and the tool refusing in a copy of
the tree where the second host stops using a public function.
**Next:** `check-dead.sh` proves nothing about itself. The backstops tool
breaks the compiler on purpose in a copy of the tree; a tool whose whole job is
to catch what nobody looks at is the next thing that should be caught failing.

## Breaking the check that catches what nobody looks at

`check-dead.sh` only fires when a header is wrong, which means nobody has seen
it fire, which means nothing said it works. That is the same argument the
backstops were written for, so it goes in the same place:

```
caught: a tree walk that does not look inside an `if`
caught: a jump that says it is a different width
caught: a `return` wider than the function gives back
caught: a header promising a function nobody wrote
caught: a function in a header that nothing outside its file calls
```

A hole used to name a program to run and a code to look for. It now names
either that or a tool to run, and one of them takes two edits, because a
function that nothing calls is a definition in one file and a declaration in
another.

The copy of the tree grew to hold `tools` and `examples`: a tool break has to
run the tool, and the tool reads what the second host calls. Four seconds, for
knowing that the thing which catches what nobody looks at has been seen
catching.

**Runs:** `make check`, everything passing, all five backstops catching what
they are for.
**Next:** `tools/frame.kest` is the one measurement and `make time` prints one
number. Nothing says what the number was last time, so a change that makes it
worse is invisible unless somebody remembers.

## The bottom of a walk

The line this turn came from wanted the number `make time` prints written down
somewhere. It is not going to be. `CLAUDE.md` says there is one measurement and
nowhere it is written down, and `tools/frame.kest` says the same thing in its
own comment: a file of numbers is the first half of a benchmark suite, and this
project deleted its predecessor for having one.

What the instrument is for is answering "did this get slower", and the way to
use it is to run it either side of a change. So this turn changed something and
ran it either side.

A counter in the dispatch loop, thrown away afterwards, said where the time
goes: seventy-nine instructions per entity-step, of which thirty-seven per cent
are `load`, eleven per cent `const` and ten per cent `store`. Five of the
seventy-nine were the bottom of the walk — load the count, push one, add, store,
jump back — and every counted walk in the language ends with those five.

`next` is those five, recorded as D091:

```
  0240  next        4  -> 19
```

Five runs of each, alternating: 175, 190, 177, 213, 215 nanoseconds an
entity-step before, and 158, 155, 158, 163, 162 after. Every run after is below
every run before.

**Runs:** `make check`, everything passing, which is every example run and both
hosts, and the tables tool holding the new instruction to its name.
**Next:** the top of a walk is still four instructions an iteration: load the
count, load the limit, compare, jump if not less. Three of the four walks have
that limit in a slot of their own.

## The top of a walk, once

The four instructions at the top of a counted walk ran every turn because the
walk went back to them. The test is at the bottom now and the instruction that
counts is the instruction that tests:

```
  0019  load        4
  0022  load        3
  0025  lt.i
  0026  jump.false  218  -> 247
  ...
  0240  next.less.i 4  < 3  -> 29
```

The four at the top run once. Recorded as D092, with a signed and an unsigned
instruction rather than one that asks, the way `lt.i` and `lt.u` are two.

Only `for i in a..b` gets it. An array walk asks the array how long it is every
turn, a store walk looks for the next live slot, and a walk over bits counts to
a number in the program rather than in a slot. Those keep D091's `next`.

Five runs each, alternating: 156, 167, 159, 161, 159 before and 152, 153, 155,
158, 151 after. Every run after is below the one before it. Four per cent is at
the edge of what the instrument says it can resolve, so the count is the harder
evidence: four fewer instructions of the seventy-five an entity costs.

Checked by hand as well as by `make check`: an empty range runs nothing, a
`continue` counts and a `break` leaves, nested walks keep their own counts,
assigning to the name still moves nothing and still warns.

**Runs:** `make check`, everything passing, and a throwaway program over the
five shapes a counted walk can take.
**Next:** `load` is thirty-seven per cent of what a frame step runs and `const`
eleven. Half of those consts are the same small integers, and a `const` that
pushes a number reads it out of a table beside the code.

## The constant table is not the problem

Half of what a frame step pushes is nought or one, and reading one back is
three loads that depend on each other: the frame's chunk, the chunk's
constants, the element. An instruction carrying the number itself should have
been cheaper.

It was written, and it worked, and it made no difference. Six runs of each,
alternating: 152, 155, 155, 153, 155, 156 nanoseconds an entity-step with the
table and 156, 158, 157, 153, 153, 157 with the number in the instruction.
There is no direction in that, so it is out again, recorded as D093. The tree
is what it was before this turn touched it.

What the number says: seventy-five instructions in about a hundred and fifty
nanoseconds is roughly six cycles each, which is what the dispatch costs by
itself. Three hot loads hide inside that.

So the thing that pays is removing instructions, not making them cheaper. The
last two turns took four each out of a walk's turn and both showed; this one
took none out and did not.

**Runs:** `make check`, everything passing before and after, and the
measurement either side of a change that has been taken out again. One of the
backstops noticed the code it breaks had moved while the change was in, which
is what it is for.
**Next:** a walk over an array asks the array how long it is every turn, so a
`push` inside the walk extends it. D053 says what the walked name is and
nothing says what the length is, which makes this true by accident either way.

## What a walk is over

A walk over an array asked how long it was every turn, so pushing inside the
body extended the walk and popping shortened it. Neither was decided: D053 says
what the walked *name* is and nothing said what the *walk* is.

It is over what the array held when it began, recorded as D094. The length is
taken once:

```
seen 2, length 4
```

Two elements walked, four in the array afterwards. What was pushed is there and
is walked by the next walk. Removing is the other half and is not silent:

```
error[K0604]: index 1 is outside an array of length 1
  --> grow.kest:22:5
   |
22 |     for y in ys {
   |     ^
```

A loop whose length its own body decides is a loop with no bound, and this
language is for programs with a frame to fit in. Refusing a `pop` inside a walk
while compiling was the other way round and cannot be done honestly, because a
function the body calls can pop too.

The length in a slot is also what D092's instruction wants, so an array walk's
turn went from six instructions to one.

**Runs:** `make check`, everything passing, plus a throwaway program over the
shapes a walk takes: the sum of an array of structs, the position as well as
the element, a body that writes the element it is reading, `break`, an empty
array, and the two that changed — pushing and popping inside the walk.
**Next:** a walk over that many of something — `[T; N]` — counts to a number
in the program rather than in a slot, so it still tests at the top. It is the
last of the four that does not use one instruction a turn.

## The last two walks that counted at the top

`[T; N]` has how many in its type and a set of bits has one for each name it
declares. Both knew the number while compiling and both pushed it every turn to
compare against. It goes in a slot beside the count now, and both end in the
instruction that counts and tests:

```
total 6      where 2      stopped 2      skipped 4
count 2      empty 0      first 1
```

Recorded as D095. Four of the five shapes a walk takes are one shape
underneath: a count, a limit beside it, one instruction a turn. The store walk
keeps `next`, because it looks for the next live slot rather than counting —
slots go dead and D020 allows removing while walking, so there is nothing to
compare against.

**Runs:** `make check`, everything passing, plus two throwaway programs: a run
walked four ways — summed, with its position, with a `break`, with a
`continue` — and a set of bits walked three, including an empty one and a
`break` on the first.
**Next:** `for` over a store calls `seek` to find the next live slot, then
compares the answer with nought and jumps. That is the last walk whose turn is
five instructions, and the search is the part that cannot be removed.

## The last walk

A store's walk was eight instructions and a step: load the store, load the
place, search, store the answer, load it again, push nought, compare, jump.
Only the search cannot be removed, because slots go dead and there is no limit
to count to.

`seek.from` and `seek.next` are the rest of it, recorded as D096. One above the
loop that leaves when there is nothing live, one at the bottom that goes back
while there is:

```
  0018  seek.from   3  4  -> 122
  0115  seek.next   3  4  -> 25
```

`next` went with it. D091 wrote it two turns ago for the bottom of every walk;
D092 and D095 gave four shapes something that counts and tests, and this gives
the fifth something that looks and tests, so nothing emitted it any more. An
opcode nothing emits is worse than admitting the shape moved under it.

The disassembler needed a second way to print a target: a forward jump and a
backward one are not the same arithmetic, and printing one as the other gave
`-> 4294967224`, which is what caught it.

**Runs:** `make check`, everything passing, plus a throwaway program that walks
a store five ways: summed, after a removal, removing every slot as it goes,
empty, and with a `break` on the first.
**Next:** every walk now has the same shape and four functions in the compiler
emit it: two guards, two closings. They agree today because they were written
in the same week.

## One description of a walk

Every `for` in the language now has the same shape, and four functions emitted
it: two guards and two closings, agreeing because they were written in the same
week. Three of the guards were the same four instructions written out three
times.

There is one pair now, `open_walk` and `close_walk`, and a `Walk` that says
what the walk keeps its place with: a count in a slot, and either a limit
beside it to count to or a store to look through. Which of the two is a field
rather than four functions.

Nothing about what is emitted changed, and that is checked rather than claimed:
the compiler from the last commit and this one were both built and asked to
print the bytecode of all twenty-nine files in the tree, and the two outputs are
identical.

**Runs:** `make check`, everything passing, and twenty-nine files emitting
byte-for-byte what they emitted before.
**Next:** `for` over a `text` is not in the language. A string is its bytes, and
walking one is `for i in 0..len(t)` and an index, which is the shape the walks
were just taught to do in one instruction.

## Walking text

Text was the only sequence `for` did not walk, so reading bytes meant
`for i in 0..len(t)` and an index while reading anything else meant a walk.

`for b in t` gives the bytes and `for i, b in t` gives the position with them,
recorded as D097:

```
bytes 4, len 4        for "hız"
spaces 2
at 2
stopped 2
nothing 0
```

The shape it replaced is quadratic: `t[i]` measures the string to know whether
the index is inside it, so an indexed walk measures once per byte. Four thousand
bytes is 196 microseconds indexed and 77 walked, and that gap grows.

The walk reads with `text.in`, which does not measure. It is allowed not to
because the walk took the length when it began, nothing writes a byte of text,
and the count is the walk's own — the same argument every other walk makes, and
`text.at` still measures for an index a program wrote itself.

**Runs:** `make check`, everything passing, plus a throwaway program walking
text five ways — a multi-byte string, a `break`, positions, an empty string, and
inside a `no.alloc` function — and one that times the two shapes against each
other.
**Next:** `find(t, needle)` and `slice(t, from, count)` are the two things that
look inside text, and both measure it. `find` says it costs nothing, which is
true of what it allocates and not of what it reads.

## Finding the second one

`find` only found the first. A program looking for the next sliced the rest of
the string and looked in that, which is a piece of the heap per step and which
a `no.alloc` function cannot do at all — so the commonest thing to do with text
was the most expensive.

`find(t, needle, from)` starts where it is told, recorded as D098. The answer is
an index into the whole string, so a scan carries one number:

```
commas 3        counted in a `no.alloc` function, no slicing
first 1
later 4
no z
nothing at the end
```

Starting outside the string says so, at the call: `looking from -1, which is
outside text of 3 bytes`. Starting at the length finds nothing, because that is
where a scan arrives when it has read everything.

The instruction takes three either way, so there is one of it: the compiler
pushes a nought when the third argument was not written.

**Runs:** `make check`, everything passing, plus a throwaway program that counts
occurrences without allocating, finds from a place, finds nothing, starts at the
end, and one that starts outside and is refused. The arity and the type of the
new argument are both reported.
**Next:** `slice` is the other half of what a program does to text, and it
always copies. Cutting a line into fields copies every field, and a program that
only wants to compare them has paid for text it will never keep.

## The rest of a piece of text

Cutting a line into fields copied the whole remainder at every step, because
`slice` was the only way to say "from here on" and `slice` makes text. A program
that only wanted to look at the fields paid for all of them and everything after
them, and a `no.alloc` function could not do it at all.

`rest(t, at)` copies nothing, recorded as D099. A piece of text is bytes ending
where they end, so the rest of one is a pointer further along the same bytes:

```
fields 3          counted with no allocation
rest two,three
all one,two,three
end []
error[K0604]: the rest from 14 is outside text of 13 bytes
```

It walks to `at` rather than measuring, so a loop taking the rest of the rest
reads each byte once between all its turns. The reason that matters is worth
saying plainly: an index into text costs the index, because nothing carries the
length. `t[i]` in a loop reads the string again for every byte.

`std.text` had that problem twice: `split` copied the remainder per piece, and
`append` and `number` walked by index. They walk now, and `ends` compares the
rest with the suffix rather than stepping through both. Every example that uses
the library prints exactly what it printed before.

**Runs:** `make check`, everything passing, plus a throwaway program over the
library — six numbers, five endings, four splits, trim, upper, lower, repeat —
and the four examples that use `std.text` compared line for line against what
they said before.
**Next:** `starts` is the one left stepping through two strings by index, which
costs the length of the prefix times the length of the subject. Nothing in the
language compares a place in one piece of text with another.

## Whether a piece is here

`starts` stepped through two strings an index at a time, which costs the length
of the subject times the length of the prefix, because an index into text costs
the index. Nothing in the language could compare a place in one piece of text
with another.

`matches(t, at, needle)` does, recorded as D100:

```
abc at 0 abc true      abc at 1 bcd false
abc at 1 bc true       abc at 3 empty true
field two true         field four false
```

`field` there is a `no.alloc` function that walks a comma-separated line with
`find` and `rest` and tests each field where it stands. Nothing is copied and
nothing is measured twice.

`find(t, needle, at) == at` answers the same question and reads the whole
string when the answer is no. Two things that answer one question at different
prices is how a program gets slow quietly, so the cheap one has a name.

`starts` and `ends` are one line each now. Every example that uses `std.text`
prints what it printed before, and a place outside the text says the length it
actually has rather than the nought it had counted to.

**Runs:** `make check`, everything passing, plus a throwaway program over ten
shapes of `matches` including both ends and an empty needle, one that is refused
for a negative place, and the four examples that use the library compared line
for line.
**Next:** `text(bytes)` builds a piece of text from a run of bytes and is what
`join`, `repeat`, `upper` and `lower` end with. Nothing says what it costs, and
it is the one thing in the library that always allocates.

## Reading a line without keeping it

The line this turn came from said nothing says what `text(bytes)` costs. The
reference says it and so does `std.text`; the premise was wrong. What was wrong
with the reference is something else: the example under it still built bytes
with an index and a `while`, which is the shape three turns of work replaced,
and the sentence beneath said gathering them costs nothing. Pushing reaches the
heap. What is true is that the array grows and the piece of text is paid for
once, rather than the whole of it being copied at every step, which is what
`slice` does. Both are fixed.

The real gap was that nothing in `examples` exercised any of it. `for b in t`,
`find` from a place, `rest` and `matches` are four turns of work that `make
check` did not touch, and an example is what makes a thing keep working here.

`examples/scan.kest` reads the line `examples/parse.kest` reads, and keeps
nothing:

```
3 fields adding up to 49, and nothing was copied
```

Every function in it promises `no.alloc` and the compiler holds all of them to
it, which is the whole demonstration: a program can now read text without
reaching the heap at all. `parse` still cuts the line into pieces, because that
is what a program that wants to keep the fields does, and the two sit beside
each other on purpose.

**Runs:** `make check`, everything passing, with the new example run under both
builds and both sanitisers like the rest, and its formatting held to what `fmt`
prints.
**Next:** `examples/parse.kest` and `examples/scan.kest` read the same line two
ways and neither says what the other costs. `make time` measures one shape and
nothing measures allocation, though `kest_heap_used` has been able to say it
since D012.

## Saying it once

The line this turn came from wanted the two ways of reading a line measured
against each other. There is not going to be a second measurement: `CLAUDE.md`
says there is one and that a second is a decision, and the decision here is no.
The question it would answer is already answered, better, by the compiler:
every function in `examples/scan.kest` promises `no.alloc` and is held to it,
and `examples/parse.kest` cannot make that promise. A number would say the same
thing less reliably and would need a file to compare against.

What was worth fixing was next to it. An unreadable character produced two
messages: what is wrong with it, from the lexer, and then "expected an
expression, found invalid token" from the parser. The second is vaguer than the
first and about the same place. It is not written any more, recorded as D101 —
recovery is unchanged, only the sentence is dropped.

And the first message now says what to do. A backslash inside a hole is the
mistake everybody makes once, because every other language with holes needs the
escape:

```
error[K0102]: unexpected character `\`
 --> scan.kest:8:18
  |
8 |     io.print("{f(\"x\")}")
  |                  ^ a hole holds code, so a string inside one needs no escape: `{f("x")}`
```

Outside a hole the same character says the other half: an escape is written
inside text, and that place is not inside any.

**Runs:** `make check`, everything passing, plus the two shapes of a stray
backslash, an unterminated string, and an ordinary parse error to see that it
still speaks.
**Next:** `kest_token_name` prints `invalid token` for a token the lexer
refused. Nothing prints it any more now that the parser does not name one, so
either it is dead or it is reachable another way.

## Showing what was lexed

`invalid token` is the name of the token the lexer makes for something it could
not read, and after the last turn nothing printed it: the parser had stopped
naming one and `kest lex` refused to show a stream from a file with an error in
it.

That refusal was the wrong way round. A token stream is whole — the lexer makes
a token and carries on — and a file that will not compile is exactly when
somebody runs `lex` on it:

```
   9:13  `=`            =
   9:15  invalid token  \
  10:1   `}`            }
```

Recorded as D102. The tree is still not printed when something was refused,
because a tree with a statement missing from it says the file is something it
is not. The file is lexed twice and the second is muted, so what is wrong with
it is still said once.

**Runs:** `make check`, everything passing, including the command sweep over
every file, and `kest lex` on a file with two bad characters: the stream in
full, each error once, and the exit status still 1.
**Next:** `kest parse` prints nothing when a statement was refused, which is
the right call and leaves a person with no way to see what the parser did make.
The tree is in there and nothing can look at it.

## What the parser did make

`kest parse` showed nothing when something was refused, which left the tree it
had built with nothing able to look at it — and a file that will not parse is
when somebody runs `parse` on it.

It is shown now, with a line saying what it is:

```
// this is what parsed; 1 thing refused
(fn one
  (result i32)
  (return 1)
)
(fn two
```

That line is the whole difference from D102, which refused this on the grounds
that a partial tree says the file is something it is not. It said that about
showing one unlabelled. Recorded as D103.

**Runs:** `make check`, everything passing, plus five broken files under the
sanitisers — a refused expression, a stray backslash, an unterminated string, a
half-written struct and function, and a broken type inside a parameter — none of
which crashes the dump, all of which still exit 1.
**Next:** `kest fmt` is the one command that still shows nothing after a
mistake, and it has the strongest reason: what it prints is meant to be written
back over the file. Nothing says that reason where a person would look for it.

## Refusing and doing nothing look the same

`kest fmt` shows nothing when a file does not parse, which is right: what it
prints goes back over the file, and a form of half a program would delete the
other half. What it did not do was say so.

```
kest: `keep.kest` is not formatted, because what `fmt` writes has to be the
same program and this one did not parse
```

`kest fmt -w` on a broken file printed the diagnostics and left the file alone,
and a person who did not read them carefully would think it had been formatted
and found nothing to change. Recorded as D104.

The sentence goes on the standard error, never on the output, because the
output is a file's contents. Asking for JSON gets the diagnostics as JSON and
no sentence — which found the other half of this: `fmt` was the one command
where `--json` did not mean what the help says it means. It does now.

**Runs:** `make check`, everything passing, plus `fmt`, `fmt -w`, `fmt --check`
and `fmt --json` over a file that does not parse — the first three say it, the
last one answers in JSON that parses, and the file is untouched afterwards.
**Next:** `kest fmt --check` names files that are not in the form it prints and
exits non-zero. A file that does not parse now exits non-zero for a different
reason and prints no name, so a caller looping over its output sees a pass
where there was a refusal.

## What a command says as JSON was not always JSON

`kest fmt --check` now names a file that does not parse, which is what this turn
was for: the question is whether every file is in the one form, a file that is
not a program is not in it, and printing nothing meant a caller looping over the
names saw a pass. Recorded as D105.

Looking at how `fmt` says things in JSON found something much worse. For any
program with an enum, `kest check --json` had been writing plain words inside a
JSON array:

```
"types":[enum embed.Event  3 slots, 16 bytes aligned 8
  0 Idle
```

It has been doing that since enums were laid out, and nothing noticed because
the tool that checks `--json` looked at the first character. An object that
goes wrong in the middle starts with a brace too. Recorded as D106.

`fmt --json` was the other half: it printed the file's contents where an object
was asked for. It says one object a file now — what was wrong with it, and
whether it is already in the one form — and does not print the text, because a
stream that is a JSON object and a file's contents at once is neither.

`check-commands.sh` parses what every command says with a JSON parser now,
`fmt` included, and requires one object a line. Proved by putting the enum fault
back in a copy of the tree: `check examples/embed.kest --json: not one object a
line`.

The two JSON string writers became one, in `diag.c`, because three files
compose JSON and the string is the part that has to be right.

**Runs:** `make check`, everything passing, plus every command against every
example and standard library file with `--json`, all parsed by something that is
not this project; `fmt` in all four of its shapes in both forms; and the fault
put back in a copy of the tree to watch the net catch it.
**Next:** `kest emit --json` prints the diagnostics as an object and the
bytecode as text on the same stream. It is the one command whose answer is not
in what it says as JSON.

## The instructions, as JSON

`kest emit --json` printed the diagnostics and nothing else. The line this turn
came from guessed it printed the listing beside them; it threw the listing away
entirely, which is worse and is the same fault `fmt` had a turn ago.

It says the instructions now, recorded as D107:

```json
{"name":"scan.spaces#text","parameterSlots":1,"slots":2,"deep":3,
 "code":[{"at":0,"op":"load","operands":[0]},{"at":9,"op":"text.len","operands":[]}]}
```

Each instruction is its offset, its name and the numbers after it. How many
numbers follow is `(width - 1) / 2`, so this cannot step differently from the
walk the machine and the prover do — the width is the one answer there has ever
been. The decorations the text form adds, the value behind a constant and where
a jump lands, are left as the numbers.

**Runs:** `make check`, everything passing, which now parses every command's
JSON for every file; `emit --json` over the examples with the biggest one
counted instruction by instruction; the text form unchanged beside it; a file
with nothing to run answering with empty lists rather than a sentence; and a
file that does not compile answering with the diagnostics and no listing.
**Next:** `kest call` prints what a function gave back, and with `--json` it
prints it to the standard error and the diagnostics to the standard output.
The one command whose answer is a value says it where a person would not look.

## Where the answer goes

`kest call --json` printed what the function gave back on the standard error.
The standard output has to be the JSON, so the value had to go somewhere, and
beside it was the wrong somewhere: the one command whose answer is a value was
the one saying its answer where nothing was reading.

It is in the object now, recorded as D108:

```
twice 21   {"diagnostics":[],"errors":0,"result":"42"}
name       {"diagnostics":[],"errors":0,"result":"one \"two\""}
maybe 0    {"diagnostics":[],"errors":0,"result":"none"}
nothing    {"diagnostics":[],"errors":0}
```

Written the way the language writes it, and written once: `result_text` makes
the characters a person reads and the characters the string holds, so the two
cannot come apart. A call that could not be made has no `result` either, and
says why for a person on the standard error, which is where D104 put that kind
of sentence.

**Runs:** `make check`, everything passing, plus a file of seven answers — an
integer, a float, text with quotes in it, a truth, an optional both ways, and a
function that gives nothing — in both forms; a call that cannot be chosen; and
one that fails while running, which answers with the diagnostic and no result.
**Next:** `kest tick` drives a program with events and prints what the heap
holds afterwards. With `--json` it says the diagnostics and not the number,
which is the last command whose answer is not in what it says.

## The last command that said its answer somewhere else

`kest tick --json` printed its lines to the standard output and then the object
after them, so what came out was not JSON:

```
onEvents  1 crossing   returned 0
onEvent   3 crossings returned 0, peak 24 bytes
heap      24 bytes, none of it freed
{"diagnostics":[],"errors":0}
```

The sweep that parses every command's JSON did not cover `tick`, because `tick`
takes a count and the loop did not. It covers it now, which is what made this
the last one: every command a person can run is parsed for every file in the
tree.

The numbers are in the object, recorded as D109:

```json
{"diagnostics":[],"errors":0,"onEvents":{"crossings":1,"gave":0},
 "onEvent":{"crossings":3,"gave":0,"peak":24},"heap":24}
```

`drive_events` fills a struct rather than printing, so what a person reads and
what the object holds come from one place. And driving a program that takes no
events now says so, because a heap of nought and nothing else looks the same as
a program that took the events and did nothing with them.

**Runs:** `make check`, everything passing, with the sweep now covering `tick`;
tick over the events example with and without `--reset`, in both forms; a
program that takes no events; and the frame example, which takes none either.
**Next:** `takes_events` decides whether a function is the one to drive by
looking at what it takes, and says nothing when a program has an `onEvent` of
the wrong shape. A program that spells its own entry point wrongly is told it
takes no events at all.

## One mistake, one sentence

The line this turn came from said an `onEvent` of the wrong shape is ignored in
silence. It is not: it has always said what it takes and what tick has to give
it. What was wrong is what the last turn added beside it —

```
kest: `onEvent` takes `text`, and tick has `i32` to give it
kest: nothing here takes events; write `onEvents(events: [i32])` or ...
```

— the second of which is false when the first has just been said. It is said
now only when neither name is there, recorded as D110.

And when neither is there, the likeliest reason is a misspelling, so it answers
the way the rest of the language answers an unknown name:

```
kest: nothing here takes events; write `onEvents(events: [i32])` or `onEvent(event: i32)`
      `e.onEvnt` is the nearest name this program has
```

**Runs:** `make check`, everything passing, plus four programs driven with
events: one with an `onEvent` taking the wrong type, one taking two arguments,
one that misspells the name, and one that has neither — each saying exactly one
thing, and the events example still driving both ways.
**Next:** `entry_name` builds the qualified name of an entry point from the
root file's module, and `kest_entry` undoes that by looking for both. Two
places know that a name is qualified and they agree by hand.

## One place qualifies a name

`entry_name` in the command line and `kest_build_name` in the library did the
same thing: put the root module in front of a name. `main` and `call` used one,
`tick` used the other, and they agreed because they were written from each
other.

They read different fields as well — the unit's alias and the module's — which
are set from each other and so were the same string. Now there is one function
and it reads the module's, which is the field `kest_entry` reads when it takes
a module off a name a host wrote plainly. Recorded as D111.

`drive_events` takes the build now rather than the program, the arena and the
root unit separately, which is what made the second function unnecessary: it
had all three in one pointer already.

**Runs:** `make check`, everything passing, plus `tick` on the events example
and on the misspelled one, `call` on two files, and `run` — the four callers of
the one function that qualifies a name.
**Next:** `kest call` prints `<[parse.Field]>` for a function that gives back an
array, which is the shape of the type and not what it gave. The one command
that answers with a value cannot write half the values the language has.

## One writer for a value

`kest call` printed `<[parse.Field]>` for a function that gives back an array —
the shape of the type where the value should be. It had its own writer, which
knew integers, floats, truths, text and optionals; the machine's writer knew
integers, floats, truths, text, sets of bits and enums. Two writers, each
missing what the other had.

There is one now, recorded as D112. `call` prints through the same function
that fills a hole in a string:

```
door 7     Door.Locked(7)
state      State.Idle | State.Armed
maybe 0    none
name       one "two"
```

The rule about what can be written at all moved out of the checker, so the
compiler refusing a struct in a hole and the command line refusing to print one
are the same rule:

```
kest: there is no text for `[parse.Field]`, which is what `fields` gives
      call something that gives a value with text, or write the fields you want to see
```

Optionals gained text on the way through. They were refused in a hole, and the
reason for refusing a struct — several spellings, and the author knows which —
is not true of one: it is `none` or what it holds. `"{maybe(5)} and {maybe(0)}"`
is `5 and none` now.

**Runs:** `make check`, everything passing, plus seven answers through `call` in
both forms, an enum and a set of bits which it could never print before, an
array which it now refuses with the compiler's own words and a non-zero exit,
and optionals of three types in a hole.
**Next:** `kest_write_value` writes `?` for a type it does not know, which is
now unreachable from a hole and from `call` because both ask first. Nothing
else calls it, so the question is whether that branch is a net or a leftover.

## Held together by the compiler

The writer in the machine ended with `default: "?"`, for a type it did not
know. After D112 nothing can reach it: a hole and the command line both ask
`kest_type_has_text` first. But unreachable by agreement is not unreachable by
construction, and the agreement was two switch statements written from each
other.

Neither has a `default` now. Both list every tag the language has, and
`-Wswitch` inside `-Wall` inside `-Werror` does the rest. Proved by adding a
tag in a copy of the tree:

```
src/types.c:197:5: error: enumeration value `KEST_T_MADEUP' not handled in switch
src/vm.c:524:5: error: enumeration value `KEST_T_MADEUP' not handled in switch
```

A tag added to the language now stops the build until somebody says whether it
has text and what it is. Recorded as D113. The branch C still wants at the end
says `<no text>` rather than `?`, because a fault should read like a fault.

**Runs:** `make check`, everything passing, and a copy of the tree with a made-up
tag in it, which does not build and names both places.
**Next:** `kest check` prints what the program holds, and its JSON says types,
functions and constants. Nothing in either says which functions promise
`no.alloc` about the *program* rather than one function at a time, which is the
one thing a frame budget is read against.

## A constant is worked out where it is written

The line this turn came from wanted `no.alloc` said about the program rather
than about a function. It is said about every function, in both forms, and a
reader wanting the set filters the list: nothing was missing. Looking for
something that was, a probe found `const B: i32 = 4 * 2` refused with `only a
literal constant is compiled yet` — a K05xx, which is the range for what the
compiler cannot emit, and marked as a gap by the word "yet".

It works out constants now, recorded as D114:

```
const CELLS: i32 = WIDTH * HEIGHT     144
const HALF: f32 = 1.0 / 2.0           0.5
const MASK: u8 = 1 << 3               8
const SAME: bool = NAME == "kest"     true
const NARROW: i8 = 120 + 10           -126
```

The last one is the interesting one: a constant wraps at its declared width
because the arithmetic that made it is the arithmetic the language has. And two
refusals say which they are rather than sharing one message — dividing by
nought, and a constant made out of itself.

**Runs:** `make check`, everything passing, plus eleven constants over every
shape the folding handles, a pair that are made of each other, and one divided
by nought.
**Next:** `[T; N]` still takes a literal for its count, which D064 decided and
gave a reason for: a size that could change. A constant cannot change, and now
a constant is worked out where it is written.

## A count may be a name

D064 asked for a literal in `[T; N]` and gave the reason: a size that could
change. A constant is worked out where it is written now, so it cannot, and
what a host has to match is readable either way — `kest check --json` says
`Grid` is forty-eight bytes of `[i32; 4]` and `[f32; 8]` whichever spelling made
it. Superseded as D115:

```kest
const SIDE: i32 = 4
const CELLS: i32 = SIDE * 2

struct Grid {
    row: [i32; SIDE]
    all: [f32; CELLS]
}
```

The folder moved out of the compiler into the type layer on the way, because
two things ask what a constant is worth now: the compiler pushing it, and a
count using it. One folder, one answer. A symbol keeps what its constant is
written as, and constants are declared before struct fields are resolved,
because a field may be that many of something.

The two literal readers — the escapes in a string and the digits of a number —
moved to the lexer, which is what knows how a thing is spelled. Both layers
call them now instead of the compiler having its own.

**Runs:** `make check`, everything passing, plus a struct of two runs counted
by constants, one counted by a constant that is nought, one counted by a piece
of text, and one counted by a name that is not there — each refused where it is
written, saying which.
**Next:** `array(n, v)` takes a count worked out while running, and `[T; N]`
takes one worked out while compiling. Nothing says what happens when a program
writes `array(CELLS, 0)`, which is the same number in both worlds.

## A constant may be a struct

The line this turn came from asked what `array(CELLS, 0)` does. It works, and
has since a constant became a value: `array` counts with it while running and
`[i32; CELLS]` counts with it while compiling, and it is the same number. The
reference says so now.

What did not work was `const ORIGIN: Vec2 = Vec2(0.0, 0.0)`. It does, recorded
as D116, and so does one built out of others:

```
const CORNER: Vec2 = Vec2(SIDE, SIDE * 2.0)
const FIRST: Box = Box(CORNER, 7)

0.0 2.0 4.0        ORIGIN.x CORNER.x CORNER.y
4.0 7              FIRST.at.y FIRST.tag
20.0               away(ORIGIN, CORNER)
```

A struct is a value laid out flat, so a constant that is one fills a slot per
scalar and using it pushes that many — the same instructions as writing the
fields out where they are used, with a name in front of them. The fold fills
slots now; the arithmetic underneath is untouched.

A piece of text with a hole in it says its own reason now: filling a hole is
what the machine does, and a constant is worked out before there is a machine.

**Runs:** `make check`, everything passing, plus a nested struct constant read
three ways — a field, a field of a field, and passed to a function — and one
copied into a local, and a constant with a hole in it refused with its own
sentence.
**Next:** `const M: [i32; 3] = [1, 2, 3]` is not a constant, and an array
literal of constants is the shape a table of them wants. A struct of them is
one now, and a run of them is the same idea laid out the same way.

## A constant may be a table

`const M: [i32; 4] = [10, 20, 30, 40]` is a constant now, recorded as D117:

```
10 40 4            M[0] M[3] len(M)
2 0.5              CELLS[1].at CELLS[0].weight
30                 pick(2), an index worked out while running
3                  walked
```

That many of something is a value laid out flat, the same as a struct, so the
fold fills a slot per element and the two walks that lay a value out learned to
say so.

Indexing one needed the other half. A run is indexed where it is — in slots, or
at an address a host lent — and a constant is neither: it is a value where it
stands. It goes into slots of its own and is indexed there, which is what a walk
of one already did.

Ordering was the trap. Constants have to be declared after struct fields are
resolved, because a constant of a struct type is measured from them; but a field
may be that many of something and that many may be a constant. Moving constants
first made `[Cell; 2]` two slots wide instead of four, and every value of it
wrong. So the ordering stayed and a count reads the file it is written in: a
count is one name, and a name from another file has a dot in it.

**Runs:** `make check`, everything passing, plus constants of four shapes — a
run of numbers, a run of floats, a run of structs, and a struct of a struct —
read by a written index, by an index worked out while running, through a field,
by walking, and passed to a function. And the four refusals a count can get,
each still saying its own thing.
**Next:** a constant is worked out where it is written and then pushed a slot at
a time wherever it is used, so a table of sixty-four numbers is sixty-four
instructions at every use. A run in the constant table would be one.

## A table is read rather than rebuilt

A constant that is a struct or a run was pushed a slot at a time, so a table of
sixty-four numbers cost sixty-four instructions at every use. It is one
instruction and one copy now, recorded as D118:

```
fn look#i32  1 parameter slot, 9 slots, 8 deep
  0000  const.run   0  8
  0005  store.n     1  8
```

And where the index or the field is written down, nothing is copied at all:

```
fn a  0 parameter slots, 0 slots, 2 deep
  0000  const       0  ; 1
  0003  const       1  ; 2
  0006  add.i
```

That is `T[0] + T[1]`, which used to copy the whole table into slots twice. An
element of a constant run and a field of a constant struct are constants, so
the fold answers them; copying into slots is what an index worked out while
running needs, and now it is only what that needs.

A run is stored once: constants are compared as a whole run rather than a value
at a time, and the entries double as the scalars they are — a program that also
writes `1` shares the first element of `[1, 2, 3, 4]`.

**Runs:** `make check`, everything passing, plus the constants of the last two
turns read every way again, a table read in two places sharing one run, and
`make time` unchanged at the noise of the instrument.
**Next:** `look(i)` still copies the whole table into slots to read one element
with an index worked out while running. The values are in the chunk; reading
one of them by an index nobody has to copy is the same instruction `load.slots`
is, one table over.

## Reading one of a table where it is

`fn look(i: i32) -> i32 { return T[i] }` copied the whole constant run into
slots to read one element, and needed nine slots to do it:

```
fn look#i32  1 parameter slot, 9 slots, 8 deep
  0000  const.run   0  8
  0005  store.n     1  8
  0010  load        0
  0013  load.slots  +1  1 of 8
```

It reads the run where it is now, recorded as D119:

```
fn look#i32  1 parameter slot, 1 slot, 1 deep
  0000  load        0
  0003  const.at    +0  1 of 8
```

`const.at` is what `load.slots` is, one table over: a first index, a stride and
how many, refusing an index outside the run in the same words. The constants
sit in the chunk beside the code and nothing writes to them, so reading one
needs nothing kept anywhere.

Copying into slots is still what a value where it stands and not a constant
needs — what a call gave back, indexed straight away — and is now only that.

**Runs:** `make check`, everything passing, plus a table read by a written
index, by an index worked out while running, and past its end, which says
`index 3 is outside 3 of them` at the line that asked.
**Next:** `const.at` and `load.slots` check the same thing in the same words
from two cases in the machine. So do `elem.addr` and `offset.addr`, which is
four places that refuse an index.

## One sentence for an index

Seven instructions check an index and each carried its own copy of the refusal:
three about an array and four about that many of something. They are two
macros now, beside `HOLD`, which is the same shape for the same reason — a
check that fails, says so at the instruction, and stops the machine.

Nothing about what runs changed, and that is checked rather than claimed: the
compiler from the last commit and this one print identical bytecode for all
thirty files in the tree. Every one of the five shapes still refuses in its own
words:

```
run       index 9 is outside 3 of them
arr       index 9 is outside an array of length 2
put       index 9 is outside an array of length 2
field     index 9 is outside an array of length 2
constant  index 9 is outside 3 of them
```

**Runs:** `make check`, everything passing, plus a file with five shapes of
index in it, each called with a good index and with one past the end.
**Next:** the five messages about text — a byte, a slice, the rest, a match and
a find — each say what is outside what in their own words, which is five
sentences for one idea. They are not the same sentence, so whether they should
be one is a question rather than a copy to remove.

## A run of structs was laid out wrong

The line this turn came from asked whether the five text messages should be
one. They should not: each names what the program was doing, and the part they
share is already the same words. Looking for what was actually wrong found
this:

```
struct WithRun  1 slot, 8 bytes aligned 8
  slot +0  byte +0   run: [Inner; 2]
  slot +0  byte +0   tag: i32
```

Both fields at slot nought. A struct holding that many of a *declared* type was
sized from noughts, because a run caches its size from what it holds when it is
composed — while fields are resolved — and structs are measured in the pass
after that. `[f32; 4]` was right, which is why nothing had noticed.

It is sized where what it holds has just been measured, recorded as D120:

```
struct WithRun  5 slots, 20 bytes aligned 4
  slot +0  byte +0   run: [Inner; 2]
  slot +4  byte +16  tag: i32
```

Those are the bytes a C compiler gives `struct { Inner run[2]; int32_t tag; }`,
which is the whole reason `[T; N]` exists.

The other half was a value too big to lay out: `[i32; 20000]` is eighty
thousand bytes, and the sizes are sixteen bits, so it wrapped to fourteen
thousand and laid the struct out wrong again. Refused now, at the count where
the count is known and at the struct where it is not.

`examples/rows.kest` is the shape: a run of structs in a struct, walked,
indexed, written into where it stands, and read out of what a call gave back.
Nothing in `examples` had one — which is why this survived.

**Runs:** `make check`, everything passing with the new example in it, plus the
layouts printed for a run of structs and a run of floats side by side, two
values too big to lay out refused in two different places, and a nested reach —
`make().run[0].v` — that used to be refused as unreachable and now answers.
**Next:** an enum case that carries that many of something is measured by
`measure_enum`, which does its own walk of what a case holds rather than asking
`measure_held`. If it steps over a run the same way, an enum has the same hole
the struct had.

## The same shape across the boundary

The line this turn came from asked whether `measure_enum` steps over a run the
way the struct pass did. It does not: it asks `measure_held` for every payload,
so D120's fix covered it already. Checked rather than assumed — an enum whose
widest case carries `[Cell; 2]` is five slots and twenty bytes, the tag and
four for the payload, and a `match` reads `cs[1].a` out of it.

What was missing was the boundary. `examples/embed.c` lent a struct with a run
of floats in it and nothing with a run of structs, which is the shape D120 got
wrong. It lends one now:

```
host lent 12 byte points: 11 across
host lent 28 byte rows: heaviest is 23
```

Twenty-eight bytes is `struct Row { Cell cells[3]; int32_t tag; }` on the host's
side and `struct Row { cells: [Cell; 3], tag: i32 }` on the program's, worked
out by each of them on its own. The program walks the rows and the cells inside
them without copying either, under the sanitisers, which is what makes it a net
rather than a demonstration.

**Runs:** `make check`, everything passing, which now includes the second host
lending a run of structs under ASan and UBSan; and an enum carrying a run,
checked and run.
**Next:** `kest_borrow` compares the host's `sizeof` with the program's stride,
so a host whose `Cell` disagreed would be caught at the lend. Nothing compares
the *offsets* inside, so two types of the same size with their fields in a
different order pass.

## Comparing where the fields are

`kest_borrow` compares the host's `sizeof` with the program's stride, which is
what it is given and not enough on its own: two types of the same size with
their fields in a different order are the same size. What says where the fields
are is the layout, one piece a slot, and nothing in the tree had ever read one.

`examples/embed.c` reads them now, beside the size it already checked, and
builds its own side out of `offsetof`:

```
`Point` is 12 bytes in 3 slots, aligned to 4
`Row` is 28 bytes in 7 slots, aligned to 4
```

Proved by turning `Cell` around in a copy of the tree — `weight` before `at`,
which is the same eight bytes — and watching it refuse before it started:

```
`Row` is laid out differently here
```

A tagged union has no one piece a slot, because which type a payload slot holds
depends on the tag, so the layout says `tagged` and `Event` is checked on its
size alone.

**Runs:** `make check`, everything passing, with the second host now reading
the pieces under both builds; and a copy of the tree with a field order that
disagrees, refused at the check rather than read wrongly later.
**Next:** the host's side of that check is written by hand out of `offsetof`,
which is the thing it is checking. A host generating it from the layout would
be checking the layout against itself; a host writing its own struct twice is
what this is for, and nothing says which of the two a reader is looking at.

## A type where a value was wanted

The line this turn came from was about a comment, and the comment is written:
the host's side of the layout check is `offsetof` on its own types, which is the
point of it — reading it out of the layout would be checking the layout against
itself.

Looking for what else a program could write and be told the wrong thing about
found `Box<i32>(7)`. Kest has no explicit type arguments at a call: a copy is
chosen by what it is built with, and `<` in an expression is a comparison. So
that line is three comparisons, and it was reported as an unknown name and then
a type error about `>`:

```
error[K0306]: unknown name `Box`
error[K0314]: `>` needs both sides to have one type, found `bool` and `i32`
```

It says what is wrong now, recorded as D121:

```
error[K0344]: `Box` is a type, and this wants a value
  |             ^^^ a generic takes its types from where it is going: `let b: Box<i32> = Box(7)`
```

And a comparison of something already broken is broken too rather than a truth,
so `if missing < 3` is one message rather than two. That is the general half:
an error type that keeps its poison through an operator is what makes one bad
name one message.

**Runs:** `make check`, everything passing, plus a generic struct built the way
the language writes it — with the types where the value is going — read through
a generic function, and the two shapes that used to cascade.
**Next:** `Pair<i32, text>` written as a type works and `Pair` alone is refused,
but the refusal comes from resolving the type rather than from the name, so a
program that writes `let p: Pair = Pair(1, "a")` is told about `Pair` twice.

## Two suggestions that were not true

The line this turn came from said `let p: Pair = Pair(1, "a")` is told about
`Pair` twice. It is told once. What was wrong was what it was told:

```
error[K0302]: `Pair` takes 2 types, and none are written here
  |            ^^^^ write them: `Pair<i32>`
```

One type for a shape that takes two, which is a suggestion that does not
compile. It uses the shape's own names now — `Pair<A, B>`, `Box<T>` — which are
in the type already because a copy is made from them.

The other stale one was beside it. An unknown generic said "`ref<T>` and
`store<T>` are the two", which stopped being true when a program could declare
its own. It answers the way every other unknown name in the language does:

```
error[K0302]: unknown generic type `Boxs`
  |            ^^^^ did you mean `Boxes`?
```

and when nothing is near, it says what the two built-in ones are and how to
write one of your own.

**Runs:** `make check`, everything passing, plus four shapes of a generic
written wrongly: none of its types, one too many, a name nothing has, and a
name one letter away from one it does.
**Next:** `kest_nearest_type` walks every type the program has, including the
copies made for each set of types a generic was used with. A suggestion could
name `Pair<i32, text>` where the program only ever wrote `Pair`.

## A field that ends in a type

A copy of a generic cannot be the nearest name to anything, because its name
has `<` in it and a written name cannot: `Pair<i32, text>` is a dozen edits from
anything anybody types. It is skipped in the walk now anyway — a name nobody
wrote is not a name anybody meant.

Looking for what else a program could write and be told the wrong thing about
found this:

```kest
struct World {
    jobs: store<Job>
    count: i32
}
```

`expected end of line, found identifier`, pointing at `count`. A line may end
after `?` and after `)` and after a name, and not after `>`, so the field ran
on into the next one. Every struct holding a reference or a store with anything
written under it was refused, and `examples/quests.kest` missed it by having its
`ref<Npc>` field last.

A line ends after `>` now, recorded as D122. The price is a comparison split
after its operator, which is refused where it is written rather than read as
two statements — and which nobody writes.

**Runs:** `make check`, everything passing, with a field added under the
`ref<Npc>` in `quests.kest` and checked by what that example already returns;
plus a struct holding a store, an enum in a struct in an array, and a generic
calling a generic, all of which the same file had been holding up.
**Next:** `ends_statement` is a list the lexer keeps and a reader has to trust.
Nothing holds it to the tokens a statement can actually end with, which is how
`byte` and now `>` were missing from it.

## Every token decided

`ends_statement` is the lexer's list of what a line may end after, and twice a
token kind has been added without anybody thinking about it: `byte`, and `>` a
turn ago. Both times a line swallowed the one under it and the message was
about the line below.

It has no `default` now, recorded as D123: all sixty-eight kinds are written
out, sixteen ending a line and fifty-two not. Adding one stops the build:

```
src/lexer.c:151:5: error: enumeration value `KEST_TOK_MADEUP' not handled in switch
```

Nothing can check that a decision is right — what a statement may end with is
the grammar's business and the lexer does not have the grammar — but this
checks that one was made, which is what was missing both times.

**Runs:** `make check`, everything passing, and a copy of the tree with a
made-up token kind in it, which does not build and names the list.
**Next:** three lists in the tree now have to be complete and are held that way
by the compiler, and two more are held by `check-tables.sh` because they are
arrays rather than switches. Nothing says which lists are which.

## Which lists are which

Five lists in the tree have to name everything of their kind. Three are held by
the compiler, because a switch without a `default` stops the build when a case
is missing. The two that are arrays indexed by an enum could not be, so they
were held only by `check-tables.sh`.

They are counted while building now, as well:

```
middle: static assertion failed: "every instruction has a name and nothing else does"
end:    instructions: 122 kinds and 121 names
```

An opcode added in the middle of the enum shifts the last one and the assertion
catches it; one added at the end does not, and the tool does. Between them
nothing gets in without a name.

`CLAUDE.md` says which lists these are and what holds each of them, because
that was the part nobody could see: a reader of one switch cannot tell whether
its missing `default` is deliberate.

**Runs:** `make check`, everything passing, plus four copies of the tree — a
token kind added at the end and an opcode added in the middle and at the end —
each caught by whichever of the two nets is for it.
**Next:** `kest_op_width` reads the operand class out of the instruction table,
so an instruction with the wrong class in that table is an instruction with the
wrong width, and the walk that proves a chunk walkable uses the same table.
Both would agree about being wrong.

## A value nobody takes

The line this turn came from worried that the table which says how wide an
instruction is, and the proof that a chunk can be walked, would agree about
being wrong. They would not: the walk is anchored by what the compiler emitted,
so a wrong class in the table puts the walk out of step with the code. Proved
by putting a wrong class in a copy of the tree — `const` as two operands — and
watching K0406 catch it on an ordinary example.

What probing for other holes found is that a value nobody takes was accepted:

```
match s {
    Left -> 1
    Right -> 2
}
if s == Side.Left -> 3 else -> 4
2 + 3
```

All three worked something out and left it lying there. All three are refused
now, recorded as D124, and a call is still a call: what it gives back may be
worth ignoring, which is what `push` and `remove` and every host function are
for.

The shape that led here has its own answer as well. A function whose body ends
in a `match` that gives values was told it can end without returning; it is now
told what to do about it — the arms give a value, so it is one.

**Runs:** `make check`, everything passing, so nothing in the tree was leaving a
value lying; plus four shapes refused and two allowed, and a copy of the tree
with a wrong operand class in it caught by the walk.
**Next:** `defer` takes an expression and is checked the same way a statement
is, so `defer 2 + 3` is refused for the same reason. But a `defer` whose call
gives something back also throws it away, and that is the one place where
ignoring what came back cannot be deliberate.

## Two loads that should have been one

The line this turn came from said a deferred call that gives something back
throws it away, and that ignoring it cannot be deliberate there. It can, and it
is: `defer pop(xs)` is what `defer` is for, and the parser already refuses
anything that is not a call — `defer 2 + 3` is `a `defer` runs something, and
this is not a call`. Nothing to do.

Probing found nothing else wrong: `defer` runs on the way out of a `continue`
and a `break`, a store's generations keep a stale reference stale through a
reused slot, nested generic structs work, and the arithmetic wraps where it
says it does.

So the turn went at the number instead. `load` is thirty-seven per cent of what
a frame step runs and an operator between two names reads two slots in a row,
so those two became one instruction. The emitted code is exactly that:

```
  0052  load2       8  1
  0057  mul.f32
```

And it is twenty nanoseconds an entity-step slower. Five runs of each: 140, 142,
142, 140, 141 before, and 161, 163, 161, 161, 160 after. With the instruction
defined but not emitted the number is what it was, so it is not the extra case
in the switch — it is running it. `load` is the best-predicted branch in the
machine and `load2` runs twice among seventy-five, so it is the worst; two
mispredictions cost more than two dispatches saved.

Recorded as D125 and taken out again. It refines D091 and D092 rather than
contradicting them: fusing pays when it takes four instructions out of a turn,
and not when it takes one out of a pair of the commonest.

**Runs:** `make check`, everything passing before and after, and the measurement
either side of a change that has been taken out.
**Next:** the profile that started this said `const` is eleven per cent and
`store` ten. A store is a slot written from the top of the stack, and half of
them are a value that was just worked out and is used once.

## What a frame holds

The line this turn came from wanted the next ten per cent out of `store`. After
the last turn that is not a tweak: `store` and `load` are the two commonest
instructions, and D125 measured what happens when the commonest one is fused —
the machine got slower because a rare opcode is a mispredicted branch. What is
left in that direction is a different machine, which is a decision and not a
turn. The profile is mined.

So this turn went to the thing the profile had been hiding: nothing in the tree
passed a struct *by value* across the boundary, and nothing said how one is laid
out in a frame. Lending is documented to the byte; calling said only "laid out
the way the declaration says".

It is one slot a scalar, in the order the fields are declared, and a float is a
double in a slot even where it is an `f32` in an array. `examples/embed.c` does
it now:

```
host passed a point by value: 9
host lent 12 byte points: 11 across
```

The first writes three doubles into the frame and gets a `Point`; the second
shares the host's twelve bytes. That is D016's two layouts, one on each side of
the same example.

**Runs:** `make check`, everything passing, with the second host passing a
struct by value under both builds; and a throwaway host calling two functions
that take two structs each and give one back.
**Next:** `kest_frame_slots` says how wide a frame has to be, and a host filling
one has to work out where each argument starts inside it. Nothing says where
the second `Vec2` begins except counting the first one's scalars.

## Where the second argument starts

A frame is one slot a scalar, so a host filling one for `between(a: Point, b:
Point)` had to know a `Point` is three scalars and put the second at slot
three. The program knows that; the host was adding up fields to arrive at it.

It asks now, recorded as D126:

```
2 arguments, the second at slot 3: 3 between them
```

A function keeps how wide each of its arguments is, in the order they are
written, filled where the parameters are declared — in both places that compile
a function, the plain one and the copy a generic makes.

**Runs:** `make check`, everything passing, with the second host asking where
its second argument goes under both builds; plus a throwaway host on a function
taking two structs, which says two arguments, the second at slot two, four
together, and gives the right answer through the frame it filled that way.
**Next:** a host can ask where an argument starts and how wide the frame is,
and nothing says what an argument *is*. A host with the wrong idea of the
second one's type writes the right number of slots with the wrong things in
them.

## What an argument is

A host could ask how many arguments a function takes and where each one starts,
and had nothing to check them against. A signature that changed under it still
takes the same number of slots, and every one of them is then read as something
else.

`kest_frame_layout` says what the argument at a position is, recorded as D127.
It is the same layout a type has by name, so the check is the one the host
already wrote for lending:

```
2 arguments, the second at slot 3: 3 between them
```

and in a copy of the tree where `between` takes a `Cell` instead:

```
`between` does not take a `Point` this host knows
```

It costs nothing new. A function kept how wide each argument is; it keeps which
layout each one has instead, and the width is the layout's own — one piece a
slot.

**Runs:** `make check`, everything passing, with the second host checking its
second argument under both builds; and a copy of the tree with the signature
changed, refused before the call.
**Next:** a host can now ask what goes in, and what comes back is still only a
width. `kest_frame_slots` says how wide the result is with the arguments, and
nothing says what the result is.

## And what comes back

The last turn made what goes into a frame askable and left what comes out a
width. A host reading `frame[0].real` was deciding on its own that a float is
what the program wrote there, and a slot is a slot: a function that started
giving back an integer would be read as a float made of its bits.

`kest_frame_gives` says what comes back, recorded as D128, in the same layout
everything else uses. `examples/embed.c` asks for one piece and an `f32` before
it reads a `double`, and in a copy of the tree where `between` gives an `i32`:

```
`between` does not give back one `f32`
```

Nothing comes back for a function that gives nothing, rather than a layout of
one slot that means nothing — a shape for something that is not there is a
thing a host would check against and pass.

**Runs:** `make check`, everything passing, with the second host checking what
it passes and what it reads under both builds; and two copies of the tree, one
where the argument changed and one where the result did, each refused before
the call.
**Next:** a host can ask what a function takes and gives, and `kest_entry` still
answers by name alone. Two functions may share a name when they take different
things, and nothing says which of them an index is.

## More than one function under one name

`kest_entry` cannot hand over a name that is several functions, and it said why
like this:

```
error[K0615]: `add` is generic and is compiled once for each set of types it is used with
```

`add` is not generic. Two functions may share a name when they take different
things, which the language has had since `math.min`, and the message called
every one of them a generic. It says what is true of both now:

```
error[K0615]: `add` is more than one function here: they take different things
      ask for one of them: `add#i32,i32`, `add#f32,f32`
```

and the same sentence for a generic's copies, whose names are
`pick#T,T$i32` and `pick#T,T$f32`. What a host does about it is the same either
way, so the message does not guess which it was.

The reference says the host's half of this now, beside the language's: asking
for one of the names gives an index, and `kest_frame_layout` says what it takes,
which is how a host checks it asked for the one it meant.

**Runs:** `make check`, everything passing, plus a program with two functions
of one name asked for by a host both ways, one with a generic used at two
types, and `kest call` picking between the two overloads from the command line.
**Next:** the command line picks an overload by reading the arguments it was
given, and a host picks by writing the compiled name. Nothing lets a host say
"the one that takes these types" without knowing how the compiler spells it.

## Walking the functions of a name

`kest_entry` names the candidates when a name is several functions, and until
now that was the only way to get them: read `add#i32,i32` out of a message and
write it back in. That is a host knowing how the compiler spells a name, which
is the thing `kest_entry` exists to keep it from having to know.

`kest_entry_of(runtime, name, at)` gives the one at a position, recorded as
D129. It adds nothing of its own: a host walks the candidates and asks each
what it takes, which is what the last three turns made possible.

`examples/embed.kest` has two `lengthOf` now — one taking a `Point` and one
taking two floats — and the host picks the one it means with the same piece
comparison it uses before it lends a `Point`:

```
host passed a point by value: 9
```

Nine is the `Point` one; the other would have said five.

Asking for the second one is also how a host asks whether a name is several
functions at all, without asking for an index that is not there — and without
`kest_entry` raising a message about an ambiguity the host is about to resolve
itself.

**Runs:** `make check`, everything passing, with the second host resolving an
overloaded name under both builds; plus a throwaway host walking two functions
of one name and reading what each takes.
**Next:** the host walks candidates by index and there is no way to ask how
many there are, so the loop ends by asking for one that is not there. Every
other list in this boundary is walked the same way, which is either the shape
or four places to change.

## Numbers that look random

The line this turn came from asked whether walking a list by index until it
answers nothing is the shape or four places to change. It is the shape, and it
has one deliberate exception: where a host needs a number to size something —
how many arguments a frame takes — it is given the number. D078 argued the
rest: a count beside an accessor is two things that can disagree, and the two
places that give both read the same field, so they cannot.

So the turn went to what a program cannot write without help. Every simulation
writes a generator, and there was not one:

```kest
let source = random.from(seed)
source = random.next(source)
let face = random.below(source, 6)
```

`std.random` is that, recorded as D130. The state is a value the program holds,
because a generator behind a name nobody passes cannot say where its numbers
came from, and a simulation worth running twice has to.

`examples/chance.kest` lays out fifty spots twice from one seed and checks they
are the same, that another seed is not, that everything lands inside the field,
and that a quarter is about a quarter — loosely, because a tight check on a
chance is a check that fails somewhere else.

**Runs:** `make check`, everything passing, with the new module checked,
formatted and used by the new example under both builds and both sanitisers;
plus a throwaway program rolling six thousand dice, which came out between 969
and 1030 a face.
**Next:** `random.below` is a remainder, so the numbers below a count that does
not divide the whole range are not quite even. Nothing says so where somebody
would read it, and the module says the opposite by not mentioning it.

## What a remainder costs, and what a shuffle is worth

`random.below` is a remainder, so the numbers below a count that does not
divide the range are not exactly even. The module says so now, and says what
the difference is: about `count` in eighteen billion billion. Throwing away the
numbers that cause it makes the function a loop that might go round again,
which is a worse thing to put in a frame than a bias nothing can measure.

What the module was actually missing is what a program reaches for after a
number:

```
shuffled 4 3 7, each once: true
picked 6
empty gives nothing
```

`shuffle` rearranges in place and gives back the source it left off at, because
it used as many numbers as the array is long. `one` picks an element and gives
nothing when there are none. Both are generic, the way `std.sort` is.

`examples/chance.kest` checks the shuffle is a rearrangement — every spot still
there, once — and that a pick lands inside.

**Runs:** `make check`, everything passing, with the two new functions used by
the example under both builds; plus a throwaway program shuffling eight numbers
and picking from an empty array.
**Next:** `random.shuffle` walks an array by index and writes through it, and
`std.sort` does the same. Both are generic over what the array holds and both
were written from scratch; nothing says whether a swap is one idea or two.

## The `f32` half of the module

The line this turn came from asked whether the swap in `random.shuffle` and the
one in `std.sort` are one idea. They are the same three lines and they cannot
disagree about anything, and a function for them would put a call in the inner
loop of an insertion sort. Left as they are.

What is worth changing is next to it. `std.math` had `sqrt`, `floor` and `ceil`
for `f32` and not `sin` or `cos`, so an angle — which is an `f32` in a frame
like everything else — had to be widened by hand:

```
error[K0310]: this argument expects `f64`, found `f32`
```

Both shapes are there now, and `round` with them, recorded as D132. `round` is
`floor(value + 0.5)` and says which way a half goes: 2.5 is 3 and -2.5 is -2.

No new `extern`. Every one of those is a function every host of every program
that imports the module has to provide, so one more is every host changed;
what can be built out of what is already declared is built.

`examples/physics.kest` uses both — the sine and cosine of an `f32` angle
squaring to one, and a rounded position agreeing with the floor of the same
thing plus a half.

**Runs:** `make check`, everything passing, with the two new shapes used by an
example under both builds and the module still formatted the way `fmt` prints
it.
**Next:** `std.math` widens an `f32` to ask the host and narrows what comes
back. That is one rounding on the way out and one on the way in, and nothing
says whether the answer is the nearest `f32` to the true one or the nearest
`f32` to the `f64` the host gave.

## What two roundings come to

`std.math` widens an `f32`, asks the host in `f64`, and narrows what comes
back. Widening is exact, so there is one rounding here and one in the host, and
the module said only that a `f64` has more than twice the precision — true, and
not the whole answer.

It says the whole answer now. `sqrt` is rounded exactly by every machine that
has one and `f64` carries more than twice the digits of `f32`, so rounding
twice lands where rounding once would: the nearest `f32` to the true answer.
`floor` and `ceil` are exact and `round` is built out of `floor`. `sin` and
`cos` are not promised to be rounded exactly by anybody, so what comes back is
the nearest `f32` to the host's own answer — the same hair a program would get
by asking in `f64` itself.

Checked rather than reasoned about alone: twenty thousand values through
`math.sqrt(f32)` against the widened path, and twenty thousand through
`math.sin(f32)`, with no disagreement in either.

**Runs:** `make check`, everything passing; plus the two comparisons above, run
as a throwaway and thrown away.
**Next:** `std.table` is a hash table over a generic struct, and
`examples/inventory.kest` uses it. What that example does with it is put things
in and read them back; nothing there removes one, which is the operation a hash
table gets wrong.

## A table full of marks

`std.table` leaves a mark where a pair was taken out, because a probe that was
going further has to carry on past it. Nothing counted them, and the table only
grew when the *pairs* did, so putting things in and taking them out again fills
it with marks while it holds almost nothing:

```
after 200 cycles: count 64, slots 256, marks 113
```

Every answer was still right — which is why this is found by looking rather
than by a failure. A hundred and seventy-seven of two hundred and fifty-six
slots were spoken for, so a lookup for a key that is not there was walking most
of the table.

A mark counts as spoken for now, recorded as D133. The same numbers become 36,
and five thousand rounds leave 47. Twice the room is for the pairs and the same
room again is for the marks, and which is asked for is which of them is
crowding it. How many there are lives in an array of one, because a struct is a
value and a number written in a function would be written on the copy.

`examples/inventory.kest` puts one in and takes it out a thousand times and
checks that the two hundred that stayed are all still found.

**Runs:** `make check`, everything passing, with the example doing its thousand
rounds under both builds; plus a throwaway that fills sixty-four pairs, removes
every other one, puts them back, and cycles five thousand times, counting the
marks each time.
**Next:** `std.table` walks every slot to fix two of them when something is
removed, which is the whole table for one pair. The two positions it is looking
for are both worked out from a hash it already has.

## A removal that costs what a lookup costs

Taking a pair out of the table changed two slots and found them by walking all
of them:

```
old: 810780us to remove two thousand from four thousand
new: 1485us
```

Both are found by hashing a key now, recorded as D134, which is what the table
is for. Five hundred times faster on that shape, and the shape is an ordinary
one: a table things are taken out of.

The probe is one function. `find` wants the pair and `remove` wants the slot,
and two probes would be two places to get the walk past a mark wrong. The order
inside is the care: the moving pair's slot is found while it is still where it
was, before the emptied slot becomes a mark, because a mark is what a probe
walks past.

A measurement went wrong first and is worth writing down. The old binary was
run from this directory, and the library is found beside the compiler — which
was here, holding the new `table.kest`. So the old compiler ran the new library
and the two numbers were the same. Running it from its own tree is what made
the difference show.

**Runs:** `make check`, everything passing; a throwaway that removes in eight
different orders from a table of forty, with strides that cover every key, and
finds nothing wrong; the one where the only pair is removed; and the timing
above.
**Next:** `refill` is called when the marks crowd the table and walks every
slot to clear them, which is the one walk left. It is also the only place that
can shrink a table, and nothing does: a table that held a thousand and holds
ten keeps the room for a thousand.

## What a table keeps

`refill` is the only place that could make a table smaller, and it never does.
That is right and the reason is not the table's: nothing here frees anything
until the heap is thrown away, so smaller slots would be a new array with the
old one still sitting there. Shrinking would cost memory rather than give it
back. Recorded as D135, in the module where somebody would look for it.

Beside it, the thing that packing is *for*: a table is walked over `keys` and
`values`, which are in step, with `count(t)` of each.

```
pairs 3, sum 8
keys acd
keys and values in step: true
```

The order is what putting things in and taking them out left — `b` went and `d`
took its place — so it is an order and not one to lean on.
`examples/inventory.kest` walks the two hundred it holds and checks every pair
against what the table answers for its own key.

**Runs:** `make check`, everything passing, with the example walking a table
under both builds; plus a throwaway that removes from the middle and walks what
is left.
**Next:** `std.table` is the only module with a struct that holds handles, and
the rule that keeps it working is that everything which changes lives behind
one. `std.random` has a struct that holds a number instead, and the two say
opposite things about what a struct is for.

## Writing what you were handed

`std.table` holds everything that changes behind a handle and `std.random`
gives back a new value; the line this turn came from asked whether the two
disagree about what a struct is for. They are the two answers to one rule, and
the reference says both now, with which to pick: whether the thing has an
identity or is a number a program carries.

What neither of them is, is the shape somebody writes who expects a struct to
be a reference:

```
warning[K0346]: `c` is a value here, so this is discarded
  |     ^^^ give the changed one back, or hold what changes behind a handle
```

`bump(c)` compiled, ran, and did nothing. It is a warning rather than a refusal
because a parameter is also a place to work — `examples/physics.kest` writes
its `Body` and returns it every frame — so it only fires where the function
gives nothing back and there is nothing it could mean.

Writing through a handle reached from a parameter is not this and stays quiet,
which is what `std.table` does on every `set`.

**Runs:** `make check`, everything passing, so nothing in the tree was writing
into a copy; plus a file with the four shapes in it — one that returns the
value it wrote, one that writes through a handle, one that writes and reads its
own copy, and one that just writes.
**Next:** the warning is about a parameter, and a `let` that copies one has the
same trap: `let held = t; held.count = 1` writes the copy and nothing says so.

## The library used the way a program uses it

A `let` that copies a struct and writes its fields is not the trap the last
turn's line suggested: it is how a changed one is made. `let moved = p` and
then `moved.x = 0.0` is the idiom, and the reference says so beside the two
answers a thing with state has to choose between.

The rest of the turn was spent writing a program the way somebody would, to see
what it is like: eight ants on a field with no walls, each turned by a number
that looks random and moved by a sine and a cosine, a hundred frames. Nothing
was missing and nothing was awkward — `math.cos` on an `f32` heading is there
because of two turns ago, the source is carried the way `random` carries it,
and the frame walks an array of value structs and writes each one back.

It is `examples/ants.kest` now, which is the first example that uses five
modules at once, none of which knows about the others. It checks that
everything stayed on the field, that the same seed lays out and walks the same
field twice, and that a hundred turns left somebody pointing somewhere else.

**Runs:** `make check`, everything passing, with the new example run under both
builds and both sanitisers and held to what `fmt` prints.
**Next:** every example checks itself by returning a number, and the number
says which check failed and nothing else. A reader who gets 4 has to count the
returns to find out what it was.

## The number an example answers says where to look

An example checks itself and reports what failed by the number it answers with,
which is the smallest thing a program can say and the least helpful: a reader
who gets 4 has the number and not the check.

The number is already written down, though — it is in the file, next to the
`if` that decided it. So `check.sh` now looks it up: when an example answers
non-zero it prints the `return` that matches, with its line, and the `if` above
it when there is one directly above.

```
examples                           examples/ants.kest answered 1
    examples/ants.kest:82:     if len(ants) != ANTS {
    examples/ants.kest:83:         return 1
```

That is the whole change, and it is in the tool rather than in the examples,
because the alternative was twenty-five files printing what they were about to
answer and a rule saying they have to.

**Runs:** `make check`, everything passing, and the failing output above is
from a copy of the tree with one comparison in `ants.kest` turned round.
**Next:** `main` answers with an `i32` and a process can only say eight bits of
one, so `main.c` hands back `exit_code & 0xff`: a program that answers 256
exits 0 and looks like it passed.

## An exit status that does not fit is said rather than cut

`run` handed back `exit_code & 0xff`, which is the width a process has. A
`main` answering 256 therefore exited 0 — the one number that means nothing
went wrong — so a program that failed told the shell it had passed, and the
tools around it agreed.

The number is checked before it is handed over now. Outside 0 to 255 it is
`K0618` and the status is 1: something wrong rather than something false.

```
error[K0618]: `main` answered 300, and an exit status carries 0 to 255
      answer inside that range, and print what does not fit
```

Negatives go the same way, because -1 came out as 255. What this settles as
well is what `run --json` has to hold, which is nothing beyond the diagnostics:
the status cannot be a truncation any more, so it is the whole answer.

**Runs:** `make check`, everything passing, plus files answering 300, -1 and 4
by hand — the first two reported and the third handed straight through.
**Next:** nobody checks what `main` returns. `fn main() -> bool` compiles and
its `true` becomes a status of 1, which is failure; `fn main() -> text`
answered 108706389085004, which was the pointer.

## `main` has a shape and the checker holds it to it

Nothing checked what `main` was. `fn main() -> bool` compiled and its `true`
became a status of 1, which is failure. `fn main() -> text` answered
108706389085004, which was the pointer read as a number — the last turn's range
check caught that one by accident, which is not the same as catching it.
`fn main(x: i32)` was called with a slot nobody had written, and answered 0.

`main` is the one function nothing in the file calls, so the file cannot say
what shape it has to be. The checker says it now: it takes nothing, and it
gives `i32` or nothing.

```
error[K0347]: `main` gives `bool`, and what `main` gives is the exit status
 --> mb.kest:3:4
  |
3 | fn main() -> bool {
  |    ^^^^ give `i32`, which is a number from 0 to 255, or give nothing
```

`K0348` is one that takes something and `K0349` is one that is generic, which
would have compiled and then not been found. Only the file that was named is
held to any of it: a `main` in a file it imports is a name like any other.

**Runs:** `make check`, everything passing, plus the four shapes above by hand
and a `main` answering 4, which still answers 4.
**Next:** `tick` checks what `onEvent` takes and not what it gives, so a
handler giving `text` has its pointer added up and printed as the total. It is
the same hole `main` had, in the other entry point.

## What an event handler gives, and a tick that drove nothing

`tick` checked what `onEvent` took and not what it gave. A handler giving
`text` had its pointer read as a number, added into the total across the
crossings, and printed as a measurement.

What it gives is now the other direction of the rule that was already there: a
whole number, or nothing. Nothing is a handler somebody would write, so it is
allowed and is not nought — the line leaves the number out and `--json` says
`gave: null`, because a handler that answers nothing and one that answers
nought are two things and they printed the same before.

```
onEvent   2 crossings, peak 16 bytes
kest: `onEvents` gives `text`, and tick reads what comes back as a whole number
      give an integer, or give nothing
```

And a `tick` that drove nothing exits 1 now, whether nothing here takes events
or what does could not be called. It exited 0, which is what a run that
happened answers — the same lie the last two turns were about, at the other
entry point.

**Runs:** `make check`, everything passing, plus `examples/events.kest` driven
four ways: as it is, with a handler that gives nothing, one that gives `text`,
and a file with no handler at all.
**Next:** those two complaints go to standard error as plain lines, so
`tick --json` on a program with a handler it cannot call prints
`{"diagnostics":[],"errors":0,...}` and exits 1. They are about a declaration
in the file, which is what a diagnostic with a code and a span is for.

## What tick says about a handler is a diagnostic now

The three complaints `tick` made about a handler went to standard error as
`kest:` lines. So `tick --json` on a program with a handler it could not call
printed `{"diagnostics":[],"errors":0,"heap":0}` and exited 1: the machine form
said nothing was wrong and the status said something was.

They are about a declaration in a file, so they are diagnostics: `K0619` for
what a handler takes, `K0620` for what it gives, `K0621` for a file with
neither. The last has no span, because what is wrong with it is that there is
nothing there — the same shape as `K0603` for a missing `main`.

```
error[K0620]: `onEvents` gives `text`, and tick reads what comes back as a whole number
 --> tt.kest:3:4
  |
3 | fn onEvents(events: [i32]) -> text {
  |    ^^^^^^^^ give an integer, or give nothing
```

The name in the message is the one the file wrote. This host finds a handler by
its qualified name, because that is how names are registered, and `tt.onEvents`
is not what is on the line. The nearest name goes the same way.

**Runs:** `make check`, everything passing, plus tick over five files by hand:
a handler that gives `text`, one that takes `text`, one that takes two things,
a misspelt `onEvnt`, and `examples/events.kest` which still drives both ways.
**Next:** `tick`'s status now has two sources — the diagnostics and the
`undriven` flag from the turn before — and the flag is there for a path that
says nothing: a name the module has and the program's globals do not. Make that
path say something and the flag can go, leaving the status as what was said.

## Tick asks once, and the answer it gives is what it said

`tick` asked twice whether a handler was there: `kest_entry` for the compiled
name and `kest_lookup_global` for the declared one. Where the two disagreed it
drove nothing and said nothing, and the flag from two turns ago turned that
into a status with no reason attached.

The disagreement had a shape, and it is one somebody would write: a generic
handler. Nothing inside a file calls its own `onEvent`, so a generic one has no
copy, `kest_entry` finds nothing, and the two lookups differ. `kest tick` on it
printed the heap and exited 0.

```
error[K0622]: `onEvent` is generic, and tick has no type to make the copy from
 --> gh.kest:3:4
  |
3 | fn onEvent<T>(event: i32) -> i32 {
  |    ^^^^^^^ take `i32` and nothing else
```

It asks the declaration now — the one that can say what is wrong with what it
finds — and only then asks for something to call. So the flag is gone and the
status is what was said: 1 when there is a diagnostic, 0 when the run happened.

**Runs:** `make check`, everything passing, plus tick over the six shapes by
hand — generic, gives `text`, takes `text`, takes two, misspelt, and none —
each of which now says one thing and answers 1, and `events.kest` which says
nothing and answers 0.
**Next:** three turns of entry points is enough. Back to the language: the
`no.alloc` proof is over the call graph, and nothing has asked what it does
with a call through a `ref` to a function — the reference does not say whether
that exists.

## A name for a function is that function

The turn's question was what the cost proof does with a call through a value.
The answer is D039's: the promise is in the type, a value that promises fits
where one that does not is wanted, and a body that is not known is read off the
shape. That holds, and `examples/shapes.kest` and `std.sort` use it.

A name bound to one was the case D039 did not cover. `let f = quiet` gives the
local the type of `quiet`, symbol and all, and the proof follows that symbol
into that body — so this compiled, ran, and allocated inside a promise:

```kest
fn careful(n: i32) -> i32 no.alloc {
    let f = quiet
    if n > 3 {
        f = grows
    }
    return f(n)
}
```

A name for a function is that function now, and assigning to it is `K0350`. A
variable that holds any function of a shape is written with the shape on the
`let`, where the symbol is not part of the type and the promise is read off the
shape, which is the thing D039 built. Dropping the symbol at the assignment
instead would not have been sound: a body is walked once and a loop assigns
after it reads.

`examples/shapes.kest` shows both, since it already had the parameter case.

**Runs:** `make check`, everything passing with the example's two new checks,
plus the seven files this turn was written against by hand: a function bound
and called, one held in a struct field, one behind a `ref`, one reassigned
under an `if`, one given a promising shape it does not keep, one pushed into an
array of them, and one passed as a parameter.
**Next:** the prover says "this allocates" at a call through a value whose
promise is not known. It does not allocate — nothing is known about it, and a
message that names the wrong reason is the one thing worse than none.

## A call nobody promises about is not a call that allocates

To a fixed point that has to be conservative, unknown and heap-reaching are the
same thing, so the prover marked a call through a value with no promise as an
allocation. To a reader they are not the same thing at all: `K0401` pointed at
`f(n)` and said "this allocates", which is a claim about a body nobody has
seen, and the fix it implies is to go and find the allocation.

The fix is somewhere else, so the message is now somewhere else too:

```
error[K0402]: nothing promises about what this calls, and `apply` promises `no.alloc`
 --> nb.kest:14:12
   |
14 |     return f(n)
   |            ^^^^ write the promise into the shape: `fn(i32) -> i32 no.alloc`
```

The shape printed is the one the value is written as, with the promise on the
end of it, so what to type is in the message. The reason travels with the
trace: a promise broken three calls down still names what is wrong where it is
wrong rather than at the top of the path.

**Runs:** `make check`, everything passing, plus the two shapes by hand — a
promise broken at the call itself and one broken through a function that takes
the value.
**Next:** `no.alloc` is proved twice, against the tree and against what was
emitted, and the second proof cannot see through `call.value`: which chunk it
enters is not known until it runs. The machine knows, and a chunk carries what
it promised, so that is one branch at the one instruction that needs it.

## The machine checks the promise at the call the proof cannot follow

`no.alloc` is proved twice, and the second proof walks the emitted code
following `call`. It stops at `call.value`: which chunk that enters is a number
on the stack, and the number is not there until it runs.

It is there while it runs, and a compiled function carries what it promised. So
both are in hand at the instruction — the frame's chunk and the one it is about
to enter — and a body that does not promise, entered from one that does, is
`K0623` and a fault in the compiler, said the same way `K0405` says it.

```
error[K0623]: `careful` promises `no.alloc` and this enters `grows`, which does not
 --> handed.kest:8:12
  |
8 |     return f(n)
  |            ^ the shape it was held in promises and the body does not, which is a fault in the compiler
```

The cost is one branch on the one instruction that needs it, and nothing on a
call to a named function. `check-backstops.sh` has a sixth hole now: with the
variance in `kest_type_equal` turned off, an allocating function is handed to a
promising shape, and the machine has to catch what the checker stopped
catching. It does.

**Runs:** `make check`, everything passing, including the new hole; and the
message above is from that broken tree, built by hand to read what it says.
**Next:** three copies of the same six lines that turn `name#params` into the
name somebody wrote — one in `value.c`, one in `vm.c`, one gone now. The two
that are left are in different modules and do the same thing.

## One place that turns a compiled name into a written one

`sort#i32` is a name this project makes and nobody writes, so everything said
to a person cuts it off at the hash. That was six lines in `value.c` and the
same six in `vm.c`, and a third copy went in with `K0623` last turn before it
was folded into the second.

It is `kest_name_written` in `value` now, which is where the module and its
chunks live, and `vm` calls it. Nothing about the output changed; what changed
is that the rule about the hash is written once.

**Runs:** `make check`, everything passing — including `check-dead.sh`, which
is what says the new declaration is called from outside the file it lives in,
and the backstops, which are what say `K0623` still reads the same way.
**Next:** `store<Node>()` is what somebody who has met another language writes,
and the parser reads it as two comparisons and a `(`, then says "expected an
expression, found `)`". The answer here is `let nodes: store<Node> = store()`
and nothing says so.

## A type written at a call is recognised, and still refused

`store<Node>()` is what somebody who has met another language writes. The
parser read it as two comparisons, ran out of expression at the `)`, and said
"expected an expression, found `)`" — two tokens past the mistake, with a
reason that is not the reason.

```
error[K0211]: `store` is not given its types where it is called
 --> nodes.kest:8:17
  |
8 |     let nodes = store<Node>()
  |                 ^^^^^^^^^^^ write `store()`, and the type on the binding it goes to
```

It is recognised rather than parsed: a name and a `<` written against each
other, then nothing but what a type is made of, then a `>` with a `(` after it.
A comparison has a space in it or something that is not part of a type, and
`a < b > (c)` is what it always was. Which half of the rule the message gives
depends on whether anything is passed — a call with arguments is told the copy
comes from them, and one without is told to write the type on the binding —
because naming the wrong one of the two would be worse than naming neither.

Nothing about this accepts the syntax. The file is refused either way; what
changed is that the reader is told where and what.

**Runs:** `make check`, everything passing, plus `store<Node>()`,
`array<i32>()` and `sort<i32>(xs)` by hand, and a file of comparisons —
`a < b`, `(a < b) == (b < c)` — which still mean what they meant.
**Next:** `check-fmt.sh` holds the formatter to printing what parses and means
the same. Nothing holds it to what it does with a file it cannot parse, and
`fmt` on a file with a `K0211` in it is a thing somebody will do.

## What the formatter refuses to do, held to

`fmt` on a file it cannot parse already does the right thing in all four
shapes: it prints nothing, does not write, says why, and answers 1. `--check`
lists the name, because a file that is not a program is not in the one form,
and `--json` says `formed: false` with the diagnostics beside it.

So nothing needed fixing, and nothing held any of it. `check-fmt.sh` has a
fourth property now: a file that does not parse goes to `fmt -w` and has to
come back byte for byte, with a non-zero status.

To see that the check is not decoration, `check-backstops.sh` has a seventh
hole: let `read` be true after a parse with errors and the formatter writes
what recovered, so

```
fn main() -> i32 {
    let n = (1 +
    return n
}
```

comes back with the `let` line gone — somebody's work deleted by the tool that
was meant to tidy it. The check catches that.

**Runs:** `make check`, everything passing; the hole is run by it and the
deletion above is from that broken tree, built by hand to read what it did.
**Next:** `fmt --check` prints the name of a file that does not parse in the
same list as a file that is merely not formatted, and `--json` gives both
`formed: false`. One of the two is fixed by running `fmt -w` and the other is
not, and a tool reading that list cannot tell which.

## Not in the one form and not a program are two answers

`fmt --check` printed the name of a file that does not parse in the same list
as one that is merely untidy, and `--json` gave both `formed: false`. Running
`-w` fixes one of them and does nothing to the other, and nothing reading
either answer could tell which was which.

The list `--check` prints is the files `-w` would rewrite now, which is what
makes it worth acting on. A file that did not parse is on the standard error
with the diagnostics saying what is wrong with it, where it already was, and
the status is 1 either way — so nothing is lost by moving it, and the two
channels mean two things.

In JSON `formed` is null for that file rather than false, the same as `gave`
for a handler that answers nothing: whether a program is in the one form is not
a question about a file that is not a program.

```
{"diagnostics":[...],"errors":1,"file":"bad.kest","formed":null}
{"diagnostics":[],"errors":0,"file":"messy.kest","formed":false}
{"diagnostics":[],"errors":0,"file":"scan.kest","formed":true}
```

**Runs:** `make check`, everything passing, and the three files above by hand
in both forms.
**Next:** `kest_write_real` says it writes the shortest spelling that reads
back as the same number, and it tries 6 digits then 9, so an `f32` needing 8
prints 9: `1.0 / 3.0` comes out `0.333333343` where `0.33333334` reads back as
the same number.

## Shortest is counted in characters

`kest_write_real` says it writes the shortest spelling that reads back as the
same number, and it tried six digits and then nine. So an `f32` needing eight
got nine: `1.0 / 3.0` printed `0.333333343` where `0.33333334` reads back as
the same number.

It counts up from one digit now. Most numbers a program prints are short, so
counting up is usually where the answer is as well.

The part that is not obvious is that shortest has to be counted in characters:
`%g` reaches for an exponent when the digits it is given run out, so
`123456792` at eight digits is `1.2345679e+08` — fewer digits and more to read.
Counting digits alone would have printed that. Once a spelling without an
exponent reads back, nothing wider can be shorter, so that is where the search
stops.

`examples/physics.kest` checks three of them now, since a number in a log line
is what this is for.

**Runs:** `make check`, everything passing; `1e-09`, `inf`, `-3.25`,
`16777216.0`, `0.3333333333333333` and `3.0517578e-05` by hand at both widths;
and `make time`, 160 ns per entity per step, which is where it was.
**Next:** `math.kest` is an example that checks nothing — its `main` gives
nothing back and prints what two functions worked out. Every other example
answers with which check failed.

## The last example that checked nothing

`examples/math.kest` printed what two functions worked out and looked at none
of it, so the only thing it could catch was a crash. It checks itself now, the
way the other twenty-four do: `factorial(0)` is one because the loop does not
run, `gcd(9, 0)` is nine because that is the end the loop stops at, and
`classify` is asked for all three of its answers.

It was also the only `main` that gave nothing back, which is a shape the
language has and now no example is written in. So `check.sh` runs one of its
own — four lines of program, and the line that says what the examples did says
it ran.

CLAUDE.md says both of these under `examples/` now: an example answers with
which check failed, and the shape no example is written in is covered
elsewhere on purpose.

**Runs:** `make check`, everything passing, with the example's seven checks and
the answer it prints unchanged.
**Next:** `check.sh` says "25 ran, 9 resolved" and the nine are the files with
no `main`, which is every file in `lib/std`. Nothing runs a line of the
standard library except an example that happens to use it, and `std.text`'s
`join` is the only part of it any example asks about.

## The library run rather than only compiled

Nine files resolve and never run, and they are all of `lib/std`. What is run of
the library is whatever an example happened to reach on its way somewhere else,
and a list of what nothing reached comes out longer than it should: `clamp`,
`ceil`, `sign` and `lerp` in `std.math`, and `sub`, `dot`, `direction`, `lerp`
and `perpendicular` in `std.vec`.

Those are one program, as it turns out. `examples/camera.kest` follows
something around a room: the difference between two places, the direction of
it, how far along to ease, which side of where it faces something is, how many
frames it takes to arrive, and every answer held inside the walls. Nothing in
it is new language; what is new is that the library is run.

Seventeen checks, and the two that are worth reading are that easing arrives
rather than overshooting, and that where a camera already is points nowhere —
`direction` of nothing is `none` and not a zero handed back quietly.

What no example names yet: `io.write`, `math.pow`, `random.between`,
`random.number`, `text.contains`, `starts`, `ends`, `lower`, and `vec.cross`.
Some of the rest of that list was reached from inside the library after all —
`sqrt` through `length`, `slotOf`, `place` and `refill` through `set` and
`remove`, `isSpace` and `bytes` through `trim` and `upper` — which is running
them, and a list that counts only what an example spells is a list that lies
about which ones those are.

**Runs:** `make check`, everything passing: 26 examples now, and the new one
under both builds and both sanitisers and held to what `fmt` prints.
**Next:** the text half of that list. `contains`, `starts`, `ends` and `lower`
are what a program reading a line asks, and `examples/words.kest` is where a
program reading lines already is.

## The three questions a program asks about a line

`contains`, `starts`, `ends` and `lower` were the text half of what nothing
ran. They are what a program asks about a line somebody else wrote, and
`examples/words.kest` is a program that reads words, so they went there.

Eight checks, and a `beginning` that does what a completion box does: the words
that start with what has been typed, in whatever case it was typed. It builds a
lowered piece of text to compare, so it does not promise `no.alloc` and does
not pretend to — the promise saying no is the promise working.

The two worth writing down: a suffix longer than the word it is looked for in
is not there, which is an answer and not a read past the end; and a prefix of
nothing matches everything, which is what a completion box does before anybody
has typed.

**Runs:** `make check`, everything passing, 26 examples with the eight new
checks under both builds and both sanitisers.
**Next:** what is left that nothing runs is `vec.cross`, `math.pow`,
`random.between`, `random.number` and `io.write`. The first belongs where three
dimensions already are, which is `examples/physics.kest`, and a normal to two
edges is what a cross product is for.

## A normal to two edges

`vec.cross` was the last of the vector library nothing ran, and three
dimensions are already in `examples/physics.kest`, so that is where it went: a
face out of three points, the direction standing off both its edges, and
whether a body is moving into that face or away from it — which is the one
thing a bounce has to know before it is a bounce.

Seven checks. The triangle wound one way faces up and wound the other way
faces down, three points in a line are not a face at all — `direction` of
nothing is nothing rather than a zero handed back — and a body falling onto a
floor is moving into it and not into the ceiling.

It also runs the `Vec3` half of `sub`, `dot` and `direction`. A copy is
compiled per type, so the `Vec2` ones the camera runs and these are different
bodies, and only one of the two was being run.

**Runs:** `make check`, everything passing, 26 examples with the seven new
checks under both builds and both sanitisers.
**Next:** `math.pow` takes `f64` and there is no `f32` of it, while `sqrt`,
`floor`, `ceil`, `sin`, `cos` and `round` all have both. A frame works in `f32`,
so `math.pow(speed, 2.0)` is `K0310` and the fix somebody writes is two
conversions around a call.

## `pow` in the width a frame is in

`sqrt`, `floor`, `ceil`, `sin`, `cos` and `round` each have an `f32` beside
their `f64`, because a frame works in `f32` and widening one by hand at every
call is the module not doing its half. `pow` did not, so `math.pow(speed, 2.0)`
was `K0310` and what somebody writes next is two conversions around a call.

It has one now, written the way the others are: through the `f64` one and back,
which is one rounding here and one in the host. No new `extern` — every one of
those is a function every host of every program that imports this module has to
provide, and this is not a thing a host has to be changed for.

`examples/physics.kest` runs it, because a power of the step is what damping
that does not depend on the frame rate is made of: multiplying by a fraction
every frame halves a speed twice as fast at sixty frames as at thirty, and a
fraction raised to `dt` does not. The check is that one second and two halves
of one come to the same speed.

**Runs:** `make check`, everything passing, with the three new checks; and
`math.pow(2.0, 3.0)` on `f32` by hand, which is 8.0 and was `K0310` this
morning.
**Next:** what is left that nothing runs is `random.between`, `random.number`
and `io.write`. The first two belong in `examples/chance.kest`, which is where
the source already is, and `io.write` is a line said in pieces.

## The last three, and the library is run

`random.between`, `random.number` and `io.write` were what nothing ran.

A band is what a placer asks for when the edges belong to somebody else, so
`examples/chance.kest` places its spots in one: nothing outside it, the same
seed laying out the same one, and a band with nowhere in it giving where it
starts, because there is no number below one of those.

`random.number` is the one number everything else in that module is cut from,
so the check is that it agrees with what is cut from it: `below(s, 100)` and
`number(s) % 100` are the same answer.

`io.write` is a line said in pieces. The example draws a row of the field a
cell at a time — nothing is built to hold the line, and the end of it is said
at the end, which is the difference between the two functions `std.io` has.

That is every function in `lib/std` run by something now. Seven of them are
named by no example and reached from inside the library anyway: `sqrt` through
`length`, `find`, `slotOf`, `place` and `refill` through `get`, `set` and
`remove`, and `bytes` and `isSpace` through `upper` and `trim`. A count of what
an example spells would call those unrun, and they are not.

**Runs:** `make check`, everything passing, with the five new checks and the
row the example now draws.
**Next:** back to the language. `store<Node>()` is recognised at a call, and
the other half of that mistake is a type name where a value is wanted —
`Pair(1, "a")` on a generic struct, which the reference says is refused and
says what to write. Nothing has read that message in a while.

## What to write, written one way

The reference says a type name where a value is wanted says so and says what to
write instead. Two of those messages were read, and both were off.

`K0302` printed the module in front of a name the file had written without one:
`p2.Pair` about a line that says `Pair`. And `K0344` suggested
`let b: Pair<i32> = Pair(7)` for a shape that takes two types — a suggestion
the compiler refuses, which is the one thing a suggestion may not be. The code
beside `K0302` already said why in a comment; the message written later did not
know about it.

Both go through `kest_type_shape` now. The name as the file would write it, and
its own names for the types it takes:

```
error[K0302]: `Pair` takes 2 types, and none are written here
  |            ^^^^ write them: `Pair<A, B>`
error[K0344]: `Pair` is a type, and this wants a value
  |            ^^^^ a generic takes its types from where it is going: `let b: Pair<A, B> = Pair(...)`
```

A type from another module keeps its module, because that is what the file
writes: `table.Table<K, V>`.

**Runs:** `make check`, everything passing, plus the four shapes by hand — a
generic named as a value, a generic annotated without its types, a plain
struct in both of those places, and an imported generic.
**Next:** the measured loop compiles a comparison and the jump that reads it as
two instructions, nine times out of fourteen: `lt.i` then `jump.false`. That is
a dispatch each time, and D091 says removing dispatches is what pays.

## The comparison and the jump that reads it

Nine of the fourteen comparisons in the measured frame are immediately followed
by the jump that reads them, and that jump only ever reads what the comparison
had just written. So `lt.i` and `jump.false` are one instruction now, along
with the five other ways to compare two whole numbers.

```
  0019  load            4
  0022  load            3
  0025  jump.false.lt.i 216  -> 244
```

The compiler fuses them where it emits the jump, not by looking for pairs
afterwards: a comparison is one byte with nothing after it, so it is the last
instruction when it is the last byte, and taking it back at that moment means
nothing has been written that could point at the byte being taken away.

Six paired runs, alternating, after three of each to warm up: 150, 147, 147
nanoseconds an entity-step with it against 153, 150, 160 without. Every pair
went the same way. D125 fused the commonest instruction on the same reasoning
and made the machine slower, so the number is why this one stays, not the
reasoning.

The disassembly column is four wider, because the longest name is longer now.

**Runs:** `make check`, everything passing — 127 instructions in step with
their names, every example run under both builds and both sanitisers, and the
walkability invariant is what says the shorter code still steps exactly onto
its end.
**Next:** the same pair with `not` in the middle. `lt.f not` and `eq.i not`
are three of the fourteen, and `not` is a dispatch that reads what the
comparison just wrote and writes it back.

## The jump asks the question it is given

Every `not` in the measured frame is followed by the jump that reads it, and
all three are `||`: asking whether the first of two things is true was written
as turning the answer round and then looking at it.

`jump.true` asks it directly. The compiler makes it where it makes the fused
comparisons — the jump takes the `not` before it back — so `!x` in a condition
now costs what `x` costs. There is no `not` left in the instrument.

Six paired runs, alternating: 148, 147, 147 nanoseconds an entity-step with it
against 149, 149, 148 without, plus two warm-up pairs that went the same way.
One nanosecond is at the edge of what this measurement can resolve, and the
frame has two of these an entity-step, so that is the size the change is.

`not` is still an instruction and still emitted, because an answer that is a
value rather than a branch still has to be turned round: `let flag = !(a > 2)`.

**Runs:** `make check`, everything passing, 128 instructions in step with their
names.
**Next:** with `jump.true` there, `a || b` over whole numbers is `eq.i` and
then a jump again — the pair the last turn fused, in the other direction. Six
more opcodes would fuse it, and D125 is the reason to measure before believing
that.

## Every comparison a jump reads

Two turns fused what a jump reads, and each left a half: whole numbers only,
and one of the two questions a jump can ask. The halves that did not meet were
the ones the measured frame still spends its time on — `x < 0.0 || x > 100.0`
is a float comparison read by a jump asking whether something is true, twice
an entity-step.

There are twenty-four fused instructions now: six comparisons, two kinds of
number, two questions. The compiler walks back at most twice where it emits a
jump — the `not` first and the comparison under it after — so

```kest
if !(a < b) {
```

is `jump.true.lt.i` and nothing else. Three instructions became one.

Four paired runs, alternating, after two warm-up pairs: 133, 131, 134, 138
nanoseconds an entity-step with them against 146, 147, 153, 148 without, and
four more with the order turned round: 138, 141, 139, 135 against 153, 156,
158, 158. The absolute number drifts over ten minutes of measuring and the gap
does not, which is what pairing is for. About
a tenth, which is more than two dispatches an entity-step should buy. Nothing
else in the disassembly of the hot function changed — one `lt.f` became one
`jump.true.lt.f`, twice — so that is what was paid for, and why it is worth
that much is not something one measurement can say.

Walking back needs the last two instructions and not the last byte: the byte
before a `not` is an operand as often as an opcode, and reading it as an opcode
would fuse something that was never there. The compiler keeps both.

**Runs:** `make check`, everything passing, 146 instructions in step with their
names, and the walkability invariant on every chunk is what says the shorter
code still lands exactly on its end.
**Next:** `gt.f` is still on its own in the frame, because the second half of
`a || b` is a value rather than a branch: it is what the whole expression
answers. A jump reads it two instructions later, with a `jump` in between.

## A condition is compiled for where it goes

`a || b` was compiled the way any expression is: work the answer out and leave
it on the stack. In a condition that answer is read once, by the jump under it,
and thrown away. So an `||` cost a `true` pushed, a jump over it, and a
comparison the last turn could not fold because no jump followed it.

Conditions are compiled for where they go now: the halves of `&&`, `||` and `!`
are jumps, what falls through is one answer and what jumps is the other, and
nothing is built. In the measured frame

```
  0079  const           1  ; 0
  0082  jump.true.lt.f  10  -> 95
  0085  load            11
  0088  const           2  ; 100
  0091  jump.false.gt.f 6  -> 100
```

is the whole of `x < 0.0 || x > 100.0`: two instructions where there were six,
and no `true`, no `jump`, no bare comparison left in the function at all.

The way out of a condition is a list rather than one place, since each half
leaves by its own jump. Sixteen is the room; a condition with more `&&` and
`||` in it is compiled as a value, which is what everything did before. `if let`
and `while let` go that way as well — what their condition leaves is the value
they bind, not an answer.

Seven paired runs in both orders, after two warm-up pairs: 125, 128, 128, 125,
124 nanoseconds an entity-step with it against 134, 134, 135, 134, 135 without.
About a fifteenth, on top of the tenth from the turn before.

**Runs:** `make check`, everything passing, and 26 examples are what says the
short circuit still short circuits: every one of them is full of `&&` and `||`.
**Next:** `mul.f32` is followed by `add.f32` twice in the hot function, which
is what integrating a position is. One instruction that multiplies and adds
would be one dispatch instead of two — as two operations and not as a fused
multiply-add, which rounds once and would be a different answer.

## Multiply and add in one instruction, and out again

`p + v * dt` is what a frame integrates with, and the multiply is only ever
read by the add above it. `mul.add.f32` was written — two operations and two
roundings in one instruction, not a fused multiply-add, which rounds once and
would be a different answer — and the answers came out the same to the last
digit, which was the first thing to check.

Then it was measured. Nine paired runs in both orders came to about half a
nanosecond an entity-step against a spread of five, and four of the nine went
the wrong way: 124, 125, 128, 131, 127, 134, 124, 126, 131 with it against 126,
125, 130, 130, 124, 131, 125, 131, 133 without. The two turns before this had
every pair going the same way with eight and thirteen nanoseconds in them, so
there is something to compare it against, and this is what nothing looks like.

So it is not here. The tree is what it was, and what is written down is that
this was tried.

**Runs:** `make check`, everything passing, both with the instruction and after
taking it out; `make time` seventeen times between the two trees.
**Next:** the hot function is fifteen `load`s and six `store`s out of
thirty-odd instructions. D125 says fusing two `load`s made it slower, and the
`store` `load` pairs in it are a different shape: five of them, and some are
the same slot written and then read straight back.

## An instruction is not free before it runs

A slot written and read straight back is the commonest pair this language
emits: two hundred and fifty-nine of them in the examples and the library.
`store.keep` folded two hundred and twenty-six into one instruction each — the
thirty-three left are pairs something jumps between, which the compiler knows
because it now keeps where the furthest landing is.

The frame got six per cent slower. Seven paired runs both ways: 137, 134, 131,
130, 131 nanoseconds an entity-step with it against 129, 124, 124, 123, 124.

That made no sense: the one pair it folds in the measured function runs once a
call, not once an entity. So the instruction was left defined, its case left in
the machine, and the compiler stopped from emitting it — 133, 134, 134 against
125, 125, 125. And then moved to the end of the enum so nothing a frame uses
was renumbered — 135, 135, 136 against 127, 126, 128.

It is the case being there. Not the instruction running, and not where it sits.
D125 ran the same test and found the opposite, and the difference is the size:
the switch has a hundred and forty-six cases now, and the nineteen added over
the last two turns made the frame a fifth faster. So the price is not a case,
it is where this build's dispatch lands when there is one more of them.

The instruction is not here. What is written down is that the instruction set
costs something before any of it runs.

**Runs:** `make check`, everything passing with the instruction and after
taking it out; `make time` twenty-six times over four trees.
**Next:** the dispatch is a `switch` in a loop, so what it compiles to is the
compiler's to choose and it just changed its mind over one case. A label per
instruction and a jump through a table of them is the other way, and it is the
one that does not depend on a hundred and forty-seventh case.

## The dispatch stays a switch, and the number says how much to believe it

The turn's line was to try a label per instruction and a jump through a table
of them. It is not being done, and the reasons are worth the turn.

It is not C11: `&&label` and `goto *` are a GNU extension, so the machine would
be written twice or the language would stop being portable C. And the
conversion is not mechanical — of a hundred and forty-six cases, thirteen hold
a loop or a switch of their own, whose `break` belongs to that and not to the
case, so a script that rewrites `break` into a dispatch changes what those
thirteen do without saying so.

What could be measured was measured: `-fno-crossjumping -fno-gcse`, the flags
an interpreter asks for so that a compiler does not merge the copies of its
dispatch back into one. Seven paired runs, five worse with them and none better
than the noise.

And the noise is the thing this turn leaves behind. The same binary measured
124 nanoseconds an entity-step in the morning and 171 in the afternoon, and the
number said nothing about which to believe. `make time` prints the spread
between its best round and its worst now:

```
130 ns per entity per step, best of 7 over 10000, spread 11%
```

At nine per cent it is a number. At nineteen the machine is still deciding how
fast it wants to run, and so is the answer.

**Runs:** `make check`, everything passing; `make time` fourteen times across
three trees, which is what the spread is for.
**Next:** back to the language. `defer` is in one example and four lines of the
reference, and what it says there is that it runs on the way out through a
`return` and at the end of every turn of a loop. One of those two is checked by
`examples/host.kest` and the other is not.

## What defer does, looked at

The line said one half of `defer` was checked and the other was not. Reading it
again, both halves are in `examples/host.kest` — a function with three ways out
and a loop whose turns each defer something — and that example does run under
`make check`, because the command line binds `Host.write` and `Host.sqrt` like
any other host.

What it does not do is look. The brackets it prints, `[ok][][ok]` and
`<.,.,.,>`, are read by a person or by nobody; what the checks compare are the
numbers the functions gave back, which come out the same whether or not
anything was deferred.

So `examples/borrow.kest` looks. Slots taken from a pool and given back on
every way out, which is what `defer` is for: three ways out of one function and
the pool full again after each, a turn of a loop that breaks with its slot
given back, a `return` from inside a loop that gives back the turn's and then
the function's, and five turns through a pool of four, which only works because
each turn gives its slot back at the end of it.

Two of them written down rather than given back say the order out loud: what
was taken last is first in the log.

The one thing the example could not do is promise `no.alloc` while writing that
log, because a deferred call counts against a promise like any other. The
prover said so, which is the rule the reference states, working.

**Runs:** `make check`, everything passing, 27 examples now.
**Next:** `array(4, true)` fills an array with a value, and `array()` builds an
empty one. Nothing in the reference says what `array(n, v)` does with `n` below
nought, and nothing in the examples asks.

## A count written where it can be read

`array(n, v)` refuses a count below nought while it runs — `K0604`, "an array
cannot have -1 elements" — which is right when `n` came from somewhere. When it
is written in the line, or is a constant, or is arithmetic on them, the
compiler already works it out for other reasons and said nothing about it.

It says it now, in the same words and where the count is:

```
error[K0351]: an array cannot have -2 elements
 --> pool.kest:4:19
  |
4 |     let d = array(-2, 5)
  |                   ^^ a count is nought or more, and nought is an array with nothing in it
```

The machine still refuses the ones it cannot know, which is the half that was
already right. The reference says both halves now, because saying only one of
them is how somebody learns the rule from a crash.

**Runs:** `make check`, everything passing, plus a written `-2`, a constant
that works out to `-3`, and a count from a parameter, which is still the
machine's to catch.
**Next:** `slice(t, 0, -1)` is the same mistake in the same shape — a count
written in the line, refused while running with `K0604` and not before. So is
an index: `a[-1]` on an array whose length is right there.

## Places written down, read where they are written

The last turn read a written array count. The same shape is in two more places:
`slice(t, 0, -1)` and `a[-1]`, both refused while running and not before.

They are read now. A count below nought is `K0351` and a place below nought is
`K0352` — how many and where are two rules, so they are two codes:

```
error[K0352]: an index is nought or more, and -1 is not
error[K0351]: a piece of text cannot be -1 bytes long
error[K0352]: text is read from nought, and -2 is before it
```

All three go through one function that asks whether the compiler can work the
number out, which is what `[T; N]` already counts with. That turned out to
widen something else: an index into a `[T; N]` was measured against `N` by
reading the digits of a literal, so `run[4]` was caught and `run[AT]` with
`const AT: i32 = 4` was not. Both are now.

Nothing came out of the machine. A count that arrives from a parameter is still
its to catch, and the two say the same thing in the same words.

**Runs:** `make check`, everything passing, plus six files by hand: a written
index and a written slice at both ends, a constant into each, and a constant
index into a `[T; N]` that is one too far.
**Next:** `pop(a)` gives a `T?` because an empty array has none to give, and
`remove(a, i)` does not ask, because naming a position is a claim there is one
there. A written `remove(a, -1)` is now refused; a written `remove(a, 3)` on an
array of three is the same claim and nothing reads it.

## Every place, not just the two that were looked at

The last turn read a written index and a written slice. The line said
`remove(a, -1)` was already refused; it was not. Neither was `rest(t, -1)`,
nor the `at` of `matches`, nor an index into a piece of text — that last one
because the check sat below the line that answers text and returns.

They all go through one function now, and it is the only place either message
is written:

```
error[K0352]: an index is nought or more, and -1 is not
error[K0352]: text is read from nought, and -1 is before it
```

An index into text, an array or a `[T; N]`; the `at` of `matches` and `rest`;
where `find` starts looking; the position `remove` takes out; where `slice`
starts and how many bytes it takes. Seven places, two messages, one function
that says which of the two a reader is looking at.

**Runs:** `make check`, everything passing, plus seven refusals by hand and one
program that indexes, removes, rests and slices with numbers that are fine and
still answers what it answered.
**Next:** `written_number` folds a constant to check it. `kest_fold_const` is
the compiler's, and it is being asked questions by the checker now — twice per
index in the worst case. Whether that is worth anything is not known, because
nothing measures how long a build takes.

## Asked once: there is still one measurement

The folding that reads written numbers is on the path of every index now, and
nothing here says how long a build takes. Rather than adding a second
measurement to find out, it was asked once: checking every file in `examples`
and `lib` five times over, with the folding and without, came to 219, 221, 215
milliseconds against 221, 217, 225. A hundred and eighty files each way, and no
difference.

So there is still one measurement. A number nobody would act on is a number
nobody should keep true, and a build of a millisecond a file is not one anybody
will notice getting worse. The sentence in CLAUDE.md stays as it is.

What the asking did find: an index into a `[T; N]` worked its number out twice,
once to see whether it was below nought and once to measure it against how many
there are. `written_place` hands the number back now, and the second fold is
gone.

**Runs:** `make check`, everything passing; the four refusals still read the
same; and `kest check` over every file three hundred and sixty times between
two trees, which is the measurement that is not being kept.
**Next:** `pop(a)` gives a `T?` and `remove(a, i)` gives a `T`, and the
difference is that naming a position is a claim there is one. An array of a
known length is the one case where that claim can be read: `let a = array(3,
0)` and then `remove(a, 3)` on the next line.

## What the type knows, and what it does not

The line asked whether `remove(a, 3)` could be read where it is written when
`a` was made three long a line earlier. It cannot, and the reason is worth
writing down: following what happens to `a` between the two lines is flow
analysis, and what it buys is a rule that holds sometimes — refused here,
allowed with a `push` in between, allowed again when the `push` is behind an
`if`. The refusals of this language should not depend on how hard the compiler
looked.

What the type does know, it now stops paying for. `len` of a `[T; N]` was
answered from the type — and the run was loaded onto the stack first and thrown
away, because the answer is worked out after the argument is. A `[f32; 256]`
counted that way copied two hundred and fifty-six slots to say 256.

```
  0014  load.n          0  3        gone
  0019  pop.n           3           gone
  0022  const           2  ; 3
```

A name is not loaded to be counted now. Anything else still is: `len(make())`
calls `make`, because a call is the point of the line as often as it is not,
and a file that declares its own `len` still gets that one.

**Runs:** `make check`, everything passing, plus a file with its own `len` over
a `[T; N]`, one counting a call, and one counting a name, which answer 99, 3
and 3.
**Next:** `for x in run` over a `[T; N]` copies the whole run into a slot
nobody can name before walking it. For a name that is already in a slot, the
copy is what `len` was doing.

## A function value is called from wherever it is

Two premises died on inspection this turn. A walk over a `[T; N]` copies the
run on purpose — D053 decided it, `examples/inline.kest` checks it, and the
copy is the semantics rather than waste. And the runs anything in this tree
walks are four slots long, so the copy nobody can remove is not worth removing.

What the looking found instead: a function in a field could be called and one
in an array could not.

```kest
let rules: [fn(text) -> bool no.alloc] = array()
push(rules, long)
rules[0]("herald")     // only a named function can be called so far
```

That is not a rule anybody wrote down; it is where the code that looks up names
stopped. The instruction was already there — `call.value` takes which function
it is off the stack — so what puts it there can be an index, a field, a name,
or anything else that gives a function. Out of an array, out of a struct, out
of a store, and out of a `let`: all four run now, and the third of them is in
`examples/shapes.kest`.

The refusal that is left says what it means rather than promising: a call whose
callee is not a function.

**Runs:** `make check`, everything passing, with two new checks in
`examples/shapes.kest`; and a file by hand that calls one out of an array, a
field, a local and a store, the last through `if let` because a store gives
back what may not be there.
**Next:** a function value in a store is reached through `get`, which gives
`T?`, so calling it is two lines. That is the same shape as every other
optional and reads the same way; what is worth knowing is whether anything else
in the language makes a value that cannot be called without a name for it.

## What a host has to provide, said in the list a person reads

Nothing else in the language makes a value that cannot be used without a name
for it. A field of a call, an index of one, a walk over one, a match on one, an
optional out of one, a write through a handle one gave back: all of them work,
which is what an afternoon of trying them says.

What that turned up instead is in `kest check`. The list it prints for a person
gave the three functions a host has to provide the same line as the ones the
file wrote:

```
fn host.Host.write(text) -> void
fn host.measured(f64, f64) -> i32
```

`--json` has said which is which all along, with a field. The list a person
reads says it now the way the file says it:

```
extern fn host.Host.write(text) -> void
fn host.measured(f64, f64) -> i32
```

**Runs:** `make check`, everything passing, and `kest check` over the two
examples that declare externs.
**Next:** `kest check` prints every function of every file it read, including
the whole of `std.io` and `std.math` for a program that imports one line of
either. What a host has to provide is in there somewhere, and the list is
thirty lines long before the program's own first one.

## The file that was asked about

`kest check examples/ants.kest` printed ninety lines. Twelve of them were the
program; the rest was `std.math`, `std.vec`, `std.random` and `std.io` written
out in full, because the file imports them.

It prints the file that was named in full and a line for each module it
imported:

```
fn ants.main() -> i32
random  1 type, 9 functions
vec  2 types, 20 functions
io  3 functions, 1 the host provides
math  37 functions, 6 the host provides
```

The count of what a host has to provide is on the line, which is the thing
somebody writing a host is looking for and the thing that was hardest to find
in ninety. Reading a module in full is `kest check` on that module.

`--json` is untouched: it holds everything, and that is what having two forms
is for.

**Runs:** `make check`, everything passing, and `kest check` over a file with
four imports and one with none.
**Next:** `check` knows which file was named because the loader keeps the alias
of the first unit. `emit` prints every function of every file too, and there
the whole of `std.math` is the code that will run, so the same question has a
different answer.

## What a program needs, printed where the program is

The line asked whether `emit` should summarise imported modules the way `check`
now does. It should not: `emit` shows what will run, a host may call anything
the program defines, and `kest_module_needs` is worked out over all of it.
Summarising there would be summarising the code.

What `emit` was missing is the number that goes with it. `kest_needs` has
answered since there was a host boundary, and nothing printed it — so a host
writer had to write a C program to find out how much stack to give:

```
host Io.write
needs 49 slots and 5 frames
```

and, where there is no answer, what there is to say instead:

```
needs a number a host picks: `shapes.kept#[T],fn(T) -> bool no.alloc$vec.Vec2` calls through a value
```

`--json` says the same with nulls and a `why`, so a tool can tell a program
that has no answer from one that has not been asked.

**Runs:** `make check`, everything passing, plus `emit` over a program that
recurses through nothing, one that calls through a value, and one that imports
`std.math` for a single function.
**Next:** a tiny program that imports `std.math` for one function compiles
thirty-two functions and is sized by the deepest of them. The comment in
`kest_module_needs` says why — a host may call anything the program defines —
and that is a decision worth reading again now that a host can see the number
it costs.

## A host that knows what it calls can ask about that

`kest_needs` is the worst of every function the program defines, because a host
may call any of them. That is the right answer to a host that has said nothing,
and printing it last turn made the size of it visible: a program that imports
`std.math` for one function is sized by the deepest thing in `std.math`.

`kest_needs_of(build, name, ...)` is the same question about one function and
what it reaches. Nothing about the program changed — every function is still
compiled and still callable — and a host that knows which ones it calls stops
paying for the rest. `examples/embed.c` asks about the one it drives:

```
the program needs 32 slots and 2 frames
  `step` alone needs 13 slots and 1 frame
```

`kest emit` prints the same pair, with `main` as the one it asks about, and
only when it is less than the whole.

Which function a name means is one lookup now. `kest_entry`, the new one and
the disassembler each did the name-then-module-qualified dance separately, and
two of those would have gone on agreeing because they were written in the same
week.

**Runs:** `make check`, everything passing — a hundred and twenty-two
declarations, which is the tool saying both new ones are called from outside
the file they live in — and `examples/embed` printing the difference.
**Next:** `kest call` takes a function and arguments and prints what comes
back. It does not say what that function needs, and it is the one command that
calls something other than `main`.

## The command line gives a program what it says it needs

`kest call` was the line's subject: it says what came back and not what the
function needed. Looking at it, the answer is that it should not say — it
should use it, and so should `run`, and neither did.

Every program got the same machine, sixty-five thousand slots and a thousand
frames, which is what a host gets for saying nothing. So a chain of calls
twelve hundred deep stopped at a thousand:

```
error[K0602]: calls nest more than 1024 deep
needs 3602 slots and 1201 frames
```

Both of those are this program: the second is what `kest emit` printed about
the file the first one refused to run.

`run` asks about `main` now, `call` about the function it was given, and `tick`
about the whole program because either handler may be the one there. Never less
than the usual numbers, because what is measured is a least and this host
prints from inside the call it makes. A program that can reach itself has no
answer and gets what it always got.

The two usual numbers are `KEST_STACK_SLOTS` and `KEST_CALL_DEPTH` in `kest.h`
now. A host could only ask for them by leaving a zero, so a host wanting "as
much as usual, and this much heap" could not say the first half.

**Runs:** `make check`, everything passing; the twelve-hundred-deep program,
which answers 0 now and could not run before; a recursive one, which still
stops at a thousand and says so; and `kest call` on `gcd`.
**Next:** `tick` asks about the whole program because either handler may be the
one the file has. It knows which one it found a moment later, and asking twice
is cheaper than a machine sized for what is not there.

## A command asks about what it will call

`tick` asked about the whole program, because either handler may be the one the
file has. It asks about both now and takes the larger of the ones that are
there.

The difference is the whole point of the last two turns, in one file: three
hundred functions of its own and an `onEvent` of two slots.

```
needs 902 slots and 301 frames
     2 and 1 for `onEvent` on its own
```

`tick` sizes for the second line now. `room_for` takes a list of names rather
than one: a name the program does not have is one this host will not call
either and is skipped, and a name it has and cannot answer for ends the
question, because a host that cannot be told picks a number.

`kest emit` prints the line for each of `main`, `onEvents` and `onEvent` the
file has — those three because they are the ones a command line calls, and a
host with its own names asks `kest_needs_of` about those.

**Runs:** `make check`, everything passing, plus `tick` on a file whose
handlers are nothing and whose own functions are three hundred deep, and every
other command on what it was doing before.
**Next:** three names are written down in two files now — `main`, `onEvents`
and `onEvent` — and a fourth place knows them as the two a `tick` drives. They
are the command line's list, not the language's, and nothing holds them
together.

## The entry names, once each

`main`, `onEvents` and `onEvent` were written in seven places: the lists a
command asks about, the lookups it does, the usage text, the checker, and the
disassembler — which had them because that is where the per-entry line landed
last turn.

One `#define` each now, and every list built from those. `main` is in `kest.h`,
because the checker holds a function of that name to the shape a host can call,
and a host that wants to call the entry point should be able to write the same
name the language uses. The two handlers are the command line's, so they live
in `main.c` beside everything that uses them.

The library stopped knowing any of them: `kest_module_disassemble` takes the
names whose own cost is worth printing. It was printing a list of names a
command line happens to call and calling that a property of the module.

CLAUDE.md has the row now, under the lists that have to be complete.

**Runs:** `make check`, everything passing; the usage text, `emit` on the file
with a shallow handler, `tick`, and `run`, all saying what they said.
**Next:** `kest_module_disassemble` takes the entry names and
`kest_module_disassemble_json` does not, so `emit --json` has the whole
program's numbers and not the ones a host would ask for.

## The same answer in the other form

`kest emit` printed what each entry point needs on its own and `emit --json`
did not, so the form a tool reads had the number a host cannot use and not the
one it would ask for.

Both forms write the answer through one function now, so the shape a tool reads
for the program and the shape it reads for a function are the same shape,
nulls and all:

```json
"needs":{"slots":902,"frames":301,
         "entries":[{"name":"onEvent","slots":2,"frames":1}]}
```

Every entry the file has is in the object, whether or not it differs from the
whole — a tool looks one up by name rather than reading a list. The text form
still leaves out the ones that are the same, because a reader would be reading
them twice.

**Runs:** `make check`, everything passing, plus `emit --json` on a file with a
shallow handler, one whose `main` is the deepest thing in it, and one that has
no answer at all, which now says so per entry as well as for the whole.
**Next:** `kest call` sizes the machine for the function it was given and says
nothing about it. `emit --json` answers about three names it may not have;
`call` is the command that always knows exactly which one.

## What the call it made needs

`kest emit --json` answers about the three names a command line might call.
`kest call` is the command that always knows exactly which function it called,
and it said what came back and nothing about what it cost:

```json
{"diagnostics":[],"errors":0,"result":"12","needs":{"slots":7,"frames":1}}
```

and for one that reaches itself, the same shape with the same nulls the program
gets:

```json
"needs":{"slots":null,"frames":null,"why":"reaches itself","where":"rec.down#i32"}
```

That shape is written in one place now — the disassembly, the entries beside
it, and this all call the same function, which writes the fields without the
braces so a caller puts them where they belong.

So a host writer can ask what their own entry point costs by name, from the
command line, without writing a program to ask. Which is what `kest_needs_of`
was for two turns ago and could not be reached from outside C until now.

**Runs:** `make check`, everything passing, plus `call --json` on two functions
of `std.math`, one recursive function, and the text form, which says what it
always said.
**Next:** `call` prints `needs` after `result`, and `run` prints neither
because what it answers is the process status. A host writer asking about
`main` uses `emit`, which is the command for reading code.

## A number with a point in it, read

The line before this one was a conclusion rather than a task, so this turn went
looking for what a program needs and cannot write. It found one: `std.text`
reads a whole number out of a field and nothing reads one with a point in it. A
position, a weight, a rate — everything a save file or a config holds — had to
be read by hand or not at all.

`text.real` reads one. `12`, `-0.5` and `3.` are numbers; `1e3`, `.5` and
`1.2.3` are not, because a program that means those can say them another way
and a rule with one shape is a rule a reader keeps. It works a digit at a time
in `f64` and narrows once, which is the two roundings everything else in
`std.math` goes through, and `3.14159` comes back as `3.14159`.

`examples/parse.kest` reads a line of them, and checks the thing worth
checking: `number` refuses `1.5` and `real` reads it. That is why there are two
of them.

**Runs:** `make check`, everything passing, with the four new checks; and
eleven spellings by hand, of which five are numbers and six are not.
**Next:** `std.text` has `real` now and `std.io` prints what a hole in a string
holds, so a number goes out the way it came in. Nothing writes one to a chosen
number of places, which is what a line of a save file wants and what
`math.round` is used for by hand.

## A number written to a chosen number of places

A hole in a string writes the shortest spelling that reads back as the same
number, which is what a log wants. A file wants the other thing: `1.5` and
`1.50` are the same number and not the same line, and a column of them lines up
only if every one is written the same way.

`text.fixed(value, places)` writes that. Half goes away from nought, places
outside nought to nine are held to that, a number too big to count in whole
parts is written the way a hole would write it, and one that rounds to nothing
loses its sign — `-0.0` is a number this language has and not a thing anybody
wants in a file.

It is written out of what `std.text` already had and nothing else: `i64(x +
0.5)` rounds because a conversion cuts towards nought, so the module still
imports nothing and a program that reads text does not have to find a host that
provides `Math.floor`.

`examples/parse.kest` writes a line and reads it back, which is what a file is
for:

```
read 3 fields adding up to 49, and wrote x=1.50, y=-0.25
```

**Runs:** `make check`, everything passing, with two new checks; and thirteen
numbers by hand at places from -3 to 20, including the two that round to
nothing and the one too big to have places at all.
While putting the new function in the one form, three files turned out not to
be in it — the two others by being written before the formatter learned what to
do with the line they hold. `check-fmt.sh` holds the tree to it now, which is
the thing a language with one form ought to have been holding all along.

**Next:** `text.fixed` writes a number into a line and `io.print` writes the
line. Nothing writes a column: `fixed` gives `1.50` and `12.00` and a table
wants them ending in the same place, which is a width and not a number of
places.

## A column

`fixed` writes `1.50` and `12.00`, and a table wants them ending in the same
place. That is a width and not a number of places, so it is two more functions:
`text.right` pushes a piece of text to the right of a column that wide, which
is where a number belongs, and `text.left` to the left of one, which is where a
name belongs.

```
sword      12.00
rope        0.35
```

A width is in bytes, because text is its bytes. `hız` is four of them and `hiz`
is three, so two lines holding those do not line up, and nothing here pretends
otherwise: what a program means by a character is the program's to say. Text
already that wide comes back as it is — losing the end of something to fit a
column is worse than a column that does not fit.

`examples/inventory.kest` prints its stock that way and checks the two things
worth checking: what a line says, and that two of them are the same length.

**Runs:** `make check`, everything passing, with three new checks; and a column
by hand of a name that fits, one that does not, and both sides of a width of
two.
**Next:** `std.text` is thirteen functions now and every one of them is written
out of `len`, `find`, `slice`, `rest`, `matches` and the bytes. `right` and
`left` are the first two that could have been one function with a sign on the
width, and were not, because `right(subject, -8)` reads like nothing at all.

## "Not yet" was a promise

Eleven refusals in the compiler said a thing could not be done *yet*: an
operator not compiled yet, a field not reachable yet, something not assignable
yet. Every one of them was tried, and not one can be reached by a program. The
checker refuses each first — a `for` over a number is `K0317`, a call of a
number is `K0308`, an assignment to a literal is `K0205`, a `[T; 0]` is `K0326`
— so what those guards catch is the two halves of this compiler disagreeing
about what a program is.

They say so now, with `K0505` and in the words `K0405` uses:

```
error[K0505]: this walks something there is no walk for, which the checker allowed
 --> w.kest:5:5
  |
5 |     for x in n {
  |     ^^^^^^^^^^^ the two halves of the compiler disagree about what a program is, which is a fault in the compiler
```

`K0501` is gone from the tree. The one that looked like a real limit — a
`match` of more than eight things at once — is refused by the checker too, with
a count in the message.

`check-backstops.sh` has the eighth hole: the checker's refusal of a walk over
a number taken out, and the compiler has to be the one that notices.

**Runs:** `make check`, everything passing, and the message above is from that
broken tree, built by hand to read what it says.
**Next:** every refusal in the compiler is now either a limit with a number in
it — names, loops, jumps — or a fault. The limits are `K0502` and `K0503`, and
what they hold to is written in the code that raises them rather than anywhere
a program's author would look.

## What there is a most of

Seven numbers hold a program: names in a function, loops nested, breaks and
continues and defers, how far a jump reaches, how many things a `match` chooses
at once, how many a `[T; N]` holds. Six were written only where they are
enforced, and two of the messages did not say the number at all — "this jumps
too far to encode" does not say how far is far.

```
error[K0503]: this loop is 156012 bytes of code, and a loop reaches back 65535
```

That is a program of twelve thousand statements in one loop, written to see the
message rather than by anybody. The reference has the table of all seven now,
under a heading somebody would look under, and every message carries its own
number.

**Runs:** `make check`, everything passing, plus two programs written to run
into a limit: three hundred names in a function, and a loop body longer than a
jump can reach.
**Next:** `MAX_EXITS` is a limit too and is not in that table, because a
condition with more ways out than that is compiled the way conditions were
compiled before — nothing is refused, so there is nothing to tell anybody. It
is the only number here that changes what is emitted rather than whether it is.

## The one number nobody could see

A condition had sixteen ways out, and one with more of them was compiled the
old way: an answer built on the stack and read by one jump. Nothing was
refused, so nothing was said — the only number in this compiler that changes
what is emitted rather than whether it is emitted at all, which is exactly the
kind of cost this language claims not to have.

The list of ways out grows now, so there is no number. A condition of forty
`||` over whole numbers is thirty-nine `jump.true.eq.i` and one
`jump.false.eq.i`, and nothing is built:

```
if n == 0 || n == 1 || ... || n == 39 {
```

`make time` is where it was, which is what it should be — nothing in the
instrument has a condition long enough for the old fallback to have fired.

**Runs:** `make check`, everything passing; a condition of forty terms; one
mixing `&&`, `||` and `!` around an `if let`; and a `while` whose condition is
two of those.
**Next:** `if let` is compiled as a value and branched on, because what it
leaves on the stack is the thing it binds. That is the last condition in the
language that is not compiled for where it goes.

## The angle a direction points, and the bug it found

`if let` turned out to be compiled the way it should be: what it leaves on the
stack is the value it binds, and the jump reads the tag above it. There is no
answer being built and thrown away, so the line before this one had nothing in
it.

What the language did lack is an angle. `std.math` asked a host for six things
and not one of them turns two numbers into where they point, so a program that
turns something towards something else worked it out from `sin` and `cos`
backwards. `Math.atan2` is the seventh, and out of it and `sqrt` come `tan`,
`asin` and `acos`, written in Kest: a host provides one more function and a
program gets four. `asin` and `acos` give nothing back outside -1 to 1, because
that is a question with no answer.

Writing `asin` found a real bug. It is

```kest
return Math.atan2(value, Math.sqrt(1.0 - value * value))
```

and it gave back the square root. A host call carries how many slots its answer
takes, and the compiler took that number from the expression rather than from
the function — so where a value stands in a place an optional is wanted, and
the checker has widened the expression to the optional, the call said two slots
for a one-slot answer and wrote over the slot beside it.

A call gives what the function gives now, in all three of the ways one is made.
`examples/camera.kest` says where it faces and how far it would have to turn,
which is that shape running.

**Runs:** `make check`, everything passing, with seven new checks; the
formatting check caught `lib/std/math.kest` before anything else did, which is
what it was added for two turns ago.
**Next:** `Math.atan2` is the seventh function every host of a program that
imports `std.math` has to provide, whether or not the program reaches it. That
is D058's rule about what a program declares, and it makes a module that grows
a function a module that breaks every host of it.

## What a host is expected to take

A growing library is a burden on a host that hard-codes what it binds, and the
answer to that was already here: `kest_build_extern` is the list, so a host
reads it rather than guessing. What was not here is the other half — what each
of those functions is expected to take.

A host binds a C function to a name and nothing checked that the two agree. One
bound to a name that takes one thing and written to read two reads whatever is
beside it. So `kest_extern_takes`, `kest_extern_layout` and `kest_extern_gives`
answer what the program expects to cross, in the layouts `kest_frame_layout`
already gives for a crossing the other way.

`examples/embed.c` says what it believes and compares:

```
`Engine.decide` is handed 4 bytes and this host reads 8
```

which is what it prints when its table is changed to claim an `int64_t`. It
names every missing binding now rather than the first, because a host writer
wants the list.

**Runs:** `make check`, everything passing — `check-dead.sh` is what says all
three new declarations are called from a host, since the header is held to
being used by the two in this tree; and two deliberately wrong tables by hand,
one about how many and one about how wide.
**Next:** the extern's shape is written down when a call to it is compiled. An
extern nothing calls is never registered, so a host cannot ask about one the
program declared and never reached — and `kest_start` will not ask it to bind
one either, which is the same rule seen from the other side.

## An extern nothing calls

`kest check` listed two `extern` declarations and `kest emit` asked a host for
one of them, and the reference said the list is what a program declares rather
than what it calls. The code was right and the sentence was wrong: an extern is
registered where a call to it is compiled, so one nothing calls is not on the
list and starting does not hold a host to it.

The gap between the two commands is now a warning where the declaration is:

```
warning[K0506]: nothing calls `Host.never`, so no host is asked for it
```

A warning and not a refusal, because a declaration nobody uses is not wrong. It
is worth a line because the file is what a host writer reads, and binding a
name nothing will ever ask for is work with nothing on the other end.

**Runs:** `make check`, everything passing — which is also the check that no
file in this tree declares one it does not call; and a file with one used and
one unused extern by hand.
**Next:** the warning walks every declaration of every file after compiling,
which is the third walk over the same list — the compiler registers, the
machine binds, and this counts. They agree because they are read from one
place, and nothing says so.

## Adding while walking a store

Removing while walking a store is defined and documented. Adding was neither,
and a program that spawns during a frame does it constantly.

What happens: a walk is a scan over live slots in slot order, and a store hands
out the slot it last took back, or a new one at the end when it is holding
none. So something added inside a walk lands where the walk has already been as
often as where it has not, and whether this walk reaches it depends on what
died before it. Deterministic, and not worth relying on.

Bounding the walk to what was live when it began would make it a rule and would
cost an instruction to read the extent and an operand to carry it; D155 is why
that is not free. So both halves are written down, and `examples/quests.kest`
gathers what it wants to spawn and adds after the walk, which is one line more
and the same every time.

**Runs:** `make check`, everything passing, with four new checks in
`quests.kest`; and a file by hand that removes inside a walk and adds inside
one, which is how the second of those was found to be neither defined nor
refused.
**Next:** `len(store)` counts what is live and the walk scans to the extent,
which are two different numbers whenever anything has been removed. Nothing
says which one a program is asking for, and `len` is the name of the other one
for an array.

## Text, the other way over the boundary

A host could lend the program an array over its own memory and could not hand
it a piece of text. Nothing refused it — `KestValue.text` is a `const char *`
and a host function could write one — and whatever it pointed at would have to
outlive everything the program did with it, which a host cannot know and
nothing said.

`kest_text` copies into the machine's heap, where the program's own text lives.
A zero byte inside the length is reported rather than cutting the rest off,
which is what `text(bytes)` does inside the language.

Both hosts in this tree hand over their own name now, and `embed.kest` asks
twice and compares:

```
one frame under kest, 1 left standing
one frame under embed, 1 left standing
```

The second is what `examples/embed` prints, which is the same program under the
other host.

**Runs:** `make check`, everything passing — the new crossing runs under both
hosts and both sanitisers, which is what says the copy outlives the call.
**Next:** `std.io` writes and nothing reads. A command-line program cannot get
a line in, and the answer is not to declare one in `std.io` — that would ask
every host of every program for it — but for the command line to provide one, a
program that wants it to declare it, and the reference to say which host has
what.

## A program that reads

`std.io` writes and nothing reads, so a program on a command line could not get
a line in. Declaring `Io.read` in that module would ask every host of every
program that imports it for a standard input, and an engine has none.

So the command line provides it, a program that wants it declares it, and the
reference says which host has what. `examples/lines.kest` reads a list of items
and adds them up, and falls back to a line of its own when there was nothing to
read, which is what it gets under `make check`:

```
sword     1200
rope        35
lamp        90
3 lines, 1325 altogether
```

and with `axe 40` and `shield 250` piped in, that is what it says instead.

Everything at once rather than a line at a time, because `std.text` splits and
reading a line at a time would ask a host to keep a place in a file between
calls.

The checks give every program nothing on the standard input now — the examples,
the sweep under the sanitisers, the commands, and the backstops. An example is
a program that answers the same thing every time or it is not one, and until
today there was nothing to read so nothing said so.

**Runs:** `make check`, everything passing, 29 examples; and the new one by
hand with nothing, with two lines piped in, and with twenty thousand bytes of
noise, which is the growing read.
**Next:** `Io.read` reads everything and gives it back as one piece of text, so
a program that wants a megabyte of it holds a megabyte twice for a moment: once
in the host's buffer and once in the machine's heap. The first is freed and the
second is the program's.

## `type` is a name

Reading `Io.read` again: it holds what it read twice for a moment, once in the
host's buffer and once in the machine's heap, and the only way round that is an
API for filling the heap directly. That is surface for a case that does not
matter — a command line reading its input — so it stays as it is, and the
reference already says the copy is the point.

What the looking found instead: `type` was a keyword the parser accepted
nowhere. The reference said it was kept back and had no meaning yet.

That is the promise D169 took out of the compiler, sitting in the lexer, and it
costs something every day it is not kept: `type` is what somebody calls the kind
of an event, and a struct could not have a field of that name.

```kest
struct Event {
    type: i32
    at: i32
}
```

That file is refused this morning and reads fine this afternoon. `flags` is how
this language takes a word back when it needs one, and a `type Health = i32`
can arrive the same way on the day somebody designs what it means.

**Runs:** `make check`, everything passing — 67 tokens in step with their
names, which is one fewer than yesterday and is the tool saying the table
followed the enum; and a struct with a field called `type` and a local called
`type` by hand.
**Next:** the language has 67 tokens and one of them, `match`, is a keyword the
parser accepts in one place. `flags` and `type` are words. Nothing says which
of the two a new word should be.

## When a word is a keyword

The language has both kinds of word and nothing said which a new one should be.
The rule is in CLAUDE.md now: a word is a keyword only when a program that used
it as a name would be ambiguous where it stands, and no word is kept back for a
feature that does not exist. What a keyword costs is paid by every program that
wanted the name, every day.

`check-tables.sh` holds a third list to that: the keywords the lexer has beside
the ones the reference prints. It found three the first time it ran.

```
keywords: the lexer holds `enum`, `match`, `none` and the reference does not say so
```

A reader was shown a list of eighteen words and told it was the whole of it,
and three were missing. There are twenty-one.

The tool also turned out to have been reading the first letter of each spelling
rather than the spelling: `[m[0] for m in re.findall(...)]` over a pattern with
one group. Nothing noticed, because the only thing asked of that list until
today was how long it was.

**Runs:** `make check`, everything passing — 146 instructions, 67 tokens and 21
keywords in step with their names; and the reference with `match` taken out of
its list by hand, which the tool refuses.
**Next:** the keyword list in the reference is sorted and the one in the lexer
is not quite: `defer` sits between `continue` and `enum` in one and after
`else` in the other. Nothing reads them in order, so nothing said.

## A shift past the width

The keyword table is sorted now, which is what the reference's list has always
been. Nothing reads either in order, and one list in two places should look
like one list.

Then the arithmetic edges were tried, because a simulation lives on them:
`2147483647 + 1` wraps to the bottom of an `i32`, `-7 / 3` is -2 and `-7 % 3`
is -1 the way C has them, an `i8` of 127 plus one is -128, and a `u8` of 255
plus one is nought. All of that is D018 working and all of it is written down.

What is not written down is the count at or past the width, which is where C
stops having an answer. This language has one: everything is shifted out, so
`1 << 64` is nought and `-8 >> 64` is -1 — what the sign says, and what a shift
of sixty-three and then one more would have given. The machine has said so
since it was written; the reference says it now, and
`examples/flags.kest` checks all three.

**Runs:** `make check`, everything passing, with three new checks; and nine
edges by hand — two overflows, two narrow types, a negative divide, a negative
remainder, and shifts of 64 and 100 either way.
**Next:** `examples/flags.kest` is where the shifts are checked because it is
about bits, and the wrapping of `i8` and `u8` is checked nowhere: the reference
says a narrower integer wraps and the only place that is run is a constant in
`docs/decisions.md`.

## Every edge of both rules

The reference says an integer going into a narrower one wraps and a float going
into an integer stops at the end of the range. Nothing that runs checked either,
so `examples/math.kest` does now: one past the top of an `i8`, a `u8`, an `i16`
and an `i32` is the bottom of it, one below the bottom of an `i8` is the top,
`i8(300)` keeps the bits it has room for, and `const NARROW: i8 = 120 + 10` is
-126 before the program runs.

Then the other rule, which is the one place the two differ: `i32` of a number a
thousand million times too big is the top of an `i32` and not a wrap, `u8(-5.0)`
is nought, and `i32` of what is not a number is nought — nothing that is not a
number has an order, so it lands on neither end. That last one was not written
down anywhere and is now.

Trying them turned up one thing worth knowing: `let g: i8 = 0 - 128` is
refused, because 128 is a literal that does not fit an `i8` and the subtraction
is a subtraction. `-128` is how the bottom of a width is written, and that is
what the file says now.

**Runs:** `make check`, everything passing, with ten new checks in
`examples/math.kest`; and every width by hand, up and down, plus four floats
that do not fit anywhere.
**Next:** `i32(nothing / nothing)` is nought and `i32(1.0 / nothing)` is the
top of the range, so a program that divides by nought and narrows gets a number
either way. Nothing says whether dividing a float by nought is a mistake here;
the integer one is `K0601`.

## Not a number has one spelling

Dividing by nought is two different things and only one of them was written
down. A whole number has no answer, so it is `K0601` and the program stops; a
float has one and it is the one C has, an infinity with a sign, or not a number
when nought is divided by nought. D018 decides it — match C where C has an
answer — and the reference says so now.

Printing those turned up something to fix. `0.0 / 0.0` printed `-nan`, because
that is what the divide left in the sign bit and what C prints for it, while
`nought minus that` printed `-nan` as well. The sign of a not-a-number says
which operation made it and nothing about the value, so there is one spelling
now:

```
inf -inf nan nan
```

An infinity keeps its sign, because that one means something. Neither reads
back — there is no way to write either in the language — and that is the one
place the rule about shortest spellings cannot hold, which the reference now
says outright.

**Runs:** `make check`, everything passing, with five new checks in
`examples/math.kest`: both infinities, a not-a-number that is not equal to
itself, and the spelling of all three.
**Next:** `examples/math.kest` is thirteen checks about arithmetic and four
about factorials and greatest common divisors, which is a file that has become
two things. The second is what it was for.

## Two files, because it had become two things

`examples/math.kest` was a factorial, a greatest common divisor and a
classification, and then thirteen checks about what an `i8` does when it runs
over. The second is not what the file was for.

`examples/numbers.kest` is the second one now: what a number does at the end of
its range. Every width up and down, narrowing that keeps what it has room for,
a constant worked out where it is written, a division that truncates towards
nought, a float that stops at the end of the range rather than wrapping, and
what dividing by nought gives — sixteen checks, all of them D018's rule
running.

`math.kest` is what it was before, seven checks about the three functions it
holds.

**Runs:** `make check`, everything passing, 30 examples now; and both halves by
hand, which answer 0 where the whole did.
**Next:** `examples/frame.kest` is the one example with no `main`, so it
resolves rather than runs, and it is where the shapes a frame is made of are
written down. `tools/frame.kest` is the instrument with the same name and a
different job, and nothing in either says so.

## A file is where it says it is

There are two files called `frame.kest` and neither said so. One is
`examples/frame.kest`, which has no `main` and is checked rather than run, and
holds the shapes a frame is declared with; the other is `tools/frame.kest`,
which is the one measurement. Each names the other now.

Reading the first one turned up something worth a rule: it called itself
`game.frame` while living at `examples/frame.kest`. Nothing imported it, so
nothing had noticed — and an import is a path, so a file whose `module` line
does not match where it is cannot be imported at all.

`check.sh` holds every `.kest` in the tree to that now:

```
modules    examples/frame.kest calls itself `game.frame`
```

which is what it says with the old line put back by hand. It was already true
of the other thirty-seven files.

**Runs:** `make check`, everything passing, 38 files held to their own names;
and the rule broken by hand to see it caught.
**Next:** `examples/game/npc.kest` is the only example in a directory of its
own, and nothing imports it either. It is `examples.game.npc`, which is where
it is, so the new check is happy — and a file nothing imports and nothing runs
is checked and nothing else.

## A name clash is a question about one file

`examples/game/npc.kest` turned out to be imported after all — by
`examples/game.kest`, which is the two-file example. What it says about itself
was wrong, though: the comment said the import reads `game/world.kest`, and it
reads `examples/game/npc.kest`. It says how now, which is the rule the
reference states: the file the command names settles where the package
directories start by having its own name taken off its path.

Then `kest check examples/*.kest` — which the reference calls checking a
project as a project — refused this project:

```
error[K0328]: two modules both put their names under `math`
 --> examples/math.kest:1:8
 --> ./lib/std/math.kest:1:8
```

Neither file imports the other and neither is ambiguous about anything. The
refusal was program-wide, so any two files anywhere with the same last segment
were a clash.

It is about one file now: what a name in a file can mean is that file's own
module and what it imports, so a clash is between two of those, and the message
points at the import that brought the second one in rather than at two files
that never meet. A project may hold a `math.kest` beside `std.math`, which is a
file name somebody will want.

**Runs:** `make check`, everything passing; all thirty examples checked as one
project, which is what found this and now answers nought; and two clashes by
hand — one file importing two modules that end the same way, and a file whose
own name ends the same as what it imports.
**Next:** `kest check examples/*.kest` reads thirty files as one program, and
`kest check` on one of them reads what that one reaches. Both are documented
and neither is run by `make check`, which checks each file on its own.

## The same file spelled two ways

`make check` reads every file on its own and the reference says `kest check
*.kest` reads a project as a project. Nothing did the second, so `make check`
does now — and it failed on the first run:

```
error[K0304]: `random.Source` is already declared
  --> lib/std/random.kest:16:8
  --> ./lib/std/random.kest:16:8
```

One file, two spellings. A command line names `lib/std/random.kest` and an
import of it from the library root works out `./lib/std/random.kest`, and the
loader compared what it was given, so it read the file twice and declared
everything in it twice.

It tidies a path before comparing now: a leading `./`, a doubled slash, and a
step into a directory and back out of it. Not the ones that need asking the
operating system — two routes through the file system to one file are two files
as far as a compiler that reads what it is given is concerned.

**Runs:** `make check`, everything passing, with the whole tree read as one
project as well as file by file; and by hand a project whose command line and
whose import name one file two ways, which reads it once.
**Next:** `make check` reads examples and library as one project, and
`tools/frame.kest` is left out of it because an instrument is not part of the
program. It is checked on its own, so nothing asks whether it could be read
beside the rest.

## Two modules that never met, sharing a table

Reading every Kest file in the tree at once refused it — `frame.Npc` declared
twice, by `examples/frame.kest` and `tools/frame.kest`, which both put their
names under `frame`. That looked like a reason to leave the instrument out of
the reading, and it was a reason to look at the rule instead.

D183, yesterday, made a shared alias a question about one file: refused where
one file reads both, allowed otherwise. What that let through is this, in a
project of three files:

```kest
module leak.user
import mine.math

fn main() -> i32 {
    return math.double(2) - math.min(4, 9)
}
```

`math.min` is `std.math`'s, which this file never imported. Another file in the
program did, and the table names go in is the program's, so `math` was one
namespace with two modules in it.

So a shared alias is refused for the whole program again and D183 is
superseded. What would make it a question about one file is keying the table by
the whole of a module's name and resolving what a file writes through its own
imports; that is a change to every lookup in the compiler and is written down
rather than made.

`make check` reads `lib/std` as one project — which is one — rather than the
whole tree, which is thirty programs in a directory.

**Runs:** `make check`, everything passing; the leak above by hand, which is
refused now and named both modules; and the library as one project.
**Next:** the message names the two modules and points at the one read second.
Which of the two a program should rename is not something it can know, and it
says `std.math` first when one of them is the library, which is the one that
cannot be renamed.

## The one that can be changed

The message about two modules under one name pointed at whichever of them was
read second, and said the same thing about both. When one of the two is the
library's, that is the wrong one to point at: `std` is the one name a program
cannot use, so the library is not the reader's to rename.

It points at the other one now, and says why:

```
error[K0328]: two modules in this program both put their names under `math`
 --> mine/math.kest:1:8
  |
1 | module mine.math
  |        ^^^^^^^^^ the other one is the library's and is not yours to rename, so this is the one to call something else
 --> lib/std/math.kest:1:8
```

Two of a program's own are what they were: either can be renamed, so the
message says so and names both.

**Runs:** `make check`, everything passing; a program with a `math.kest` beside
`std.math`, and one with two of its own that end the same way.
**Next:** `module_named` reads the module line out of the source text to see
whether it starts with `std.`. The loader already knows — it is what decides
where a file is read from — and nothing carries the answer forward.

## Decided where it is decided

Whether a file is the library's is what decides where it is read from, and the
message about two modules under one name was working it out again by reading
the module line and looking for `std.` at the front of it. Two answers to one
question, and the loader's is the one that matters — it is the answer the file
was found by.

The unit carries it now. `is_library` is called once, where the alias is taken
off the module line, and the message reads the flag.

**Runs:** `make check`, everything passing; and the clash with the library by
hand, which says what it said.
**Next:** `KestUnitInfo` now holds what a file calls itself, what it may reach,
and which side of `std` it is on. Two of those three the loader works out and
the third it copies from the parser, and nothing says which of them a reader of
that struct can trust to be filled in.

## What a unit holds, and a file that names nothing

`KestUnitInfo` says what a file is: its path and text, what parsed, what it
calls itself, which side of `std` it is on, and what it may reach. Which of
those a reader can trust was not written down, so it is now — and the answer is
all of them, because the loader zeroes a unit before it reads anything into it
and every field has an answer for a file that has none of what it comes from.

Writing that down turned up the file with none. A program with no `module` line
is legal, its names live under nothing, and `kest check` was printing it as if
it were something imported:

```
main  1 function
```

because "the root's own names" was being decided by matching a prefix, and a
file that names no module has no prefix to match. It lists the file now, which
is what a reader of it asked for.

**Runs:** `make check`, everything passing, plus two files with no `module`
line by hand — one that imports nothing and one that imports `std.io`, which
lists itself and summarises what it read.
**Next:** a file with no `module` line is what the backstops write, and the
tools that generate one write it because a name is not wanted. Nothing in
`examples` is written that way, so what a reader of the reference sees is
always a file that names itself.

## A file that names nothing is a program

A file may say what it is called, and one that does not puts its names under
nothing — which is what a program written to answer one question wants, and
what this project's own generated programs are. Nothing said what happens if
somebody imports one.

What happened is that it worked, and the imported file's names went into the
importing file's own:

```kest
import bits.thing

fn main() -> i32 {
    return helper() - 7
}
```

`helper` is `bits/thing.kest`'s. Every other import in this language writes
where a name came from at every use of it, and this was the one that did not.

It is `K0702` now, at the import, and it suggests the name the import asked
for: an import is a path, so the name the file should have is the one already
written in the file doing the importing.

**Runs:** `make check`, everything passing, and the program above by hand,
which is refused and told what to write.
**Next:** the loader reports two things about a file it was told to read: that
it cannot be read, and that it has no name to be read under. Both are about the
import rather than the file, and only one of them stops the walk.

## How it got there

A failure while running said where it was and not how it came to be there:

```
error[K0601]: division by zero
 --> deep.kest:4:12
```

Three functions deep that is a line and a guess. The machine has the frames in
front of it at that moment — every one below the failing one has an `ip` just
past the call it made — so each of them is now a note, outermost first:

```
 --> deep.kest:12:12  `deep2.middle` was called here
 --> deep.kest:8:12   `deep2.inner` was called here
```

Eight is what a message holds, and a run of calls deeper than that says how
many were left out rather than showing the middle of it: twenty deep says "and
13 more under it".

It costs nothing while a program runs — the walk is where a failure is already
being reported — and `make time` is where it was, 127 nanoseconds an
entity-step.

**Runs:** `make check`, everything passing; a failure three calls deep, one
twenty deep, and the same in `--json`, where the notes are a list beside the
message.
**Next:** the notes name the function that was called, and a copy of a generic
carries what it takes in its name, so a failure inside one says
`sort#i32,fn(i32) -> bool` was called. That is the name the program compiled it
under and not the one somebody wrote.

## Which of a name's two meanings was meant

Eight ordinary mistakes were written into one file to read what the compiler
says about each. Seven were answered once and well. The eighth was answered
twice:

```
error[K0309]: expected 2 arguments, found 3
error[K0310]: `add` works on a store, found `i32`
```

for a file that declares its own `add` and calls it with three arguments. The
second is about a function the reader did not write. Which of the two `add`s
was meant was being decided by how many arguments the call hands over as well
as by what it hands over first — so a call with the wrong arity fell past the
user's function and the builtin answered. How many is a mistake in the call;
what it takes first is what says which function it is.

While there: `p.len()` is what somebody writes who has met a language with
methods, and the answer was that `Point` has no field `len`. It says what to
write instead now, looking in the language's own names, in what the file
declared, and under what it imported:

```
7 |     let u = t.upper()
  |               ^^^^^ there are no methods here: write `text.upper(...)`
```

**Runs:** `make check`, everything passing; the file of eight mistakes, which
now answers each once; and a program that declares `add` of one argument and
calls the store's `add` of two, which still reaches the builtin.
**Next:** the list of builtin names in that suggestion is written out in
`check.c` beside the twenty-odd places that check for one by name. It is a list
that has to be complete and nothing holds it.

## Fifteen names, in three places

The suggestion about methods needed a list of what the language answers to on
its own, and it was written out by hand beside the twenty-odd places that ask
about one. Two lists of the same thing, and a third: the compiler emits for
those names too.

The three were compared. The checker and the compiler agree exactly, which is
what the language working means. The hand-written one had eighteen: it added
`has`, `sort` and `text`, none of which is a builtin — `table.has` and
`sort.sort` are the library's, and `text(bytes)` is a conversion. So a suggestion
about `p.sort()` would have said `sort(...)`, which is not a function.

It is the same fifteen now, and `check-tables.sh` holds all three together:

```
146 instructions, 67 tokens, 21 keywords and 15 builtins are in step with their names
```

The other three names still get a suggestion, because everything that is not
one of the fifteen is looked for under whatever module it is in — which is why
`t.upper()` says `text.upper(...)`.

**Runs:** `make check`, everything passing, and each of the three lists broken
by hand in turn, which the tool refuses with the name that went missing.
**Next:** `check-tables.sh` reads C with regular expressions, which is what it
has always done, and it now knows five patterns for four lists. A sixth would
be a tool that parses C badly rather than one that reads a table.

## What a generation is for

A console parser was written the way somebody would write one — an enum with
payloads, a `match` whose arms build text, a table keyed by text, and
`split`, `trim`, `lower` and `number` over a line — and it ran the first time.
Nothing to report, which is what a language is for.

So the reading went to the reference instead, and to the claim the store rests
on: a reference can go stale, and reading through a stale one fails. That much
`examples/quests.kest` checked. What it did not check is the half that a slot
map without generations gets wrong: after the slot has been taken back by
something added later, the old reference must still read nothing rather than
answering with whoever moved in.

It does, and the example checks it now — reading, writing and removing through
a reference to something that is gone, with the slot occupied again by a
`cooper` who is not it.

**Runs:** `make check`, everything passing, with seven new checks in
`examples/quests.kest`; and the console parser by hand, which found nothing to
fix and is not in the tree.
**Next:** a reference is a slot and a generation in one slot of memory, and a
generation that wraps would make an old reference read as a live one. Nothing
says how many removals that takes.

## A slot that has used all its counts

A reference is a slot and a generation in one value, thirty-two bits each, and
the generation counts removals of that slot. Four thousand million removals of
one slot and the count comes round to where it started, so a reference from the
first occupant would read as the newest one — the one thing a reference is for,
failing quietly.

A slot whose count has come round is not handed out again. One comparison on
the removal path; what it costs is one slot in a store that has been removed
from four thousand million times.

Nothing that runs demonstrates it, and that is worth saying rather than
implying: a tree with the count started near its end shows the slot being
retired and shows nothing going wrong without the retirement, because the
collision is another four thousand million removals away. What can be said is
what it costs and that a long-running program reaches it — a thousand removals
a frame at sixty frames a second is twenty hours.

**Runs:** `make check`, everything passing; and a tree by hand with the count
started at its last two values, which retires the slot and reads both stale
references as gone.
**Next:** `store->used` never goes down, so a store that has retired a slot
keeps it in every walk it does — the walk skips it because it is not live, and
what it costs is one comparison a turn for the rest of the program.

## A store with nothing in it reaches nothing

A walk over a store steps over its dead slots, so what it costs is how far the
store has ever reached rather than how much is in it. A level that spawned a
million and ended with none would walk a million dead slots for the rest of the
program.

A store whose last live one is removed now goes back to reaching nothing, and
the free list with it. What is kept is what each slot has counted, which is the
easy thing to lose here: filling the store again hands back slot nought, and a
reference to the first occupant of slot nought must still read nothing. A slot
is given its first count only when it has never been used at all — the extent a
walk goes to and the extent the counts have been written to are two numbers now.

`examples/quests.kest` checks it: a store emptied and filled again, where the
reference from before reads nothing and the one from after reads what it should.

The fragmented case is left alone. Shrinking to the highest live slot means
taking slots out of the free list, which is a scan, and nothing has measured
the walk over the dead ones as worth one.

**Runs:** `make check`, everything passing, with three new checks in
`examples/quests.kest`; and by hand a store filled with five, emptied, walked —
which takes no turns — and filled again, where all five references from before
are stale and all five from after read what they hold.
**Next:** `store()` writes two fields of a `Store` and leaves the other nine to
the arena, which zeroes what it hands out. That is true and is written nowhere
near the code that counts on it.

## A handle used as something it is not

`store()` writes two fields and leaves nine to the arena. That is not a hole:
`kest_arena_alloc` says it returns zeroed memory, and a comment at every place
that counts on it would be the repetition this project avoids. The one thing
worth checking was whether the promise survives a heap thrown away between
frames, and it does — a reset builds a fresh arena rather than rewinding the
old one.

What the looking found instead: every handle carries what it is, every
instruction that follows one reads that first, and nothing had ever seen the
check fire. `check-backstops.sh` has the ninth hole now — `kest_type_equal`
told that an array and a store are the same type — and this is what the machine
says:

```
error[K0612]: this is not a store
 --> h.kest:6:12
  |
6 |     return len(world)
  |            ^
 --> h.kest:12:12
  |
12 |     return count(xs)
  |            ^ `count` was called here
```

The note is yesterday's trace, and it is what makes the answer useful: the
message is at `len` and the array was handed over two lines away.

**Runs:** `make check`, everything passing, nine backstops each catching what
it is for; and the broken tree by hand to read what it says.
**Next:** the nine holes are nine breaks in five files, and each names the code
it expects to break by quoting it. Three of them quote code that has been
edited this month, and the tool says so when a quote no longer matches — which
is a check that the checks are still about something.

## Reading the reference against the machine

A file of claims from the reference was written and run: escapes and a brace
that is not a hole, what `hash` applies to, byte literals, and what a flag set
and an enum case print as. Everything held except two, and neither was in the
part being audited.

The first was writing the file. `flags State {` — the width left off — was
answered with "expected a declaration", and the list of what a file may hold
left out `enum` and `flags`, which are two of the eight. So a reader who nearly
wrote a flag set was told they had written nothing of the kind, and shown a
list missing the thing they meant. Both are fixed: the list is all eight, and

```
error[K0212]: a flag set says how wide it is
 --> audit.kest:5:1
  |
5 | flags State {
  | ^^^^^ the width is what a host sees, so it is written rather than counted off the names: `flags State: u8 {`
```

The second was the claim itself. "A struct combines what its fields decide:
`hash(a) * 31 ^ hash(b)`" reads as though the language does the combining. It
does not, and it is right not to — `==` does not apply to a struct either,
because which fields decide is the program's to say. The sentence says who
writes it now.

**Runs:** `make check`, everything passing; the audit file, which answers
nought; and a file holding a `let` at the top, which is told all eight things a
file may hold.
**Next:** the eight are in a message in `parser.c` and in a sentence in the
reference, and the parser knows them as eight `if`s in one function. That is a
list that has to be complete, held together by nothing.

## Which copy the message is about

A generic body is checked once per set of types, and a mistake in it is
reported at the body — which reads the same for every copy. So a file with two
calls to one generic was told `>` does not apply to `Pair` at a line that
mentions neither.

Every message raised while checking a copy now carries a note at the call that
asked for it:

```
error[K0314]: `>` does not apply to `gsort.Pair`
  --> gsort.kest:15:12
   |
15 |         if one > best {
   |            ^^^^^^^^^^
  --> gsort.kest:30:20
   |
30 |     if let worst = largest(ps) {
   |                    ^^^^^^^^^^^ this copy was asked for here
```

Each of them and not the last, because a body says more than one thing and they
are all about the same copy — which took a way to add a note to a diagnostic
that is not the newest one.

The turn started somewhere else: the eight things a file may hold are known in
three places and nothing holds them together. Making the parser's chain into a
table would be a function-pointer refactor of a delicate file for a list that
changes when the language does, which is a decision and gets remembered
elsewhere. So it was left, and the reading went to what a generic says instead.

**Runs:** `make check`, everything passing; a generic over a type that does not
compare, and one with two mistakes in a copy, which are both told which copy.
**Next:** the note says where the copy was asked for and not what it is a copy
of. `largest#[Pair]` is the name it was compiled under, and a reader looking at
two calls in one line still has to count.

## Which copy, in the note

The note said where a copy was asked for and not what it is a copy of, so two
calls on one line left a reader counting. It names what the type names stand
for now:

```
12 |     if both(Pair(1), Pair(2)) {
   |        ^^^^^^^^^^^^^^^^^^^^^^ this copy was asked for here, with `K` as `twoc.Pair` and `V` as `twoc.Pair`
```

which is the whole of what tells one copy from another: a copy is a body and a
set of types, the body is in the message already, and the types are what was
missing.

**Runs:** `make check`, everything passing; a copy over one type name and one
over two, which say `T` as one thing and `K` and `V` as two.
**Next:** the note is built with `snprintf` into two hundred and fifty-six
bytes, and a copy over eight type names of long names would fill it. What it
does then is stop, which is what every other message in this compiler does with
a name too long to print.

## Ending in the middle of a name

The note that says what a copy's type names stand for is built into two hundred
and fifty-six bytes, and a copy over eight long names filled it:

```
`T5` as `long.AVeryLongStructNameIndeed5
```

— stopped mid-name, with no closing mark and nothing to say that anything was
missing. Every other message in this compiler that cannot fit a name stops the
same way, and none of them says so either.

This one does now. Room is kept back for a tail, so what does not fit is
counted rather than cut:

```
`T4` as `long.AVeryLongStructNameIndeed4`, and 3 more
```

which is the shape the trace of a deep failure already uses.

**Runs:** `make check`, everything passing; a copy over eight long names, which
says five and counts three, and one over a single short name, which reads as it
did.
**Next:** every message that has to fit in five hundred and twelve bytes is
one that can stop mid-name. There are four of them left.

## The buffer a message was built in

Twice in a week a message ended in the middle of a name, and both times the fix
was to that message. The thing they had in common is that each was built in a
fixed buffer belonging to whoever raised it — `char message[512]` in the
checker, the compiler, the parser and the machine — and then copied into the
arena the diagnostics are kept in. The buffer was the only reason there was a
length to run out of.

So the message is built where it is kept. `kest_diags_addv` and
`kest_diags_suggestv` take the `va_list` and format it into the arena, which
sizes itself from `vsnprintf`, and everything that raised a diagnostic through
a buffer of its own now forwards its arguments instead:

```
    va_list args;
    va_start(args, format);
    kest_diags_addv(parser->diags, KEST_SEVERITY_ERROR, code, span, format,
                    args);
    va_end(args);
```

There is one place a diagnostic is recorded now, `add_formatted`, and no
message anywhere in the compiler is built in a fixed buffer. A name six hundred
letters long comes out six hundred letters long:

```
error[K0306]: unknown name `aaaa...aaa`
```

— six hundred and fifteen characters of message, where before it was five
hundred and eleven and a half a name.

What is still cut is what a name is worth cutting for: the note that says what
a copy's types stand for counts what it left out, because eight of them is
noise however much room there is. That is a decision about the message. A
buffer is not.

**Runs:** `make check`, everything passing; the long name above through the
checker, and through a return type, both whole in `--json`.
## A line too long to show

The six-hundred-letter name from the last entry was printed twice: once in the
message, where it belongs, and once as the source line, with six hundred carets
under it. Nothing that reads a terminal is helped by that.

A diagnostic frame now shows at most a hundred columns of the line, and shows
them around the span, because the span is what the reader was sent there to
look at. What was cut off is marked:

```
4 |     aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa...
  |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
```

and when the span is late in the line, what is cut is the other end, with the
carets still under it:

```
5 | ...xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx + nowhere
  |                                                        ^^^^^^^
```

Twenty columns of what comes before the span are kept when there are any, so a
span is not against the left edge with its context thrown away.

A span that runs off the end of a line still carets to where it ends — that is
how a span over more than one line has always been shown — but one that runs
off the end of what is shown stops at the cut, because the mark after the line
has already said there is more.

A hundred columns is not eighty: a formatted file has no line this long, so
every line that reaches this came from a file the formatter could not read, or
from a machine, which can put a program on one line. Nothing in the tree
changed shape, which is what `make check` says by passing.

**Runs:** `make check`, everything passing; the long name above, a span three
hundred columns into a line, and an ordinary line, which prints as it did.
## A caret under a tab

The caret line was built by counting bytes and padded with that many spaces.
The terminal does not count bytes; it counts tab stops. A body indented with
two tabs came out like this:

```
5 |                 return missing
  |          ^^^^^^^
```

— the carets under nothing, six columns short of the word they were about.

A frame now shows a tab as the spaces it stands for, four of them to the stop,
and both lines of the frame are built by the same walk, so they agree whatever
the terminal would have done:

```
5 |         return missing
  |                ^^^^^^^
```

A tab inside the span counts too, so a span that has one is as many carets wide
as it was shown:

```
4 |     let x = "a  b" + 1
  |             ^^^^^^^^^^
```

Four columns rather than eight because that is what this language is written
with, and the number is only about how wide the line looks: where the caret
lands is not a guess, because the caret line is measured from the same walk
that wrote the line.

The window from the last entry still counts bytes when it decides where to cut.
A line long enough to be cut is a machine's, and a machine that writes a
program on one line does not indent it.

**Runs:** `make check`, everything passing; one tab, two tabs, and a tab inside
a text literal that a span covers.
## A caret under a letter that took two bytes

The number beside the path has counted characters since it was written —
`kest_source_locate` skips continuation bytes on purpose — and the caret line
counted bytes, so the two disagreed by however much of the line was not ASCII:

```
4 |     let x = "köprü" + missing
  |                         ^^^^^^^
```

The walk that writes the line and measures the caret line now counts a byte as
a column only when it starts a character, which is the same rule the number
uses. The caret is under the name, and the `4:23` beside the path is the column
it is at:

```
4 |     let x = "köprü" + missing
  |                       ^^^^^^^
```

A span with letters like these inside it is as many carets wide as it has
characters, not bytes:

```
4 |     let x = "köprü değil" + 1
  |             ^^^^^^^^^^^^^^^^^
```

What is still a guess is a character wider than one column — a Chinese one, or
an emoji — which this counts as one. Knowing better means a table of every
character's width, which is a dependency this project does not have. A guess
that is right for every file anybody has written in this language is worth
more than a table that is only needed for one nobody has.

**Runs:** `make check`, everything passing; the sum above, one with two-byte
letters inside the span, and one indented with a tab as well, which stays
lined up.
## Two names that look like one

Any byte over 127 starts a name, which is how a name in the writer's own
language works without carrying a table of every character there is. It is also
how these checked clean:

```
let ab = 1
let a​b = 2
```

— two names, one of them with a zero-width space in it, and nothing on the
screen to tell them apart. A name with a byte in it that is no character at all
checked clean too.

A file is now read as a whole before anything is made of it. It has to be
UTF-8, and every character in it has to be one that is on the screen:

```
error[K0107]: the byte `0xff` starts no character
error[K0108]: `U+200B` is a mark with no width
error[K0108]: `U+00A0` is a space that is not the space
error[K0108]: `U+FEFF` is a mark with no width
```

The last one is a file that begins by saying it is UTF-8, which every file here
already is, and it is told to save without the mark. The rest are told to take
the character out.

What is refused is a short list: spaces that are not the space, marks with no
width, marks saying which way the line reads, and the two line breaks that no
line ends with. What is not refused is a character somebody writes with —
`köprü` is a name, and `ı` and an em dash are in this tree already — because a
language that refuses those is one people have to write in a second language.

The check is a walk over the bytes rather than a lex: a byte that is no
character is reported where it is and the walk carries on at the next one, so a
file is told everything wrong with it at once.

**Runs:** `make check`, everything passing; a name with `0xff` in it, two names
apart by a zero-width space, a no-break space between two words, a file with a
byte order mark, and `let köprü = 1`, which is a name and stays one.
## A message nothing says

The reference showed what a diagnostic looks like:

```
error[K0104]: unknown function `printf`
```

K0104 is a number written without digits. There is no `unknown function` in
this compiler at all — an unknown name is K0306 — and the suggestion beside it
was invented too. It had been there since the reference was written, read every
time somebody read that section, and nothing checked it, because nothing could:
a message in a document is prose.

It is not prose. A code and a message are a promise that a run says them, so
`check-docs.sh` now holds every `error[Kxxxx]` and `warning[Kxxxx]` line in the
reference and the decisions to a message that code is raised with. It reads the
string literals out of `src/*.c`: a literal that is a code is followed by the
literal the message is formatted from, which is the shape of every call whether
it goes to `kest_diags_add` or through one of the wrappers. A `%s` in the
format stands for anything, so a documented message keeps its own names.

Seventeen of them are shown, sixteen were right, and the one that was not now
reads what a run of this compiler actually printed for a misspelt field:

```
error[K0307]: `player.Player` has no field `healt`
 --> player.kest:9:18
  |
9 |     let left = p.healt - amount
  |                  ^^^^^ did you mean `health`?
```

The drift this catches goes both ways, and the way it will happen again is the
other one: a message is reworded in the compiler and the document keeps the old
wording. So the backstop breaks it in that direction — it rewords `has no
field` in `check.c`, in a copy of the tree, and requires the check to say so.
That is the tenth hole, and the first that needed a tool to be given the
documents to read.

**Runs:** `make check`, everything passing, with the documentation line now
counting both; the reference with an invented message put back, which is
refused.
## The nearest name was never asked for

An unknown name has asked for the nearest match since it was written, and it
was asking the wrong list. A global is held under its module — `player.hurt` —
and a name is written the way it is reached, `hurt`, so every comparison was
between a short name and a long one, and nothing was ever close enough. Locals
were not looked at at all, which is where the misspelling usually is, and
neither were the names the language answers to itself.

```
error[K0306]: unknown name `healt`
  |            ^^^^^ did you mean `health`?

error[K0306]: unknown name `hurtt`
  |            ^^^^^ did you mean `hurt`?

error[K0306]: unknown name `pusht`
  |     ^^^^^ did you mean `push`?
```

Three lists, in the order a name is looked for: what the body declared, what
the language answers to on its own, and what is declared where this file can
reach. The last is compared on the part that was written the same way — the
piece after the last dot, when what was written has no dot in it — and
suggested the way it would have to be written:

```
error[K0306]: unknown name `uppar`
  |            ^^^^^ did you mean `text.upper`?
```

which is the mistake of leaving the module off, and it is one suggestion rather
than two messages. A module this file did not import is not suggested from at
all: it is not reachable, so a name from it is not what was meant.

The builtin list moved out of the message that used it, because two messages
read it now. `check-tables.sh` holds it in the same three places it always did.

**Runs:** `make check`, everything passing; a misspelt local, a misspelt
function in the same file, a misspelt builtin, a member written without its
module with the module imported, and the same without the import, which
suggests nothing.
## Which half of the name was wrong

A name under a module is two names, and when the one after the dot was the
misspelt one, both messages blamed the other half:

```
error[K0306]: unknown name `io`
error[K0344]: `text` is a type, and this wants a value
```

The first is what `io.prnt("x")` said, because the whole name was not found and
the parts were then checked one at a time, starting with a module that is not a
value. The second is what `text.uppar(t)` said, because a module can be spelt
like a type — `std.text` is imported as `text`, and `text` is a type — so the
half that was right resolved to the wrong thing.

A module is not a thing in this program: it is what the names under it have in
common. So the check is that something is declared under this name and this
file imported it, and then the mistake is the part after the dot:

```
error[K0353]: `io` has nothing called `prnt`
  |        ^^^^ did you mean `io.print`?

error[K0353]: `text` has nothing called `uppar`
  |                 ^^^^^ did you mean `text.upper`?

error[K0353]: `shape` has nothing called `Poimt`
  |                   ^^^^^ did you mean `shape.Point`?
```

The last is a type rather than a function, which is why what is looked through
is both lists: `shape.Point` is a type and `shape.zero` is not, and somebody
writing one of them has no reason to care which.

A local named after a module is still a value, and a module this file did not
import still says that it did not import it, which is a message of its own.

**Runs:** `make check`, everything passing; a misspelt function under a module,
a misspelt member of a module spelt like a type, a misspelt type under a
module, the same three spelt right, and a file that names a module it did not
import.
## A case that is nearly one it has

A case that is not there showed the whole declaration back — a note under every
case the enum does have, which is the right answer when nothing is nearly it,
and ten lines of noise when something is:

```
error[K0330]: `probe.Event` has no case `Idl`
9 |     let e = Event.Idl
  |                   ^^^ did you mean `Idle`?
```

A near miss is now suggested and the list is not shown, on the reasoning the
list is there for: it says what could have been meant, and a suggestion says it
better when there is one.

When there is not, the list is still shown — and it now says when it stops. A
diagnostic holds eight notes, so an enum with ten cases showed eight and let
the reader believe that was all of them:

```
11 |     Eight
   |     ^^^^^ this one it has, and 2 more
```

A set names bits, not cases, and the message says so now:

```
error[K0330]: `probe.State` has no bit `Movng`
  |                   ^^^^^ did you mean `Moving`?
```

**Runs:** `make check`, everything passing; a near miss and a name near nothing
in a two case enum, an enum of ten, and a bit of a set.
## The same list, and the same mistake in it

A type is held under its module and written without it, so `Poimt` was compared
with `probe.Point` and nothing was ever near enough. That is the defect of two
entries ago in the other list, and it is fixed the same way: compare the part
that was written the same way, and give it back the way it has to be written.

```
error[K0301]: unknown type `Poimt`
  |            ^^^^^ did you mean `Point`?

error[K0301]: unknown type `shape.Poimt`
  |            ^^^^^^^^^^^ did you mean `shape.Point`?

error[K0301]: unknown type `Poimt`
  |            ^^^^^ did you mean `shape.Point`?

error[K0302]: unknown generic type `Pairs`
  |            ^^^^^ did you mean `Pair`?
```

The third is the same misspelling as the first with the module left off as
well, and it is answered with the whole name to write. A module this file did
not import is not suggested from, as with names.

Both places that ask for the nearest type ask the same one thing, so a generic
gets it too, and the sentence about `ref<T>` and `store<T>` is what is left for
when nothing is near.

**Runs:** `make check`, everything passing; a misspelt type in this file, the
same one written under its module, the same one written without a module it
belongs to, and a misspelt generic.
## Two letters the wrong way round

Every suggestion in this compiler is allowed a third of the name in mistakes,
so a name of four letters is allowed one. Swapping two letters is the way a
word is most often mistyped, and it counted as two: a substitution each way.
So the shortest names — which is most of what the language answers to itself —
were the ones a swap could not be suggested for.

```
error[K0306]: unknown name `psuh`
  |     ^^^^ did you mean `push`?

error[K0302]: unknown generic type `Piar`
  |            ^^^^ did you mean `Pair`?

error[K0330]: `probe.Event` has no case `Idel`
  |                   ^^^^ did you mean `Idle`?
```

The count keeps the row before last as well as the last, and a pair standing
where the other one is costs one. Every suggestion in the compiler goes through
this one function, so all of them can see a swap now: names, types, cases and
bits, fields, module members, and the names on the command line.

**Runs:** `make check`, everything passing; a swapped builtin, a swapped
generic, a swapped case, and the longer names from the entries before, which
answer as they did.
## A word this language nearly has

`retrun 0` was answered with a caret under the `0`:

```
error[K0201]: expected end of line, found integer
4 |     retrun 0
  |            ^
```

which is the one thing in the line that is not wrong. A misspelt keyword is a
name as far as the lexer is concerned, so the statement is a name followed by
something that cannot follow a name, and the message is about the something.

The parser now asks the list it has been carrying all along:

```
error[K0201]: expected end of line, found integer
4 |     retrun 0
  |            ^
4 |     retrun 0
  |     ^^^^^^ did you mean `return`?
```

and the same for `lot x = 1`, `whlie x < 3 {`, and a declaration, where the
word itself is what the message is already about:

```
error[K0202]: expected a declaration, found identifier
3 | fnn main() -> i32 {
  | ^^^ did you mean `fn`?
```

There the sentence listing what a file holds is what is said when the word was
near none of them, because a reader who wrote something else entirely needs the
list and not a guess.

The distance moved to make this possible. The parser is above the types in the
pipeline and cannot call down to them, so what every stage measures a
suggestion with now lives beside the diagnostics: `kest_word_distance`, which
the lexer, the parser, the types, the checker and the machine all reach the
same way. That was a commit of its own, because moving a thing and using it are
two changes.

**Runs:** `make check`, everything passing; a misspelt `return`, `let` and
`while`, a misspelt `fn` at the top of a file, and a declaration that is near
no keyword at all, which is told what a file holds.
## The map and the tree

The pipeline in `CLAUDE.md` is the map of this tree, and it said three things
the tree did not:

- a module `str` for string interning, which does not exist and never did;
- no `kest` at all, though `src/kest.c` is there and `kest.h` is what a host
  includes;
- `diag` above `mem`, when `diag.h` includes `mem.h` — so the one rule the list
  is written to state was broken by the list itself.

That rule is also what stopped the last entry's work halfway: the parser cannot
reach the types, so the distance had to move. A rule worth planning around is a
rule worth checking, and this one was a sentence.

`check-tables.sh` holds it now. The pipeline names every `src/*.c` once and
nothing else, and every `#include "x.h"` in a module's own files points at a
module at or above it. The list is the order:

```
kest mem diag lexer ast parser loader types check contract value fmt compile
vm build main
```

Which is the corrected one, with no violations in the tree as it stands.

The eleventh backstop is the parser reaching down: `src/lexer.c` gains
`#include "types.h"` in a copy of the tree, which compiles perfectly well and
is caught.

**Runs:** `make check`, everything passing, with the tables line now counting
sixteen modules; a copy of the tree with an include the wrong way round, and
another with `str` back in the list, both refused.
## A count of the checks, and the checks themselves

`CLAUDE.md` said `make check` runs "the six tools below". It runs seven. The
count was written when there were six and nothing ever read it again.

The count is gone rather than corrected. It said nothing a reader needed: what
matters is not how many checks there are but that every one of them is named
where a reader meets it and reached for by the one thing that runs them. Which
is three lists — the files in `tools`, the names in `CLAUDE.md`, and the lines
in `check.sh` — and `check-tables.sh` now holds them to each other:

```
checks: `check-tables.sh` is in `tools` and is not run by `check.sh`
checks: `check-nothing.sh` is in `tools` and is not named in `CLAUDE.md`
```

The twelfth backstop is the first of those, and it is the hole that would hide
every other hole: a check that is written, named, and never run looks exactly
like a check that passes. `make check` says nothing about a check it does not
make. So a copy of the tree loses the line that runs `check-header.sh`, and
this has to notice.

**Runs:** `make check`, everything passing, with the tables line counting seven
checks; a copy with a `run` line deleted and a copy with a tool nothing names,
both refused; twelve backstops, all caught.
## A run that allocates nothing was refused

The premise of that line was wrong twice over: `examples/frame.kest` is exactly
a frame's shapes read rather than run, and `embed.c` calls `step` five times in
a row with the world it was handed back. Both were there before this entry.

What writing a frame-shaped program found instead is worth more. This was
refused:

```
error[K0401]: this allocates, and `probe.make` promises `no.alloc`
8 |     return P([1.0, 2.0])
  |              ^^^^^^^^^^
```

It allocates nothing. A run of a written length is laid out where it stands
(D064), so those two floats are the struct's own eight bytes, and what the
compiler emits says so — three `const`s and a `store.n`, and no instruction in
the set that could reach the heap:

```
  0000  const           0  ; 1
  0003  const           1  ; 2
  0006  const           2  ; 3
  0009  store.n         0  3
```

The contract walked the tree and called every array literal an allocation,
whatever its type said. So the two proofs this project makes about `no.alloc`
disagreed: the one over the tree refused a function the one over the emitted
code had nothing to say about. The tree was the wrong one.

The walk now asks the type the checker settled: a literal that is a run of a
written length allocates nothing, and one that can grow allocates. Which is the
one line that makes the language's own shape — a fixed run of floats inside a
struct, built and returned inside a frame step — writable inside the promise it
exists for.

`embed.kest` gained the case, because that is where the shape already lives: a
`Point` built from three floats inside `no.alloc`, moved, and measured.

**Runs:** `make check`, everything passing; a struct built from a fixed run
inside a promise, a `let` of a written length inside one, a frame step over a
store of bodies with flags, an enum of orders and fixed runs of floats — and
both growable literals, with and without a written type, which are still
refused.
## Which word on the line reaches the heap

`"score {n}"` inside a promise is refused and should be: text with a hole in it
is built, and what is built is on the heap. There is nothing in the language
that turns a number into text without it, and that is the promise doing its
job — a frame step gives numbers back and the text is written where it is
shown.

What was wrong is what the refusal said. `this allocates` names the line and
not the thing on it, and a line can hold several things:

```
6 |     return "score {n}"
  |            ^^^^^^^^^^^ text with a hole in it is built, and what is built is on the heap

4 |     let a = [1.0, 2.0]
  |             ^^^^^^^^^^ a run that can grow is one on the heap

4 |     push(a, n)
  |     ^^^^^^^^^^ `push` grows what it is given

4 |     return slice(t, 0, 2)
  |            ^^^^^^^^^^^^^^ `slice` copies the piece it names
```

Each site now carries what it was, and the six builtins that reach the heap are
a table with a sentence each rather than a chain of comparisons: `array()` and
`store()` make something that can grow, `push` and `add` grow what they are
given, `slice` copies the piece it names, and `text` copies the bytes it is
given.

The reference said "building an array is the only thing in the language that
reaches the heap", which has not been true for as long as text has had holes in
it, and its two diagnostics were written by hand rather than copied out of a
run: one carried a label — `reached through second -> third -> leaf` — that
nothing has ever printed. Both are real runs now, of a program written to
produce them.

**Runs:** `make check`, everything passing; text with a hole, a growable run,
`push`, `slice`, and a four hop chain from a promise to the line that breaks
it, which is the reference's own example and now its output.
## A block that belonged to the line that failed

The honest answer to that line is that the language needs nothing: a promise is
per function, so the function that makes the store is not the one that steps
the frame, which is how every example here is already written. `quests.kest`
builds its world in `main` and walks it in a `decay` that promises. There is
nothing to add and nothing was added.

What the turn found instead came from writing a program to check the things the
reference says: a whole number wraps at its width, a remainder keeps the sign of
what was divided, `(0 - 7) / 2` is `0 - 3`, a `continue` skips the rest of the
step and not the step after it, a deferred call runs after the answer is
settled, a `match` over an enum with payloads binds what it carries, an
optional that is nothing is nothing. Every one of them held. Two of my own
assumptions did not: two structs are not compared with `==` — the message says
to compare the fields that decide it — and `slice(t, from, count)` takes a
count and not an end.

The defect was in what the parser did with my mistakes. `if let none = f() {`
is refused, because `none` is a word and not a name, and then this:

```
error[K0202]: expected a declaration, found `let`
11 |     let x = 2
   |     ^^^ a file holds `module`, `import`, `const`, `struct`, `enum`, ...
```

— three lines further down, inside the same function, a message about what a
file holds. Recovery skipped to the end of the failed line, which threw away
the `{` that line had opened. Everything after it was one brace shallower, so
the `}` that ended the `if` ended the function, and the rest of the body was
read as declarations.

Recovery follows a block it opened now, to the brace that closes it. One
mistake reports one error, and the errors after it are the ones that are
actually there:

```
error[K0201]: expected identifier, found `none`
error[K0201]: expected end of line, found integer   (`retrun 0`, three lines on)
error[K0204]: expected an expression, found `}`     (`let z = 4 +`, six lines on)
```

**Runs:** `make check`, everything passing; a bad `if let` followed by two real
mistakes, all three reported and nothing invented; the same file cut off in the
middle, which ends at the end of the file rather than going round again; and
`let 3 = 1`, which is one error and was one before.

## Where a file ran out

A file that stops in the middle of a function was answered like this:

```
error[K0201]: expected `}`, found end of file
  --> rec3.kest
```

A path and nothing else. The end of a file is a token with no width, and the
renderer shows a frame for a span that has some, so the one message a reader
most needs a place for was the one message without one.

It has one now, just past the last character there is:

```
error[K0201]: expected `}`, found end of file
 --> rec3.kest:5:17
  |
5 |         return 1
  |                 ^
```

Past the last character rather than under it, because what is wrong is not the
`1`: it is that there was nothing after it. Trailing line breaks are stepped
back over, so the line shown is the last line with something on it, whether or
not the file ends with a newline.

This is in the parser rather than in the renderer, because a span with nothing
in it means two different things: the end of a file, and a diagnostic about a
whole program that has no line of its own. The second is still shown as a path
and a suggestion, which is what it is.

**Runs:** `make check`, everything passing; a file cut off inside a block, one
cut off with no trailing newline, one holding nothing at all, and one holding
only line breaks.

## A file that holds nothing

Three commands answered a file with nothing in it by printing nothing and
exiting nought: `parse`, `check` and `emit` — the last of them with a sentence
that was not true.

```
$ kest parse nothing.kest
// this file declares nothing
$ kest check nothing.kest
this file declares nothing
$ kest emit nothing.kest
nothing to run: nothing here has a body, and a function that takes types only
gets one where it is called
```

`emit` used to say "every function here takes types, and a copy is compiled
where one is called", which is a true sentence about a file of generics and a
false one about a file with no functions at all. Now it is one sentence that
holds for both.

`fmt` is the exception and stays silent, because what it writes is the file: a
file that holds nothing has to hold nothing after `kest fmt` writes it back.
That is checked too, the other way round — that its output is empty.

`check-commands.sh` reads the files in this tree, and no file in this tree
holds nothing, which is why none of this was noticed. It makes one now, in a
temporary directory, and holds the same five commands to answering it. The
thirteenth backstop takes the answer away again and requires the check to say
so.

**Runs:** `make check`, everything passing; a file with nothing in it through
five commands, a file of nothing but generics, a file that imports and declares
nothing of its own, which still lists what it imported, and every example,
which reads as it did.

## Which of the two reasons there is no `main`

A file with nothing in it and a file full of generic functions were told the
same thing: `this file has no \`main\` to run`. It is true of both and useful
about neither, because the two are nothing alike — one of them needs a `main`
written and the other needs a program written.

```
$ kest run nothing.kest
error[K0603]: this file declares nothing, so there is nothing to run
      add `fn main() { }`
```

Two messages under the one code rather than one message with a choice inside
it, so that each is a code beside the words it is raised with — which is the
shape `check-docs.sh` reads, and a message it cannot see is a message the
reference can quote wrongly.

`kest lex` on the same file prints `1:1 end of file`, which is the whole truth
about the tokens in it, and stays as it is.

`check-commands.sh` now runs that file as well as reading it: running a file
that holds nothing has to be a refusal, and the refusal has to say which of the
two reasons it was.

**Runs:** `make check`, everything passing; a file with nothing in it and a
file of nothing but generics, which are told different things, and every
example, which still runs.

## The call, and the line that says what it takes

`expected 2 arguments, found 1` named nothing and pointed nowhere. It now names
what is being called and shows where it was written:

```
error[K0309]: `probe.hurt` takes 2 arguments, found 1
 --> arg.kest:8:12
  |
8 |     return hurt(3)
  |            ^^^^^^^
 --> arg.kest:3:4
  |
3 | fn hurt(who: i32, amount: i32) -> i32 {
  |    ^^^^ declared here
```

The note carries no words about the parameters, because the line it points at
is the parameters. What the note is for is the second place: a call in one file
and a declaration in another is the case where a reader has nothing to go on.

Finding the declaration meant knowing that a function is compiled under a name
with what it takes written into it — `probe.hurt#i32,i32`, because two
functions may share a name — and that it is declared under the part before the
mark. Then the one to point at, of the several a name may be, is the one whose
type is being called.

Builtins have no declaration to show, and they now name themselves too:

```
error[K0309]: `push` takes 2 arguments, found 1
error[K0309]: `matches` takes 3 arguments, found 2
```

A call through a function value keeps the old words, because there is no name
and nowhere it was declared: the shape is all there is to say about it.

**Runs:** `make check`, everything passing; a call short of an argument, two
builtins called short, and every example.

## Which one was not written

`declared here` pointed at the line and left the reader to count along it. The
names are in the declaration, so the note points at the one that is missing:

```
error[K0309]: `probe.hurt` takes 2 arguments, found 1
8 |     return hurt(3)
  |            ^^^^^^^
3 | fn hurt(who: i32, amount: i32) -> i32 {
  |                   ^^^^^^ this one was not written
```

A symbol carries the declaration it came from now, which is what a type does
not: a parameter has a name where it is written and only a type after that.

The same question is asked of a struct built with too few fields, and it is the
same answer, in the file that declares the struct rather than the file that
built it:

```
error[K0309]: `shape.Point` has 2 fields, found 1
6 |     let p = shape.Point(1)
  |             ^^^^^^^^^^^^^^
 --> shape.kest:5:5
5 |     y: i32
  |     ^ this one was not written
```

One walk does both, because the difference between them is only where the
names are kept. Too many rather than too few is the other way round: the note
goes on the first argument there is nothing to take, in the file that wrote it,
and the declaration keeps the plain `declared here`.

A call through a function value has no declaration to read, and a copy of a
generic is compiled under a name nobody wrote, so both keep the line and no
names.

**Runs:** `make check`, everything passing; a call short one argument, a call
with one too many, a call short nine of ten — which says two and counts the
rest — a struct short two fields, one short a field across two files, and
every example.

## The one that was given the wrong thing

An argument of the wrong type was `this argument`, and a field of the wrong
type was `this field`. The declaration is already being read for the message
before it, and it holds what each of them is called:

```
error[K0310]: `who` expects `text`, found `i32`
8 |     return hurt(3, "x")
  |                 ^

error[K0310]: `health` expects `i32`, found `text`
9 |     let n = Npc(1, "x")
  |                    ^^^
```

The name comes out of the file that declared it and the caret is in the file
that wrote the call, which are not always the same file, so the two are read
from different places on purpose.

`this argument` is still what is said where there is no name to read: a call
through a function value, and a shape with no file behind it. A copy of a
generic keeps its names, because the copy is made from the shape and the shape
was written down — `Pair(1, 2)` where the second is `text` says `second`.

**Runs:** `make check`, everything passing; a call with two arguments the wrong
way round, a struct built with two fields the wrong way round, a copy of a
generic built wrongly, and every example.

## The names the reference already prints

`this field` is gone, taken by the entry before this one, and the rest of the
phrases turn out to be worth having: `this return`, `this arm`, `this binding`,
`this assignment`, `this constant`, `this end`. Each of them is what the thing
is, in a place where it has no name.

Four sites were not like that. The language's own functions have names for what
they take, printed in the reference — `slice(t, from, count)`, `find(t, needle,
from)`, `matches(t, at, needle)`, `rest(t, at)` — and the checker said `this
argument` at all of them:

```
error[K0310]: `from` expects `i32`, found `text`
error[K0310]: `needle` expects `text`, found `i32`
error[K0310]: `at` expects `i32`, found `text`
```

A reader who has read that line in the reference knows which one this is
without counting along the call, which is the whole of what a name is for.

The ones left alone are the ones the reference calls by a letter: `remove(a,
i)` says `this position`, which says more than `` `i` `` would.

**Runs:** `make check`, everything passing; `slice`, `find`, `rest` and
`matches` each given the wrong type in each place that has a name.

## A name is only worth the page it was learnt from

`from` in a message is worth more than `this argument` for one reason: the
reader has met `from` in the reference. Rename it on the page and the message
is worth less than what it replaced, because now it names something that is
called nothing.

So the four are a table beside the builtin list they belong with, and
`check-tables.sh` holds it against the signatures the reference prints — the
same shape the keywords are held in:

```
builtins: the checker calls slice's `t`, `start`, `count` and the reference
calls them `t`, `from`, `count`
```

which is what a copy of the tree says with either half changed. The reference
prints two forms for some of them, `find(t, needle)` and `find(t, needle,
from)`, and the longest is the whole of what it takes.

The tool also stopped answering a list that has moved with a stack trace. Every
table it reads is found by a pattern, and a pattern that matches nothing used
to raise a Python error at the reader; it says which file and which pattern
now.

**Runs:** `make check`, everything passing, with the tables line unchanged at
five lists; a copy with the checker's name changed, a copy with the
reference's changed, and a copy with the table renamed away, all three refused
and each saying which.

## What the command line answers to, and what it says it does

The row that line pointed at is about something else and is true: the names the
command line calls *in a program* — `main`, `onEvents`, `onEvent` — are a
`#define` each, and `RUN_CALLS`, `TICK_CALLS` and `EVERY_CALL` are built from
them, so a name cannot be spelt twice.

The commands themselves are the list nothing held. They are written in two
places — what `main` compares the first argument against, and what `help`
prints — and either can move without the other:

```
commands: `kest lex` runs and `kest help` does not say so
commands: `kest help` prints `explain` and nothing answers to it
```

which is what a copy of the tree says with a line taken out of the help text,
and with one added to it. A command that works and is not printed is one nobody
finds; one printed and not answered is a mistake in the first place a reader
looks.

`check-tables.sh` holds the two together, reading the second out of the body of
`help` rather than out of the file, because the file holds other lines that
begin with two spaces and a word.

**Runs:** `make check`, everything passing; a copy missing the line for `lex`,
and a copy printing a command nothing answers to, both refused.

## Calling one function, over every file there is

`call` was the one command nothing swept, because it needs the name of a
function and a list of names here would go stale. So the file is asked: the
first function it declares that takes nothing but numbers, text or a bool, with
nought for a number and a letter for text. That is a call on nearly every file
in the tree, and it found three things.

A name the compiler prints could not be pasted into the command that calls it.
`kest check` says `fn math.factorial(i32) -> i32`, and `kest call math.factorial
5` answered `no `math.math.factorial``, because the command line puts the
module in front of what it is given. A name already under its module is left
alone now.

A function that gives back a struct could not be called at all:

```
error[K0611]: `inline.identity` gives 5 slots back and this frame holds 2
```

which reads as the program's fault and was the command line's: it sized the
frame from the function's own slots, and a function is one slot however wide
the thing it gives. From what comes back, now — and the answer is the message
that was always meant for it, that there is no text for a `Transform`.

And a function that gives nothing printed nothing and answered nought, which is
the shape this tool exists to refuse. It says `nothing came back`, and `--json`
says `"result":null`, which is a call that happened rather than a call that did
not.

**Runs:** `make check`, everything passing, with `call` now swept over every
file that declares something it can call; a function giving a struct, one
giving nothing, one named under its module and the same one without.

## Nothing at minus one

Calling a function that takes types answered with the machine's own bookkeeping:

```
error[K0607]: there is nothing at -1 to call
      `kest_entry` gives -1 for a name the program does not define
```

Both lines are true and neither is about the file. `table<K, V>` has no body
until something calls it with types, so there is nothing compiled to call, and
the command line handed the machine the minus one it got back rather than
reading it.

```
kest: `table` takes types, and a copy of it exists where one is called
      write the call in a file and run that
```

A name that is not there for any other reason gets the other half of it —
`nothing in this program compiled ...` — which is the case that should not
happen and now says so plainly instead of walking into the machine.

**Runs:** `make check`, everything passing; a generic called from the command
line, an ordinary function called beside it, and a name that is three functions,
which still lists them.

## What the command line refused, said the way everything else is

Four refusals were written straight to standard error as `kest:` lines, so
`--json` said `{"diagnostics":[],"errors":0}` beside an exit status of one. A
tool reading that sees a command with nothing to report and a status that
disagrees.

They are diagnostics now — `K0624` to `K0629`, beside `K0603`, which is the
same kind of thing said about `main` — so they come out as JSON with the rest
and count towards the errors:

```
{"diagnostics":[{"severity":"error","code":"K0627","message":"`table` takes
types, and a copy of it exists where one is called","suggestion":"write the
call in a file and run that"}],"errors":1}
```

Being diagnostics gets them the rest of it for nothing. A name that is several
functions used to print a plain list; each of them is a note at the line that
declares it now, which is where somebody choosing between them has to look:

```
error[K0625]: more than one `sort.ascending` takes what was typed
  --> lib/std/sort.kest:24:4
   |
24 | fn ascending(a: i32, b: i32) -> bool no.alloc {
   |    ^^^^^^^^^ this one takes `i32`, `i32`
   ...
```

What is still a plain line is what happens before there is a program to say it
about: an unknown command, a command with no file, and running out of memory.
Those have no diagnostics to go in and nothing to point at.

**Runs:** `make check`, everything passing; a name that is three functions, a
function that takes types, a call with no function named, and one giving back a
struct that has no text — each as words and as JSON, with the status and the
count agreeing.

## A number nobody typed

`kest call examples/math.kest factorial notanumber` was already refused. The
one beside it was not:

```
$ kest call examples/math.kest factorial 99999999999999999999
```

which ran, with a number nobody typed. The read stopped at whether the whole
word was a number and never asked whether it fitted: `strtoll` gave back the
largest number it has, `ERANGE` went unread, and an `i32` parameter was handed
a count to loop to that a frame budget has no name for.

An argument now has to fit what it is being given to, in width and in sign, and
a float has to fit the width it is going into:

```
error[K0624]: no `probe.small` takes what was typed
      `256` does not fit in `u8`
      `-1` does not fit in `u8`
      `1e300` does not fit in `f32`
```

And where there is one function of that name, the refusal says which argument
it was and what was wrong with it, rather than a list of one and the reader
counting along it. `255`, `1.5` and `true` are read as they were.

**Runs:** `make check`, everything passing; a number past the end of a signed
read, a number past the end of a `u8` at both ends, a float past the end of an
`f32`, a word that is not a number, a word that is not `true` or `false`, and
the four beside them that are.

## Reading what came back

A host can ask what a function takes, where each argument starts, what comes
back and how wide it is. What it could not do is read what came back: the
writing of a value lived in `main.c`, behind a header a host does not include,
so every host but this one wrote its own.

`kest_gave_text` is the door. It gives the number of bytes the answer needs,
the way `snprintf` does, and fills what it was given:

```c
int64_t kest_gave_text(KestRuntime *runtime, int32_t entry,
                       const KestValue *frame, char *out, size_t room);
```

Minus one for a function that gives nothing, and for one that gives back
something the language has no text of its own for — a struct, a run, a store, a
reference. Those a host walks with `kest_frame_gives` and writes itself,
because what a program means by them is the host's to decide.

The command line goes through it now rather than past it. It carries sixty-four
bytes for an answer and asks again into the arena when the number says it needs
more, which is what the number is for: a four hundred letter answer comes back
whole.

`check-dead.sh` had the last word, and it was right: nothing outside `vm.o`
calls `kest_write_value` any more, so it is that file's own and not a
declaration in a header. A door for hosts, and one fewer for everybody else.

**Runs:** `make check`, everything passing; a number, a piece of text, a
function that gives nothing, one that gives a struct, and one that gives four
hundred letters, which is longer than the command line's buffer.

## The other host asks the same question

`embed.c` reads what came back now, twice over: once as this host makes of a
slot, and once as the program writes it for the type it declared. The two have
to be the same word, and the host stops if they are not.

```
what `step` gave, in the program's own words: 0
```

And a store is asked as well, which is the other half of what the door says: a
thing the language has no text of its own for answers minus one, and the host
that wanted one would have to walk it.

Writing that found a bug in yesterday's door within a minute. `kest_gave_text`
asked `kest_type_has_text` whether a type has words and passed nothing for the
place that says which type has none — and that function writes there whatever
the answer:

```
AddressSanitizer: SEGV on unknown address 0x000000000000
    #0 kest_type_has_text src/types.c:654
    #1 kest_gave_text src/vm.c:2473
    #2 main examples/embed.c:374
```

Nothing else would have caught it. The command line passes a place because it
wants the answer, and every call in the tree did until this one. Which is what
the second host is for, and why it is built under the sanitisers.

**Runs:** `make check`, everything passing, both hosts and the sanitised one;
the host's own words beside the program's, and a store, which has none.

## A place nobody chose

That line was wrong. `check-dead.sh` holds the public header to being used, by
the two hosts, and says so in its own first paragraph: "what a host cannot be
shown using is what nobody has run". Every one of the thirty-one is called by
`main.o` or `embed.o`, and a declaration that is not fails the run. Nothing to
do, and the thing worth writing down is that the claim was checked.

What the look around found instead is in the other form of a diagnostic. A
message about a whole file — nothing here takes events, this file declares
nothing — is shown as a path and no line, because there is no line it is about.
In JSON it said:

```
"file":"examples/math.kest","line":1,"column":1,"offset":0,"length":0
```

which is a place nobody chose. A tool reading that draws a marker at the first
character of the file, and the two forms are supposed to carry the same set.

A diagnostic with nothing in its span now says which file and no more, in both
forms. One with a span says everything it did.

**Runs:** `make check`, everything passing; a file that takes no events, a file
that declares nothing, and an ordinary error with its notes, which carries its
line, its column, its offset and its length as it did.

## What a tool reads, written down

The reference has said for a long time that `--json` carries the same set as
the words, and it printed the words and not one field of the JSON. A tool
writer had to run the thing and read the output, which is fine until a field is
renamed and their reading is a year old.

Two blocks now, copied out of runs: a diagnostic with a place, a suggestion and
a note, and what `kest check` adds — every type with its layout and its fields,
every function with what it takes and gives and where it was declared, and
every constant. With the two sentences that are not in either: `suggestion` is
there when there is one, a diagnostic about a whole file carries `file` and no
line, and one about the whole program carries no `file`.

Which is a promise, so it is checked. `check-docs.sh` reads every ```json block
in the reference, walks it, and holds every name in it to being a name a run
writes — collected from a program with one of everything in it and a program
with a mistake in it, through every command that answers in JSON:

```
docs/language.md:1730: nothing writes `col` into JSON
```

A name nobody writes is worse than no documentation at all: somebody builds a
reader for it and finds nothing there.

**Runs:** `make check`, everything passing, with the documentation line now
counting three things; a copy of the tree with `column` renamed to `col` in the
reference, which is refused.

## Both ways round

Forty-six names come out of this compiler in JSON. Thirty-four were somewhere
in the reference; twelve — `crossings`, `deep`, `frames`, `heap`, `hosts`,
`is`, `layouts`, `op`, `operands`, `parameterSlots`, `peak`, `pieces` — were
nowhere in it, so a tool reading them had read them off a run.

Three more blocks, copied out of runs: what `emit` adds, with a layout and its
pieces, the names the host must provide, what the machine needs and the
entries beside it, and a function with three instructions; what `tick` puts
back, with the crossings, what they gave and the peak between calls; and the
one line `fmt` answers with.

And the check goes both ways now. A name in a block has to be one a run writes,
and a name a run writes has to be one a block shows:

```
docs/language.md:1730: nothing writes `length` into JSON
docs/language.md: `width` is written into JSON and nothing shows it
```

which is what a copy of the tree says with `length` renamed in `diag.c`. Half
of that pair would let a field be renamed and the reference quietly go stale;
the other half would let a field be added that nobody is told about.

Making the second half pass took a third program in the tool: a run of `tick`
crosses into a handler, and a handler that calls anything the command line does
not bind never runs, so the file that gives `emit` a host to name cannot be the
file that gives `tick` its crossings.

**Runs:** `make check`, everything passing, five blocks held both ways; a copy
of the tree with `length` renamed to `width`, which is refused twice, once from
each side.

## A refusal nobody could read

It was worse than `tick`. A program that asks the host for a name this host has
not got was answered by `run`, `tick` and `call` alike with nothing at all, and
an exit status of nought — a program that never ran, reported as one that ran
and was fine.

The machine had said it. `kest_start` gives the machine a fresh set of
diagnostics to say what it says while running, `kest_runtime_new` wrote `the
host does not provide \`Nobody.here\`` into that set and gave back nothing, and
the set went out of scope with it. A refusal is asked of a machine, and there
was no machine to ask.

What a host has when there is no machine is the build, so that is where it goes
now:

```
error[K0606]: the host does not provide `Clock.now`
 --> ub.kest:3:11
  |
3 | extern fn Clock.now() -> i64
  |           ^^^^^^^^^
```

from all three commands, with an exit status that agrees, and as JSON with the
error counted.

`check-commands.sh` sweeps such a program now, the way it sweeps a file that
holds nothing: no file in this tree is one, because every extern here is a name
the command line binds, so nothing would have found this. The fourteenth
backstop drops the line that hands the words over and requires the sweep to
say a program the host cannot run ran.

**Runs:** `make check`, everything passing; a program wanting a name the
command line has not got, through `run`, `tick` and `call`, as words and as
JSON; fourteen backstops, all caught.

## The same words, for a host

Yesterday's fix put the refusal where the command line reads it. A host could
still not read it: `kest_build` writes diagnostics only when the build fails
and hands back nothing afterwards, so a host that got NULL from `kest_start`
had a build that knew why and no way to ask. `embed.c` did what any host would
do with that — `return 1`, silently.

`kest_build_report` is the question asked of the build, in the shape
`kest_report` already has: what has been said and not yet written, in the form
asked for, and nothing written twice. A host asking after every start is told
once and told nothing on the starts that worked.

```
error[K0606]: the host does not provide `Clock.now`
 --> ub.kest:3:11
  |
3 | extern fn Clock.now() -> i64
  |           ^^^^^^^^^
```

which is a host of nine lines, built against the header and the library and
nothing else, asking twice and being told once.

`embed.c` asks it now instead of leaving silently, which is also what holds it:
a declaration in the public header that neither host calls is one this project
refuses to keep.

**Runs:** `make check`, everything passing, both hosts; a nine line host that
starts a program wanting a name it has not got, and is told which name, at
which line, once.

## Walking through the door it built

Half of that line holds. In JSON the command line writes one object a file, and
the object carries what the program holds as well as what is wrong with it, so
it cannot be a call that writes an object of its own. In words there is no
difference at all, and the command line reaches into the struct for no reason
but that it can.

It goes through `kest_build_report` now. Which matters for one reason: a way in
that only ever runs when something has gone wrong for somebody else is a way in
nobody has walked through. `embed.c` calls it where a machine fails to start,
which is a path no run of `make check` takes; the command line calls it on
every run of every command that says anything, and every example, every sweep
and every backstop goes through it.

The probe that went with this found nothing: a copy of a shape inside a copy of
a shape, a `match` over an enum with payloads inside a text hole, a `defer` in
a loop with a `continue` past it, `i32` of a number too wide for it, `u32` of
minus one, and the text builtins the reference names, all answered as the
reference says. `f32(1) / f32(3) == f64(1) / f64(3)` is refused, which is the
one rule that made me write it twice.

**Runs:** `make check`, everything passing; the same words from `check`, `run`
and a program the host cannot start, and every example.

## How many events

`tick <file> [n]` decided what was a count by looking at the first letter of
the word: a digit made it a count, anything else made it another file. So `-3`
was a file, and the command said it could not read it; `2x` was read with
`atoi` and became two.

After the file, whatever is left is the count, whatever it is spelt like, and
it has to be a whole number in range:

```
$ kest tick events.kest -3
kest: between 0 and 65536 events
$ kest tick events.kest 2x
kest: `2x` is not a number of events
$ kest tick events.kest events2.kest
kest: `events2.kest` is not a number of events
```

The last of those is the rule the help has always printed: this command reads
one program. `8`, `0` and no count at all answer as they did, and the flags on
either side of the count still land where they belong.

These stay plain `kest:` lines rather than diagnostics, which is the rule the
entry two before this one drew: a mistake in the arguments happens before there
is a program to say anything about, and there is nothing to point at in a file.

**Runs:** `make check`, everything passing; a negative count, a count with a
letter in it, a count past the end, two files, a count either side of a flag,
and none.

## As many as were asked for

The events a `tick` lends were a static run of the largest number allowed:
sixty-five thousand `int32_t`, a quarter of a megabyte in the command line's
own bytes, there whether it was asked for one event or none. It was also
mutable state hanging off nothing, which the rules of this project say there is
none of, and it had been there long enough to stop being read.

They come out of the build's arena now, as many as were asked for, and the
whole of the number's meaning changed with it: the largest allowed used to be
the size of an array and is now how many this command is willing to lend at
once. Which is what it says above it, because the next reader will ask.

**Runs:** `make check`, everything passing; no count, none, eight, and the
largest allowed, which is a run of sixty-five thousand crossings and answers
the same number it did.

## Events somebody chose

`tick` counted up from nought and handed that over, so a program that reads
what it was given was measured against a list nobody wrote. It takes one now,
where the count goes:

```
$ kest tick events.kest 4,5,6
onEvents  1 crossing   returned 6
onEvent   3 crossings returned 6, peak 24 bytes
```

which is that program adding the events divisible by three, and 6 is the one.
`0,1,2,3` gives 3, the same as the count `4` does, because that is the run a
count makes.

A word with a comma in it is a list and a word without one is a count, so
there is no flag: `4,x` is refused as a list and `2x` as a count, each saying
which it was being read as. A list longer than the largest allowed is refused
the same way a count past it is.

Writing it went wrong once in a way worth keeping: freeing the list after the
argument loop, which is where every other thing the loop made is freed, freed
it before the run that reads it. What the run got was three events of nothing,
and the program answered nought — a right-looking answer to a question nobody
asked. It is freed on each way out instead.

**Runs:** `make check`, everything passing; three events written down, four
counted, the same four written down, a list with a letter in it, and a count
beside them all.

## One rule about what follows a file

Reading the arguments asked which command this was four times, in four
branches, and one of those branches did nothing: `call` collected what followed
into the files, and so did the branch below it for everything else. It was
there to keep the `tick` branches from firing, and they already ask whether the
command is `tick`.

So there is one question now — whether what follows the first file is one thing
of its own rather than more files — and one function that answers it. `tick` is
the only command that says yes.

Asking it in one place made a second thing sayable that was not before: being
given two of them.

```
$ kest tick events.kest 4 5
kest: `tick` takes one count, and was given `5` as well
```

which used to take the second quietly and use it.

**Runs:** `make check`, everything passing; two counts, a list and a count, one
count, a call with its function and argument, and a check over two files, which
are files and stay files.

## What was typed was nothing

`kest call file gcd` was answered with `no \`math.gcd\` takes what was typed`,
which reads as though something had been. Nothing had:

```
error[K0624]: no `math.gcd` takes nothing
      nothing was written after the name
  --> examples/math.kest:15:4
   |
15 | fn gcd(a: i32, b: i32) -> i32 {
   |    ^^^ this one takes `i32`, `i32`
```

Two messages under the one code rather than one with a choice inside it, so
each is a code beside the words it is raised with. The note is what it always
was and is now the answer: what to write.

**Runs:** `make check`, everything passing; a call with nothing after the name,
one with half of what it takes, one with all of it, and one to a function that
takes nothing, which still runs.

## The one form, inside a hole too

A hole holds code, and code in this language has one form. The formatter copied
the whole literal out as written, so the one place a program could keep its own
spacing was inside a string:

```
io.print("{measure(t,Kind.Two(3))}")     before
io.print("{measure(t, Kind.Two(3))}")    after
```

What is between the holes is still the author's — escapes, spaces, everything —
because a string's contents are not the formatter's business. What is in a hole
is printed like any other expression, with one rule of its own: it cannot
break. A text literal is one line by what it is, so a call that would have gone
over eighty columns stays on the line and runs long.

Nested text inside a hole keeps working, because it is text like any other and
goes through the same printing; an escaped brace stays an escaped brace; and
the programs answer what they answered.

Writing it went wrong first in a way the sanitised build would not have caught:
the case for a text with holes shared its line with numbers, names and strings
without them, so giving it a body gave them all one, and every literal in the
tree came out as `""`. It reads like a formatter with nothing left to say.

**Runs:** `make check`, everything passing, which reformats the whole tree and
finds it unchanged; a file written badly on purpose, whose holes come out
spaced and which formats to itself; holes with operators in them, a hole
holding text holding a hole, and an escaped brace beside a hole, all of which
run and answer what they did.

## Where a chain breaks

Fourteen lines in this tree run past eighty columns. Most are text with holes
in them, which cannot break and is the point of the entry before this one. Two
were not: a condition and a sum that the formatter could have broken and did
not.

A chain of one operator was not a chain. `a || b` has one operator, the rule
asked for more than one, so a condition with a single `||` in it ran as long as
it liked — and the line was then broken somewhere worse, inside the call on the
right of it:

```
    if wide(alpha, beta, alpha, beta, alpha) == 7 || wide(
                beta,
                ...
            ) == 8 {
```

One operator is a chain, and the shape is what D003 is for: the operator ends
the line, and the line that ends in one continues.

The decision moved as well. It used to be made before anything was printed,
from the flat width of the whole expression, which is wrong when the left side
has already broken inside itself: `math.abs(...)` over three lines put `>` and
`0.0001 {` on two more, with thirty columns spare. It is made after the left is
printed now, about what is left to print — all of them or none, like a list,
because half on one line and half on the next is the arrangement nobody asked
for.

Measuring it from inside itself is how this went round forever the first time,
which the comment beside `fits` already warned about; the measure is taken only
where it can be acted on.

Two files in the tree changed shape and both read better: an `if` with a `||`
in it that was eighty-five columns is two lines that fit, and `1 + math.max(a,
b)` breaks after the `+` rather than exploding the call underneath it.

**Runs:** `make check`, everything passing, which reformats the whole tree and
finds it as written; the two files rewritten, both of which still run and
answer what they did.

## The brace at the end of the line

A condition is never the end of its line. What follows it is a brace, or the
arrow of an `if` that gives a value, and the formatter measured the condition
against the whole eighty as though the line ended there. So a call that ended
exactly at eighty was left alone and the line came out at eighty-two.

The printer carries what is going to follow now — two columns for a brace, four
for an arrow — and what a line may hold is the limit less that. One line in the
tree changed, the one that was eighty-two, and it breaks as any long call does.

Twelve lines are left over eighty. Ten are text with holes in them and one is a
comment, both of which are the author's. The twelfth is not.

**Runs:** `make check`, everything passing, which reformats the tree and finds
it as written; the file that changed, which still runs and says what it said.

## The one place a break can go

The formatter had nowhere to put a break in an `if` that gives a value, and
neither did anybody writing one by hand. Both of these were refused:

```
let x = if c ->
    1
else -> 2

let x = if c -> 1
    else -> 2
```

because a line that ends in a value ends the statement, and nothing can begin
one with `else`. Which is exactly why the parser can look past a line break for
it: no program is taken from anybody, because no program could have used that
word there. So it does, and only for that word.

The formatter breaks there when the line will not hold the whole of it, and
nowhere else:

```
    let rounded = if scaled >= 0.0 -> i64(scaled + 0.5)
        else -> i64(scaled - 0.5)
```

Eleven lines in this tree run past eighty columns now, and every one of them is
text with a hole in it or a comment — the author's, both of them, and nothing
the formatter has an opinion about. The five entries before this one started at
fourteen.

**Runs:** `make check`, everything passing; the two shapes above, which run and
answer; `std.text` rewritten and every example that uses it; and the reference,
which gained the shape and is held to it parsing.

## After the arrow

An arm that runs long had somewhere to go all along: a line ending in `->`
carries on, so the value of an arm has always parsed on the next line. Nothing
wrote it there.

```
        Locked(key) ->
            if with == key -> Door.Shut else -> Door.Locked(key + alpha)
```

One line in the tree changed, and it changed for the better twice over: it used
to break in the middle of the chain it holds, because that was the only break
the formatter knew, and now it breaks after the arrow and the chain fits whole
on the line under it.

The reference needed correcting while I was there. It said what is inside a
string, "including the expressions in its holes", is left exactly as written,
which stopped being true three entries ago. And it now says the rule the last
three entries have been discovering one case at a time: where a line cannot
hold what is on it and there is one place a break may go, it goes there.

**Runs:** `make check`, everything passing; a match with three arms of
different lengths, `tree.kest` rewritten and run, and the reference, whose
blocks still parse.

## A comment about the thing it was written on

Nothing was dropped, which is the good half. The other half is where they
ended up. A comment goes above the first thing that starts after it, and things
start after a comment that was written in the middle of one:

```
    let x = 1 // trailing          ->    // trailing was here, above `return`
    return ...

    return a + // carried          ->    the comment came after the `return`
        b

    Open(w) ->                     ->    after the whole `match`, at the end
        // the width matters             of the function
        w
```

Each of those is the author's words moved onto something they were not written
about, which is worse than losing them: a reader believes them.

Everything written on the line a thing starts on was written about that thing,
so it goes above it. And a thing that is printed as one line however many the
author wrote it over — an arm that gives a value — takes everything written
inside it the same way.

A comment lifted out of the middle of something keeps no blank line above it,
because the blank line above it was never there. That was the first version's
mistake: the comment came out two lines below the arm above, so the gap read as
one the author had left.

**Runs:** `make check`, everything passing; a file of comments in every awkward
place, which formats to itself, runs, and now says each of its comments about
what it was written about.

## What a formatter is allowed to lose

It can be written at the end of a line, and after the formatter it is not: a
comment shares a line with nothing. That is D207, written down now, with the
reason it is one place rather than two.

Which left the question of what holds it. The formatter is held to writing the
same program, and a comment is not the program — every promise `check-fmt.sh`
makes would still be kept by a formatter that quietly dropped what a reader was
told. So it holds one more: every comment in a file is in the file the
formatter writes, in the order it was written.

Over the tree that is nearly free, because the tree is already in the one form.
The file it is really about is one nobody has formatted, so the tool writes one:
a comment at the end of a line, inside a signature, inside the value of a match
arm, in an empty block, and after the last statement. It formats it, compares
what was said, and runs the result.

The fifteenth backstop is a formatter that keeps every comment but the ones
written at the end of a line — three lines in `fmt.c`, everything else about it
still true — and the check says `comments changed`.

**Runs:** `make check`, everything passing; a file with comments in every place
one can go, which formats, keeps all of them, and runs; a copy of the tree with
those three lines in it, which is refused; fifteen backstops, all caught.

## Two slashes that begin nothing

The check written yesterday read a file for comments by looking for two
slashes. `"http://kest"` holds two slashes and begins nothing, which the
formatter has always known — the comment above `scan_comments` says so and is
the reason that function exists rather than a search.

It matters more than a miscount. The check compares what was said in order, and
a comment written at the end of a line is moved above it, so a line holding
both a string with slashes in it and a comment comes out as two things in the
other order. The check would have called that a formatter losing what somebody
wrote, on a file where nothing was lost at all.

So the tool reads a file the way the formatter does: step over the strings, and
what is left that begins with two slashes is a comment. The file it writes for
itself now holds a line with both:

```
    let where = "http://kest" // and a comment may follow one
```

which the old reading refuses and the new one does not.

**Runs:** `make check`, everything passing; the tree, the file nobody had
formatted, and a copy of the tool with yesterday's reading put back, which
calls the formatter a liar about a URL.

## One reading, and a second that has to agree with it

What a comment is was decided in two places: the formatter scanned the file for
itself, and the check written the day before scanned it again. A check that
sees fewer comments than the formatter writes finds nothing wrong with a
formatter that drops the ones it cannot see.

The reading moved into the lexer, which is where what a file is made of is
decided, and the formatter asks it. `kest lex --json` answers it too, which is
worth having on its own: a comment is not a token, so the text form — which
prints tokens — is not where they are, and anything that folds them or gathers
them was reading the file itself and getting the slashes inside a string wrong.

```json
{"comments": [{"line": 3, "column": 1, "text": "// above the struct"}]}
```

The check keeps its own reading, because a check that asks the thing it is
checking has checked nothing. What it does now is compare the two:

```
read 0 comment(s) and the compiler read 4: examples/math.kest
```

which is a copy of the tree whose reading stopped seeing a comment written at
the end of a line. Two readings, and neither can go quiet without the other
saying so.

The documentation check earned its keep on the way past: adding a field to the
JSON refused the run until the reference showed it, and then until the
programs it runs had a comment in them for `text` to appear at all.

**Runs:** `make check`, everything passing; a file of comments in every place,
which formats as it did; a copy with a reading that misses trailing comments,
which is refused; and `kest lex --json` over the tree, which agrees with the
tool on every file.

## Two numbers nobody chose

Four thousand and ninety-six comments, and thirty-two operators in a chain. A
file with four thousand two hundred comments came out with four thousand and
ninety-six, and nothing said which two hundred and four were gone. A chain of
forty came out as eight on one line and one on each line after — a chain of
thirty-two whose left side was the rest of itself.

Both are gone. The comments are counted first and the run is as long as the
answer; the chain is walked twice, once to count and once to fill. Both come
out of the arena the formatter is already writing into, which is thrown away
when it is done.

A file bigger than either number is what `check-fmt.sh` writes for itself now,
beside the file with a comment in every place: four thousand two hundred
comments and a chain of forty. Formatted, its comments compared, formatted
again to see it settled, and run. A copy of the tree with the old number put
back says `comments changed: a file with more of them than fitted`.

**Runs:** `make check`, everything passing; the big file through the tool, the
tree, and a copy carrying the number that used to be there.

## A promise broken thirty calls down

The path from a promise to the line that breaks it was sixteen hops long. A
program that breaks one further down than that was told:

```
error[K0401]: this allocates, and `deep.frame` promises `no.alloc`
129 | fn frame(x: i32) -> i32 no.alloc {
    |    ^^^^^
```

pointing at the declaration and saying `this allocates` of a body that
allocates nothing at all. The refusal was right — the graph knows — and
everything it said about where was wrong.

The path is as deep as the graph now, which is what it costs: a path cannot be
longer than the number of functions, because a function on it is not walked
into twice. So it points at the `array()`, thirty calls down, and says which
one of the language's own functions it is.

What is left is the notes, and there are eight of them. The path is shown from
the promise down and the last note there is room for counts what is under it:

```
 24 |     return step6(x)
    |            ^^^^^^^^ which calls `deep.step6`, and 24 calls under that
```

A path that stops without saying so reads as a path that ended, which is the
same mistake the list of an enum's cases made two weeks ago and was fixed the
same way.

**Runs:** `make check`, everything passing; a promise broken fifteen, twenty
and thirty calls down, and the four hop chain from the reference, which reads
as it did.

## The ninth note

Three callers count what they leave out and say it in their own words. The rest
did not, and the one that shows it is `kest call` on a name that is nine
functions: eight of them were listed and the ninth was gone, with nothing to
say it had been there.

The counting moved to where the dropping happens. A diagnostic knows it had no
room, so it says so — under the notes in words, and beside them in JSON:

```
31 | fn take(a: bool) -> i32 {
   |    ^^^^ this one takes `bool`
   and 1 more place
```

The three that count for themselves are unchanged, because what they have to
say is better: `and 24 calls under that` is about calls and not places, and it
is said at the last note rather than after the list. They keep room for
themselves, so nothing is left out and the new line says nothing.

The documentation check earned its keep twice more. A new field refused the run
until the reference showed it, and showing it needed a program with nine
functions of one name for a run to produce one — which is now the fourth
program that check writes for itself, and it brought `suggestion` in with it,
which the reference had described in prose and never shown.

**Runs:** `make check`, everything passing; a name that is nine functions, a
promise broken thirty calls down, and an enum of ten cases, which say what they
said.

## Eight places, or nine functions

Counting what there was no room for is right for a place — the reader knows
there is more to look at, and looking is what the frame was for. It is wrong
for a menu. A name that is nine functions, listed as eight and a count, hides
one somebody could have called and does not say what it takes.

So past the room there is, `kest call` stops pointing and says them:

```
error[K0624]: no `many2.take` takes nothing
      nothing was written after the name, and they take (`i32`), (`i64`),
      (`f32`), (`f64`), (`u8`), (`u16`), (`u32`), (`bool`), (`text`)
```

and under that, the frames stay, because for three of them where each is
written is what somebody choosing has to look at anyway.

Which left the counting with nothing to count, and a documented field nothing
writes is what this project refuses to keep. The cases of an enum give it
something: they used to count their own tail, in their own words, and a case
that is not shown is a line in a declaration — a place, which is what a count
is right for. So they are counted by the diagnostic now, one rule instead of
two, and the ten-case enum in the documentation check's own programs is what
makes `leftOut` a thing a run writes.

Adding that enum found three more names nothing had shown: `cases`, `tag` and
`carries`. The reference had a struct in it and no enum, so what `kest check`
says about one was undocumented from the day it was written.

**Runs:** `make check`, everything passing; a name that is nine functions and
one that is three, an enum of ten cases, and the reference, which now shows an
enum.

## Marks round the whole of a thing

Three shapes for one sentence. A note said a function takes `` `i32`, `i32` ``,
a list said `` (`i32`) ``, and `kest check` prints `(i32, i32) -> i32`. The
third is what the language writes; the other two were a message making up its
own punctuation, and the commas in the first belong to neither.

One shape now: what a function takes is `(i32, i32)`, written the way it is
written in the language, in one pair of marks put there by whoever says it.
`()` for a function that takes nothing, which the first shape could not say at
all.

```
   |    ^^^^^^^^^ this one takes `(text, text)`
      nothing was written after the name, and they take `(i32)`, `(i64)`, ...
```

A list of several is still a list of marked things, because each of them is
whole. What the rule refuses is the marks falling inside one, which is the
difference between a comma the language wrote and a comma the sentence did.

**Runs:** `make check`, everything passing; a name that is three functions and
one that is nine, and a call to something that takes nothing.

## A suggestion that does not compile

`kest_type_shape` writes what a reader would have to write: `Pair<A, B>`, with
the module left off when it is their own file's. It wrote it into a hundred and
twenty-eight bytes of the caller's, and the comment above it says what that
costs — "a suggestion showing one type for a shape that takes two is a
suggestion that does not compile" — which is exactly what came out:

```
let b: BBBB...BBBB<Alpha> = ...
```

for a shape taking two, on a name long enough to fill the buffer. The reader is
told to write something the compiler will refuse.

It is built in the arena now, sized from the name and the type names, so it is
as long as it is. Both suggestions that use it read the same for ordinary names
and are whole for the ones that are not.

**Runs:** `make check`, everything passing; a shape of three types with a name
of forty-six letters, one of two with a name of a hundred and twenty, and
`Pair<A, B>` written with none of its types, which says to write them.

## Two copies, one name

It is not a message, so it does not cut a sentence in half. It cuts a program:

```
$ kest run collide.kest ; echo $?
2
```

Two copies of one generic, told apart by the types they were given, whose names
agreed for two hundred and fifty-six bytes. They were compiled under one name,
the second was the one that ran, and it read the first one's struct as its own.
The answer was wrong and nothing said a word.

Three names were being built in buffers and all three are built in the arena
now: the name a copy is compiled under, the name one of several functions of a
name is compiled under, and the name a file's own module puts in front of what
it declares. The last had a different symptom and the same cause — a name too
long to join was not looked up at all, so a struct with a two hundred and fifty
character name was unknown in the file that declared it.

Then the net. A module holds one function per name, and two of them is this
project having built one of those names wrongly, so `kest_module_add` refuses
and the compiler says it in the words it keeps for its own mistakes:

```
error[K0505]: two functions are compiled under `probe.held#T$probe.AAA...`,
which the checker allowed
```

The sixteenth backstop cuts each type name to twenty characters in a copy of
the tree and requires that to be caught. Until today nothing would have: it was
a program that answered wrongly and exited nought.

**Runs:** `make check`, everything passing; two copies whose type names agree
for two hundred and fifty characters, two functions of one name taking types
that agree that far, and both again with the cut put back, which is refused
rather than run.

## What a type is called

The last of the buffers that a name was built in, and the one every other name
asks. `[T]`, `[T; N]`, `ref<T>`, `store<T>`, `T?` and `fn(...) -> T` were
written into two hundred and fifty-six bytes and what fitted came back:

```
`a` expects `[probe.CCCC...CCCC`, found `i32`
```

— a type whose name ends without its closing bracket, and a name that is not
the type's. The second half of that is the half that matters: this is the name
a copy of a generic is compiled under, and the entry before this one is about
two of those being the same name.

Sized from what it is made of now, in the arena, all of it. `[i32]`,
`ref<probe.Npc>`, `store<probe.Npc>` and `[f32; 3]` read as they did, and the
three hundred letter one reads as itself.

**Runs:** `make check`, everything passing; every composed shape the language
has in one message, and a struct whose name is three hundred letters inside a
run.

## The shape a host got wrong

When a host lays a struct out differently from the program, the machine shows
both. What it showed for a struct of five fields with long names was this:

```
   |        ^^^ `theFirstField...: i32` at 0, ..., `theFourthField...:
```

— a field with no type, no offset, and no `and 1 more` after it, because the
count is written at the end of a list that had already run out of room. The
reader is comparing two declarations, and the one thing they cannot do is
count the fields of the one in front of them.

Sized from the four fields it shows, in the arena. The list is whole and the
count is there:

```
`theFirstFieldWithAnAwkwardlyLongName: i32` at 0, ... , and 1 more
```

Shown by a host of thirty lines built against the header, laying out five
fields where the program lays out five and padding one of them: the machine
refuses the lend and says what each side holds.

**Runs:** `make check`, everything passing; the host above against the same
program built twice, once with the buffer put back — which cuts a field in
half and loses the count — and once without.

## The two the machine had

`kest_name_written` takes the name a function is compiled under and gives back
the name a file wrote: what is before the `#`. It wrote it into a hundred and
twenty-eight bytes of the caller's, and a copy of a generic is compiled under a
name longer than that — so the trace of a failed call, the refusal of a promise
the machine watched being broken, and the message about a frame too narrow all
named a function that is not the one they are about. It gives back a piece of
the name it was handed now, in the arena, all of it.

The other was the list a host is given when it asks for a name that is several
functions. Two copies of one generic, with type names of a hundred and twenty
characters:

```
ask for one of them: `copies.held#T$copies.DDDDDDDDDDDDDDDDDDDDD      before
ask for one of them: `copies.held#T$...One`, `copies.held#T$...Two`   after
```

The first ends in the middle of the first name, and the second one — the one
the host has to ask for if it wants the other copy — was never said at all.

**Runs:** `make check`, everything passing; a host of twenty lines asking for a
name that is two copies, against the same program built twice, once with each
buffer put back.

## Two copies that were one type

A copy of a shape is found by its name and its name is built from the types it
was given, so a name cut short is two copies being one struct:

```
error[K0310]: `held` expects `boxes.EEEE...One`, found `boxes.EEEE...Two`
```

which is a program refused for holding exactly what it holds. `Box<...One>` had
been made, `Box<...Two>` was asked for, the two names agreed for two hundred
and fifty-six bytes, and the second was handed the first.

Built in the arena and sized, like the four before it. Then the net, because
this one can be checked: a copy found by name holds the types the name was
built from, so if the one found holds different ones, the name is wrong.

```
error[K0354]: two copies of `table.Table` are one type, which the naming of
them allowed
```

The first version of that check compared the types by which object they were,
and `std.table` refused to compile: two `K`s bound in two rounds are two
objects and one type. The second compared by what they are, and `std.table`
still refused, because `kest_type_equal` says nothing useful about two type
parameters. The third compares their names — which is what the key was built
from, and therefore the one comparison that is exactly the rule being checked.

The seventeenth backstop cuts each type name in that key to twenty characters
and requires `K0354`.

**Runs:** `make check`, everything passing; two copies whose type names agree
for two hundred and fifty characters, the same at a hundred and thirty which
always worked, `std.table` and the example that uses it, and a copy of the tree
with the cut put back.

## Eight names, and the ninth

A shape with nine type names was told `unknown type \`I\`` about the ninth of
them — a name it had declared between its own angle brackets. The limit was
this compiler's and the message was about the program.

Eight was written in eight places: what the program has bound while a generic
is resolved, what a copy of a function was given, the names a shape's fields
are resolved against, the arguments a use writes, and the copies they are
substituted into. Two of them clamped and dropped the rest; two wrote past the
end of a run of eight without looking, which is the sanitised build's business
and had never been asked the question.

All of them are as many as were written now, out of the arena the program is
already in. A shape of nine types works; one of twelve works; the whole tree
reads as it did.

**Runs:** `make check`, everything passing; a struct of nine types with a field
for each, and one of twelve with a generic function over it that gives the
first back.

## The numbers a program can run into

That line was wrong twice. The reference has matching several things at once,
with an example of five arms over nine combinations, and it has the limit as
well — in a table of every number a program can run into, under a heading that
says why they are written down: "a number a program can run into belongs where
somebody can read it, rather than only where it is enforced".

What nothing held was the table itself. Seven numbers in a document, five
`#define`s and two sentences in the compiler, and no way to find out that they
had come apart except by running into one.

`check-tables.sh` holds them now, both ways:

```
limits: the compiler holds a program to 24 and the reference does not say so
limits: the reference says 16 and nothing holds a program to it
```

which is a copy of the tree with `MAX_LOOPS` doubled, and another with the
table's `8` written as `9`. `UINT16_MAX` is read as the number it is, because
that is what the message prints.

**Runs:** `make check`, everything passing; a `match` over two enums, which
runs; a `match` over nine things, which is refused with the number in it; and
the two copies above.

## A number written once

Most of the messages take their number from the thing that enforces it —
`"a function holds at most %d names", MAX_LOCALS` cannot drift from what it is
about. Two did not. `a match chooses between at most 8 things` had the `8`
written beside a `subjects[8]` two lines above it, and `between one and 65535`
had it beside a `how_many > 65535`. In each of those, one number is enforced
and the other is a sentence, and nothing but a reader held them together.

Each is written once now — `MAX_SUBJECTS` and `MAX_ELEMENTS` — and the check
reads the definitions rather than the sentences it was reading before, which is
the same thing said the other way round: what a check reads should be what the
compiler obeys.

**Runs:** `make check`, everything passing; a `match` over nine things and a
`[i32; 70000]`, both of which say their number; and a copy of the tree with
`MAX_SUBJECTS` set to twelve, which the table refuses.

## The one the table did not have

```
error[K0333]: this `match` has 343 combinations to answer, which is more
than 256
      `else` answers the rest in one place
```

Seven colours over three subjects, and a limit a program can run into that the
table of limits did not have. It got in under the check written yesterday
because the check compares numbers and 256 was already there for something
else — names in a function.

The table has the row now, and the check reads every `MAX_` the three files
that check and compile a program hold, rather than the ones somebody thought to
list. The machine's own and the command line's are not those: how deep the
calls go and how many events a run makes are not numbers written in a program.

A copy of the tree with `MAX_COMBINATIONS` set to three hundred is refused.

**Runs:** `make check`, everything passing; a `match` over three of a seven case
enum, which says its number, the same with an `else`, which runs, and the copy
above.

## What to do instead, for the thing in front of you

The machine's numbers are the host's: a host that gives sixteen frames is told
`calls nest more than 16 deep`, not the number in the header. They are not the
language's limits and the table is right not to have them, which the check
already enforces from the other side — a number in the table that nothing holds
a program to is refused.

So the turn went to a program written against the whole of the standard
library, and what it found was a message. Comparing an optional:

```
error[K0314]: `==` does not apply to `i32?`
  |        ^^^^^^^^^ compare the fields that decide it
```

An optional has no fields. Neither has an array, a store, or a reference, and
all four were told to compare the ones that decide it. What each of them should
be told is different, and is now what it says:

```
`if x == none`      take what it holds out with `if let`
`if a == b`         walk them and compare what they hold
`if r == r`         read what they name with `get` and compare that
`if P(1) == P(1)`   compare the fields that decide it
```

The library itself held: `text`, `math`, `sort`, `table` and `vec` all answered
what they were asked. Three of my calls were wrong — `sort.by`, `table.put`,
`vec.length2` — and each was answered by name: "`sort` has nothing called
`by`".

**Runs:** `make check`, everything passing; a program over the whole library,
and the four things that do not compare, each with its own advice.

## The stammer

```
sort.sort(words, sort.ascending)      sort.by(words, sort.ascending)
table.table()                         table.empty()
```

Nothing was wrong with the first column. Both resolve, both are unambiguous,
and the compiler never minded. They are read far more often than they are
written, and both read as a stammer.

`by` says what the second thing is for, and `empty` says what comes back. Four
examples and the reference say them the new way, and `std.table` is the one
place in the tree that made a table other than by writing `table.empty()`
itself.

This is the sort of rename that costs nothing today and is refused a year from
now for the sake of what somebody wrote.

**Runs:** `make check`, everything passing; the four examples that sort or hold
a table, which answer what they answered.

## The usual order, for anything that has one

`sort.ascending` was three functions — one for `i32`, one for `f32`, one for
`text` — and sorting a `u8` meant writing a fourth yourself. It is one now:

```kest
fn ascending<T>(a: T, b: T) -> bool no.alloc {
    return a < b
}
```

which works because a copy is checked as the type it was asked for. A `[u8]`
and a `[f64]` sort with it; a `[P]` for a struct with no order is refused in
the copy, at the line that asked for it.

Writing it that way needed the language to grow something. A generic could be
called and not handed over, so `sort.by(items, sort.ascending)` was refused —
which copy of `ascending` is meant cannot be told from the name. It can be told
from where it is going, and now is: a generic named where a function type is
wanted is the copy that fits, made the way a call makes one (D212).

Three things had to move for that. A name that is several functions no longer
answers with a generic one, because a shape full of type names compares equal
to every shape wanted. Which pass of a call an argument is settled in is
decided by what the parameter is rather than by what came back for the
argument, because a generic named where a function is wanted comes back as an
error until it is asked again with the shape in hand. And a module with nothing
but generics in it says `nothing to run` even when it has a layout to show,
because `std.sort` is now exactly that.

**Runs:** `make check`, everything passing; the four examples that sort, a
`[u8]` and a `[f64]` sorted by the library's order, a struct that has none,
which is refused where it was asked for, and a program over the whole library.

## What the reference says about handing one over

That a file of generics compiles to nothing is not a defect: a copy exists
where one is called, which is D040, and `kest check` prints the shapes with
their type names in them. Nothing to do there.

What was missing is the page. Yesterday's rule — a generic named where a
function type is wanted is the copy that fits — is what lets a library say "the
usual order" once, and the reference did not have it. It does now, with the two
places the shape comes from: a parameter, and a `let` that says what it holds.

The turn's checking went into the rule itself, in the shapes I had not tried: a
generic named at a `let`, a generic passed inside another generic — where which
copy of `ascending` is wanted depends on which copy of the caller is being
checked — and a `[u8]` and a `[f64]` sorted by a library that names neither.
All four hold.

**Runs:** `make check`, everything passing, with the reference's blocks now
sixty-eight; a generic named at a `let`, one passed from inside another
generic, and a struct handed to `std.table`, which is refused for having no
`hash` — in the copy, with what its type names stand for.

**Next:** that last refusal names two lines in `std.table` and none of the
reader's. The copy of `slotOf` was asked for by `find`, which was asked for by
`set`, which is the line the reader wrote — and "this copy was asked for here"
stops at the first hop.
