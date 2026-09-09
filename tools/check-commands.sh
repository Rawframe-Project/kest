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
mkdir "$scratch"/holding-nothing
nothing="$scratch"/holding-nothing/nothing.kest
: > "$nothing"
expect "$nothing" lex 'end of file'
expect "$nothing" parse 'declares nothing'
expect "$nothing" check 'declares nothing'
expect "$nothing" emit '^nothing to run'
if [ -n "$($kest fmt "$nothing" 2>&1)" ]; then
    complain "fmt $nothing: a file that holds nothing formatted to something"
fi
# And a file that holds one comment and nothing else, which is a file that says
# something and declares nothing — a shape every command has its own sentence
# for, and none of them had ever been asked to say it. What is held is that all
# of them answer and that each says the thing it is for: `fmt` keeps what was
# written, and the rest say there is nothing to do with it.
mkdir "$scratch"/saying
saying="$scratch"/saying/saying.kest
printf '// what this file is for\n' > "$saying"
expect "$saying" lex 'end of file'
expect "$saying" parse 'declares nothing'
expect "$saying" check 'declares nothing'
expect "$saying" emit '^nothing to run'
expect "$saying" fmt 'what this file is for'
for one in "run:K0603" "tick:K0621"; do
    if answered=$("$kest" "${one%%:*}" "$saying" 2>&1 </dev/null); then
        complain "${one%%:*} $saying: a file with nothing to run ran"
        printf '%s\n' "$answered" | sed 's/^/    /' | head -2
    else
        case "$answered" in
        *"${one#*:}"*) ;;
        *)
            complain "${one%%:*} $saying: not the refusal a file with nothing to run gets"
            printf '%s\n' "$answered" | sed 's/^/    /' | head -2
            ;;
        esac
    fi
done
rm -rf "$(dirname "$saying")"

# A program that asks the host for something this host does not have. No file
# in the tree is one — every extern here is a name the command line binds — and
# what it stands for is any refusal that happens between compiling and running,
# which is where a message has no machine to be read from.
mkdir "$scratch"/asking
asking="$scratch"/asking/asking.kest
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
mkdir "$scratch"/piped
piped="$scratch"/piped/piped.kest
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

# A path that is not a file at all. It opens, it measures nought, and it
# refuses to be read, which is how a directory used to be a file with nothing
# in it: `kest check` said it declared nothing.
where="$scratch"/where
mkdir "$where"
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
said="$scratch"/said
mkdir "$said"
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

# And which library a program gets when there is more than one. A tree being
# installed has both — the one beside the binary and the one under the prefix —
# and what a program reads is whichever the search reaches first. The order is
# the whole of the answer, so it is run: three libraries that differ by one
# function, and the one that answers says which was read.
places="$scratch"/check-places
mkdir -p "$places/bin" "$places/lib/kest" "$places/other"
cp "$here/$kest" "$places/bin/kest"
cp -r "$here"/lib "$places/bin/lib"
cp -r "$here"/lib/std "$places/lib/kest/std"
cp -r "$here"/lib/std "$places/other/std"
for copy in "beside:$places/bin/lib/std/io.kest" \
            "installed:$places/lib/kest/std/io.kest" \
            "told:$places/other/std/io.kest"; do
    printf '\nfn which() -> text {\n    return "%s"\n}\n' "${copy%%:*}" \
        >> "${copy#*:}"
done
cat > "$places/asking.kest" <<'KEST'
module asking

import std.io

fn main() -> i32 {
    io.print(io.which())
    return 0
}
KEST
read_from=$(cd "$places" && ./bin/kest run asking.kest 2>&1 </dev/null)
if [ "$read_from" != "beside" ]; then
    complain "run: a program with two libraries read the wrong one"
    printf '    it read `%s` where the one beside the command is `beside`\n' \
           "$read_from"
fi
read_from=$(cd "$places" && KEST_LIB="$places/other" ./bin/kest run \
            asking.kest 2>&1 </dev/null)
if [ "$read_from" != "told" ]; then
    complain "run: a library named by a host did not win"
    printf '    it read `%s` where the one it was told is `told`\n' "$read_from"
fi
rm -rf "$places"

