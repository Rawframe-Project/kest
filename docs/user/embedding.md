# Embedding Kest

A host is any C or C++ program that runs Kest: an engine, a tool, a server. It includes `include/kest.h`, links `libkest.a` (which needs only the C library and `-lm`), compiles a program, binds the doors the program asks for, starts a machine and calls into it. This page covers that path; [Host memory](host-memory.md) covers text, lending memory, keeping values between frames and reloading, and [Running untrusted code](untrusted-code.md) covers mods and player scripts. The API reference, generated from the header, is on the documentation site.

## Contents

- [The shape of a host](#the-shape-of-a-host)
- [Doors: what a program asks the host for](#doors-what-a-program-asks-the-host-for)
- [Calling in: frames](#calling-in-frames)
- [Sizing a machine](#sizing-a-machine)
- [When a call fails](#when-a-call-fails)
- [Budgets, cancelling and threads](#budgets-cancelling-and-threads)
- [Versions](#versions)
- [The example hosts](#the-example-hosts)

## The shape of a host

The program:

```kest
module rules

extern fn Host.write(value: text) no.alloc

fn damage(hp: i32, hit: i32, armour: i32) -> i32 no.alloc deterministic {
    let taken = hit - armour
    if taken < 1 {
        return hp - 1
    }
    return hp - taken
}

fn announce() {
    Host.write("the rules are loaded\n")
}
```

The host:

```c
#include <stdio.h>

#include "kest.h"

// The door the program declares as `extern fn Host.write(value: text)`.
static void write_it(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(&frame[0], &length);
    fwrite(bytes, 1, length, stdout);
}

int main(void) {
    KestBuild *build = kest_build("rules.kest", NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 1;
    }
    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Host.write", write_it, NULL)) {
        kest_host_free(host);
        kest_build_free(build);
        return 1;
    }
    KestLimits limits = {0, 0, 1024 * 1024, 0};
    KestRuntime *vm = kest_start(build, host, &limits);
    if (vm == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        kest_host_free(host);
        kest_build_free(build);
        return 1;
    }
    kest_call(vm, kest_entry(vm, "announce"), NULL, 0);

    int32_t damage = kest_entry(vm, "damage");
    KestValue frame[3] = {{.integer = 80}, {.integer = 25}, {.integer = 5}};
    if (kest_call(vm, damage, frame, 3)) {
        printf("hp left: %lld\n", (long long)frame[0].integer);
    } else {
        kest_report(vm, stderr, KEST_FORM_TEXT);
    }
    kest_runtime_free(vm);
    kest_host_free(host);
    kest_build_free(build);
    return 0;
}
```

```sh
cc -std=c11 -Iinclude host.c libkest.a -lm -o host && ./host
```

The objects, and what must outlive what:

| Object | Made by | Freed by | Notes |
| --- | --- | --- | --- |
| `KestBuild` | `kest_build`, `kest_build_within`, `kest_build_from` | `kest_build_free` | the compiled program; read-only once built, and it outlives every machine started from it (`kest_build_free` refuses while machines stand on it) |
| `KestHost` | `kest_host_new` | `kest_host_free` | a list of bound doors; a machine copies it at start, so it may be freed once every machine using it has started |
| `KestRuntime` | `kest_start`, `kest_start_untrusted` | `kest_runtime_free` | a machine: its own stack, heap and report; one build may start many |

`kest_build` takes the path, the standard library directory (`NULL` for the usual search), where to write what stopped the build, the form to write it in, and a ceiling on the bytes compiling may take (0 for none). `kest_build_from` compiles files held in memory, which is how a release binary carries its program. Compiling a large program takes tens of milliseconds and starting a machine from a build takes well under one, so a host that wants many machines compiles once.

## Doors: what a program asks the host for

A program reaches the outside only through `extern` functions, and a host provides each by name. The part before the dot is a capability: `Host.write` is under `Host`, `Clock.now` under `Clock`. Denying a capability is not binding it. A program that declares a door the host did not bind is refused at `kest_start`, by name, before anything runs.

A host that did not write the program reads what it asks for before deciding: `kest_build_extern(build, i)` walks every door the program can call (from 0 until it answers `NULL`), `kest_build_capability(build, i)` walks the capabilities, and `kest_extern_takes`, `kest_extern_layout` and `kest_extern_gives` say what crosses at each door. A door is bound once; binding a name again is refused. A door nothing in the program calls is not on the list, and the compiler warns about it (`K0506`).

A bound function receives the frame: its arguments laid out in slots, which it overwrites with its answer. A door that cannot do what it was asked calls `kest_native_failed(runtime, why)` and returns; the call in the program then fails with the host's words (`K0662`) and whatever the door wrote is not read.

`std.io`, `std.math` and `std.os` declare doors too. A program that imports `std.math` asks for `Math.sqrt`, `Math.floor` and `Math.ceil`, whichever functions it calls, because the host may call any function of the program.

## Calling in: frames

`kest_entry(vm, name)` finds a function once; it answers -1 for a name the program does not define, and is refused (with the copies named in the report) for a name that is several functions. Calls then go by the number. A number belongs to the build it came from, so after a reload a host asks again.

`kest_call(vm, entry, frame, slots)` puts the arguments in the frame and writes the answer over them. A frame is `KestValue` slots, and a value takes one slot per scalar:

- whole numbers and `bool` go through `.integer` (a `bool` is 0 or 1);
- `f32` and `f64` go through `.real` (an `f32` in a slot is a double);
- text is two slots, the bytes and the length; a host reads it with `kest_text_bytes` and hands it in with `kest_text`, which copies the host's bytes onto the machine's heap;
- a struct is its fields in order, a `vec3` is three slots, and an enum is its tag followed by its widest case's slots.

The frame has to be as wide as the larger of what goes in and what comes out; `kest_frame_slots` says how wide. `kest_frame_takes` and `kest_frame_at` say where each argument starts, `kest_frame_layout` and `kest_frame_gives` describe each argument and the answer piece by piece, and `kest_slot_of` says which member of `KestValue` a piece goes through. `kest_frame_fills` and `kest_frame_reads` let a host state what it is about to write or read, and the machine checks that against the program before the call. A host that only has words (a console, a config file) hands them over with `kest_takes_text` and reads the answer back with `kest_gave_text`.

The machine checks what crosses where it can: a handle or text that did not come from this machine, a tag that is no case of the enum, or a frame too narrow are refused before the call runs. What it cannot check is a bound function that reads more slots than its declaration says.

Calling by name costs a search; calling by number costs about the same as a call inside the program. A host that has many things to process hands a batch in one call (a lent array, see [Host memory](host-memory.md#lending-memory)) rather than calling once per item.

## Sizing a machine

`KestLimits` is four numbers: stack slots, call depth, heap bytes and fuel. Zero for the first two means what the program needs, which the machine works out; zero heap means no ceiling; zero fuel means no budget.

`kest_needs(build, &limits, &why)` answers the stack and depth the whole program needs; `kest_needs_of` answers for one function and what it reaches; `kest_needs_from` answers where the machine stands when the program calls into the host, which a host that calls back in from inside a door adds to what that callee needs. A program that can call itself, or calls through a function value, has no least: `why` says which, and the host gives a number of frames, which `kest_bound` and `kest_bound_of` turn into slots. `kest emit` prints the same numbers beside each function. A machine that runs out says what it needed.

The heap is the ceiling a host watching a frame budget sets. Crossing it fails at the line that asked, with how much the program had, how much it asked for and what was growing (`K0617`); the machine itself running out of memory is a different code (`K0605`). `kest_heap_used`, `kest_heap_taken` and `kest_heap_most` read the heap.

## When a call fails

`kest_call` answers `false` when the program faulted (a division by zero, an index past the end, a ceiling crossed, a door that failed, a budget spent), and `kest_report(vm, out, form)` writes what happened: the code, the line and the calls that led there. `kest_build_report` is the same for the build, including warnings a successful build had and why a start was refused. Every diagnostic has the same shape at compile time and at run time, in text or JSON.

A fault is not a rollback. The program's state is whatever the fault left: a body that wrote half the world before faulting leaves half the world written. The machine is not spoiled, and the next call runs on that state. A host holding a world through a failure picks one of three answers on purpose: throw the machine away and start again, restore a save taken before the call, or carry on knowing what it carries on with. No `defer` runs during a fault, so state the host paired through two doors (open, close) may be left open, and the host closes it when a call answers `false`.

A machine keeps the last sixteen diagnostics nobody has read (`KEST_MOST_UNREAD`) and counts the rest. A host that writes the report into a file of its own, such as one `tmpfile` reused for the life of the host, reads it back without touching a terminal.

## Budgets, cancelling and threads

`KestLimits.fuel` (or `kest_fuel_set`) is a budget of steps, where a step is a backward jump or a call, plus a unit for every 64 bytes or elements an instruction such as a text join or an array copy touches. It is a count, so two machines given the same budget stop at the same place. A machine that runs out fails with `K0659`; it is not broken, and a host that gives it more with `kest_fuel_set` can call it again. `kest_fuel_left` reads what is left, and `kest_fuel_spend` lets a door charge the program for work it did in C. The cost of checking the budget is too small to measure.

`kest_cancel(vm)` asks a running machine to stop. It is a single store, safe from a signal handler or another thread, and the machine stops at its next step with `K0660`.

A build may be shared by any number of threads. A machine belongs to one thread while it runs: two machines of one build run on two threads at once without seeing each other, but two threads calling into one machine is undefined, and nothing locks. Start machines from any thread, but handle a failed `kest_start` on one thread at a time, since a failed start writes the build's report. A breakpoint written by the debugger is written into the build, so debug a build no other machine is running.

## Versions

`kest_version()` is the version string `kest --version` prints. `KEST_ABI_VERSION` is the shape of the header; a host compares it with `kest_abi_version()` before calling anything else and stops if they differ, because a header and a library from two versions disagree about memory in ways C cannot see. `kest_profile()` names the deterministic profile. `kest_checked()` says whether the library was built with its own checks and sanitizers, which catch host mistakes a release build cannot. See [Versions and editions](editions.md).

## The example hosts

- `examples/least.c` is the smallest real host: it compiles, binds by name against a table of what it provides, checks each door's layout, sizes the machine from the program and calls. It is meant to be read top to bottom.
- `examples/engine.c` is shaped like a game: it builds a program, asks it for a world, keeps that world between frames, lends its own memory a frame at a time, spends a budget, saves the world and reloads the program under it. `make engine && ./examples/engine`.
- `examples/embed.c` calls every door of the API, including the mistakes a host can make and what each is told. It is the place to look when a door does not behave as this page says.
- `examples/tetris/` and `examples/colony/` are games drawn with raylib; see [Examples](examples.md).
