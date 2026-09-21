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
    if $cc -O1 -c -o "$work"/wrote.o "$work"/wrote.c 2>"$work"/why; then
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
    if ! $cc -O2 -o "$work"/one "$work"/one.c libkest.a -lm 2>"$work"/why; then
        {
            echo "    the C written for $name will not compile:"
            sed 's/^/        /' "$work"/why | head -5
        } >>"$said"
        wrong=$((wrong + 1))
        continue
    fi
    machine_said=$(./kest run "$file" 2>/dev/null </dev/null)
    machine_was=$?
    c_said=$("$work"/one 2>/dev/null </dev/null)
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
    both=$((both + 1))
done

# One of them is a program that stops while it is running, and both halves have
# to stop: a backend that wrote a division by nought as one the host's machine
# traps on, or as one it quietly answers, would be a program that means
# something else. Counted rather than assumed, because a program that stops is
# one whose answer is the same either way for the wrong reason.
stops=$(./kest run "$work"/programs/stopped.kest 2>/dev/null </dev/null; echo $?)
if [ "$stops" -eq 0 ]; then
    echo "    the program written to stop while it runs does not stop" \
        >>"$said"
    wrong=$((wrong + 1))
fi

if [ "$wrong" -ne 0 ]; then
    echo "$wrong thing(s) wrong with the C this backend wrote"
    cat "$said"
    exit 1
fi
echo "$written of $bodies body(s) over $compiled program(s) written as C the \
host compiler takes, and $both program(s) run both ways for the same answer"
