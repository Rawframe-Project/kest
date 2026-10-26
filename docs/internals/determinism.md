# Determinism

How the profile in [Determinism](../user/determinism.md) is kept, and what to watch when changing anything that touches arithmetic, iteration order or hashing.

## The arithmetic is the language's

Every operation whose result could differ by platform or compiler is either defined by Kest and implemented the same in the folder, the machine and the C backend, or behind a host door.

- **Integers** are computed in 64-bit slots and cut to their declared width after every operation, so wrapping is defined at every width and never relies on C's signed overflow (which is undefined). Shifts at or past the width, `INT_MIN / -1` and its remainder, and narrowing are all given answers in one place each, and the folder calls the same functions the machine does.
- **Float to integer** truncates toward zero, saturates at the width and gives 0 for not-a-number, in one function shared by the folder and the machine, because C's conversion is undefined out of range.
- **`f32`** is rounded to `f32` after every operation even though a slot holds a double. Vectors are their components' single operations.
- **No contraction.** A multiply then an add is two roundings in both engines. The machine never fuses them; the generated C turns contraction off with pragmas, and the one compiler setting that overrides them (clang `-ffp-contract=fast`) is documented as outside the profile.
- **Not-a-number has one spelling.** `bits` gives `0x7FC00000` for every `f32` NaN and `0x7FF8000000000000` for every `f64` NaN, and `hash` hashes each as that, because x86 and arm64 produce NaNs with different signs from the same operation (`inf - inf`).
- **Zero.** `0.0` and `-0.0` compare equal and hash alike.

## Mathematics in Kest

`sin`, `cos`, `pow`, `atan` and `atan2` are fdlibm's algorithms written in Kest, constant by constant, in `lib/std/fdlibm.kest`, so they are the same sequence of additions, multiplications and divisions everywhere. `tan` is a sine over a cosine; `asin` and `acos` are built from `atan2` and a square root. Against glibc they agree to within an ulp for ordinary arguments; past about 1.6 million a sine is the same on every machine and further from the true value. Only `sqrt`, `floor` and `ceil` are host doors, declared `deterministic` because IEEE 754 requires them to be correctly rounded and exact. Before this, the four were the host's libm and outside the profile; moving them into Kest is what profile 3 changed.

## Order and hashing

- Iteration order is defined for every container: arrays by index, stores by place, text by byte, tables in the order their puts and removes left (a deterministic order, not a sorted one).
- `hash` of text is FNV-1a over the bytes from a fixed offset basis, which is the same number a build gives as the mark of a file holding those bytes. Hashes of other types use the compiler's own fixed arithmetic. Nothing is seeded from the run. The same arithmetic is used for folded constants and at run time, in one walk in the type layer.
- `std.hash.join` is the one fold for combining hashes, so two programs folding the same fields agree.
- A reference's stamp comes from a process-wide counter, so it depends on how many places every machine in the process has made. The program cannot see it: references have no text or number, and hash by place. Hashing the stamp would have made a `deterministic` function answer differently on the second machine of a run; that was a real bug once.

## What a program starts with

Every allocation is zeros. No value a program can read comes from an address, a clock, the environment or the order files were read in. The compiler reads files in import order, which is fixed by the source.

## Holding it

- `examples/determinism.kest` exercises every rule (wrapping, narrowing, float to integer, `f32` and `f64` rounding, signed zero, subnormals, NaN, walk order, text hashing, a seeded stream, a table walk, the mathematics and the three doors) and folds each part and the whole into numbers it prints.
- `tools/conformance.sh` runs every example and records what each prints into a trace; CI makes the trace on Linux x86-64, Linux arm64, macOS arm64 and Windows x86-64, and the `agree` job fails if any two differ by a byte.
- `.github/workflows/determinism.yml` runs one simulation in Kest, Lua 5.4, LuaJIT, Luau and JavaScript on the four platforms and charts which languages agree with themselves across machines; it measures other languages and is not part of the gate.
- Any change to arithmetic, NaN handling, hashing or iteration order is a change to what programs observe, which moves the profile number and is named in the changelog.
