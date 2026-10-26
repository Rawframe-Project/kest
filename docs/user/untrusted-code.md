# Running untrusted code

A host that runs code it did not write (a mod, a player's script, a level from the internet) starts the machine differently and gives it ceilings. This page says what Kest promises such a host, how far each promise is kept, and what is the host's to get right.

## Starting a machine for it

```c
KestHost *host = kest_host_new();
kest_host_bind(host, "Game.spawn", spawn, world);
kest_host_open(host, "Game.spawn");
KestLimits limits = {0, 0, 4 * 1024 * 1024, 1000000};
KestRuntime *vm = kest_start_untrusted(build, host, &limits);
```

`kest_start_untrusted` differs from `kest_start` in three ways:

- **Only opened doors.** The program may ask only for doors the host bound and then opened with `kest_host_open`. A door bound for the host's own tools is not handed to every mod; a program asking for one is refused by name (`K0663`).
- **Ceilings are required.** `limits` must give a fuel budget and a heap ceiling. Zero for either is refused (`K0664`, `K0665`) rather than read as "no ceiling", because a host that forgot is exactly the host this is for.
- **Only the verified machine runs.** The machine runs only bytecode the verifier proved, never a body compiled to C by the release engine, even if the host linked one in.

Compile untrusted source with ceilings too: `kest_build_within(path, library, errors, form, room, work)` bounds the bytes compiling may take and the work it may do, counted in units that are the same on every machine (`K0658`, `K0666`). Without a work ceiling, a crafted file can make compiling take time that grows faster than the file.

## The threat model

The attacker writes Kest source and nothing else: never bytecode (Kest has no bytecode format) and never the host. The source is compiled and run on someone else's machine by a host that started it with `kest_start_untrusted`.

For that source, Kest is to promise:

1. **No memory error.** No source makes the compiler, the machine or the standard library read or write memory it does not own.
2. **Nothing but what was given.** A program reaches only the doors the host bound and opened.
3. **Everything bounded, by counts.** Running time, the heap, call depth and the compiler's own work are bounded by numbers the host sets, and reaching one is a refusal that says which, at the same place on every machine.
4. **Nothing of the process in a value.** No address, and nothing else about the process, can be read out of a value the program holds.
5. **Only the machine runs it.** Untrusted code never runs as C compiled by the release engine.

Outside the model: timing side channels; bugs in the host's own doors; a door the host opened being used for what it does.

## Where each promise stands

| Promise | How it is kept |
| --- | --- |
| No memory error | Every chunk of bytecode is verified before it runs: every slot, constant, function, door and layout an instruction names exists; every jump lands on an instruction; the operand stack has one depth on every path and never under- or overflows; every slot is read only as what it holds on every path; memory from a `scratch` block never outlives it; and the two slots of a piece of text belong to one piece. The one read the verifier cannot place, a walk's byte of text, is checked in every build. The compiler, machine and verifier are fuzzed under the sanitizers, including with bytecode no compiler wrote. |
| Nothing but what was given | Every door is one the host bound; an untrusted machine takes only opened ones. A door handed a handle checks that this machine made it, text handed to a door is one verified piece, and a door's answer is refused if the program could not have made it. |
| Everything bounded | Fuel, heap, compile memory (`--room`), compile work (`--work`) and every count a program can have (nesting, names, loops; see [Diagnostics](diagnostics.md#limits)) are bounded. An untrusted machine without fuel and heap ceilings does not start. |
| Nothing of the process | A reference hashes by its place, not its address, and has no text or number. An audit of every value a door or instruction hands back is still to be done. |
| Only the machine | An untrusted machine never enters a release-compiled body. |

The verified machine costs nothing extra over a trusted one running the same program; the ceilings cost a few percent. What untrusted code gives up is the release engine, which runs the same workloads 1.4 to 10 times faster.

## What the host owns

**Its doors.** What a door does with the values it is handed is the host's to get right, and it is where most real bugs will be. `kest hostile file.kest [seed]` writes the file back with a function after it for every call of every door it declares, with the values a hostile program could pass: numbers at the ends of every width, floats that are not numbers, empty text and very long text, text with a zero in it, every case of every enum. The host builds that file, binds its doors as usual, calls `hostile0`, `hostile1` and so on (`hostile` answers how many there are), and runs under its own sanitizers. Each call must answer, or be refused by the door in words with `kest_native_failed`. The engine in this tree is held to it; the first run found two of its doors turning an infinite float into a number.

**A second wall.** For a host that wants a fault in the machine kept out of its own memory regardless, the library builds as WebAssembly (`kest.wasm`, with `-DKEST_WASM=ON`) and runs inside a WebAssembly runtime. Every example answers the same there, at three to four times the cost of the native machine.

## Reporting a vulnerability

Report it privately through [GitHub's private vulnerability reporting](https://github.com/Rawframe-Project/kest/security/advisories/new), not in a public issue. A useful report has the Kest source that shows it, what `kest --version` says, and the platform. The organization's [security policy](https://github.com/Rawframe-Project/.github/blob/main/SECURITY.md) says what happens next.
