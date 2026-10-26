# Verifier

[`src/verify.c`](../../src/verify.c) proves a compiled module is something the machine may run without asking, before any of it runs. The machine trusts what it is handed: it moves the operand stack pointer, reads slots as numbers, text or handles, and follows jumps without checking. The verifier is what makes that safe, and it is the first line of the [sandbox](sandbox.md): the threat model's attacker writes source, the compiler might be wrong, and the verifier does not trust the compiler.

## What is proved

Each pass runs over every chunk and refuses the module with a code and the instruction when anything fails (`K0408` for a number that names nothing, `K0409` for a jump that lands nowhere, and so on).

1. **Every body can be walked.** Instructions are decoded from the start, and each one's width is known from the instruction table in `value.c`.
2. **Every number names something.** Every slot, constant, function, door and layout an instruction carries is one the body or the module has, every run of slots fits, and every jump lands where an instruction starts: not past the end, not in the middle of one, not before the start.
3. **The operand stack is one depth everywhere.** Walking every path from the first instruction, each reachable instruction is reached at exactly one depth, never takes more than is there, never leaves more than the body was given room for, and every `return` hands back exactly the declared width with nothing under it. Unreachable code is not walked, since nothing runs it.
4. **Every slot is read as what it holds.** The machine keeps no tag beside a value: a slot is eight bytes read as a number, as text, as a handle or as an address by whichever instruction reads it. The verifier tracks, for every slot at every reachable instruction on every path, what it can hold: a number, text, a handle of a given layout, an address into a given layout (with a step for a walk along a run), a function of a given type or exactly one known function, or an enum tag that can be some set of cases with what each case carries. An instruction that reads a slot as something it may not hold is refused.
5. **Working memory does not escape.** What was made inside a `scratch` block carries how deep it was made, and it may not be read, kept or handed on after the block closes.
6. **Text is one piece.** A piece of text is two slots, bytes and length, and both are marked as belonging together only where they were made or moved together. A slot moved on its own, or a run cut between the two, loses the mark, and an instruction that reads text from slots not marked as one piece is refused. That is what stops a length from one string being paired with the bytes of another.
7. **The promises, again.** `no.alloc`, `no.host` and `deterministic` are proved over the emitted instructions, following calls, using the instruction table's own column of what reaches the heap and the host (`KEST_OP_CALL_HOST` is the only way to the host). If this disagrees with the proof over the source, it is a fault in the compiler.

## What is not proved, and is checked instead

One read cannot be placed by the walk: a `for` over text reads a byte at a position the compiler worked out from a length measured once. The verifier cannot prove the compiler's arithmetic, so that read is checked while running, in every build and both engines (`K0645`); the cost is not measurable. Everything that depends on values (an index into an array, a division by zero, a stale reference) is checked by the instruction that does it, as the language requires.

## For the debugger

The same walk answers what a stopped frame may show. `kest_frame_written` asks whether the body has written a slot on every path to where the frame stands, and `kest_frame_text` hands over a slot's text only where it is one piece of text on every path. Both walk the body again for the asking, in an arena that is given back, so a debugger never follows a slot that is not text as an address. Before this, `locals` read whatever a not-yet-written slot held as text, and crashed.

## How it is tested

The verifier is fuzzed with bytecode no compiler wrote: `tools/fuzz.c`'s `chunks` target compiles a program, changes one number in one instruction, and requires the verifier either to refuse it or the machine to run it safely under the sanitizers. `tools/check-verifier.sh` changes each kind of operand in turn (a slot, a constant, a function, a door, a layout, a run of slots, a jump past the end, into the middle of an instruction, back past the start) and requires each refusal with its code. That fuzzing found three places the verifier was wrong, now fixed.

The cost is measured as compile work and memory: the verifier's tables live in an arena taken under the build's and given back as it goes, so it is bounded by the same `--room` and `--work` ceilings as the rest of compiling.
