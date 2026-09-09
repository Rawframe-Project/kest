#!/bin/sh
# What every command has to be true of: it produces something of the right
# shape, it says why when it does not, and what it says as JSON is JSON.
#
# The examples check their own answers by returning a number, so `run` exiting
# zero is the answer being right. What this adds is that a command which
# prints nothing no longer looks the same as one that works.
set -u

# A scratch of this run's own. Two of these run at once when the backstops put
# one out of order while another is being asked, and fixed names in `/tmp` are
# two runs writing to one file.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
kest=./kest
failed=0

# What this was given. A check that reads the files it is handed passes when it
# is handed none: every loop runs no times and the count at the end is nought,
# which reads like a success.
if [ "$#" -eq 0 ]; then
    echo "commands: nothing was given to run anything on"
    exit 1
fi

complain() {
    echo "$1"
    failed=1
}

# Output of the right kind, or a reported reason and a non-zero exit.
expect() {
    file=$1
    command=$2
    pattern=$3

    out=$("$kest" "$command" "$file" 2>"$scratch"/cmd-err)
    status=$?
    if [ $status -ne 0 ]; then
        if [ ! -s "$scratch"/cmd-err ]; then
            complain "$command $file: failed and said nothing"
        fi
        return
    fi
    if [ -z "$out" ]; then
        complain "$command $file: succeeded and printed nothing"
        return
    fi
    if ! printf '%s' "$out" | grep -qE "$pattern"; then
        complain "$command $file: printed nothing matching /$pattern/"
    fi
}

# A file that holds nothing is the one a command is likeliest to answer with
# silence, and no file in this tree is one. `fmt` is left out on purpose: what
# it writes is the file, and the file is empty, so `kest fmt` over a file that
# holds nothing has to hold nothing after it.
nothing=$(mktemp -d)/nothing.kest
: > "$nothing"
expect "$nothing" lex 'end of file'
expect "$nothing" parse 'declares nothing'
expect "$nothing" check 'declares nothing'
expect "$nothing" emit '^nothing to run'
if [ -n "$($kest fmt "$nothing" 2>&1)" ]; then
    complain "fmt $nothing: a file that holds nothing formatted to something"
fi
# A program that asks the host for something this host does not have. No file
# in the tree is one — every extern here is a name the command line binds — and
# what it stands for is any refusal that happens between compiling and running,
# which is where a message has no machine to be read from.
asking=$(mktemp -d)/asking.kest
cat > "$asking" <<'ASKING'
module asking

extern fn Nobody.here() -> i32

fn onEvent(event: i32) -> i32 {
    return event + Nobody.here()
}

fn main() -> i32 {
    return Nobody.here()
}
ASKING
for command in run tick call; do
    if [ "$command" = call ]; then
        $kest call "$asking" main >/dev/null 2>"$scratch"/cmd-err </dev/null
    else
        $kest "$command" "$asking" >/dev/null 2>"$scratch"/cmd-err </dev/null
    fi
    if [ $? -eq 0 ]; then
        complain "$command $asking: a program the host cannot run ran"
    elif ! grep -q "does not provide" "$scratch"/cmd-err; then
        complain "$command $asking: refused without naming what it wanted"
    fi
done
rm -rf "$(dirname "$asking")"

# A program handed over as a stream rather than a file: a shell writes
# `kest check <(...)` and what arrives cannot be measured, only read to the
# end. Every file in this tree is a file, so nothing else asks this.
if command -v mktemp >/dev/null 2>&1; then
    piped=$(mktemp -d)/piped.kest
    cat > "$piped" <<'PIPED'
module piped

fn main() -> i32 {
    return 0
}
PIPED
    # Through a pipe rather than a redirect: a file redirected in can still be
    # measured, and what this is about is the stream that cannot be.
    out=$(cat "$piped" | $kest check /dev/stdin 2>&1)
    if [ $? -ne 0 ] || [ -z "$out" ]; then
        complain "check /dev/stdin: a program read from a stream said nothing"
    fi
    rm -rf "$(dirname "$piped")"
fi

# A path that is not a file at all. It opens, it measures nought, and it
# refuses to be read, which is how a directory used to be a file with nothing
# in it: `kest check` said it declared nothing.
where=$(mktemp -d)
for command in check run fmt lex parse emit; do
    if $kest "$command" "$where" >/dev/null 2>"$scratch"/cmd-err </dev/null; then
        complain "$command $where: read a directory as a file"
    elif ! grep -q "cannot read" "$scratch"/cmd-err; then
        complain "$command $where: refused without saying it could not read it"
    fi
done
rmdir "$where"

# And running it is a refusal that says which of the two reasons it is.
if $kest run "$nothing" >/dev/null 2>"$scratch"/cmd-err </dev/null; then
    complain "run $nothing: a file that holds nothing ran"