# A program read with a library that is not the one it was written against.
# There is no version on a library here and there is nothing to mismatch: the
# library is source, compiled with the program every time, so what a program
# gets is a name that is not there rather than a call into something else. What
# it needs beside that is which library it looked in.
against="$scratch"/check-against
mkdir -p "$against/std"
cp "$here"/lib/std/*.kest "$against/std/"
sed -i.was 's/^fn print(/fn say(/' "$against/std/io.kest"
rm -f "$against/std/io.kest.was"
cat > "$against/using.kest" <<'KEST'
module using

import std.io

fn main() -> i32 {
    io.print("hello")
    return 0
}
KEST
if elsewise=$(KEST_LIB="$against" "$kest" check "$against/using.kest" 2>&1 \
              </dev/null); then
    complain "check: a name a library does not have was read as one it has"
    printf '%s\n' "$elsewise" | sed 's/^/    /' | head -3
else
    case "$elsewise" in
    *"has nothing called"*"$against/std/io.kest"*"that was read"*) ;;
    *)
        complain "check: a name a library does not have did not say which library"
        printf '%s\n' "$elsewise" | sed 's/^/    /' | head -6
        ;;
    esac
fi

# A module written nearly right. What a file writes as often as anything else
# is the name in front of the dot, and a name nothing declares — a module is a
# file, not a declaration — was the one kind of name nothing was ever suggested
# for.
spelt="$scratch"/check-spelt.kest
cat > "$spelt" <<'KEST'
module spelt

import std.io

fn main() -> i32 {
    ioo.print("hello")
    return 0
}
KEST
meant=$("$kest" check "$spelt" 2>&1 </dev/null)
case "$meant" in
*"unknown name \`ioo\`"*"did you mean \`io\`?"*) ;;
*)
    complain "check: a module written nearly right was not named back"
    printf '%s\n' "$meant" | sed 's/^/    /' | head -5
    ;;
esac

# Two names equally near the one that was written. What a suggestion says is
# what this knows, and knowing two and saying one is choosing for a reader —
# so it says both, and says nothing at all when more than two are level,
# because a list of names is not a suggestion.
level="$scratch"/check-level.kest
cat > "$level" <<'KEST'
module level

fn health() -> i32 {
    return 1
}

fn wealth() -> i32 {
    return 2
}

fn main() -> i32 {
    return xealth()
}
KEST
both=$("$kest" check "$level" 2>&1 </dev/null)
case "$both" in
*"did you mean \`health\` or \`wealth\`?"*) ;;
*)
    complain "check: two names equally near were said as one"
    printf '%s\n' "$both" | sed 's/^/    /' | head -5
    ;;
esac

# A short name written wrong. Nothing under three letters was ever answered for
# — every short name is one edit from every other — and what made that rule
# necessary is gone: two names equally near are both said and three are said as
# nothing. A module named `io` is two letters and a file writes it everywhere.
short="$scratch"/check-short.kest
cat > "$short" <<'KEST'
module short

import std.io

fn main() -> i32 {
    ip.print("hello")
    return 0
}
KEST
answered=$("$kest" check "$short" 2>&1 </dev/null)
case "$answered" in
*"unknown name \`ip\`"*"did you mean \`io\`?"*) ;;
*)
    complain "check: a name of two letters was not answered for"
    printf '%s\n' "$answered" | sed 's/^/    /' | head -5
    ;;
esac

# Two letters the other way round, which is the commonest way to write a name
# wrong and the one an edit count gets wrong: `pirnt` is two edits from `print`
# by counting insertions and removals and one by any reader's reckoning. The
# machine counts it as one and nothing had ever asked it to.
swapped="$scratch"/check-swapped.kest
cat > "$swapped" <<'KEST'
module swapped

import std.io

fn main() -> i32 {
    io.pirnt("hello")
    return 0
}
KEST
turned=$("$kest" check "$swapped" 2>&1 </dev/null)
case "$turned" in
*"did you mean \`io.print\`?"*) ;;
*)
    complain "check: two letters the other way round were not one mistake"
    printf '%s\n' "$turned" | sed 's/^/    /' | head -5
    ;;
esac

# A name longer than the table the distance is measured in. Names are compared
# qualified, so a real one can be longer than anybody expects, and a name past
# the ceiling was near nothing without a word about why. What is held is that a
# name of seventy letters is answered for; past two hundred and fifty-six it is
# not, which is a length nobody writes twice.
lengthy="$scratch"/check-lengthy.kest
long_name=$(printf 'a%.0s' $(seq 70))
{
    printf 'module lengthy\n\n'
    printf 'fn %s() -> i32 {\n    return 1\n}\n\n' "$long_name"
    printf 'fn main() -> i32 {\n    return %sb()\n}\n' \
           "$(printf '%s' "$long_name" | cut -c1-69)"
} > "$lengthy"
lengthily=$("$kest" check "$lengthy" 2>&1 </dev/null)
case "$lengthily" in
*"did you mean \`$long_name\`?"*) ;;
*)
    complain "check: a name of seventy letters was near nothing"
    printf '%s\n' "$lengthily" | sed 's/^/    /' | head -4
    ;;
esac

# Two files read as one program, and which of them is written out. What `check`
# says in full is what the first file named declares; every other module is a
# line saying how much it holds. Naming the same two files the other way round
# is a different question and gets a different answer, and nothing had ever
# asked either.
both_at="$scratch"/check-both
mkdir -p "$both_at"
cat > "$both_at/first.kest" <<'KEST'
module first

fn one() -> i32 {
    return 1
}
KEST
cat > "$both_at/second.kest" <<'KEST'
module second

import first

fn two() -> i32 {
    return first.one() + 1
}
KEST
led=$("$kest" check "$both_at/second.kest" "$both_at/first.kest" 2>&1 </dev/null)
case "$led" in
*"fn second.two() -> i32"*"first  1 function"*) ;;
*)
    complain "check: the first file named was not the one written out"
    printf '%s\n' "$led" | sed 's/^/    /' | head -4
    ;;
esac
led=$("$kest" check "$both_at/first.kest" "$both_at/second.kest" 2>&1 </dev/null)
case "$led" in
*"fn first.one() -> i32"*"second  1 function"*) ;;
*)
    complain "check: the other file named first was not the one written out"
    printf '%s\n' "$led" | sed 's/^/    /' | head -4
    ;;
esac
# And the two forms of that answer. The words write out one module and count
# the rest; the JSON writes every function there is, because a tool wants all
# of them and a reader wants the one they asked about. What holds them together
# is that the count in the words is how many the JSON has under that name, and
# that what was written out in full is what the JSON has under the first
# file's.
sides=$( { "$kest" check "$both_at/second.kest" "$both_at/first.kest" 2>&1 \
           </dev/null;
           echo "----";
           "$kest" check "$both_at/second.kest" "$both_at/first.kest" --json \
           2>&1 </dev/null; } |
         python3 -c '
    import json
    import re
    import sys

    words, _, machine = sys.stdin.read().partition("\n----\n")

    written = []
    counted = {}
    for line in words.splitlines():
        one = re.match(r"(?:extern )?fn ([A-Za-z0-9_.]+)\(", line)
        if one:
            written.append(one.group(1))
            continue
        many = re.match(r"([a-z][A-Za-z0-9_.]*)\s+(?:\d+ types?, )?"
                        r"(\d+) functions?", line)
        if many:
            counted[many.group(1)] = int(many.group(2))

    try:
        told = json.loads(machine or "{}").get("functions", [])
    except ValueError:
        print("what was said as JSON is not JSON")
        raise SystemExit(0)
    under = {}
    for one in told:
        module = one["name"].rsplit(".", 1)[0]
        under.setdefault(module, []).append(one["name"])

    if not written or not counted:
        print("the words wrote out %u and counted %u modules"
              % (len(written), len(counted)))
        raise SystemExit(0)

    first = written[0].rsplit(".", 1)[0]
    if sorted(written) != sorted(under.get(first, [])):
        print("%s: %s written out and %s in the JSON"
              % (first, sorted(written), sorted(under.get(first, []))))
    for module, how_many in sorted(counted.items()):
        if len(under.get(module, [])) != how_many:
            print("%s: %u counted and %u in the JSON"
                  % (module, how_many, len(under.get(module, []))))
    ')
if [ -n "$sides" ]; then
    complain "check: a program of two files says one thing in words and another in JSON"
    printf '%s\n' "$sides" | sed 's/^/    /' | head -4
fi

rm -rf "$both_at"

# A program that prints and then goes wrong. The two streams are kept apart, so
# a shell that puts both in one pipe is where their order shows: what a program
# printed is buffered until the run ends and what went wrong is not, so the
# failure would arrive before the lines that led to it — a lie about the order
# things happened in, told by the machine that watched them happen.
saidfirst="$scratch"/check-saidfirst.kest
cat > "$saidfirst" <<'KEST'
module saidfirst

import std.io

fn main() -> i32 {
    io.print("before")
    let xs = [1]
    return xs[3]
}
KEST
ordered=$("$kest" run "$saidfirst" 2>&1 </dev/null | head -2)
case "$ordered" in
"before"*) ;;
*)
    complain "run: what a program printed came after what went wrong"
    printf '%s\n' "$ordered" | sed 's/^/    /' | head -3
    ;;
esac
# And apart, which is where each half belongs.
printed=$("$kest" run "$saidfirst" 2>/dev/null </dev/null)
if [ "$printed" != "before" ]; then
    complain "run: what a program printed is not what its answer stream held"
    printf '    it held `%s`\n' "$printed"
fi

# And a run that ends well, which is where nothing is written on purpose: what
# a program printed is all that comes out, and the whole of it. Ten thousand
# lines is more than a stream holds at once, so what this asks is whether the
# last of them arrives — a command line that ends without emptying what it
# holds loses whatever was still in hand, and a program that printed a
# thousand lines and answered nought looks like one that printed nine hundred.
plenty="$scratch"/check-plenty.kest
cat > "$plenty" <<'KEST'
module plenty

import std.io

fn main() -> i32 {
    for i in 0..10000 {
        io.print("line {i}")
    }
    return 7
}
KEST
"$kest" run "$plenty" </dev/null 2>/dev/null > "$scratch"/check-plenty-out
plenty_gave=$?
plenty_lines=$(wc -l < "$scratch"/check-plenty-out)
if [ "$plenty_lines" -ne 10000 ] ||
   [ "$(tail -1 "$scratch"/check-plenty-out)" != "line 9999" ]; then
    complain "run: a program that printed ten thousand lines lost some of them"
    printf '    %s lines, ending `%s`\n' "$plenty_lines" \
           "$(tail -1 "$scratch"/check-plenty-out)"
fi
if [ "$plenty_gave" -ne 7 ]; then
    complain "run: a program that printed and then answered did not answer"
    printf '    it answered %s\n' "$plenty_gave"
fi
rm -f "$scratch"/check-plenty-out

# A reference from one store handed to another. A reference is a number — a
# place and the stamp that place was handed out with — and a stamp comes from
# the machine rather than from the store, so no two places anywhere carry the
# same one. Before that, a reference into a store of the same shape named
# whoever was standing in that place: somebody else's value, given back with
# nothing wrong said about it.
elsewhere_ref="$scratch"/check-elsewhere-ref.kest
cat > "$elsewhere_ref" <<'KEST'
module elsewhereRef

struct Thing {
    n: i32
}

fn main() -> i32 {
    let a: store<Thing> = store()
    let b: store<Thing> = store()
    let one = add(a, Thing(7))
    add(b, Thing(9))
    if let wrong = get(b, one) {
        return wrong.n
    }
    return 0
}
KEST
"$kest" run "$elsewhere_ref" >/dev/null 2>&1 </dev/null
crossed=$?
if [ "$crossed" -ne 0 ]; then
    complain "run: a reference used with another store named somebody else"
    printf '    it answered %s, which is what that store holds\n' "$crossed"
fi

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

    try:
        written = json.loads(machine or "{}").get("diagnostics", [])
    except ValueError:
        # A form that is not JSON is a thing the check below says plainly; what
        # this would say is a stack trace, which says it in a language nobody
        # reading this speaks.
        print("what was said as JSON is not JSON")
        raise SystemExit(0)
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

    try:
        written = json.loads(machine.splitlines()[-1] if machine.strip()
                             else "{}")
    except ValueError:
        print("what was said as JSON is not JSON")
        raise SystemExit(0)
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

# Which of the two streams each half goes to. What is wrong with a program goes
# where a shell keeps errors and what a program holds goes where a shell keeps
# answers, so `kest check x.kest > held` writes the answer and shows the errors
# — and in JSON everything is on one stream, because a tool reads one thing and
# an object split over two is neither.
alone=$("$kest" check "$told" 2>/dev/null </dev/null)
if [ -n "$alone" ]; then
    complain "check: what is wrong with a program was written where its answer goes"
    printf '%s\n' "$alone" | sed 's/^/    /' | head -3
fi
aside=$("$kest" check "$told" 2>&1 >/dev/null </dev/null)
case "$aside" in
*K0304*) ;;
*)
    complain "check: what is wrong with a program was not written where errors go"
    printf '%s\n' "$aside" | sed 's/^/    /' | head -3
    ;;
esac
aside=$("$kest" check "$told" --json 2>&1 >/dev/null </dev/null)
if [ -n "$aside" ]; then
    complain "check: a tool was given something on the stream it does not read"
    printf '%s\n' "$aside" | sed 's/^/    /' | head -3
fi
streams="$scratch"/check-streams.kest
cat > "$streams" <<'KEST'
module streams

fn one() -> i32 {
    return 1
}
KEST
alone=$("$kest" check "$streams" 2>/dev/null </dev/null)
case "$alone" in
*"fn streams.one() -> i32"*) ;;
*)
    complain "check: what a program holds was not written where an answer goes"
    printf '%s\n' "$alone" | sed 's/^/    /' | head -3
    ;;
esac


# What each form says about a program that did not check. The words say what is
# wrong and nothing else: a listing of what a half-worked-out program holds is
# a reader being shown a program that does not exist. The JSON says both,
# because a tool reading a file somebody is still writing wants what has been
# worked out so far — an editor greys out what it cannot see yet rather than
# forgetting it.
unfinished=$("$kest" check "$told" 2>&1 </dev/null)
case "$unfinished" in
*"fn told."*)
    complain "check: a program that did not check was written out anyway"
    printf '%s\n' "$unfinished" | sed 's/^/    /' | head -3
    ;;
esac
held_anyway=$("$kest" check "$told" --json 2>&1 </dev/null | python3 -c '
    import json
    import sys

    said = json.load(sys.stdin)
    if said.get("errors", 0) < 1:
        print("a program that did not check said nothing was wrong")
    if not said.get("functions"):
        print("a program that did not check said it holds nothing")
    ')
if [ -n "$held_anyway" ]; then
    complain "check: what a tool is given about a program that did not check"
    printf '%s\n' "$held_anyway" | sed 's/^/    /' | head -3
fi

two_ways "run" run "$broke"
two_ways "tick" tick "$broke" 3
two_ways "call" call "$broke" nope

# What a diagnostic carries, asked of one that carries all of it. The two forms
# are held to each other elsewhere in this file, and two forms that agree are
# two forms that lost the same thing: a suggestion that stopped being printed
# and stopped being written is the same diagnostic in both. So this asks for
# the four the reference says there are — the code, the place, the fix, and the
# notes around it, each with a place of its own — of a program written to have
# every one of them.
mkdir "$scratch"/carried
cat > "$scratch"/carried/carried.kest <<'KEST'
fn second(n: i32) -> i32 {
    let trail = [n, n, n]
    return len(trail) + n
}

fn stepFrame(n: i32) -> i32 no.alloc {
    return second(n)
}

fn main() -> i32 {
    return stepFrame(1)
}
KEST
carried="$scratch"/carried/carried.kest
carried_said=$("$kest" check "$carried" 2>&1 </dev/null)
for want in "error[K0401]" "carried.kest:2:17" \
            "a run that can grow is one on the heap" \
            "\`stepFrame\` promises it here" "which calls \`second\`"; do
    case "$carried_said" in
    *"$want"*) ;;
    *)
        complain "check: a diagnostic with everything in it did not say \
\`$want\`"
        printf '%s\n' "$carried_said" | sed 's/^/    /' | head -8
        ;;
    esac
done
# Three places rather than one: what it is about, and the two notes. A note
# without a place of its own is prose about a line nobody can find.
pointed_at=$(printf '%s\n' "$carried_said" | grep -c -- '-->')
if [ "$pointed_at" -ne 3 ]; then
    complain "check: a diagnostic about three places pointed at $pointed_at"
    printf '%s\n' "$carried_said" | sed 's/^/    /' | head -8
fi

# And the same four in the other form, where they are named rather than laid
# out: a tool reads these by name and a name that is not written is a field a
# reader of the JSON has to guess at.
wrote=$("$kest" check --json "$carried" 2>&1 </dev/null)
for want in '"code":"K0401"' '"line":2' '"column":17' '"offset":' '"length":9' \
            '"suggestion":' '"notes":' '"message":'; do
    case "$wrote" in
    *"$want"*) ;;
    *)
        complain "check --json: a diagnostic with everything in it wrote no \
$want"
        printf '%s\n' "$wrote" | sed 's/^/    /' | head -4
        ;;
    esac
done

# And what those notes point at. A note that names something is a note about
# where that something is: `\`stepFrame\` promises it here` under a line that is
# not the declaration is worse than no note, and reads exactly like a right
# one. What each note says it is about is in the message, in backticks, and
# where it says it is is a line of a file this check wrote — so the two are put
# together and the file is read.
# And which of them a program may promise `no.alloc` for. That is not a fact
# about the names: it is what crossing back costs, so the ones that hand over
# text or an array make it on the machine's heap and the ones that answer a
# number do not. A program says `no.alloc` on an `extern` and the machine holds
# the host to it — this host included, which is the one place where what
# somebody typed at a shell is checked against what the compiler's own C does.
mkdir "$scratch"/promises
for promising in "Io.read() -> text|let t = Io.read()|takes" \
                 "Engine.name() -> text|let t = Engine.name()|takes" \
                 "Host.samples() -> [f32]|let s = Host.samples()|takes" \
                 "Io.write(value: text)|Io.write(\"\")|keeps" \
                 "Engine.decide(h: i32) -> i32|let n = Engine.decide(1)|keeps" \
                 "Host.sqrt(v: f64) -> f64|let n = Host.sqrt(4.0)|keeps" \
                 "Host.write(value: text)|Host.write(\"\")|keeps" \
                 "Host.clock() -> i64|let n = Host.clock()|keeps" \
                 "Host.sample(i: i32) -> f32|let n = Host.sample(0)|keeps"; do
    declares=${promising%%|*}
    rest=${promising#*|}
    calls=${rest%|*}
    expected=${rest##*|}
    cat > "$scratch"/promises/promised.kest <<KEST
module promised

extern fn $declares no.alloc

fn main() -> i32 {
    $calls
    return 0
}
KEST
    kept=$("$kest" run "$scratch"/promises/promised.kest 2>&1 </dev/null)
    case "$expected:$kept" in
    takes:*K0631*"promises \`no.alloc\` and this host took"*) ;;
    keeps:"") ;;
    takes:*)
        complain "run: \`$declares\` reaches the heap and was let promise \
\`no.alloc\`"
        printf '%s\n' "$kept" | sed 's/^/    /' | head -3
        ;;
    keeps:*)
        complain "run: \`$declares\` keeps \`no.alloc\` and was refused"
        printf '%s\n' "$kept" | sed 's/^/    /' | head -3
        ;;
    esac
done

# What a cut costs, which is nothing when it ends where the text already ends.
# The nought after such a piece is the one that was already there, so there is
# nothing to copy — the same thing `rest` is, asked with a length. A cut that
# stops sooner needs a nought of its own and pays for the piece.
mkdir "$scratch"/cutting
cat > "$scratch"/cutting/cutting.kest <<'KEST'
fn measured(t: text) -> i32 {
    return len(t)
}

fn whole(t: text) -> i32 {
    return len(slice(t, 0, len(t)))
}

fn tail(t: text) -> i32 {
    return len(slice(t, 2, len(t) - 2))
}

fn middle(t: text) -> i32 {
    return len(slice(t, 1, len(t) - 2))
}

fn past(t: text) -> i32 {
    return len(slice(t, 8, 9))
}

fn back(t: text, from: i32) -> i32 {
    return len(slice(t, from, 2))
}

fn at(t: text, index: i32) -> i32 {
    return i32(t[index])
}
KEST
cutting="$scratch"/cutting/cutting.kest
# And what a cut, or a read, says when it is asked for what is not there. The
# length is in the message, which is the one thing either of them measures the
# whole of the text for — so it is measured there and nowhere else: what
# reaching a place costs is the part walked to it, and a run that is stopping
# can pay for the rest.
for asking in "past abcdefghij|9 bytes from 8 is outside text of 10 bytes" \
              "back abcdefghij -1|2 bytes from -1 is outside text of 10 bytes" \
              "at abcdefghij 10|index 10 is outside text of 10 bytes" \
              "at abcdefghij -1|index -1 is outside text of 10 bytes"; do
    calling=${asking%%|*}
    wanted=${asking#*|}
    # shellcheck disable=SC2086
    refused_cut=$("$kest" call "$cutting" $calling 2>&1 </dev/null)
    case "$refused_cut" in
    *"$wanted"*) ;;
    *)
        complain "call $calling: reaching outside the text said \`$refused_cut\`"
        ;;
    esac
done
cut_heap() {
    "$kest" call --json "$cutting" "$1" abcdefghij 2>/dev/null </dev/null |
        sed -n 's/.*"heap":\([0-9][0-9]*\).*/\1/p'
}
just_measured=$(cut_heap measured)
whole_cut=$(cut_heap whole)
tail_cut=$(cut_heap tail)
middle_cut=$(cut_heap middle)
if [ -z "$just_measured" ] || [ -z "$middle_cut" ]; then
    complain "call --json: a cut said nothing about what it cost"
