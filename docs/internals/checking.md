# Checking and promises

`types` registers every declaration and resolves names, `check` holds every body to the declarations, and `contract` proves the promises. All three report and recover, so one run says everything wrong with a program.

## Types

[`src/types.c`](../../src/types.c) is what a type is and every name a program declares.

- **Registration.** Every struct, enum, flag set, constant, function and door is registered under its whole module name (`game.render.draw`). Primitives are registered too, including an internal `void` that is looked up by name and cannot be written.
- **Composed types are interned.** An optional, an array, a function type or a copy of a generic is made once and found again, so two `[i32]`s are one type and comparing types is comparing pointers. `check --json` reports `typesMade`.
- **Generic copies** are made the first time a set of types is written and found after that. A copy's name carries its types (`pick#i32,i32`, a struct `Pair<i32, text>`), which is the name hosts see. A copy whose name would be very long is cut for messages at 120 bytes on a character boundary, because a type made of copies of copies can double in name length at each level.
- **Retired names.** A table of library names that went away maps each to what replaced it, so a program using one is told the replacement instead of "unknown name".
- **References** are taken apart the same way as in the machine (`types.h` defines the split), because the type layer hashes a reference by its place alone: the stamp is a process-wide count, and hashing it would make `deterministic` code answer differently on a second machine.
- **Hashing** of values the compiler works out (constants, `hash` folded while compiling) uses the same arithmetic as the machine, in one walk, so a folded hash and a run-time hash agree.

## Check

[`src/check.c`](../../src/check.c) checks every body against the declarations.

**Overloads.** A call is resolved in two passes. The first lets a literal fit any width of its family; if more than one function is left, the second asks with the literal's default type (`i32`, `f32`). Nothing ranks and nothing converts, so the answer is one function or a refusal that lists every candidate, the near misses first (those taking as many arguments, then those agreeing on most of them). The same rule types a bare `let`, written once.

**`x.f(a)`.** Looked for among the file's own functions, then in the module that declared the value's type, then among the builtins, taking the first whose first parameter can be `x`. A file that declares a builtin's name (`len`, `get`, `remove`) adds to it: its own answers for the types it takes and the builtin for the rest.

**Generics.** A generic body is checked once against what its declaration asks of its type parameters (`compares`, `orders`); each copy is then checked against its own types, so a promise can hold for one copy and not another, and a refusal in a copy carries a note at the call that asked for it, with what each type name stood for. A requirement the body never uses is a warning, since it refuses types that would have worked.

**Blocks** are checked where they are written, in the caller's scope, as the body handed to the parameter. A block may only be handed to a `block` parameter and called or handed on there.

**Field walks.** `for name, value in fields(x)` is checked once per field, each with a tree of its own re-parsed from the source (`kest_parse_block_again`) so each copy can record its own types, and each refusal carries the field it was for.

**Resumable bodies.** `resumes p.f` and `wait` are checked here: the parameter is taken and given back, the field is an enum of the program's, no local is in reach of a `wait`, no case is waited at twice, and none of the excluded constructs (`for`, `defer`, an open `scratch`) surround one (`K0368`).

**`scratch` escape.** A value made inside a `scratch` block may not be returned, stored in an outer name or container, or handed to a call beside something older that could keep it (`K0507`). Growing anything older than the block is refused, and a call inside the block that is handed something older must promise `no.alloc`. A `defer` that could grow something older is refused, since it runs before the block's memory goes back.

**What warnings are for.** Warnings are said about the file that was named and not its imports. Lost writes to a parameter's field (`K0346`) are reported only when the function cannot give that type back. An array made with `array()` and filled with `fit` with no `room` or `push` on that name (`K0347`) is reported where it was made, and not when the array was handed elsewhere, since it may come back with room.

**The index.** With `index_names` set (the language server sets it), the checker records what every name resolved to, a name written under its module (`thing.n`) and a call settled among overloads included, at the last word of what was written; hover, definition, references and rename read that index rather than resolving again, so the editor and the compiler cannot disagree.

**Messages.** Most of what a program is told comes from here. A mismatch is always said in one shape (`expected_but`), and where a type mismatch has a known fix the fix is the suggestion: an optional where its value would do says `if let`; a number where text is wanted says to use a hole; a module where a value is wanted names a function under it. A suggestion never offers the word that was written, and never offers a name from a module the file did not import as a spelling.

## Contract

[`src/contract.c`](../../src/contract.c) proves `no.alloc`, `no.host` and `deterministic`.

- **A graph.** The first walk builds a call graph with every call site and every call through a function value; the other promises are proved on that graph instead of walking every body again.
- **Every builtin has an opinion.** `REACHES` lists every builtin the language answers to with whether it reaches the heap and why. A builtin missing from the table would be one the proof says nothing about, so `check-tables.sh` holds this table to the checker's and the compiler's lists of builtins.
- **Judged by body, transitively.** A function defined in the program is judged by what it does; a promise is written only where a host or a function type needs it. A door is judged by what it declares, since its body is not there to read. A call through a function value is judged by the function type's promises; a type that promises nothing is `K0402`, with the fix written into the type. For a generic, a call through a parameter's function type is judged by the shape as written, so a promise on a generic nobody instantiated is still proved in the file that made it.
- **The path.** A refusal walks from the promise down to the body that breaks it, as deep as the graph goes, and names every call on the way.
- **`deterministic`** refuses any reach outside the profile, which is a call to a door that is not itself declared `deterministic`.
- **The second proof.** After compiling, the verifier proves the same three promises again over the emitted instructions, using the machine's own table of what each instruction does. If the two proofs disagree, it is reported as a fault in the compiler.
- **What was proved anyway.** The walk runs for every function, promised or not, and `check --cost` and `check --json` (`proved`) report what each function reaches and which promises it could make.