elif ! grep -q "declares nothing" "$scratch"/cmd-err; then
    complain "run $nothing: refused without saying the file holds nothing"
fi
rm -rf "$(dirname "$nothing")"

# One file, asked everything. What it says is what is wrong with it, so a
# file that is right says nothing at all — which is what lets these run at
# once and be read in order afterwards.
sweep_one() {
    file=$1
    mine=$2
        expect "$file" lex 'end of file'
        expect "$file" parse '^\(|^// '
        expect "$file" fmt '.'
        expect "$file" check '^(fn|struct|const|import) '
        # A file of nothing but generic functions has no bodies until a call
        # asks for one, and it says so rather than printing nothing.
        expect "$file" emit '^fn |^nothing to run'

        # `call` needs the name of a function, and a list of them here would go
        # stale, so the file is asked: the first one it declares that takes
        # nothing but numbers, text or a bool. Nought for a number and a letter
        # for text, which is enough for a call to happen.
        chosen=$("$kest" check "$file" --json 2>/dev/null </dev/null |
                 python3 -c '
    import json
    import sys

    TYPED = {"i8": "0", "i16": "0", "i32": "0", "i64": "0", "u8": "0", "u16": "0",
             "u32": "0", "u64": "0", "f32": "0", "f64": "0", "bool": "false",
             "text": "x"}

    held = json.load(sys.stdin)
    for one in held.get("functions", []):
        if one.get("foreign") or one.get("file") != sys.argv[1]:
            continue
        takes = one.get("parameters") or []
        if any(what not in TYPED for what in takes):
            continue
        print(" ".join([one["name"]] + [TYPED[what] for what in takes]))
        break
    ' "$file")
        if [ -n "$chosen" ]; then
            # shellcheck disable=SC2086
            out=$("$kest" call "$file" $chosen 2>"$mine.err" </dev/null)
            status=$?
            if [ $status -ne 0 ]; then
                if [ ! -s "$mine.err" ]; then
                    complain "call $file $chosen: failed and said nothing"
                fi
            elif [ -z "$out" ]; then
                complain "call $file $chosen: succeeded and printed nothing"
            fi
        fi

        # Running is the answer being right, because an example that disagrees
        # with itself returns which check it failed.
        "$kest" run "$file" >/dev/null 2>"$mine.err" </dev/null
        status=$?
        if [ $status -ne 0 ] && [ ! -s "$mine.err" ]; then
            complain "run $file: exit $status and said nothing"
        fi

        # The two forms of `check` say the same file's declarations. One is read
        # by a person and the other by a tool, and they are two readings of one
        # answer: a kind of shape added to one and not the other is a type the
        # printed form describes and nothing machine-readable can see, which is
        # exactly what happened to `flags`.
        said=$( { "$kest" check "$file" 2>/dev/null </dev/null;
                  echo "----";
                  "$kest" check "$file" --json 2>/dev/null </dev/null; } |
                python3 -c '
    import json
    import re
    import sys

    text, _, written = sys.stdin.read().partition("\n----\n")
    printed = set()
    for line in text.splitlines():
        what = re.match(r"(struct|enum|flags) (\S+)", line)
        if what:
            printed.add(what.group(2))
        called = re.match(r"(?:extern )?fn ([^(]+)\(", line)
        if called:
            printed.add(called.group(1))
        held = re.match(r"const (\S+):", line)
        if held:
            printed.add(held.group(1))

    named = set()
    for one in json.loads(written or "{}").get("types", []):
        if one.get("file") == sys.argv[1]:
            named.add(one["name"])
    for what in ("functions", "constants"):
        for one in json.loads(written or "{}").get(what, []):
            if one.get("file") == sys.argv[1]:
                named.add(one["name"])

    for name in sorted(printed - named):
        print("printed and not in the JSON: %s" % name)
    for name in sorted(named - printed):
        print("in the JSON and not printed: %s" % name)
    ' "$file")
        if [ -n "$said" ]; then
            complain "check $file: the two forms disagree"
            printf '%s\n' "$said" | sed 's/^/    /' | head -4
        fi

        # The two forms of `lex`, which is the smallest of these and the one whose
        # whole answer is a list: every token by what it is, where it is, and what
        # it says.
        read_twice=$( { "$kest" lex "$file" 2>/dev/null </dev/null;
                        echo "----";
                        "$kest" lex "$file" --json 2>/dev/null </dev/null; } |
                      python3 -c '
    import json
    import re
    import sys

    text, _, written = sys.stdin.read().partition("\n----\n")
    printed = []
    for line in text.splitlines():
        step = re.match(r"\s*(\d+):(\d+)\s+(\S+(?: \S+)*?)\s\s+(.*)$", line)
        if step:
            printed.append((int(step.group(1)), int(step.group(2)),
                            step.group(3), step.group(4)))

    machine = [(one["line"], one["column"], one["kind"], one["text"])
               for one in json.loads(written or "{}").get("tokens", [])]

    # What a token says is compared where the printed form shows it whole. A
    # token that is a line break prints as one — the reader sees the line end —
    # and the JSON writes the two characters that stand for it, which is the same
    # byte said two ways rather than two answers.
    if len(printed) != len(machine):
        print("%u tokens printed, %u in the JSON" % (len(printed), len(machine)))
    else:
        for at, (one, two) in enumerate(zip(printed, machine)):
            if one[:3] != two[:3] or (one[3] and one[3] != two[3]):
                print("token %u: %s printed, %s in the JSON" % (at, one, two))
                break
    ')
        if [ -n "$read_twice" ]; then
            complain "lex $file: the two forms disagree"
            printf '%s\n' "$read_twice" | sed 's/^/    /' | head -3
        fi

        # And the two forms of `emit`, which is where a wrong answer is hardest to
        # see: a walk over the code printed for a person and the same walk written
        # for a tool. What is compared is what both say — the functions, how wide
        # and how deep each is, and every instruction in it by where it sits and
        # what it is called.
        walked=$( { "$kest" emit "$file" 2>/dev/null </dev/null;
                    echo "----";
                    "$kest" emit "$file" --json 2>/dev/null </dev/null; } |
                  python3 -c '
    import json
    import re
    import sys

    text, _, written = sys.stdin.read().partition("\n----\n")

    printed = {}
    name = None
    layouts = 0
    hosts = []
    needs = None
    for line in text.splitlines():
        if line.startswith("layout "):
            layouts += 1
            continue
        if line.startswith("host "):
            hosts.append(line[len("host "):].strip())
            continue
        asked = re.match(r"needs (\d+) slots and (\d+) frames", line)
        if asked:
            needs = (int(asked.group(1)), int(asked.group(2)))
            continue
        # A name may have spaces in it — a copy of a generic is named for the
        # types it was given, and one of those is a function type — so what ends
        # the name is the two spaces before what it is wide, not the first space.
        written_fn = re.match(r"fn (.+?)  (\d+) parameter slots?, (\d+) slots?, "
                              r"(\d+) deep(, promises `no.alloc`)?$", line)
        if written_fn:
            name = written_fn.group(1)
            printed[name] = {"wide": tuple(int(written_fn.group(i))
                                           for i in (2, 3, 4)),
                             "promises": written_fn.group(5) is not None,
                             "code": []}
            continue
        step = re.match(r"\s+(\d+)\s+(\S+)", line)
        if step and name is not None:
            printed[name]["code"].append((int(step.group(1)), step.group(2)))

    said = json.loads(written or "{}")
    machine = {}
    for one in said.get("functions", []):
        machine[one["name"]] = {
            "wide": (one["parameterSlots"], one["slots"], one["deep"]),
            "promises": one["noAlloc"],
            "code": [(step["at"], step["op"]) for step in said and one["code"]],
        }

    if layouts != len(said.get("layouts", [])):
        print("layouts: %u printed, %u in the JSON"
              % (layouts, len(said.get("layouts", []))))
    if hosts != said.get("hosts", []):
        print("hosts: %s printed, %s in the JSON" % (hosts, said.get("hosts")))
    asked = said.get("needs")
    if needs is not None and asked is not None and \
            needs != (asked["slots"], asked["frames"]):
        print("needs: %s printed, %s in the JSON" % (needs, asked))
    for missing in sorted(set(printed) - set(machine)):
        print("printed and not in the JSON: %s" % missing)
    for missing in sorted(set(machine) - set(printed)):
        print("in the JSON and not printed: %s" % missing)
    for name in sorted(set(printed) & set(machine)):
        if printed[name]["wide"] != machine[name]["wide"]:
            print("%s: %s printed, %s in the JSON"
                  % (name, printed[name]["wide"], machine[name]["wide"]))
        if printed[name]["promises"] != machine[name]["promises"]:
            print("%s: promises %s printed, %s in the JSON"
                  % (name, printed[name]["promises"], machine[name]["promises"]))
        if printed[name]["code"] != machine[name]["code"]:
            print("%s: %u instructions printed, %u in the JSON"
                  % (name, len(printed[name]["code"]), len(machine[name]["code"])))
    ')
        if [ -n "$walked" ]; then
            complain "emit $file: the two forms disagree"
            printf '%s\n' "$walked" | sed 's/^/    /' | head -4
        fi

        # What a chunk carries against what the declaration promised. These are
        # two commands rather than two forms of one, and the machine reads the
        # chunk: at the one call the second proof cannot see through, what says
        # a promise was kept is the flag a chunk was compiled with and not the
        # declaration anybody wrote. A copy of a generic is a chunk of its own,
        # made by substituting into a type, which is where the two could come
        # apart without a program noticing.
        carried=$( { "$kest" check "$file" --json 2>/dev/null </dev/null;
                     echo;
                     echo "----";
                     "$kest" emit "$file" --json 2>/dev/null </dev/null; } |
                   python3 -c '
    import json
    import sys

    declared, _, emitted = sys.stdin.read().partition("\n----\n")

    # A host provides a foreign function, so there is no chunk for one. What a
    # host promises is checked where it is called and said as K0631.
    promised = {}
    for one in json.loads(declared.strip() or "{}").get("functions", []):
        if one.get("foreign"):
            continue
        promised.setdefault(one["name"], set()).add(one["noAlloc"])

    for one in json.loads(emitted.strip() or "{}").get("functions", []):
        # A chunk is named for the types it was made with, and a declaration is
        # not. Two declarations under one name that disagree about the promise
        # cannot be told apart this way, and are left to the checker.
        says = promised.get(one["name"].split("#")[0])
        if says is None or len(says) != 1:
            continue
        said = next(iter(says))
        if one["noAlloc"] != said:
            print("%s: the declaration promises %s and the chunk carries %s"
                  % (one["name"], said, one["noAlloc"]))
    ')
        if [ -n "$carried" ]; then
            complain "emit $file: a chunk carries what its declaration does not"
            printf %s\n "$carried" | sed "s/^/    /" | head -4
        fi

        # Not "starts with a brace": an object that goes wrong in the middle
        # starts with one too, which is how a command spent a while writing plain
        # words inside a JSON array without anything noticing.
        for command in lex parse check emit run fmt tick; do
            if ! "$kest" "$command" "$file" --json 2>/dev/null </dev/null | python3 -c '
    import json
    import sys

    lines = [line for line in sys.stdin.read().splitlines() if line.strip()]
    if not lines:
        raise SystemExit(1)
    for line in lines:
        if not isinstance(json.loads(line), dict):
            raise SystemExit(1)
    ' 2>/dev/null; then
                complain "$command $file --json: not one object a line"
            fi
        done
}

# Nothing here reads what another writes and each has a scratch of its own,
# so they are asked at once, eight at a time. What they say is kept and read
# back in the order they were given, because a sweep that reports itself in
# whatever order finished first is one nobody can read twice.
said=$(mktemp -d)
at=0
for file in "$@"; do
    at=$((at + 1))
    sweep_one "$file" "$said/$(printf %04d $at)" \
        > "$said/$(printf %04d $at)" 2>&1 &
    if [ $((at % 8)) -eq 0 ]; then
        # `jobs` says nothing in a script — job control is off — so what
        # holds the number down is counting them: eight are started and
        # waited for, and then eight more.
        wait
    fi
done
wait

at=0
# A number written the shortest way that reads back as the same number, which
# is a promise about what a reader does with it and not about how it looks. The
# digits are in an example and nothing had ever read one back: this writes the
# number down through the machine's own writer and hands it to the machine's
# own reader, and the program says whether what came back is what it had.
back="$scratch"/check-back.kest
cat > "$back" <<'KEST'
module back

fn wide(i: i32) -> f64 {
    if i == 0 { return 1.0 / 3.0 }
    if i == 1 { return 0.1 }
    if i == 2 { return -1234567.891 }
    if i == 3 { return 0.0000001 }
    if i == 4 { return 123456789012345.6 }
    return 0.0
}

fn narrow(i: i32) -> f32 {
    if i == 0 { return f32(1.0 / 3.0) }
    if i == 1 { return 0.1 }
    if i == 2 { return -1234567.891 }
    if i == 3 { return 0.0000001 }
    if i == 4 { return 123456789012345.6 }
    return 0.0
}

fn sameWide(i: i32, x: f64) -> i32 {
    if x == wide(i) {
        return 0
    }
    return 1
}

fn sameNarrow(i: i32, x: f32) -> i32 {
    if x == narrow(i) {
        return 0
    }
    return 1
}

// The two a comparison cannot answer for. What is held about them is what they
// are rather than what they equal: nothing equals a number that is not one.
fn tooBig() -> f64 {
    return 1.0 / 0.0
}

fn notANumber() -> f64 {
    return 1.0 / 0.0 - 1.0 / 0.0
}

fn isTooBig(x: f64) -> i32 {
    if x > 0.0 && x + 1.0 == x {
        return 0
    }
    return 1
}

fn isNotANumber(x: f64) -> i32 {
    if x != x {
        return 0
    }
    return 1
}

fn main() -> i32 {
    return 0
}
KEST

which=0
while [ "$which" -lt 5 ]; do
    for pair in "wide:sameWide" "narrow:sameNarrow"; do
        digits=$("$kest" call "$back" "${pair%%:*}" "$which" 2>&1 </dev/null |
                 head -1)
        again=$("$kest" call "$back" "${pair#*:}" "$which" "$digits" 2>&1 \
                </dev/null | head -1)
        if [ "$again" != "0" ]; then
            complain "call: a number written down did not read back as itself"
            printf '    %s %s was written %s and read back %s\n' \
                   "${pair%%:*}" "$which" "$digits" "$again"
        fi
    done
    which=$((which + 1))
done

for pair in "tooBig:isTooBig" "notANumber:isNotANumber"; do
    digits=$("$kest" call "$back" "${pair%%:*}" 2>&1 </dev/null | head -1)
    again=$("$kest" call "$back" "${pair#*:}" "$digits" 2>&1 </dev/null | head -1)
    if [ "$again" != "0" ]; then
        complain "call: ${pair%%:*} did not read back as what it is"
        printf '    it was written %s and read back %s\n' "$digits" "$again"
    fi
done

# And the two refusals that hold that rule for a program rather than for this
# tree, neither of which anything here had ever run: a file that calls itself
# something else, and an import of a file that is not there. Every file in this
# tree is where it says it is, so both are written on the spot.
crossing="$scratch"/check-crossing
mkdir -p "$crossing/parts"
cat > "$crossing/wrong.kest" <<'EOF'
module wrong

import parts.one

fn main() -> i32 {
    return one.n()
}
EOF
cat > "$crossing/parts/one.kest" <<'EOF'
module parts.two

fn n() -> i32 {
    return 0
}
EOF
crossed=$("$kest" check "$crossing/wrong.kest" 2>&1 </dev/null)
case "$crossed" in
*K0703*"calls itself"*) ;;
*)
    complain "a file that calls itself something else was read as it"
    printf '%s\n' "$crossed" | sed 's/^/    /' | head -3
    ;;
esac

cat > "$crossing/missing.kest" <<'EOF'
module missing

import parts.nothing

fn main() -> i32 {
    return 0
}
EOF
crossed=$("$kest" check "$crossing/missing.kest" 2>&1 </dev/null)
case "$crossed" in
*K0701*"cannot read"*) ;;
*)
    complain "an import of a file that is not there said nothing"
    printf '%s\n' "$crossed" | sed 's/^/    /' | head -3
    ;;
esac
rm -rf "$crossing"

# Where the package directories start, which is what the file a command names
# says about itself: `module a.b.c` at `x/y/a/b/c.kest` means the root is
# `x/y`, so `import a.b.d` is `x/y/a/b/d.kest` and not something under the
# directory the file happens to be in. Every program in this tree is named from
# beside its own package, so nothing here has ever asked; this asks it from
# four directories down.
deep="$scratch"/check-deep/x/y/a/b
mkdir -p "$deep"
cat > "$deep/c.kest" <<'KEST'
module a.b.c

import a.b.d

fn main() -> i32 {
    return d.n()
}
KEST
cat > "$deep/d.kest" <<'KEST'
module a.b.d

fn n() -> i32 {
    return 0
}
KEST
if ! rooted=$("$kest" run "$deep/c.kest" 2>&1 </dev/null); then
    complain "run: a package rooted where its file says it is did not run"
    printf '%s\n' "$rooted" | sed 's/^/    /' | head -4
fi

# Where the library is, which is not where the program is. `std` resolves from
# a path built out of the name the command line was run under — beside the
# binary in a tree, beside its directory once installed — and every check here
# runs `./kest` from the root of the tree, where that path and the working
# directory are the same thing. This runs it from somewhere else by its whole
# name, which is how anybody who has installed it runs it.
here=$(pwd)
elsewhere="$scratch"/check-elsewhere
mkdir -p "$elsewhere"
cat > "$elsewhere/asking.kest" <<'KEST'
module asking

import std.io

fn main() -> i32 {
    io.print("the library was found")
    return 0
}
KEST
if ! found=$(cd "$elsewhere" && "$here/$kest" run asking.kest 2>&1 </dev/null)
then
    complain "run: the library is not where a program run from elsewhere looks"
    printf '%s\n' "$found" | sed 's/^/    /' | head -4
fi

# And where a host says it is instead, which is the one thing that overrides
# the rest: a library named and not there is a message about the library rather
# than about the program that imported from it.
if told=$(cd "$elsewhere" && KEST_LIB="$elsewhere/none" \
          "$here/$kest" run asking.kest 2>&1 </dev/null); then
    complain "run: a library that is not where it was said to be was read"
    printf '%s\n' "$told" | sed 's/^/    /' | head -3
else
    case "$told" in
    *K0701*"none/std/io.kest"*) ;;
    *)
        complain "run: a library that is not there did not say where it looked"
        printf '%s\n' "$told" | sed 's/^/    /' | head -3
        ;;
    esac