elif [ "$whole_cut" != "$just_measured" ] || [ "$tail_cut" != "$just_measured" ]; then
    complain "call: a cut that ends where the text ends cost \
$whole_cut and $tail_cut where measuring it cost $just_measured"
elif [ "$middle_cut" -le "$just_measured" ]; then
    complain "call: a cut that stops sooner cost $middle_cut, which is what \
measuring it costs"
fi

# Text that ends in the middle of a character, which is what text arriving a
# piece at a time does. The library counts a character by its first byte, so
# the last one of a half-read line says it is three bytes wide when two are
# there — and asking for it used to stop the program at a line it could not
# help. What is there is what comes back.
mkdir "$scratch"/cut
cat > "$scratch"/cut/cut.kest <<'KEST'
import std.text

fn main() -> i32 {
    let raw: [u8] = array()
    push(raw, u8(104))
    push(raw, u8(226))
    let half = text(raw)
    if text.chars(half) != 2 {
        return 1
    }
    if let last = text.charAt(half, 1) {
        if len(last) != 1 {
            return 2
        }
    } else {
        return 3
    }
    // And a character that says it is three bytes wide with something that is
    // not the middle of one after it: what ends it is that byte, so the `i`
    // is a character of its own rather than something swallowed by the one
    // before it.
    let swallowing: [u8] = array()
    push(swallowing, u8(104))
    push(swallowing, u8(226))
    push(swallowing, u8(105))
    let three = text(swallowing)
    if text.chars(three) != 3 {
        return 4
    }
    if let letter = text.charAt(three, 2) {
        if letter != "i" {
            return 5
        }
    } else {
        return 6
    }
    // And the walk back, which has to land where the walk forwards started —
    // on text somebody chose the bytes of and on text nobody did.
    if !walksBack("hız") || !walksBack(half) || !walksBack(three) {
        return 7
    }
    return 0
}

