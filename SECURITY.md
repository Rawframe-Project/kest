# Security

## Reporting a vulnerability

Report it privately, through GitHub's private vulnerability reporting:
<https://github.com/Rawframe-Project/kest/security/advisories/new>. Please do
not open a public issue for it.

A report is most useful with the Kest source that shows it, what `kest --version`
says, and the platform it ran on. A confirmed report is fixed, and the fix is
published with an advisory that says what was wrong and which versions had it.

## What Kest promises today

Kest is a boundary for code the host wrote or trusts to be cooperative. A
program reaches only what the host bound -- there is no ambient filesystem,
clock, network or environment (D981) -- a budget stops a program that will not
stop, and a heap ceiling stops one that grows.

**It is not yet a sandbox for code that is trying to get out.** That is what
the rest of this page is about: the promises Kest is being built to keep for
code nobody trusts, and how far each one is kept today. Until the first three
below are kept, code you do not trust belongs in another process or in Wasm.
See D1233.

## The threat model

**The attacker** writes Kest source. Nothing else: never bytecode, which Kest
has no format for, and never the host. The source is compiled and run on
somebody else's machine -- a player's, a server's -- by a host that asked for
the profile for code it does not trust.

**What Kest is to promise, for that source:**

1. **No memory error.** No source, however it is written, makes the compiler,
   the machine or the standard library read or write memory it does not own,
   compiling or running. Any source that does is a vulnerability.
2. **Nothing but what was given.** A program reaches only the doors the host
   bound and marked as safe for code it does not trust.
3. **Everything bounded, by counts.** Running time (steps of a budget), the
   heap, the depth of calls, and the compiler's own work are bounded by numbers
   the host sets. Reaching one is a refusal that says which, never a crash, and
   it is reached at the same place on every machine.
4. **Nothing of the process in a value.** No address, and nothing else about
   the process the program runs in, can be read out of a value it holds.
5. **Only the machine runs it.** Code the host does not trust is run by the
   bytecode machine. `kest build --release` writes a program as C for the
   host's compiler, and that is never done with it.

**Outside the model:** timing side channels; bugs in a host's own doors, which
the host answers for (Kest will give it a way to fuzz them); a host that binds
a door not marked as safe; and a door that was granted being used for what it
does.

## Where each promise stands

| Promise | Today | What is left |
| --- | --- | --- |
| 1. No memory error | The compiler and the machine are fuzzed over six boundaries under the sanitisers (D984, D1216, D1227); nothing has been found in the last two campaigns. Every chunk is verified before it runs: every slot, constant, function, door and layout an instruction names is one the program has, and every jump lands on an instruction (D1237). | The verifier holding how deep the operand stack goes and what each slot holds, so an instruction that reads a handle is only ever handed one; and `text.in` asking in the untrusted profile. |
| 2. Nothing but what was given | Every door is one the host bound (D981); `kest_build_capability` lists them in groups before anything is bound. | Doors marked as safe for untrusted code, and a profile in which nothing else can be bound. |
| 3. Everything bounded | Steps, the heap, how much memory reading and compiling may take (`--room`), and how many of each thing a program may have (nesting, names, loops) are bounded. | The compiler's work, which nothing bounds today, as a count rather than a time; and all of them required rather than chosen in the untrusted profile. |
| 4. Nothing of the process | A reference hashes by its place and not by its address (D1054). | An audit of every value a door or an instruction hands back. |
| 5. Only the machine | Nothing enforces it. | The untrusted profile refusing a release build. |
