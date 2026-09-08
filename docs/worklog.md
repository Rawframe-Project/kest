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