fn walksBack(subject: text) -> bool no.alloc {
    let at = 0
    let tail = subject
    while tail != "" {
        let wide = text.charWidth(tail)
        if text.charBack(subject, at + wide) != at {
            return false
        }
        at += wide
        tail = rest(tail, wide)
    }
    return true
}
KEST
"$kest" run "$scratch"/cut/cut.kest >"$scratch"/cut-said 2>&1 </dev/null
if [ $? -ne 0 ] || [ -s "$scratch"/cut-said ]; then
    complain "run: a character cut off at the end of what was read"
    sed 's/^/    /' "$scratch"/cut-said | head -4
fi

# A comment written inside a hole in a string. A hole is code, and the
# formatter writes it back from what it means rather than copying it, so a
# comment in one is a comment nothing can put back — and at the level of the
# file the whole string is one token, so no tool is told there is a comment
# there at all. It was accepted, and formatting the file quietly took it away.
mkdir "$scratch"/hole
cat > "$scratch"/hole/hole.kest <<'KEST'
import std.io

fn main() -> i32 {
    let a = 1
    io.print("A[{a // note}]")
    return 0
}
KEST
held=$("$kest" check "$scratch"/hole/hole.kest 2>&1 </dev/null)
case "$held" in
*"K0111"*"a comment inside a hole"*"write it above the line"*) ;;
*)
    complain "check: a comment inside a hole said \`$held\`"
    ;;
