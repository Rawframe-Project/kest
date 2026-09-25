# Proposals

A change to what a program, a host or a tool leans on starts here, as a file,
before any code is written. That is syntax, what a program means, the shape of
the standard library, a door of the C API, a name in the JSON, and the
deterministic profile. A fix to something that does not do what the reference
says it does is not a proposal; it is a fix, and `CHANGELOG.md` says it was one.

## What one is

A file in this directory named by its number and a few words, numbered in the
order they are written, with these parts and in this order:

- **What changes.** The rule, the door or the name, as the reference would say
  it once it is taken.
- **Why.** What a program, a host or a tool cannot do today, shown with the
  smallest thing that asks for it.
- **What it breaks, and for whom.** Every program, host or tool that means
  something else or stops working afterwards, and what each has to write
  instead. "Nothing" is an answer, and it is the one that needs showing.
- **The edition.** A change that stops a program compiling or makes it mean
  something else is made under a new edition, and says which. Everything else
  is made in every edition.
- **What else was weighed.** Each other way of getting there, and why it was
  not taken.

## What happens to one

It is read against the promise in the reference (*What is promised to a
program, and how a change is made*). One that is taken becomes a decision in
`docs/decisions.md`, which names the proposal, and the code that makes it
follows. One that is not taken stays here with why at its end, so the same
proposal is not written twice.

Something a new edition will refuse is warned about, with its own code and
what to write instead, in the edition before it. A name the library had and
has not is refused with what took its place (`kest_retired` in
`src/types.c`), so a break says what to do about itself.