fi

# And the third place a library can be, which is where it was put when this was
# installed: `../lib/kest/` beside the binary's own directory. Nothing had ever
# installed anything, so what held `make install` was a check reading the
# `Makefile` — the lines being there rather than the files arriving. This puts
# them somewhere of its own, runs what it put there, and takes it away again.
put="$scratch"/check-put
if ! made=$(make -C "$here" install DESTDIR="$put" PREFIX=/usr/local 2>&1); then
    complain "install: this does not install"
    printf '%s\n' "$made" | sed 's/^/    /' | head -3
elif ! ran=$(cd "$elsewhere" &&
             "$put/usr/local/bin/kest" run asking.kest 2>&1 </dev/null); then
    complain "install: what was installed cannot find the library it was installed with"
    printf '%s\n' "$ran" | sed 's/^/    /' | head -4
else
    gone=$(make -C "$here" uninstall DESTDIR="$put" PREFIX=/usr/local 2>&1)
    left=$(find "$put" -type f 2>/dev/null | wc -l)
    if [ "$left" -ne 0 ]; then
        complain "install: what was installed is still there after removing it"
        find "$put" -type f 2>/dev/null | sed 's/^/    /' | head -4
        printf '%s\n' "$gone" | sed 's/^/    /' | head -2
    fi
