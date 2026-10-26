# Performance

Where Kest stands against the engines it sits beside, what dominates its cost, what is still open, and the designs that were measured and rejected. The tables and charts are in `bench/`, with the commit, machine and load they were taken at; [Benchmarks](../operations/benchmarks.md) says how to take them again.

## Where it stands

Numbers are from one sitting on an 8-processor Haswell virtual machine, best of fifteen runs by processor time, against Luau 0.740, Lua 5.4, LuaJIT 2.1 (interpreter and compiler), QuickJS-ng and daslang. They are the shape to expect, not a promise for another machine.

- **Interpreter.** Across five workloads (`bench/{control,graph,kernel,rules,words}`), Kest's machine is ahead of Luau's interpreter on all five (by 19 to 47 percent), of Lua 5.4 by 1.5 to 2.4 times, and of daslang's interpreter everywhere. Against LuaJIT's interpreter it is ahead on four and behind by 1.15 times on `control`.
- **Release engine.** Ahead of Luau's native code on all five (2.4 to 5.2 times), of daslang's ahead-of-time compiler on four (behind on `rules` by 1.3 times), and within a few percent of LuaJIT's trace compiler on `kernel` and `words`, level on `graph`, ahead on `control` and `rules`.
- **Crossing the boundary.** A host calling a program is cheaper than in any other engine measured, interpreted or compiled. A program calling its host costs about 28 ns in the machine (17 compiled), level with LuaJIT's compiled code.
- **Frames.** A frame step over an array of value structs is about thirty instructions per entity in the machine. With 5,000 entities making garbage every frame, Kest has the lowest worst-of-a-hundred and lowest worst frame of the engines measured, because the walk is linear in reachable memory and `no.alloc` frames allocate nothing.
- **Budgets.** A fuel budget costs a frame nothing measurable, where Lua's instruction hook costs 2.2 times the frame. A runaway loop stops 15 µs after `kest_cancel`.
- **Threads.** A machine per thread, each with its own world, does 6.8 times the work on eight threads.
- **Footprint.** About 544 KB added to an executable linked with unused sections removed (the compiler and checker come with it; Lua adds 260 KB, LuaJIT 556 KB). A machine holding a frame program is about 26 KB.
- **Compiling.** 107,135 lines to running in 211 ms (Luau 276 ms; Lua, which checks no types, about a quarter of that). A generic used 3,200 ways compiles in 80 ms. Reloading the Tetris clone is 1.7 ms against Lua's 0.5 ms, because a reload checks, proves and compiles every line again.
- **Determinism costs.** On Luau's own benchmarks Kest's interpreter is 1.1 to 1.3 times behind Luau's, and compiled it is behind on `trig` by two times: Kest computes sines in Kest so every machine gets the same bits, where Luau calls the C library.

## What dominates

- **Moving values on and off the stack.** About half of a frame step's instructions are loads and stores; the arithmetic is a fifth. That is a stack machine, and it is why the fused instructions in [Compiler](compiler.md#lowering) target the commonest pairs. The frame step went from 57 instructions to about 30 through fusions, carried bodies and in-place element access, each measured and kept only if it changed the count without changing any output.
- **A call** is the one thing the machine does that is not one instruction; carrying small bodies removed most calls from frame code.
- **A reference read** costs about twice an index read: the stamp comparison and the optional it answers in. A world of removable entities costs about a sixth of a frame step more than a run of entities that cannot be removed.
- **A crossing into the host** costs a few nanoseconds more than a call inside the program; a crossing in from the host costs less than a turn of a loop. A host processes a batch in one call (a lent array) rather than calling per item.

Instruction counts are taken from the checking build with `KEST_DEEP=1`, which counts every instruction; durations from `make time` (`tools/frame.kest`, `tools/crossing.kest`, `tools/reference.kest` and `tools/inward.c`), which print a spread so a run on a busy machine is recognizable.

## Open

- `rules` compiled is behind daslang's ahead-of-time compiler: a struct read out of an array, handed to a function and written back is copied where daslang changes it in place. The copies around the call were measured and cost nothing to remove; what remains is a body working on an element in the array's bytes, which is a different engine.
- `control` in the machine is behind LuaJIT's interpreter.
- A reload costs 1.7 ms because nothing is carried between builds.

## Measured and rejected

- **A register machine.** Built and measured against the stack machine over three shapes of work: 1.35 times slower, because the operand traffic moved into wider instructions rather than going away.
- **A per-instruction budget or count.** A test at the top of the dispatch loop measured at a sixth to a third of the machine's time; budgets are checked at backward jumps and calls, and counting lives in the checking build.
- **An incremental collector.** Needs a write barrier on every store that could write an address, which untagged slots cannot target and every program would pay.
- **Reference counting.** Paid on every copy by every program for the sake of the ones that churn, and cannot follow interior pointers.
- **A bytecode cache or format.** Compiling a hundred thousand lines takes about 200 ms; a cache would buy less than freezing a format would cost.
- **A place form for store elements.** Reading an element once, changing several fields and writing it back is faster than resolving the element per field (65 ms against 84 ms over 20,000 elements, four of seven fields changed).
- **Insertion sort in `std.sort`.** Quadratic: 4,000 numbers out of order cost 260 million instructions. `std.sort` sorts with gaps, in place and without recursion, 3.7 times dearer on nearly sorted input and 247 times cheaper on reversed.
