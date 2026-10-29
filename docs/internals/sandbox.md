# Sandbox

How the promises in [Running untrusted code](../user/untrusted-code.md) are kept. The attacker writes Kest source and nothing else; the host is trusted, and so are the doors it opens, as far as Kest is concerned.

## Layers

1. **Bounded compiling.** Every stage of compiling allocates from arenas under the build's one ceiling (`--room`, `kest_build_within`'s `room`), and counts its work in units that are the same on every machine (`--work`). A unit is a token read, a node made or checked, an instruction laid down or walked while proving, or a byte of a generic copy's name: that last one is there because each copy of a copy is named twice as long as the one before, and twenty-two of them in 1,300 bytes of source once took two seconds and 180 MB before anything counted them. Without a work ceiling a crafted file can make compiling take time that grows faster than the file.
2. **Every count a program can have is bounded** while compiling (names, nesting, loops, breaks, defers, jump distances, doors), each a diagnostic with the number in it. See [Diagnostics](../user/diagnostics.md#limits).
3. **The verifier** proves every chunk before it runs, over operands, jumps, stack depth, what each slot holds, scratch escape and text pairing. See [Verifier](verifier.md). It does not trust the compiler.
4. **Bounded running.** `kest_start_untrusted` refuses to start without a fuel budget and a heap ceiling. Fuel bounds time by counting backward jumps, calls and bulk work; the heap ceiling bounds memory; stack and depth are sized from the program or by the host.
5. **Only opened doors.** An untrusted machine starts only if every door the program asks for was opened with `kest_host_open`, and refuses the program by name otherwise (`K0663`).
6. **Only the machine.** An untrusted machine never enters a body the C backend compiled, even one a host linked in; nothing proves that C the way the verifier proves bytecode.
7. **The boundary checks what crosses.** Handles and text handed to a door or answered by one must come from this machine; tags must be cases; a door that promised `no.alloc` must leave the heap where it was. See [Machine](machine.md#calling-the-host).

## Values carry nothing of the process

A reference is a place and a process-wide stamp, and a program cannot read the stamp: there is no text of a reference, no integer conversion, and `hash` covers the place only. Values that are not numbers have one set of bits as far as `bits` and `hash` can see. An audit of every value an instruction or a door hands back, for anything else of the process (an address in an uninitialized slot, a pointer in padding), is the remaining work on this promise; memory arrives as zeros and the verifier refuses reading a slot nothing wrote, which covers the common cases.

## Testing the hostile side

- **Fuzzing.** `tools/fuzz.c` runs seeded campaigns under the sanitizers over nine boundaries: source text, handles, lends, references, text, migration between program versions, bytecode chunks changed one number at a time, the formatter's output read back, and manifests. `tools/fuzz-cover.c` is a libFuzzer target over the source boundary that keeps inputs reaching new code, so it gets past the lexer into the checker, compiler, verifier and machine. Every input is compiled with a ceiling on bytes and work and run with a ceiling on heap and fuel, as an untrusted host would; anything other than a program or a diagnostic (a sanitizer report, a hang, a crash) is a finding. `tools/oss-fuzz-build.sh` builds the libFuzzer target the way OSS-Fuzz builds projects.
- **Hostile doors.** `kest hostile` writes every door a file declares called with the edges of every type, for the host to run under its own sanitizers; `tools/check-doors.sh` holds the engine in this tree to it at eight seeds.
- **Ceilings reached.** `tools/check-ceilings.sh` writes a program one past each limit and requires the refusal with the number in it; the limits that take gigabytes to reach (2^31 elements, store places, stamps) are met in a copy of the tree with the ceiling lowered.
- **WebAssembly.** `tools/check-wasm.sh` builds the library as WebAssembly and requires every example to answer the same there, which is the second wall for a host that wants a fault in the machine kept out of its own memory.

## Outside the model

Timing side channels; a host's own doors (the host answers for them, and `kest hostile` is how it finds out); a door the host opened being used for exactly what it does.
