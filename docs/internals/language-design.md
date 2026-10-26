# Language design

The rules the language is built on, and the alternatives that were weighed and rejected, so a maintainer does not reopen them without new evidence. A change to the language starts as an issue labeled `feature` whose body has the design; when it is made, its reasoning is written here or on the page of the part it governs.

## Rules

**One spelling for one thing.** No semicolons, no parentheses around conditions, braces always, `//` comments only, one way to say a function gives nothing (no `-> void`), no `++`, no compound operators beyond the four arithmetic ones, one layout. Every second spelling is a choice a reader has to recognize and a formatter has to normalize.

**Nothing converts on its own.** The only implicit conversion is a value becoming an optional where an optional is wanted, and it happens once (a value never becomes `T??`). Literals are typed by where they go, with `i32` and `f32` as the fallback, and a literal that does not fit is an error.

**Every arithmetic edge has an answer.** Where C has an answer (wrapping, IEEE 754), Kest matches it; where C has none (shift counts at or past the width, `INT_MIN / -1`, float to integer out of range), Kest defines one and the folder and the machine give the same. Integer division by zero is the one fault, because no number would be honest.

**Values and handles.** Structs, fixed runs, vectors, enums and optionals are values copied on assignment; arrays, stores and function values are handles. A value returned from a helper costs nothing extra, which is what lets frame code be written as functions that take a value and give one back. The price is the most common newcomer bug (writing a parameter's field), which the compiler warns about (`K0346`) rather than forbids, because the fix is one line.

**An array element is a place; a store element is not.** `xs[i].field = v` writes in place. A store element is read with `get` (it may be stale) and written with `set`. A place form for stores was measured and would buy nothing: reading the element once into a local, changing several fields and writing it back is faster than resolving the element per field.

**No ambient authority.** A program reaches nothing outside itself except through doors the host binds. `std.io`, `std.math` and `std.os` are declarations of doors, not built-ins.

**Words, not keywords.** `flags`, `scratch`, `own`, `compares`, `orders`, `resumes`, `wait` and `block` are words with a meaning only where they stand, and names everywhere else. A new feature takes a word back this way instead of reserving it, so no existing program breaks.

**Declare at every boundary, infer inside.** Signatures, constants, struct fields and doors write their types; bodies infer. A `const` writes its type because it is read from other files.

## Choices with the alternative that was rejected

**Function values without closures.** A closure needs a capture mode, a lifetime, an environment on the heap that the collector follows and a `no.alloc` body cannot create, a rule for capturing out of a `scratch` block, a value with no name for a reload to match, and a shape the host boundary does not have. Without them the cost is one parameter: what a closure would capture is passed beside the function, or held in a struct with it. Blocks cover the case of a body written at the call site.

**Blocks are inlined, not values.** A function taking a `block(T) -> R` parameter is compiled into each place it is called, and the block into each place that function calls it, the same way a generic is copied per set of types. That is why a block can read and write the caller's names without capturing them and why a `no.alloc` function may use one. It is also why a block cannot be stored, returned, handed to a host, or used in a recursive function, and why `return` inside one is refused: there is nowhere for it to return to. A block's body gives a value only as `|x| value`; the `{ }` form gives nothing.

**Resumable bodies instead of coroutines.** A coroutine keeps a suspended frame, which is an instruction pointer into the old code: a save has nothing to write for it and a reload has nothing to point it at. `resumes p.f` keeps the sequence and not the frame: where a body waits is a case of an enum the program declares, and what it has is the fields of its own struct, so the state saves, loads, prints and reloads like any value, and a reload under a changed enum is refused like any shape change. The rule that nothing but the parameter crosses a `wait` (no locals, no `for`, no `defer`, no open `scratch`) is what keeps it data. Two other designs were weighed: a compiler-generated state struct holding every local, which saves typing but makes the saved state something nobody wrote and nothing can name; and a compiler-generated enum of wait points, which is a type no program declared.

**A compile-time walk of fields instead of reflection or macros.** `for name, value in fields(x)` is written out once per field while compiling, each copy checked with `value` at that field's type, so overloads pick per field. It covers the two jobs that wanted reflection, a save/load written once for every shape and an inspector, without a runtime table of fields (which would make every field a value of no particular type, the one thing the language does not have) and without macros or code that writes code. A compiler-written save format was also rejected: a program's save is its own to decide.

**No exceptions and no built-in result type.** A fault stops the machine and is the host's to handle; an expected failure is a value (`T?`, `bool`, or an enum the API declares). A built-in `Result` would choose one shape for every API, and `enum Answer<T> { Held(T) Trouble(text) }` is one line.

**`defer` does not run on a fault.** Running deferred calls during a fault needs a rule for a fault inside a deferred call, which is exception machinery. The machine's own resources go with the machine; state a host paired through doors is the host's to close when a call answers false.

**Two constraints on generics.** A type parameter may require `compares` or `orders` and nothing else. `hash` follows from `compares`. Arithmetic and fields on a type parameter are refused because a body that adds two `T`s is written for numbers and declared for anything. Generic bodies are checked once against what they declare, so a generic nobody has called is still a checked body.

**No order on structs.** Equality on a struct is the language's to define (field by field); order is a choice among at least as many orders as fields. What sorts is told the order with a function value.

**Operators only on the language's vectors.** `vec2`, `vec3` and `vec4` get component-wise `+ - * /` because games use them everywhere and the bits must be the same in both engines. A program's own types get no operators: a symbol means one thing everywhere.

**Overloads resolve exactly or not at all.** No ranking and no conversion: one function matches or none does. A literal fits any width of its family in the first pass, and a second pass with the literal's default type breaks ties. The same rule types a bare `let`, so a call and a binding cannot disagree.

**`match` only on enum cases.** Numbers and text have too many values to exhaust, and `if` already chooses between them. Exhaustiveness is the reason `match` exists.

**No character type.** What a character is depends on what the program means by one. Text is bytes; `std.text` says what UTF-8 means by a character; everything above that (graphemes, collation, normalization) is a host's, because it needs tables tied to a Unicode version.

**No bytecode format.** The bytecode is unversioned and unstable on purpose. A program ships as source with the machine or as a release binary carrying its source; compiling is fast enough that a cache would not be worth freezing a format.

**Promises proved twice.** Once over the trees, where a refusal can name the path, and once over the emitted instructions, where the machine's own list of what each instruction does is the authority, so a new instruction cannot be added without saying whether it allocates or calls the host.

## Editions

A change that would break a program, or change what it means, is made under a new edition, warned about in the edition before it, and a project names its edition in its manifest. The edition is the one compatibility promise made to programs while the version is `0.0.x`; the C API, the JSON and the library shape still move, each move named in the changelog. Retired library names are kept in a table in `src/types.c` so a program using one is told what replaced it.