esac
# And the same comment where it can be kept, which is the line above.
cat > "$scratch"/hole/fine.kest <<'KEST'
import std.io

fn main() -> i32 {
    let a = 1
    // note
    io.print("A[{a}]")
    return 0
}
KEST
if ! "$kest" run "$scratch"/hole/fine.kest >/dev/null 2>&1 </dev/null; then
    complain "run: the same comment above the line was refused too"
fi

# What this host calls itself and what it decides, which is the rest of what
# the command line provides that no module declares. Every host binds what it
# likes beyond the library, so what this one binds is a thing a program can
# only find out by asking — the reference says `kest` and 1, and nothing had
# ever asked.
mkdir "$scratch"/hosted
cat > "$scratch"/hosted/hosted.kest <<'KEST'
module hosted

import std.io

extern fn Engine.name() -> text
extern fn Engine.decide(health: i32) -> i32 no.alloc

fn main() -> i32 {
    io.print("{Engine.name()} decides {Engine.decide(7)}")
    return 0
}
KEST
hosted=$("$kest" run "$scratch"/hosted/hosted.kest 2>/dev/null </dev/null)
if [ "$hosted" != "kest decides 1" ]; then
    complain "run: this host says it is \`$hosted\`, and the reference says \
\`kest decides 1\`"
fi

