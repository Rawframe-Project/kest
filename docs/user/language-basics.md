# Language basics

How a Kest file is laid out, its names, numbers, operators and control flow. [Data](data.md) covers structs, arrays, stores, enums and optionals; [Text](text.md) covers strings; [Functions](functions.md) covers generics, function values and blocks.

## Contents

- [Files and modules](#files-and-modules)
- [Lines, blocks and comments](#lines-blocks-and-comments)
- [Names, `let` and `const`](#names-let-and-const)
- [Numbers and conversions](#numbers-and-conversions)
- [Operators](#operators)
- [Control flow](#control-flow)
- [`defer`](#defer)
- [When a program faults](#when-a-program-faults)
- [Keywords](#keywords)

## Files and modules

A file that starts with `module` is a module, and its name is its path: `module game.map` is `game/map.kest`, relative to the root the first file was found in or to a project's `source` directories. A file with no `module` line is a program to run and cannot be imported.

What another module declares is always written through the last part of its name, including after `import`: `map.Tile`, `map.load(...)`. There is no `use`, no bare import and no wildcard, so every name says where it came from.

```kest
module shop

import std.io
import std.math

fn main() -> i32 {
    io.print("{math.max(2, 7)}")
    return 0
}
```

```text
7
```

A module may write its own declarations without a prefix. Two modules may end in the same word (`render.math` and `physics.math` can both be in one program), but one file may not import both, since `math.` would then mean either; the error points at both import lines. `std` is reserved for the standard library.

Two files may import each other. Each file is read once, however many files import it.

## Lines, blocks and comments

A newline ends a statement; there are no semicolons. A statement continues onto the next line while a bracket is open or when the line ends in a binary operator. One exception: a line may end after `>` only where a type ends (`ref<Npc>`), so a comparison has to stay on one line.

Blocks are braces, always, even around one statement. Conditions take no parentheses: `if x < 3 { }`, never `if (x < 3) { }`. Comments are `//` to the end of the line, and there is no other kind. Names are UTF-8, so `let hız = 5` is a valid line.

The layout is the formatter's: four spaces a level, one space around a binary operator, and a list that does not fit in 80 columns goes one item to a line. `kest fmt -w` writes it and `kest fmt --check` names the files that are not in it. A file that does not parse is never rewritten.

## Names, `let` and `const`

`let` names a value. Its type comes from the value and may be written. A `let` always has a value: `let a: i32` on its own is an error, so no name is ever read before it holds something.

`const` names a value worked out while compiling, and always writes its type, because it can be read from other files.

```kest
module totals

import std.io

const SLOTS: i32 = 4 * 8
const STEP: f32 = 1.0 / 60.0

fn main() -> i32 {
    let count = 3
    let speed: f32 = 2.5
    count += SLOTS
    io.print("{count} {speed * STEP * 60.0}")
    return 0
}
```

```text
35 2.5
```

A constant may be arithmetic on numbers and other constants, a conversion, a struct or enum value built from those, a fixed run such as `[1.0, 0.5, 0.25]`, `len` of a fixed run and `hash` of a value. It may not call a function, choose between values with `if` or `match`, or fill a hole in a string; those happen while running. A constant can count a fixed run: `[i32; SLOTS]`, or `[Npc; npc.PARTY]` from another module.

Inside a body, a `let` or a parameter may reuse the name of something the file declares. From there on the name means the local, and the file's own is written with its module in front. Two locals may not share a name.

## Numbers and conversions

| Type | What |
| --- | --- |
| `i8` `i16` `i32` `i64` | signed whole numbers |
| `u8` `u16` `u32` `u64` | unsigned whole numbers |
| `f32` `f64` | IEEE 754 floats |
| `bool` | `true` or `false` |

A whole-number literal is an `i32` unless the place it goes wants another width, and a literal with a point is an `f32` unless an `f64` is wanted. A literal that does not fit where it is written is an error: `let x: u8 = 300`.

Nothing converts on its own. Naming a type converts: `f32(n)`, `i32(x)`, `u8(300)`. The rules are the same while compiling and while running:

- Whole numbers wrap at their width, in both directions. Narrowing keeps the low bits, so `u8(300)` is 44.
- A float converted to a whole number truncates toward zero and stops at the ends of the range; a float that is not a number converts to 0.
- `/` truncates toward zero, and `%` takes the sign of the left side, for floats too: `-7.5 % 2.0` is `-1.5`.
- Dividing a whole number by zero stops the program. Dividing a float by zero gives an infinity, or `nan` for `0.0 / 0.0`, as IEEE 754 says.
- The smallest number of a width divided by -1 is itself, and its remainder is 0.
- A shift count at or past the width shifts everything out: `1 << 64` on an `i64` is 0 and `-8 >> 64` is -1. A negative count is an error.

```kest
module widths

import std.io

fn main() -> i32 {
    let n = 7
    let half = f32(n) / 2.0
    let small = u8(300)
    let wide: i64 = 5000000000
    let cut = i32(-2.9)
    io.print("{half} {small} {wide} {cut} {-7 % 3}")
    return 0
}
```

```text
3.5 44 5000000000 -2 -1
```

`bits(x)` is a float's bits as a `u32` or `u64`, and `float(b)` turns them back; neither rounds. `f32` and `f64` are separate types and an `f32` operation rounds to `f32` at every step.

## Operators

Tightest first:

```
~  -  !               on one value
*  /  %
+  -
<<  >>
&
^
|
<  <=  >  >=
==  !=
&&
||
```

The bitwise operators bind tighter than comparisons, so `state & MOVING == MOVING` means what it looks like. `&`, `|`, `^` and `~` work on whole numbers and on [sets of flags](data.md#flags); `bool` has `&&`, `||` and `!`. `>>` brings in the sign on a signed type and zeros on an unsigned one.

Assignment is `=`, `+=`, `-=`, `*=` and `/=`, and nothing else: there is no `++`, no `%=` and no ternary operator. There is no `+` on text; text is joined with holes, see [Text](text.md).

A statement that only computes a value is an error, which catches `a == b` written where `a = b` was meant. A call may be a statement on its own, and its answer may be ignored.

## Control flow

```kest
module control

import std.io

fn main() -> i32 {
    let hp = 3
    let said = if hp > 0 -> "alive" else -> "down"
    let total = 0
    for i in 0..5 {
        if i == 2 {
            continue
        }
        if i == 4 {
            break
        }
        total += i
    }
    let left = 10
    while left > 7 {
        left -= 1
    }
    for at, x in [5, 6] {
        total += at * x
    }
    io.print("{said} {total} {left}")
    return 0
}
```

```text
alive 10 7
```

- **`if`** gives a value when its arms are written with `->`, and then it needs an `else`. All arms of one `if` are either values or blocks. `else` may start the next line in an `if` that gives a value.
- **`for`** walks a range `a..b` (up to but not including `b`), an array, a fixed run, text (a `u8` at a time), a store (a reference at a time) and a set of flags. `for i, x in xs` also gives the position. A range is worked out once, and an array walk takes its length once, so pushing inside the body does not lengthen the walk; removing inside it is an error when the walk reaches past the end.
- **`while`**, **`while let`**, **`break`** and **`continue`** work as expected; `break` and `continue` belong to the loop they are written in.
- **`match`** chooses between the cases of an enum, see [Data](data.md#enums-and-match).

A function that gives something back has to end in something that gives it: a `return`, an `if` or `match` whose every arm does, or a `while true` that nothing breaks out of. A `for` and a `while let` do not count, since they may run no times.

`main` takes nothing and gives `i32` or nothing; one file may declare only one `main`. An exit status holds 0 to 255, and a `main` that answers anything else is an error rather than a number cut to fit.

## `defer`

`defer f(x)` runs the call when the enclosing block ends, however it ends: at the closing brace, through `return`, `break` or `continue`. Several run in reverse order. A block on its own is a statement, which gives a name a shorter life and makes a `defer` run sooner.

```kest
module closing

import std.io

fn close(what: text) {
    io.print("closed {what}")
}

fn main() -> i32 {
    {
        defer close("the first")
        defer close("the second")
        io.print("inside")
    }
    io.print("after")
    return 0
}
```

```text
inside
closed the second
closed the first
after
```

A `defer` takes a call and nothing else, and the call sees the names as they are when the block ends, not when the `defer` was written. What a deferred call does counts against the function's [promises](promises.md).

## When a program faults

A program that divides a whole number by zero, reads past the end of an array or runs out of its budget stops there with an error naming the line and the calls that led to it. That is a fault, not an exception: nothing catches it, and no `defer` runs. The host is told the call failed, and the program's state is whatever the fault left; nothing is rolled back. [Embedding Kest](embedding.md#when-a-call-fails) covers what a host does about it.

For an expected failure, a function answers with a value instead. [Data](data.md#saying-that-something-failed) lists the three shapes.

## Keywords

```
break   const   continue  defer   else    enum    extern
false   fn      for       if      import  in      let
match   module  none      return  struct  true    while
```

`flags`, `scratch`, `own`, `compares`, `orders`, `resumes`, `wait` and `block` mean something only where they stand (a declaration, a block, a field, a type parameter, a signature) and are ordinary names everywhere else. `type` is not reserved.
