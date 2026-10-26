# Coming from Lua or Rust

What is different in Kest, ordered by how often it trips people up. Each point links to the page with the whole story.

**A name keeps its module.** After `import std.math`, `max` is written `math.max`. There is no `use`, no bare import, and no global `print`: printing is `io.print` from `std.io`, and it takes text. See [Language basics](language-basics.md#files-and-modules).

**A struct is built by position.** `Point(3, 4)`, in the order the fields are declared. There are no struct literals with names and no named arguments. See [Data](data.md#structs).

**A struct is a value, an array is a handle.** Handing a struct to a function hands over a copy, so writing its fields changes nothing for the caller; give the changed struct back. Arrays, stores and tables are handles and are shared. This is the mistake Lua programmers make most. See [Data](data.md#values-and-handles).

```kest
module bodies

import std.io

struct Body {
    x: f32
    speed: f32
}

fn moved(b: Body, dt: f32) -> Body {
    let after = b
    after.x += b.speed * dt
    return after
}

fn main() -> i32 {
    let b = Body(0.0, 2.0)
    b = moved(b, 0.5)
    io.print("{b.x}")
    return 0
}
```

```text
1.0
```

**Arrays count from zero**, and reading past the end stops the program instead of answering `nil`.

**Numbers are sized and never convert on their own.** A whole number is `i32` and a float is `f32` unless written otherwise, and `f32(n)` converts. Whole numbers wrap at their width; a float converted to a whole number truncates and saturates. See [Language basics](language-basics.md#numbers-and-conversions).

**There is no `nil`.** A value that may be missing is `T?`, opened with `if let`, and asked about with `== none`. See [Data](data.md#optionals).

**`match` is for enum cases only.** Numbers and text are chosen between with `if`. An `if` gives a value with `->` arms: `let s = if hp > 0 -> "alive" else -> "down"`. There is no ternary. See [Data](data.md#enums-and-match).

**Text is bytes and is joined with holes.** `"{name} has {n}"`, never `+` or `..`. `len` counts bytes, and characters are what `std.text` says UTF-8 means. See [Text](text.md).

**No closures.** A function value names a function, and what a closure would capture is passed as an argument. For a body written at the call site, use a [block](functions.md#blocks): `xs.countWhere(|x| x > floor)` reads `floor` where it is written.

**No exceptions, no `Result`, no `?` operator.** A function that can fail answers `T?`, `bool` or an enum of its own. See [Data](data.md#saying-that-something-failed).

**No lifetimes, no borrow checker, no `unsafe`.** Memory is collected. Entities that point at each other live in a `store<T>` and hold `ref<T>`s, which go stale safely when what they named is removed. See [Data](data.md#stores-and-references).

**Generics ask for two things only.** A type parameter may require `compares` or `orders`, and nothing else; there are no traits, no operator overloading and no methods declared inside types. `x.f(a)` calls `f(x, a)`. See [Functions](functions.md).

**The compiler proves performance promises.** `no.alloc`, `no.host` and `deterministic` on a function are checked over everything it calls. A frame step is usually all three. See [Promises](promises.md).

**The program can only do what the host lets it.** There is no file, clock or network access except through doors the host binds; the command line binds files, arguments and a clock through `std.os`. See [Embedding Kest](embedding.md#doors-what-a-program-asks-the-host-for).

**One layout.** `kest fmt -w` writes it; there are no style options. Braces always, no semicolons, no parentheses around conditions, `//` comments only.