# And what a program reads when the reading fails. `Io.read` gives back text
# and has no way to say that it could not, so a stream that would not be read
# hands over an empty piece: a closed stream and a directory both read as an
# empty input, and a program counting what it was given counts nought either
# way. The host finds out, the same way it does about writing.
mkdir "$scratch"/reading
cat > "$scratch"/reading/reading.kest <<'KEST'
module reading

extern fn Io.read() -> text

fn main() -> i32 {
    let all = Io.read()
    return len(all)
}
KEST
reading="$scratch"/reading/reading.kest
printf 'abc' | "$kest" run "$reading" >"$scratch"/reading-said 2>&1
if [ $? -ne 3 ] || [ -s "$scratch"/reading-said ]; then
    complain "run: three bytes on the standard input were not three"
    sed 's/^/    /' "$scratch"/reading-said | head -3
fi
"$kest" run "$reading" >"$scratch"/reading-said 2>&1 </dev/null
if [ $? -ne 0 ] || [ -s "$scratch"/reading-said ]; then
    complain "run: an empty standard input was read as something wrong"
    sed 's/^/    /' "$scratch"/reading-said | head -3
fi
# Something that is not a file at all, which reads as nothing and is not
# nothing: the one case a program cannot tell from an empty input.
unread=$("$kest" run "$reading" 2>&1 <"$scratch"/reading)
if [ $? -eq 0 ]; then
    complain "run: a standard input that would not be read answered nought"
fi
case "$unread" in
*K0642*"could not be read"*) ;;
*)
    complain "run: a standard input that would not be read said nothing"
    printf '%s\n' "$unread" | sed 's/^/    /' | head -3
    ;;
esac

# What a program says when the writing fails. `Io.write` gives nothing back, so
# a program cannot be told and does not know; the host is the one that finds
# out, and here the host is this command line. A run into a stream that will
# not take anything used to write nothing and answer nought, which is a script
# carrying on with an empty file.
mkdir "$scratch"/loud
cat > "$scratch"/loud/loud.kest <<'KEST'
module loud

import std.io

fn main() -> i32 {
    io.print("hello")
    return 0
}
KEST
if [ -w /dev/full ]; then
    refused_write=$("$kest" run "$scratch"/loud/loud.kest 2>&1 >/dev/full \
                    </dev/null)
    if [ $? -eq 0 ]; then
        complain "run: a program whose writing went nowhere answered nought"
    fi
    case "$refused_write" in
    *K0641*"could not be written"*) ;;
    *)
        complain "run: a program whose writing went nowhere said nothing"
        printf '%s\n' "$refused_write" | sed 's/^/    /' | head -3
        ;;
    esac
    # And the same run into somewhere that takes it, which has to say nothing
    # at all: a check for a failure that fires when nothing failed is worse
    # than none.
    into="$scratch"/loud/into
    if ! "$kest" run "$scratch"/loud/loud.kest >"$into" 2>"$into".err \
         </dev/null || [ -s "$into".err ] || [ "$(cat "$into")" != "hello" ]; then
        complain "run: a program whose writing arrived was told it had not"
        sed 's/^/    /' "$into".err | head -3
    fi
fi

# Which line of what a command printed is its answer. A program says things
# while it runs, and a command that answers with something of its own — the
# value a call gave, the numbers a frame cost — used to say both on the same
# stream with nothing between them: a shell reading `kest call` got the
# program's writing above the value and no way to tell them apart. What
# answers is on standard output and the program's writing is beside it now,
# which is what `--json` has always done.
mkdir "$scratch"/aside
cat > "$scratch"/aside/aside.kest <<'KEST'
module aside

import std.io

fn greet(name: text) -> i32 {
    io.print("hello {name}")
    return len(name)
}

fn onEvent(event: i32) -> i32 {
    io.print("event {event}")
    return event
}

