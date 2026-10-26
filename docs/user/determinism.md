# Determinism

A replay, a rollback or a lockstep game between two machines needs the same program to compute the same world everywhere. Kest writes that promise down as a versioned profile, `kest-det 3`, and a function promises to keep it with `deterministic`.

## Three questions

- **Does the same source mean the same thing?** Yes. Every rule below is the language's, not the C compiler's.
- **Does the same program on one machine do the same thing twice?** Yes. Nothing reads a clock, an address or the environment unless the program asks a host door, and nothing about a run is seeded from outside.
- **Is a run bitwise identical on two platforms?** Yes, for everything in the profile, including `sin`, `cos`, `pow` and `atan2`. CI runs every example on Linux x86-64, Linux arm64, macOS arm64 and Windows x86-64 and fails if any of them prints a different byte.

## The profile

**What a target must be.** Two's complement integers of 8, 16, 32 and 64 bits; IEEE 754 binary32 and binary64 in the default rounding mode (round to nearest, ties to even); 8-bit bytes. A host that changes the rounding mode, or turns on flush-to-zero, has left the profile.

**Where state comes from.** Every allocation starts as zeros, and no value a program can read comes from an address, a clock or the order files were read in. A reference is the one value that holds something from outside the program (which handout of the process it was), and a program cannot read that: a reference has no text and no number, and it hashes by its place.

**Whole numbers.** Every width wraps at its own end. Narrowing keeps the low bits; widening a signed number carries its sign. Division truncates toward zero and the remainder takes the left side's sign. Division by zero is a fault, never a value.

**Floats.** `f32` arithmetic rounds to `f32` at every step, and a multiply followed by an add is always two roundings: nothing is ever fused into one. Subnormals behave as IEEE 754 says. The two zeros compare equal and hash alike. Every value that is not a number has one set of bits (`0x7FC00000` for `f32`, `0x7FF8000000000000` for `f64`) as far as `bits` and `hash` can see, because processors disagree about the sign of `inf - inf`.

**Floats to whole numbers.** Truncate toward zero, stop at the ends of the width, and give 0 for a value that is not a number, the same while compiling and while running.

**Mathematics.** `sin`, `cos`, `pow` and `atan2` in `std.math` are written in Kest out of additions, multiplications and divisions (fdlibm's algorithms, in `std.fdlibm`), so they are the same steps on every machine. They are within an ulp of the C library's for ordinary arguments. `sqrt`, `floor` and `ceil` are host doors that IEEE 754 requires to be exact, and `std.math` declares them `deterministic`.

**Order.** An array walks by index, a store by place, text by byte, a range from first to last, and a table in the order its puts and removes left, which the same operations always reproduce.

**Hashing and randomness.** `hash` is FNV-1a from a fixed start for text and fixed arithmetic for everything else, with nothing taken from the run. `std.random` is a source the program holds and seeds; the same seed gives the same numbers everywhere, and none of its functions call the host.

**What is outside.** Clocks, files, the program's arguments and every host door not declared `deterministic`.

## Promising it

```kest
module lockstep

struct Unit {
    x: f32
    vx: f32
}

fn step(u: Unit, dt: f32) -> Unit no.alloc deterministic {
    return Unit(u.x + u.vx * dt, u.vx * 0.98)
}
```

The compiler refuses a `deterministic` function that can reach anything outside the profile, with the path to the call that does. A host asks with `kest_entry_promises(..., KEST_PROMISE_DETERMINISTIC)` before installing a step it means to replay, and writes `kest_profile()` down beside a replay or a save, because a run under another profile is a different run.

## What a host owes

The three doors `std.math` declares `deterministic` are promises the host keeps: `Math.sqrt` must be correctly rounded, and `Math.floor` and `Math.ceil` exact, including for negatives and for values that are already whole. A host that binds a fast approximate square root has left the profile. `examples/determinism.kest` folds every rule above, including those doors, into one number and prints each part, so a platform that disagrees says where.

The release engine (`kest build --release`) keeps the profile too, as long as the C compiler does not contract floating point on its own: GCC and MSVC respect what the generated file asks for, and clang told `-ffp-contract=fast` does not. See [Release builds](release-builds.md).