fi
rm -rf "$put"

# A value written the way the language writes one, which is the same writer
# wherever it is asked from: a hole in a piece of text, `call` saying what came
# back, and `call --json` saying it to a tool. What a program prints and what a
# command line prints for the same value are the same words or one of them is
# lying about what the program holds.
values="$scratch"/check-values.kest
cat > "$values" <<'KEST'
module values

import std.io

enum Door {
    Shut
    Open(i32)
}

flags State: u8 {
    Moving
    Armed
}

fn whole() -> i32 { return -12 }
fn wide() -> i64 { return 9000000000 }
fn small() -> u8 { return 200 }
fn near() -> f32 { return 1.0 / 3.0 }
fn far() -> f64 { return 1.0 / 3.0 }
fn truth() -> bool { return true }
fn words() -> text { return "a line" }
fn shut() -> Door { return Door.Shut }
fn open() -> Door { return Door.Open(4) }
fn state() -> State { return State.Moving | State.Armed }
fn maybe() -> i32? { return 7 }
fn never() -> i32? { return none }

fn main() -> i32 {
    io.print("{whole()}")
    io.print("{wide()}")
    io.print("{small()}")
    io.print("{near()}")
    io.print("{far()}")
    io.print("{truth()}")
    io.print("{words()}")
    io.print("{shut()}")
    io.print("{open()}")
    io.print("{state()}")
    io.print("{maybe()}")
    io.print("{never()}")
    return 0
}
KEST

