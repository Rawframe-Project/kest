#!/bin/sh
# The other backend, held to answering what the first one does. `kest emit --c`
# writes the same bodies as C for the compiler a release is built with, and
# what says the two are one program is that a program compiled both ways
# answers the same thing. `make check` runs this. See D1093.
#
# Two halves. Every program it is given is written as C and handed to the
# host's compiler, because C that will not compile is the one kind of wrongness
# this backend can have that reading the file does not show, and the programs
# in this tree are a corpus of a thousand bodies nobody had to write for it.
# And then the programs below, written here and run both ways, because a file
# that compiles says nothing about what it does. They are written here rather
# than kept in the tree for the reason `check-lends.sh` writes its own: what
# they are for is this check, and a program in `examples` is for a reader.
set -u
cc=${CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

if [ $# -eq 0 ]; then
    echo "nothing was given to write as C, which is every sweep below passing"
    echo "without reading a file"
    exit 1
fi
if [ ! -x ./kest ] || [ ! -f libkest.a ]; then
    echo "the compiler and the library are not built"
    exit 1
fi

compiled=0
written=0
bodies=0
wrong=0
# What is wrong, kept rather than printed as it is found: what reads a check
# that refuses reads the first line, so the sentence saying how much is wrong
# comes before the list of it.
said="$work"/said
: >"$said"
for file in "$@"; do
    if ! ./kest emit --c "$file" >"$work"/wrote.c 2>"$work"/why; then
        continue
    fi
    # How much of the file it wrote, read out of the file itself. A count of
    # bodies rather than of files: a module of generic functions nothing has
    # used yet has no bodies at all, and a file with none of them written and
    # a file with nothing in it read the same from outside.
    counted=$(sed -n 's,^// \([0-9]*\) of \([0-9]*\) bodies written$,\1 \2,p' \
              "$work"/wrote.c)
    if [ -z "$counted" ]; then
        echo "    the C written for $file does not say how much of it was \
written" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    written=$((written + ${counted% *}))
    bodies=$((bodies + ${counted#* }))
    if $cc -O1 -Iinclude -c -o "$work"/wrote.o "$work"/wrote.c \
            2>"$work"/why; then
        compiled=$((compiled + 1))
        continue
    fi
    {
        echo "    the C written for $file will not compile:"
        sed 's/^/        /' "$work"/why | head -5
    } >>"$said"
    wrong=$((wrong + 1))
done

# And that both halves are there, which is what says the sweep above was about
# anything. A backend that wrote nothing would hand the host's compiler a file
# of comments and pass; one that claimed everything would be writing C for a
# crossing into the host, which it has none for.
if [ "$compiled" -eq 0 ] || [ "$written" -eq 0 ] ||
        [ "$written" -ge "$bodies" ]; then
    echo "    $written of $bodies body(s) written over $compiled file(s), \
which is not both halves of what this backend is" >>"$said"
    wrong=$((wrong + 1))
fi

mkdir "$work"/programs
cat >"$work"/programs/numbers.kest <<'PROGRAM'
module numbers

fn mix(a: i64, b: i64) -> i64 no.alloc no.host deterministic {
    let c = a * 3 - b / 2
    let d = c % 7
    return c + d * 2
}

fn widths(i: i32) -> i64 no.alloc {
    let a: u8 = u8(i % 256)
    let b: i16 = i16(0 - i)
    let c: u32 = u32(i) * 3000000
    let d: i8 = i8(i % 128)
    let e: u16 = u16(c % 65536)
    let f: u64 = u64(c) << 3
    let g: i64 = i64(b) >> 2
    return i64(a) + i64(b) + i64(c) + i64(d) + i64(e) + i64(f % 1000) + g
}

fn main() -> i32 {
    let total: i64 = 0
    let i: i64 = 1
    while i < 200 {
        total = (total + mix(i, i * 2 + 1) + widths(i32(i))) % 1000003
        i += 1
    }
    return i32((total % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/floats.kest <<'PROGRAM'
module floats

fn main() -> i32 {
    let sum: f64 = 0.0
    for i in 0..500 {
        let x = f64(i) * 0.5
        sum += x * x - x / 3.0 + x % 7.0
    }
    let narrow: f32 = f32(sum) * 2.0
    if narrow > 0.0 {
        sum = sum + f64(narrow)
    }
    return i32((i64(sum) % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/shapes.kest <<'PROGRAM'
module shapes

struct Point {
    x: f64
    y: f64
    tag: i32
}

enum State {
    Idle
    Hunting(i32)
    Fleeing(i32, i32)
}

fn moved(p: Point, by: f64) -> Point no.alloc {
    return Point(p.x + by, p.y - by, p.tag + 1)
}

fn worth(state: State) -> i32 no.alloc {
    return match state {
        Idle -> 1
        Hunting(at) -> at * 2
        Fleeing(from, to) -> to - from
    }
}

fn pick(i: i32) -> State no.alloc {
    if i % 3 == 0 {
        return State.Idle
    }
    if i % 3 == 1 {
        return State.Hunting(i)
    }
    return State.Fleeing(i, i * 2)
}

fn main() -> i32 {
    let p = Point(1.0, 2.0, 0)
    let total: i32 = 0
    for i in 0..300 {
        p = moved(p, f64(i) * 0.25)
        total = (total + worth(pick(i))) % 100003
    }
    return i32((i64(p.x + p.y) + i64(total)) % 251) + p.tag % 7
}
PROGRAM
cat >"$work"/programs/control.kest <<'PROGRAM'
module control

fn fib(n: i32) -> i64 no.alloc {
    if n < 2 {
        return i64(n)
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() -> i32 {
    let total: i64 = fib(18)
    let i: i32 = 1
    while i < 400 {
        if i % 17 == 0 {
            i += 1
            continue
        }
        for k in 0..4 {
            total += i64(k * i)
        }
        total = total % 1000003
        if total < 0 {
            break
        }
        i += 1
    }
    return i32(total % 251)
}
PROGRAM
cat >"$work"/programs/elements.kest <<'PROGRAM'
module elements

struct Body {
    x: f64
    y: f64
    tag: i32
    small: u8
}

fn step(world: [Body], rounds: i32) -> f64 no.alloc no.host deterministic {
    let sum: f64 = 0.0
    let i = 0
    while i < rounds {
        for at in 0..len(world) {
            let one = world[at]
            one.x += one.y
            one.tag = (one.tag + 1) % 7
            one.small = u8((i32(one.small) + 3) % 256)
            world[at] = one
        }
        i += 1
    }
    for one in world {
        sum += one.x + one.y + f64(one.tag) + f64(one.small)
    }
    return sum
}

fn main() -> i32 {
    let world: [Body] = array()
    for i in 0..64 {
        push(world, Body(f64(i), f64(i) * 0.5, i % 5, u8(i % 256)))
    }
    return i32((i64(step(world, 20)) % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/stacked.kest <<'PROGRAM'
module stacked

struct Point {
    x: f64
    y: i32
}

fn bump(n: i32) -> i32 {
    return n + 1
}

fn walk(world: [Point], rounds: i32) -> i64 {
    let sum: i64 = 0
    let i = 0
    while i < rounds {
        for at in 0..len(world) {
            let one = world[at]
            one.y = bump(one.y) % 11
            one.x += f64(one.y)
            world[at] = one
            sum += i64(one.y)
        }
        i = bump(i)
    }
    return sum
}

fn main() -> i32 {
    let world: [Point] = array()
    for i in 0..32 {
        push(world, Point(f64(i), i % 7))
    }
    return i32((walk(world, 20) % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/deep.kest <<'PROGRAM'
module deep

fn down(n: i32) -> i64 no.alloc no.host deterministic {
    if n <= 0 {
        return 0
    }
    return 1 + down(n - 1)
}

fn main() -> i32 {
    return i32(down(100000) % 251)
}
PROGRAM
cat >"$work"/programs/words.kest <<'PROGRAM'
module words

struct Tag {
    name: text
    rank: i32
}

fn same(a: Tag, b: Tag) -> bool no.alloc no.host deterministic {
    return a == b
}

fn weigh(t: Tag) -> i64 no.alloc no.host deterministic {
    let mixed = hash(t)
    let named = hash(t.name)
    let ranked = hash(t.rank)
    let odd = hash(f64(t.rank) * 0.5)
    return i64(mixed % 1000) + i64(named % 1000) + i64(ranked % 100) +
        i64(odd % 10)
}

fn label(which: i32) -> text no.alloc no.host deterministic {
    if which == 0 {
        return "bread"
    }
    if which == 1 {
        return "hammer"
    }
    return "coin"
}

fn main() -> i32 {
    let total: i64 = 0
    for i in 0..30 {
        let one = Tag(label(i % 3), i % 7)
        let two = Tag(label((i + 1) % 3), i % 7)
        total += weigh(one)
        if same(one, two) {
            total += 3
        }
        total += i64(len(label(i % 3)))
    }
    return i32((total % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/tagged.kest <<'PROGRAM'
module tagged

enum Kind {
    Food(i32)
    Tool(i32)
    Coin
    Note(i32)
}

struct Item {
    name: text
    kind: Kind
    many: i32
    spare: f32?
}

fn worth(items: [Item], round: i32) -> i32 no.alloc no.host deterministic {
    let total = 0
    for at in 0..len(items) {
        let one = items[at]
        total += match one.kind {
            Food(fills) -> fills * one.many
            Tool(power) -> power * 3
            Coin -> one.many
            Note(which) -> if which == 0 -> 0 else -> 1
        }
        if let held = one.spare {
            total += i32(held)
        }
        one.many = (one.many + round) % 5
        one.kind = Kind.Tool(at % 3)
        items[at] = one
    }
    return total
}

fn main() -> i32 {
    let items: [Item] = array()
    for i in 0..16 {
        let spare: f32? = none
        if i % 3 == 0 {
            spare = f32(i)
        }
        push(items, Item("a", Kind.Food(i % 4), i, spare))
    }
    let total = 0
    for round in 0..5 {
        total += worth(items, round)
    }
    return i32((total % 251 + 251) % 251)
}
PROGRAM
cat >"$work"/programs/outside.kest <<'PROGRAM'
module outside

fn look(world: [i32], at: i32) -> i32 no.alloc no.host deterministic {
    return world[at]
}

fn main() -> i32 {
    let world: [i32] = array()
    for i in 0..4 {
        push(world, i)
    }
    return look(world, 9)
}
PROGRAM
cat >"$work"/programs/shifted.kest <<'PROGRAM'
module shifted

fn main() -> i32 {
    let by: i32 = 0
    for i in 0..3 {
        by -= i
    }
    return 1 << by
}
PROGRAM
cat >"$work"/programs/stopped.kest <<'PROGRAM'
module stopped

fn main() -> i32 {
    let n: i32 = 0
    for i in 0..3 {
        n += i * 0
    }
    return 10 / n
}
PROGRAM

both=0
for file in "$work"/programs/*.kest; do
    name=$(basename "$file")
    ./kest emit --c "$file" >"$work"/one.c 2>"$work"/why
    # Every one of these is written whole on purpose: a program half of which
    # the machine runs would be a differential test of the half that is left.
    if ! grep -q '^int main' "$work"/one.c; then
        {
            echo "    $name has no \`main\` written for it:"
            grep '^// not written' "$work"/one.c | head -3 | sed 's/^/        /'
        } >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    if ! $cc -O2 -Iinclude -o "$work"/one "$work"/one.c libkest.a -lm \
            2>"$work"/why; then
        {
            echo "    the C written for $name will not compile:"
            sed 's/^/        /' "$work"/why | head -5
        } >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    # Both streams, because what a program writes and what a refusal says are
    # both what it said: the program that stops while it runs is here to hold
    # a compiled body's refusal to being the machine's refusal, word for word
    # and with the same line under it.
    machine_said=$(./kest run "$file" 2>&1 </dev/null)
    machine_was=$?
    c_said=$(KEST_LIB=lib/ "$work"/one "$file" 2>&1 </dev/null)
    c_was=$?
    if [ "$machine_was" -ne "$c_was" ]; then
        echo "    $name answers $machine_was run by the machine and $c_was \
compiled as C" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    if [ "$machine_said" != "$c_said" ]; then
        echo "    $name writes something else compiled as C" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    # And that any of it ran. Two engines that answer alike answer alike when
    # one of them never started, so what this reads is the run's own count of
    # which bodies it entered: a backend whose C is never called is a backend
    # nothing here would notice. Every program above is written whole, so at
    # least one of its bodies has to have been the C's.
    ran=$(KEST_LIB=lib/ KEST_NATIVES=1 "$work"/one "$file" 2>&1 >/dev/null |
          sed -n 's/^natives: [0-9]* written, \([0-9]*\) entered.*$/\1/p')
    if [ -z "$ran" ] || [ "$ran" -eq 0 ]; then
        echo "    nothing written for $name was entered, so what ran was the \
machine" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    both=$((both + 1))
done

# And every program in this tree that runs, run both ways. The ones above are
# written for this check and are what it can write; these are what somebody
# wrote for another reason, which is where a fixture's blind spot shows. A
# program that asks the host for something a file this backend wrote does not
# provide -- a clock, a file, whatever an engine offers -- cannot be run this
# way, and is counted rather than passed over quietly.
alike=0
wants_a_host=0
for file in "$@"; do
    if ! grep -q '^fn main(' "$file"; then
        continue
    fi
    if ! ./kest emit --c "$file" >"$work"/one.c 2>/dev/null; then
        continue
    fi
    if ! grep -q '^int main' "$work"/one.c; then
        continue
    fi
    if ! $cc -O2 -Iinclude -o "$work"/one "$work"/one.c libkest.a -lm \
            2>"$work"/why; then
        {
            echo "    the C written for $file will not link:"
            sed 's/^/        /' "$work"/why | head -5
        } >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    machine_said=$(./kest run "$file" 2>&1 </dev/null)
    machine_was=$?
    c_said=$(KEST_LIB=lib/ "$work"/one "$file" 2>&1 </dev/null)
    c_was=$?
    case "$c_said" in
    *"the host does not provide"*)
        wants_a_host=$((wants_a_host + 1))
        continue
        ;;
    esac
    if [ "$machine_was" -ne "$c_was" ] || [ "$machine_said" != "$c_said" ]; then
        echo "    $file answers $machine_was run by the machine and $c_was \
with what was written as C, or says something else" >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    alike=$((alike + 1))
done

# Four of them are programs that stop while they are running, and both halves
# have to stop: a backend that wrote a division by nought as one the host's machine
# traps on, or as one it quietly answers, would be a program that means
# something else. Counted rather than assumed, because a program that stops is
# one whose answer is the same either way for the wrong reason.
for stopping in stopped shifted outside deep; do
    stops=$(./kest run "$work"/programs/$stopping.kest 2>/dev/null </dev/null
            echo $?)
    if [ "$stops" -eq 0 ]; then
        echo "    $stopping.kest is written to stop while it runs and does \
not stop" >>"$said"
        wrong=$((wrong + 1))
    fi
done

if [ "$wrong" -ne 0 ]; then
    echo "$wrong thing(s) wrong with the C this backend wrote"
    cat "$said"
    exit 1
fi
echo "$written of $bodies body(s) over $compiled program(s) written as C the \
host compiler takes, $both program(s) written here and $alike of this tree's \
own run both ways for the same answer and the same words, and $wants_a_host \
that ask the host for what a file this wrote does not provide"
