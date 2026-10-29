# Diagnostics

The compiler reports every error it can find in one pass, not the first. Each has a stable code, the place, the reason, and what to write instead where that is known. Failures while running have the same shape, so nothing that reads diagnostics needs to know which kind it has.

## Reading one

```
error[K0201]: expected end of line, found `{`
  --> typo.kest:16:15
   |
16 |     let p = P { x: 1, y: 2 }
   |               ^ a struct is built by position, in the order its fields are declared: `P(...)`
```

A diagnostic about more than one place shows each: a broken promise shows the line that breaks it, the promise, and every call in between; a failure while running shows the calls that led to it, outermost last. A diagnostic shows at most eight places and says how many it left out. A warning starts with `warning` and does not stop the build.

## Codes

A code's meaning does not change between versions; its words may be improved. The first two digits say where it comes from:

| Codes | From |
| --- | --- |
| `K01xx` | reading the characters of a file: an unterminated string, a byte that begins no character, an escape that is not one |
| `K02xx` | the shape of the program: a missing bracket, a statement that cannot start there |
| `K03xx` | types and bodies: an unknown name, a type mismatch, a missing case, an argument count |
| `K04xx` | promises: `no.alloc`, `no.host` and `deterministic` |
| `K05xx` | what cannot be compiled: a limit reached, a constant that cannot be worked out, an unused declaration |
| `K06xx` | running: division by zero, an index past the end, a ceiling, a budget, a failed door, a host mistake caught at the boundary |
| `K07xx` | files and modules: an import that is not found, a module in the wrong place, a file that cannot be read |

## The mistakes newcomers make

These are what people and models new to Kest write most often, and what the compiler says.

| Written | Said | Instead |
| --- | --- | --- |
| `max(1, 2)` without importing | `K0306` unknown name `max` | `import std.math` and write `math.max(1, 2)` |
| `P { x: 1, y: 2 }` or `P(x: 1)` | `K0201` a struct is built by position | `P(1, 2)` |
| `io.print(count)` | `K0310` `value` expects `text`, found `i32` | `io.print("{count}")` |
| `"{f(\"x\")}"` | `K0102` a hole holds code, so a string inside one needs no escape | `"{f("x")}"` |
| `"{n:4}"` | `K0201` expected the end of the hole, found `:` | `"{text.right("{n}", 4)}"` with `import std.text` |
| `fn hurt(n: Npc) { n.hp -= 1 }` | warning `K0346`: the parameter is a value, so the write is discarded | `fn hurt(n: Npc) -> Npc` and give the changed one back |
| `store<Node>()` | `K0211` the type is not given where it is called | `let nodes: store<Node> = store()` |
| `if (x < 3) { }` | `K0213` a condition is written without brackets round the whole of it | `if x < 3 { }` |
| `match` over a number or text | `match` chooses between the cases of an enum | `if` |

Three mistakes the compiler cannot refuse: a field of a parameter written and not given back (it warns), `remove(xs, i)` inside `for i in 0..len(xs)`, which reads past the end once something is removed (walk a store instead, or gather what to remove and remove it after), and treating a struct as shared when it is a copy.

## Warnings worth reading

| Code | Says | Why it is there |
| --- | --- | --- |
| `K0346` | a write to a parameter's field is discarded | the most common bug for someone coming from a language where objects are references |
| `K0347` | an array has no room, so every `fit` into it writes nothing | `array()` makes no room; give it some with `room` or `array(n, v)` |
| `K0506` | nothing calls an `extern`, so no host is asked for it | a host author reading the file would bind it for nothing |
| `K0508`, `K0509` | nothing reads a constant, nothing names a type | dead declarations |
| `K0511` | nothing writes through an import | an import is read and compiled whether used or not |
| `K0512` | nothing reads a local | a value worked out for nobody, or an `if let` whose name could be `!= none` |

These are said about the file that was named, not the library it imports.

## JSON

`--json` emits the same set, notes and suggestions included, for tools and for models repairing their own output:

```json
{
  "schema": 4,
  "diagnostics": [
    {
      "severity": "error",
      "code": "K0309",
      "file": "bad.kest",
      "line": 8,
      "column": 12,
      "offset": 109,
      "length": 7,
      "message": "`doc.hurt` takes 2 arguments, found 1",
      "notes": [
        {"file": "bad.kest", "line": 3, "column": 19,
         "message": "this one was not written"}
      ]
    }
  ],
  "errors": 1
}
```

A diagnostic about a whole file has `file` and no line; one about the whole program has neither. `suggestion` is the line shown under the caret, and `leftOut` counts the places that did not fit. `schema` changes when a field changes meaning or goes away, not when one is added; see [Versions and editions](editions.md).

## Limits

A few numbers are limits because an instruction holds them in two bytes or a frame counts them in one. Each is a diagnostic with the number in it (`K0502` for how many, `K0503` for how far, `K0630` for more than `len` can count), never a wrap or a silent cut:

| At most | What |
| --- | --- |
| 256 | names in a function, counting its parameters |
| 16 | loops one inside another |
| 128 | expressions one inside another |
| 32 | `break`s in one loop, and 32 `continue`s |
| 32 | `defer`s in a function |
| 65535 | bytes of code a jump reaches, or a loop reaches back |
| 8 | `scratch { }` blocks one inside another in one body |
| 64 | `scratch { }` blocks one machine holds open at once |
| 8 | things one `match` chooses between at once |
| 256 | combinations one `match` answers before it needs an `else` |
| 65535 | elements in a `[T; N]`, and at least one |
| 65536 | doors a program asks the host for |
| 2147483647 | elements in an array or a store, and bytes in text |
| 16777215 | places in one store |
| 1099511627775 | places a process hands out, counting the ones reused |

All but the last three are found while compiling. The rest are met while running.