printed=$("$kest" run "$values" 2>&1 </dev/null)
nth=0
for one in whole wide small near far truth words shut open state maybe never; do
    nth=$((nth + 1))
    printed_line=$(printf '%s\n' "$printed" | sed -n "${nth}p")
    back=$("$kest" call "$values" "$one" 2>&1 </dev/null | head -1)
    machine=$("$kest" call "$values" "$one" --json 2>&1 </dev/null | tail -1 |
           python3 -c 'import json, sys; print(json.load(sys.stdin)["result"])')
    if [ "$printed_line" != "$back" ] || [ "$printed_line" != "$machine" ]; then
        complain "call: \`$one\` is not written the way the program writes it"
        printf '    printed %s, said %s, told %s\n' "$printed_line" \
               "$back" "$machine"
    fi
done

# What `run` says when it works, which is nothing: the status is the answer.
# That is the whole of the promise and nothing had ever held it — every example
# answers nought, so a command line that always exited nought would have passed
# every check here. A program that answers seven has to make a run answer
# seven, in both forms, because a status is not a thing a form changes.
answers="$scratch"/check-answers.kest
cat > "$answers" <<'KEST'
module answers

fn main() -> i32 {
    return 7
}
KEST

"$kest" run "$answers" >/dev/null 2>&1 </dev/null
gave=$?
"$kest" run "$answers" --json >/dev/null 2>&1 </dev/null
written=$?
if [ "$gave" -ne 7 ] || [ "$written" -ne 7 ]; then
    complain "run: what a program answered is not what the run answered"
    printf '    words %s, json %s, and the program answered 7\n' \
           "$gave" "$written"
