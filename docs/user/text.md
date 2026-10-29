# Text

`text` is UTF-8 bytes and a length. This page covers writing strings, building text, walking it cheaply, and where Unicode stops being the language's business.

## Contents

- [Strings and holes](#strings-and-holes)
- [Escapes and bytes](#escapes-and-bytes)
- [Text is its bytes](#text-is-its-bytes)
- [Finding and cutting](#finding-and-cutting)
- [Building text](#building-text)
- [`std.text`](#stdtext)
- [Characters](#characters)
- [What a host decides](#what-a-host-decides)

## Strings and holes

A string may hold code in braces, a hole, and the hole's value is written into the text. There is no `+` on text: text is joined with holes.

```kest
module greet

import std.io

fn main() -> i32 {
    let name = "ann"
    let apples = 3
    io.print("{name} has {apples} apples, {apples * 2} by tomorrow")
    io.print("a hole holds code, so {len("quotes")} need no escape")
    return 0
}
```

```text
ann has 3 apples, 6 by tomorrow
a hole holds code, so 6 need no escape
```

A hole may hold any value that compares, written the way a program writes it; see [Data](data.md#what-compares-hashes-and-prints). A string with a hole is built while running, on the heap, so a function that promises `no.alloc` may hold a plain string but not fill a hole. A hole holds one value and nothing after it, so there is no width or format to write there (`"{n:4}"` is an error); an empty hole `{}` is an error, and so is a comment inside a hole.

## Escapes and bytes

The escapes are `\n`, `\t`, `\r`, `\\`, `\"`, `\{`, `\}`, `\0` and `\u{...}`. `\u{...}` is a character by its number, one to six hex digits up to `U+10FFFF`, written as its UTF-8 bytes; surrogates are refused. A file may not hold a carriage return inside a string, or an invisible character such as a zero-width joiner or a direction mark, written as itself: those are written with `\r` or `\u{...}` so a reader can see them.

`'a'` is one byte, of type `u8`. It is not a character: `'ı'` is two bytes and is an error. `\0` is an ordinary character inside text: text carries its length, so a zero byte in the middle is counted like any other.

## Text is its bytes

`len(t)` counts bytes, `t[i]` is a byte, and `for b in t` walks the bytes. Text compares with `==` and `<` by its bytes, which is a total order, and `t != ""` asks whether anything is left. There is no character type: `"hız"` is four bytes.

All of these are cheap because the length is part of the value: `len` is a read, not a walk.

## Finding and cutting

The builtins for searching and cutting:

| Call | What it gives | Heap |
| --- | --- | --- |
| `find(t, needle)` | where `needle` first is, as `i32?` | none |
| `find(t, needle, from)` | the same, starting at `from`, as a place in all of `t` | none |
| `matches(t, at, needle)` | whether `needle` is exactly at `at` | none |
| `rest(t, at)` | the text from `at` to the end | none |
| `slice(t, from, count)` | `count` bytes from `from` | none |

A cut is a place inside the text it was cut from and a length, so none of these copies anything. Starting past the end is an error rather than a read past it; starting exactly at the end finds nothing or gives empty text. A position below zero written as a number is refused while compiling.

The cheap way to walk text is forwards, keeping what is left:

```kest
module fields

import std.io

fn count(line: text, separator: text) -> i32 no.alloc {
    let seen = 0
    let tail = line
    while tail != "" {
        seen += 1
        if let at = find(tail, separator) {
            tail = rest(tail, at + len(separator))
        } else {
            tail = ""
        }
    }
    return seen
}

fn main() -> i32 {
    io.print("{count("ann,bo,cy", ",")}")
    return 0
}
```

```text
3
```

## Building text

Text built a piece at a time is built as bytes. `push(out, piece)` appends a whole piece of text to a `[u8]`, and `text(out)` makes one piece of text at the end, refusing bytes that are not UTF-8. Gathering bytes copies each piece once; joining with a hole in a loop copies everything gathered so far every time round.

```kest
module building

import std.io

fn listed(names: [text]) -> text {
    let out: [u8] = array()
    for at, name in names {
        if at > 0 {
            push(out, ", ")
        }
        push(out, name)
    }
    return text(out)
}

fn main() -> i32 {
    io.print(listed(["ann", "bo", "cy"]))
    return 0
}
```

```text
ann, bo, cy
```

`fit(out, piece)` appends without growing and is all or nothing: it answers `false` and leaves `out` as it was when the piece does not fit. With a buffer made once with room, `text.fitting` and `text.fittingNumber` build a line in a function that promises `no.alloc`. A `[u8]` with any bytes in it is fine; only `text(...)` cares about UTF-8.

## `std.text`

What a program does with text beyond the builtins is `std.text`, written in Kest:

- reading: `number` and `real` give `T?` and refuse anything that is not exactly a number of that type, including one too large for it; there are no exponents in what they read except the spellings a hole writes;
- writing: `fixed(value, places)` writes a float with that many places; `left` and `right` pad to a column width in bytes;
- testing: `contains`, `starts`, `ends`;
- cutting and joining: `trim`, `split`, `join`, `repeat`;
- ASCII case: `upper`, `lower`;
- characters: see below.

```kest
module words

import std.io
import std.text

fn main() -> i32 {
    let line = "  ann,bo,cy  "
    let names = text.split(text.trim(line), ",")
    io.print("{len(names)} {names[1]} {text.join(names, " and ")}")
    if let n = text.number("42") {
        io.print("{n + 1} {text.upper("hi")} {text.contains(line, "bo")}")
    }
    io.print("{text.fixed(2.0 / 3.0, 2)} [{text.right("7", 3)}]")
    return 0
}
```

```text
3 bo ann and bo and cy
43 HI true
0.67 [  7]
```

`kest doc lib/std/text.kest` lists every function with its comment.

## Characters

A character here means what UTF-8 says. `text.chars(t)` counts characters, `text.charWidth(t)` is the byte width of the character at the front of `t`, `text.charBytes(b)` is the width a leading byte announces (0 for a continuation byte), `text.charBack(t, at)` is where the character before `at` starts, and `text.charAt(t, i)` and `text.charsOf(t)` give characters as text of their own. Bytes that are not valid UTF-8 count as one character each, so a count never stops early.

There is no `for` over characters. The cheap walk keeps what is left and steps by width, which reads the text once and allocates nothing:

```kest
module characters

import std.io
import std.text

fn characters(line: text) -> i32 no.alloc {
    let count = 0
    let tail = line
    while tail != "" {
        tail = rest(tail, text.charWidth(tail))
        count += 1
    }
    return count
}

fn main() -> i32 {
    if let second = text.charAt("hız", 1) {
        io.print("{characters("hız")} {len("hız")} {second}")
    }
    return 0
}
```

```text
3 4 ı
```

`charAt` counts from the start each time it is asked, so a loop over `charAt` reads the text once per character; `charsOf` walks once.

## What a host decides

The language owns UTF-8 and nothing above it: that source and incoming bytes are valid, counting and stepping over characters, comparing and searching by bytes (correct for UTF-8), and ASCII case. A host owns everything that depends on a human language or a Unicode version: normalization (`é` and `e` plus an accent are different text here), collation, locale-aware case (the capital of `i` in Turkish), grapheme clusters, display width, bidirectional text and shaping. A program that needs those asks a host door for them; `examples/locale.kest` shows the line.
