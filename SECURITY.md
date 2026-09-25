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
| 1. No memory error | The compiler and the machine are fuzzed over seven boundaries under the sanitisers (D984, D1216, D1227), the seventh being instructions nobody's compiler wrote handed to the verifier and run where it lets them through, which found three places the verifier was wrong, all fixed; and the source boundary is fuzzed by a fuzzer that keeps what reached new code, three million inputs in its first campaign with nothing found (D1253). Every chunk is verified before it runs: every slot, constant, function, door and layout an instruction names is one the program has, and every jump lands on an instruction (D1237); every path of every body keeps the operand stack at one depth, never under what an instruction takes nor over the room the body was given, and every call and `return` is its declaration's width (D1239); and every slot is read only as what it holds -- a number, text, a handle of the layout the instruction reads, an address, a function of the type it is called as, what an enum's tag says its case carries -- on every path, before anything runs (D1242); and what a block of working memory made is never read, kept or handed on past the block (D1243); and the two slots of every piece of text read are one piece's (D1244). The one read the verifier cannot prove the place of, a walk's byte of text, asks in every build (D1245). | Nothing structural: what is left for this promise is the adversarial fuzzing of S6 and the door audit of S7. |
| 2. Nothing but what was given | Every door is one the host bound (D981); `kest_build_capability` lists them in groups before anything is bound. A machine started with `kest_start_untrusted` takes only the doors the host opened to code nobody trusts with `kest_host_open`, and is refused by name for any other (D1246). Every library door handed a handle asks whether this machine's heap handed it out, text a door is handed is one piece the verifier proved (D1244), and what a door answers is refused if the program could not have made it (D1254). `kest hostile` writes every door a program declares called with the ends of every width, floats that are not numbers and text of every length, for the host to run under its sanitisers; the engine in this tree is held to it and it found two of its doors turning an infinite float into a number (D1254). | Nothing structural: each door a host opens is the host's to get right, and `kest hostile` is how it asks. |
| 3. Everything bounded | Steps, the heap, how much memory reading and compiling may take (`--room`, every stage of it, D1247), how long compiling may take as a count of work that is the same on every machine (`--work`, `kest_build_within`, D1248), and how many of each thing a program may have (nesting, names, loops) are bounded. A machine started untrusted is refused without a ceiling on its fuel and its heap (D1246). | Nothing structural. A host that gives no ceiling on work has none, and a build with no ceiling can be made to take time that grows faster than the file does. |
| 4. Nothing of the process | A reference hashes by its place and not by its address (D1054). | An audit of every value a door or an instruction hands back. |
| 5. Only the machine | A machine started with `kest_start_untrusted` runs the instructions the verifier proved and never enters a body the release engine wrote as C, even one the host linked in (D1246); `tools/check-c.sh` counts the instructions the machine itself ran to say so. A release is written down as for code its author trusts (D1249). What it costs is measured: the machine runs the same instructions started untrusted as trusted, and 1.4 to 9.9 times the cycles of a release on the five workloads in `bench`. | Nothing structural. |