fi

# And an answer a status cannot carry, which is a message rather than a number
# cut down to what fits: 300 as an exit status is 44, and 44 is a lie about
# what the program said.
too_much="$scratch"/check-too-much.kest
cat > "$too_much" <<'KEST'
module tooMuch

fn main() -> i32 {
    return 300
}
KEST

out=$("$kest" run "$too_much" 2>&1 </dev/null)
gave=$?
case "$out" in
*K0618*) ;;
*)
    complain "run: an answer a status cannot carry was not a message"
    printf '%s\n' "$out" | sed 's/^/    /' | head -3
    ;;
esac
if [ "$gave" -eq 44 ] || [ "$gave" -eq 0 ]; then
    complain "run: an answer a status cannot carry was cut down to fit"
    printf '    the run answered %s\n' "$gave"
fi

# A diagnostic said two ways. One is read by a person and the other by a tool,
# and what is in one and not the other is a thing only half of them can see: a
# fix shown in the words and left out of the JSON is a fix nothing
# machine-readable knows about, and a place named in the JSON and not in the
# words is a line nobody is shown.
#
# Every command that says one is asked, because a diagnostic is the same thing
# whichever command it came out of and the two forms are written by different
# hands at different times.
two_ways() {
    what=$1
    shift
    both=$( { "$kest" "$@" 2>&1 </dev/null;
              echo "----";
              "$kest" "$@" --json 2>&1 </dev/null; } |
            python3 -c '
    import json
    import re
    import sys

    words, _, machine = sys.stdin.read().partition("\n----\n")

    # What the words say: a diagnostic begins at a line with a code in it, and
    # everything under it belongs to it until the next one. A place is a line
    # with an arrow, and what is said about a place is what follows the carets
    # under it. A diagnostic with nowhere to point says its fix on a line of
    # its own, indented and under nothing.
    said = []
    for line in words.splitlines():
        head = re.match(r"error\[(K\d{4})\]: (.*)", line)
        if head:
            said.append({"code": head.group(1), "message": head.group(2),
                         "places": [], "labels": []})
            continue
        if not said:
            continue
        where = re.match(r"\s*--> (.*):(\d+):(\d+)$", line)
        if where:
            said[-1]["places"].append((where.group(1), int(where.group(2)),
                                       int(where.group(3))))
            continue
        under = re.match(r"\s*\|\s*\^+ (.*)$", line)
        if under:
            said[-1]["labels"].append(under.group(1))
            continue
        alone = re.match(r"\s+(\S.*)$", line)
        if alone and not said[-1]["places"] and not said[-1]["labels"]:
            said[-1]["labels"].append(alone.group(1))

    written = json.loads(machine or "{}").get("diagnostics", [])
    if len(said) != len(written):
        print("%u in the words and %u in the JSON" % (len(said), len(written)))
        raise SystemExit(0)

    for one, two in zip(said, written):
        if one["code"] != two.get("code"):
            print("%s in the words and %s in the JSON"
                  % (one["code"], two.get("code")))
            continue
        if one["message"] != two.get("message"):
            print("%s: %r in the words and %r in the JSON"
                  % (one["code"], one["message"], two.get("message")))
        first = one["places"][0] if one["places"] else None
        told = (two.get("file"), two.get("line"), two.get("column"))
        if first != (None if told == (None, None, None) else told):
            print("%s: %s in the words and %s in the JSON"
                  % (one["code"], first, told))
        # What is under the first caret is the fix and what is under the rest
        # is a note apiece, so the labels are the fix and the notes in order.
        notes = [note.get("message") for note in two.get("notes", [])]
        wanted = list(notes)
        if two.get("suggestion") is not None:
            wanted = [two["suggestion"]] + wanted
        if one["labels"] != wanted:
            print("%s: %s under the carets and %s in the JSON"
                  % (one["code"], one["labels"], wanted))
        places = one["places"][1:]
        pointed = [(note.get("file"), note.get("line"), note.get("column"))
                   for note in two.get("notes", [])]
        if places != pointed:
            print("%s: %s said after the first and %s in the JSON"
                  % (one["code"], places, pointed))
    ')
    if [ -n "$both" ]; then
        complain "$what: a diagnostic says one thing in words and another in JSON"
        printf '%s\n' "$both" | sed 's/^/    /' | head -4
    fi
}

# The file it happens on is written here, because no file in the tree is wrong
# and this needs one that is wrong in every way at once: a name that is nearly
# another, a function declared twice, and a body that says nothing about it.
told="$scratch"/check-told.kest
cat > "$told" <<'KEST'
module told

fn counted(a: i32) -> i32 {
    return a
}

fn counted(a: i32) -> i32 {
    return a + 1
}

fn main() -> i32 {
    return countd(1)
}
KEST

# And one that compiles and then goes wrong, which is where the other three
# commands say anything at all: what a program did while running is said the
# same two ways as what it was refused for before it ran.
broke="$scratch"/check-broke.kest
cat > "$broke" <<'KEST'
module broke

fn onEvent(n: i32) -> i32 {
    let xs = [1]
    return xs[n]
}

fn main() -> i32 {
    let xs = [1, 2, 3]
    return xs[5]
}
KEST

# And what `tick` says beside the diagnostics, which is what a frame costs: how
# many times the boundary was crossed, what came back, and what the heap did.
# `check` and `emit` are held to saying the same in both forms and this was
# not, so a number a tool reads could have been a different number from the one
# a reader is shown.
crossed="$scratch"/check-crossed.kest
cat > "$crossed" <<'KEST'
module crossed

fn onEvents(events: [i32]) -> i32 {
    let total = 0
    for e in events {
        total += e
    }
    return total
}

fn onEvent(n: i32) -> i32 {
    let said: [i32] = array()
    push(said, n)
    return len(said)
}

fn main() -> i32 {
    return 0
}
KEST

ticked=$( { "$kest" tick "$crossed" 3 2>&1 </dev/null;
            echo "----";
            "$kest" tick "$crossed" 3 --json 2>&1 </dev/null; } |
          python3 -c '
    import json
    import re
    import sys

    words, _, machine = sys.stdin.read().partition("\n----\n")

    said = {}
    for line in words.splitlines():
        many = re.match(r"onEvents\s+(\d+) crossings?"
                        r"(?:\s+returned (-?\d+))?$", line)
        if many:
            said["onEvents"] = {"crossings": int(many.group(1)),
                                "gave": None if many.group(2) is None
                                        else int(many.group(2))}
            continue
        one = re.match(r"onEvent\s+(\d+) crossings?"
                       r"(?:\s+returned (-?\d+))?, peak (\d+) bytes$", line)
        if one:
            said["onEvent"] = {"crossings": int(one.group(1)),
                               "gave": None if one.group(2) is None
                                       else int(one.group(2)),
                               "peak": int(one.group(3))}
            continue
        over = re.match(r"events\s+(\d+) lent:\s*(.*)$", line)
        if over:
            said["events"] = {"count": int(over.group(1)),
                              "lent": [int(one) for one
                                       in over.group(2).split(",")]}
            continue
        counted = re.match(r"events\s+(\d+), counted up from nought$", line)
        if counted:
            said["events"] = {"count": int(counted.group(1)), "lent": None}
            continue
        heap = re.match(r"heap\s+(\d+) bytes, (.*)$", line)
        if heap:
            said["heap"] = int(heap.group(1))
            thrown = re.match(r"thrown away (\d+) times?$", heap.group(2))
            said["thrown"] = 0 if thrown is None else int(thrown.group(1))

    written = json.loads(machine.splitlines()[-1] if machine.strip() else "{}")
    for what in ("onEvents", "onEvent", "events", "heap", "thrown"):
        if (what in said) != (what in written):
            print("%s: %s in the words and %s in the JSON"
                  % (what, what in said, what in written))
            continue
        if what in said and said[what] != written[what]:
            print("%s: %s in the words and %s in the JSON"
                  % (what, said[what], written[what]))
    if not said:
        print("a tick said nothing about what it cost")
        raise SystemExit(0)

    # And what the numbers mean, which is the half no comparison of two forms
    # can see: both of them saying the same wrong thing agree.
    if said["onEvent"]["crossings"] != int(sys.argv[1]):
        print("a tick of %s events crossed %d times"
              % (sys.argv[1], said["onEvent"]["crossings"]))
    if said["onEvents"]["crossings"] != 1:
        print("a batch of events crossed %d times"
              % said["onEvents"]["crossings"])
    # The most it held is at least what it was holding at the end, whether the
    # heap was thrown away between events or never at all.
    if said["onEvent"]["peak"] < said["heap"]:
        print("the most the heap held was %d and it ended holding %d"
              % (said["onEvent"]["peak"], said["heap"]))
    # And what it ran over is as many as it crossed, whichever way they came.
    if said["events"]["count"] != said["onEvent"]["crossings"]:
        print("it ran over %d events and crossed %d times"
              % (said["events"]["count"], said["onEvent"]["crossings"]))
    ' 3)
if [ -n "$ticked" ]; then
    complain "tick: what a frame cost is one thing in words and another in JSON"
    printf '%s\n' "$ticked" | sed 's/^/    /' | head -4
fi

# And the same over a tick that was told which events to run, because what was
# lent is the half a counted run never says.
lent=$("$kest" tick "$crossed" 4,5,6 2>&1 </dev/null | sed -n 's/^events *//p')
lent_json=$("$kest" tick "$crossed" 4,5,6 --json 2>&1 </dev/null |
            tail -1 |
            python3 -c 'import json, sys; print(json.load(sys.stdin)["events"])')
case "$lent:$lent_json" in
"3 lent: 4, 5, 6:{'count': 3, 'lent': [4, 5, 6]}") ;;
*)
    complain "tick: what it was lent is one thing in words and another in JSON"
    printf '    words %s\n    json  %s\n' "$lent" "$lent_json"
    ;;
esac

two_ways "check" check "$told"
two_ways "run" run "$broke"
two_ways "tick" tick "$broke" 3
two_ways "call" call "$broke" nope

for file in "$@"; do
    at=$((at + 1))
    mine="$said/$(printf %04d $at)"
    if [ -s "$mine" ]; then
        cat "$mine"
        failed=1
    fi
done
rm -rf "$said"

rm -f "$scratch"/cmd-err
if [ $failed -eq 0 ]; then
    echo "every command does something on $# file(s)"
fi
exit $failed
