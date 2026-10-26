# Machine

[`src/vm.c`](../../src/vm.c) runs verified bytecode. It is a stack machine over 8-byte slots with no type tags, which is what makes it fast and is also why the [verifier](verifier.md) exists: every assumption the dispatch loop makes about a slot was proved before the chunk could run.

## Shape

- **Slots carry no tags.** A `KestValue` is a union of a 64-bit integer, a double, a pointer to text and a pointer to an object. The language is statically typed, so each instruction knows what it reads; nothing is checked at run time that the verifier already proved.
- **Frames and an operand stack** live in one slot array the machine owns. A frame is a function's locals; the operand stack sits above them. A call puts its arguments where the callee's parameters are, so passing arguments copies nothing.
- **Dispatch** is threaded where the C compiler can take the address of a label (GCC and clang): each instruction ends by jumping through a table indexed by the next opcode. Elsewhere, and in the checking build, it is a `switch` in a loop. On WebAssembly the table becomes a second `switch` and runs a third slower, so it is turned off there.
- **Two layouts.** A value has a frame layout (one slot per scalar, an `f32` widened to a double, in field order) and a memory layout (the C layout, which arrays, stores and lends use). Instructions that read an element of an array read the memory layout in place; a struct read out of memory onto the stack is laid out flat.
- **A handle says what it is.** Arrays, stores and lends begin with a four-byte kind, so a store handed where an array was wanted is refused by the instruction rather than misread. This is the one thing about a handle the machine checks itself, because the host boundary cannot see inside a frame.

## Stores and references

A store is a slot map: a run of elements, a stamp per place, a bitmap of which places are live, and a free list. A reference is one 64-bit number: 24 bits of place and 40 bits of stamp. Stamps come from one process-wide atomic counter and are never handed out twice, so a reference from another store, another machine, a freed machine, or the same place before it was reused, carries a stamp this place never had and reads `none`. The process can hand out 2^40 - 1 places before the counter runs out, which is a refusal at the `add` that asked, never a stamp handed out twice. (An earlier design kept a 16-bit world number in the reference; the 65,537th machine was told it was the first, and a reference from the first world resolved in the new one. One counter for the process removed the world field.)

A program cannot read a stamp: a reference has no text and no integer form, and hashes by its place. The live bitmap is read 64 places at a time, so walking a sparse store costs its high-water mark divided by 64; a store that empties goes back to reaching nothing.

## Fuel and cancelling

The budget counts steps: a backward jump or a call, plus one unit for every 64 bytes or elements an instruction touches when it does work proportional to its input (joining text, making text from bytes, cutting, measuring, ordering, filling, making room, and a push or add that copies). Without the weight, one instruction could spend a second without spending a unit.

The machine does not decrement a counter per instruction. It takes fuel in slices and checks only at backward jumps and calls, the two ways a program goes on doing something, so a straight run of code is bounded by its own length. A check at every instruction measured at a sixth of the machine's time; at jumps and calls it is too small to measure. `kest_cancel` sets a flag the same check reads, so cancelling costs nothing either. A host spends fuel for work its doors did with `kest_fuel_spend`, at the same rate.

A machine whose budget ran out, or that was cancelled, has its stack, heap and world intact; the call that stopped has returned, and a host that gives more fuel can call again.

## Calling the host

A call to a door (`KEST_OP_CALL_HOST`) hands the bound function the frame in place: the arguments are the top slots of the operand stack, and the answer is written over them. Before the call the machine gives back the slice of fuel it was spending, so `kest_fuel_spend` inside a door sees the whole budget. After a door that promised `no.alloc`, the machine checks that the heap is where it was (`K0631`). After a door that answers text, a handle or an enum, it checks the answer is something the program could have made: text and handles must come from this machine (`K0652`), and a tag must be a case of the enum (`K0650`). A door calling `kest_native_failed` makes the call fail with the host's words (`K0662`).

A door may call back into the program. That run stands above the frames already running, with its own frames, and gives them back however it ends; a refused call back in leaves the outer call to carry on and answer what it would have. It cannot stop at a breakpoint, because the host's C frame under it is gone by the time anyone could resume (`K0708`).

## Faults

A fault (an integer division by zero, an index past the end, a stale lend, a ceiling, a budget, a cancel) stops the machine at the instruction, records a diagnostic with the place and the calls under it, and returns false from `kest_call`. Every diagnostic is written into the machine's own report, which a host reads with `kest_report`. A fault is not unwound through the program: no `defer` runs. What the machine owns goes with the machine, and `scratch` blocks open at the fault are put back.

## Breakpoints

A breakpoint is written into the bytecode: the debugger replaces the first byte of an instruction with an opcode nothing compiles to (`kest_break_byte()`) and puts the byte back when the machine stops there. There is no test in the dispatch loop, which measured at a third of the machine's time, so a machine nobody debugs runs the compiled program byte for byte. Because the bytecode belongs to the build, a breakpoint is seen by every machine of that build; a debugger uses a build no other machine is running.

A stopped machine is mid-call and standing on its heap, so the doors that would take the heap away or move its ceiling (`kest_collect`, `kest_heap_reset`, `kest_scratch_mark`, `kest_scratch_rewind`, `kest_heap_allow`) refuse while it is stopped, as they do inside a call.

## Counting

`kest_count` turns on per-function call counts for a machine (`kest profile` uses it); a machine nobody asked pays one pointer test. There is no per-instruction count in a release build, because that test at the top of the loop measured at a third of the machine. The checking build counts every instruction and, with `KEST_DEEP=1`, reports the counts and how many questions the machine asked itself, which is how instruction counts in this documentation were taken.