fn main() -> i32 {
    return greet("x")
}
KEST
aside="$scratch"/aside/aside.kest
answered=$("$kest" call "$aside" aside.greet world 2>/dev/null </dev/null)
if [ "$answered" != "5" ]; then
    complain "call: what a program said is on the answer's stream: \
\`$answered\`"
fi
beside=$("$kest" call "$aside" aside.greet world 2>&1 >/dev/null </dev/null)
if [ "$beside" != "hello world" ]; then
    complain "call: what a program said while it ran is not beside the \
answer: \`$beside\`"
fi

# And the other two, which answer with different things: what `run` answers
# with is what the program said, so that stays where a reader looks, and what
# `tick` answers with is a frame's cost, which a program writing into the
# middle of would spoil the same way.
ran=$("$kest" run "$aside" 2>/dev/null </dev/null)
if [ "$ran" != "hello x" ]; then
    complain "run: what a program said is not what this answered: \`$ran\`"
fi
ticked=$("$kest" tick "$aside" 2 2>/dev/null </dev/null)
case "$ticked" in
*"hello"*|*"event "*)
    complain "tick: what a program said is in the middle of what a frame cost"
    printf '%s\n' "$ticked" | sed 's/^/    /' | head -4
    ;;
esac

# The standard library from a command line, which nothing had ever tried. A
# program is the file named and everything it imports, so every function of the
# library is one `call` reaches, and what it can hand one is what a shell can
# type. The reference says `min 3 7` is the `i32` one and `min 3.5 7.5` is the
# `f32` one — the overload settled by how the number is written rather than by
# what it could fit — and that sentence had nothing behind it.
mkdir "$scratch"/library
cat > "$scratch"/library/using.kest <<'KEST'
module using

import std.math
import std.text
import std.sort

fn main() -> i32 {
    let names: [text] = array()
    push(names, "b")
    sort.by(names, sort.ascending)
    return len(text.upper("a")) + math.min(1, 2) + len(names)
}
KEST
using="$scratch"/library/using.kest
for asked_for in "math.min 3 7:3" \
                 "math.min 3.5 7.5:3.5" \
                 "math.clamp 5 0 3:3" \
                 "text.upper hi:HI" \
                 "text.number 42:42" \
                 "text.number abc:none" \
                 "text.number 2147483647:2147483647" \
                 "text.number 2147483648:none" \
                 "text.number -2147483648:-2147483648" \
                 "text.number -2147483649:none" \
                 "text.real 340282400000000000000000000000000000000:none" \
                 "math.isNumber 5.0:true" \
                 "text.starts hello he:true"; do
    asking=${asked_for%:*}
    wanted=${asked_for##*:}
    # shellcheck disable=SC2086
    answered=$("$kest" call "$using" $asking 2>&1 </dev/null)
    if [ "$answered" != "$wanted" ]; then
        complain "call $asking: answered \`$answered\` and not \`$wanted\`"
    fi
done

# And what a shell cannot type, which is most of what a library holds: the
# refusal says what could not be read and where the ones of that name are, so
# a reader is told which functions there were rather than that there was a
# problem.
handed=$("$kest" call "$using" sort.by 1 2 2>&1 </dev/null)
case "$handed" in
*K0624*"cannot be written as a word"*"sort.kest"*) ;;
*)
    complain "call sort.by: a function a shell cannot hand anything to was \
refused without saying which functions there were"
    printf '%s\n' "$handed" | sed 's/^/    /' | head -4
    ;;
esac

# A program of two files that works, which is the one thing this check had
# never written: everything here is a program written to be refused, and what a
# module boundary does when nothing is wrong was left to the examples. A struct
# made in one file and read in the other, an array grown there and counted
# here, a piece of text built there and compared here — and a call from a
# command line into the imported module by the name somebody would type, which
# used to be a name this command line put its own module in front of and then
# said back.
mkdir "$scratch"/working
cat > "$scratch"/working/shapes.kest <<'KEST'
module shapes

struct Row {
    at: i32
    weight: f32
}

fn made(n: i32) -> Row {
    return Row(n, f32(n) * 0.5)
}

fn grown(n: i32) -> [i32] {
    let out: [i32] = array()
    for i in 0..n {
        push(out, i)
    }
    return out
}

fn named(r: Row) -> text {
    return "row {r.at}"
}

fn doubled(n: i32) -> i32 {
    return n * 2
}
KEST
cat > "$scratch"/working/working.kest <<'KEST'
module working

import shapes

fn main() -> i32 {
    let r = shapes.made(3)
    let xs = shapes.grown(4)
    let name = shapes.named(r)
    if r.at != 3 {
        return 1
    }
    if len(xs) != 4 {
        return 2
    }
    if name != "row 3" {
        return 3
    }
    return 7
}
KEST
working="$scratch"/working/working.kest
"$kest" run "$working" >"$scratch"/working-said 2>&1 </dev/null
crossing_status=$?
if [ $crossing_status -ne 7 ] || [ -s "$scratch"/working-said ]; then
    complain "run: a program of two files that works answered $crossing_status"
    sed 's/^/    /' "$scratch"/working-said | head -4
fi
doubled=$("$kest" call "$working" shapes.doubled 4 2>&1 </dev/null)
if [ "$doubled" != "8" ]; then
    complain "call: a function of an imported module answered \`$doubled\`"
fi
missing=$("$kest" call "$working" shapes.nope 1 2>&1 </dev/null)
case "$missing" in
*"\`shapes.nope\`"*) ;;
*)
    complain "call: a name that is not there was said back as something else"
    printf '%s\n' "$missing" | sed 's/^/    /' | head -3
    ;;
esac

# And the same thing across two files, which is where a note has something to
# say that a line number on its own cannot: the promise is in one module and
# what breaks it is in another, so the diagnostic is about one file and its
# notes are about the other. Every note carries the file it is in for exactly
# this, and nothing here had ever made one.
mkdir "$scratch"/crossed
cat > "$scratch"/crossed/helper.kest <<'KEST'
module helper

fn grow(n: i32) -> i32 {
    let trail = [n, n, n]
    return len(trail) + n
}
KEST
cat > "$scratch"/crossed/world.kest <<'KEST'
module world

import helper

fn stepFrame(n: i32) -> i32 no.alloc {
    return helper.grow(n)
}

fn main() -> i32 {
    return stepFrame(1)
}
KEST
crossed="$scratch"/crossed/world.kest
if ! python3 - "$kest" "$carried" "$crossed" <<'NOTES' >"$scratch"/carried-notes 2>&1
import json
import os
import re
import subprocess
import sys

asked = sys.argv[1]
read = {}


def held(path):
    """The lines of a file a note says it is about."""
    if path not in read:
        read[path] = open(path).read().split("\n")
    return read[path]


looked = 0
crossed = 0
for where in sys.argv[2:]:
    ran = subprocess.run([asked, "check", "--json", where],
                         capture_output=True, text=True,
                         stdin=subprocess.DEVNULL)
    said = json.loads(ran.stdout)
    for diagnostic in said["diagnostics"]:
        for note in diagnostic.get("notes", []):
            # The file the note says it is in rather than the one the
            # diagnostic is about: a promise in one module broken in another
            # is one diagnostic about two files, and a note read out of the
            # wrong one of them points at whatever is on that line.
            if note["file"] != diagnostic["file"]:
                crossed += 1
            named = re.findall(r"`([^`]+)`", note["message"])
            if not named:
                # `the first one` and its like: a note that names nothing is
                # about a place rather than about a thing, and the place is
                # all there is to check.
                continue
            looked += 1
            # The last part of it, because a note says what the checker calls
            # a function — module and all — and the line says what somebody
            # wrote.
            want = named[0].split(".")[-1]
            line = held(note["file"])[note["line"] - 1]
            if want not in line:
                print("a note says `%s` and points at a line of %s without "
                      "it: %s" % (note["message"],
                                  os.path.basename(note["file"]),
                                  line.strip()))
                raise SystemExit(1)
if looked == 0:
    print("nothing here has a note that names anything")
    raise SystemExit(1)
if crossed == 0:
    print("nothing here has a note about a file other than its own")
    raise SystemExit(1)
NOTES
then
    complain "check --json: a note points somewhere its own words are not"
    sed 's/^/    /' "$scratch"/carried-notes | head -4
fi

# The two things the command line answers that are not commands and take no
# file. A tool that wants to know what it is talking to reads the first, and a
# person who has typed the wrong thing reads the second: both were held to
# nothing, so a version that printed nothing and a `-h` that printed the usage
# to nowhere would each have been a run that looked like it worked.
# The heap a program keeps between events, and the same run with it thrown
# away. It is the one option that changes what a program is standing on rather
# than what this prints about it, and it was printed, answered, and run by
# nothing: a `--reset` that reset nothing would have said the same words as one
# that worked, because what says it happened is the number beside them.
mkdir "$scratch"/ticking
cat > "$scratch"/ticking/ticking.kest <<'KEST'
fn onEvent(event: i32) -> i32 {
    let all: [i32] = array()
    push(all, event)
    return len(all)
}

fn main() -> i32 {
    return 0
}
KEST
kept=$("$kest" tick "$scratch"/ticking/ticking.kest 3 2>&1 </dev/null)
threw=$("$kest" tick "$scratch"/ticking/ticking.kest 3 --reset 2>&1 </dev/null)
# The number rather than the words, because the words are what a heap thrown
# away by nobody would say too: what is left on the heap is nought when it has
# been thrown away and is not when it has not.
kept_bytes=$(printf '%s\n' "$kept" |
             sed -n 's/^heap *\([0-9][0-9]*\) bytes, none of it freed$/\1/p')
threw_bytes=$(printf '%s\n' "$threw" |
              sed -n 's/^heap *\([0-9][0-9]*\) bytes, thrown away 3 times$/\1/p')
if [ -z "$kept_bytes" ] || [ "$kept_bytes" -eq 0 ]; then
    complain "tick: a heap nobody threw away is not still there"
    printf '%s\n' "$kept" | sed 's/^/    /' | head -4
fi
if [ -z "$threw_bytes" ] || [ "$threw_bytes" -ne 0 ]; then
    complain "tick --reset: a heap thrown away between events is still there"
    printf '%s\n' "$threw" | sed 's/^/    /' | head -4
fi

told=$("$kest" --version 2>&1 </dev/null)
if [ $? -ne 0 ]; then
    complain "--version: came back with something to say and a number"
    printf '%s\n' "$told" | sed 's/^/    /' | head -3
fi
case "$told" in
"kest "[0-9]*) ;;
*)
    complain "--version: said \`$told\`, which is not this being named and \
numbered"
    ;;
esac

# And the same words whichever way they are asked for, because a reader who
# typed one of the three has read the other two nowhere.
spelled=$("$kest" help 2>&1 </dev/null)
for flag in -h --help; do
    if [ "$("$kest" "$flag" 2>&1 </dev/null)" != "$spelled" ]; then
        complain "$flag: does not say what \`help\` says"
    fi
done
case "$spelled" in
*"--version"*) ;;
*)
    complain "help: does not say the command line answers \`--version\`"
    ;;
esac

for file in "$@"; do
    at=$((at + 1))
    mine="$said/$(printf %04d $at)"
    if [ -s "$mine" ]; then
        cat "$mine"
        failed=1
    fi
done
rm -rf "$carried_said"

rm -f "$scratch"/cmd-err
if [ $failed -eq 0 ]; then
    echo "every command does something on $# file(s)"
fi
exit $failed
