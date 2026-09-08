#!/bin/sh
# What every command has to be true of: it produces something of the right
# shape, it says why when it does not, and what it says as JSON is JSON.
#
# The examples check their own answers by returning a number, so `run` exiting
# zero is the answer being right. What this adds is that a command which
# prints nothing no longer looks the same as one that works.
set -u
kest=./kest
failed=0

complain() {
    echo "$1"
    failed=1
}

# Output of the right kind, or a reported reason and a non-zero exit.
expect() {
    file=$1
    command=$2
    pattern=$3

    out=$("$kest" "$command" "$file" 2>/tmp/kest-cmd-err)
    status=$?
    if [ $status -ne 0 ]; then
        if [ ! -s /tmp/kest-cmd-err ]; then
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
        $kest call "$asking" main >/dev/null 2>/tmp/kest-cmd-err </dev/null
    else
        $kest "$command" "$asking" >/dev/null 2>/tmp/kest-cmd-err </dev/null
    fi
    if [ $? -eq 0 ]; then
        complain "$command $asking: a program the host cannot run ran"
    elif ! grep -q "does not provide" /tmp/kest-cmd-err; then
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
    if $kest "$command" "$where" >/dev/null 2>/tmp/kest-cmd-err </dev/null; then
        complain "$command $where: read a directory as a file"
    elif ! grep -q "cannot read" /tmp/kest-cmd-err; then
        complain "$command $where: refused without saying it could not read it"
    fi
done
rmdir "$where"

# And running it is a refusal that says which of the two reasons it is.
if $kest run "$nothing" >/dev/null 2>/tmp/kest-cmd-err </dev/null; then
    complain "run $nothing: a file that holds nothing ran"
elif ! grep -q "declares nothing" /tmp/kest-cmd-err; then
    complain "run $nothing: refused without saying the file holds nothing"
fi
rm -rf "$(dirname "$nothing")"

for file in "$@"; do
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
        out=$("$kest" call "$file" $chosen 2>/tmp/kest-cmd-err </dev/null)
        status=$?
        if [ $status -ne 0 ]; then
            if [ ! -s /tmp/kest-cmd-err ]; then
                complain "call $file $chosen: failed and said nothing"
            fi
        elif [ -z "$out" ]; then
            complain "call $file $chosen: succeeded and printed nothing"
        fi
    fi

    # Running is the answer being right, because an example that disagrees
    # with itself returns which check it failed.
    "$kest" run "$file" >/dev/null 2>/tmp/kest-cmd-err </dev/null
    status=$?
    if [ $status -ne 0 ] && [ ! -s /tmp/kest-cmd-err ]; then
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
                          r"(\d+) deep", line)
    if written_fn:
        name = written_fn.group(1)
        printed[name] = {"wide": tuple(int(written_fn.group(i))
                                       for i in (2, 3, 4)), "code": []}
        continue
    step = re.match(r"\s+(\d+)\s+(\S+)", line)
    if step and name is not None:
        printed[name]["code"].append((int(step.group(1)), step.group(2)))

said = json.loads(written or "{}")
machine = {}
for one in said.get("functions", []):
    machine[one["name"]] = {
        "wide": (one["parameterSlots"], one["slots"], one["deep"]),
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
    if printed[name]["code"] != machine[name]["code"]:
        print("%s: %u instructions printed, %u in the JSON"
              % (name, len(printed[name]["code"]), len(machine[name]["code"])))
')
    if [ -n "$walked" ]; then
        complain "emit $file: the two forms disagree"
        printf '%s\n' "$walked" | sed 's/^/    /' | head -4
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
done

rm -f /tmp/kest-cmd-err
if [ $failed -eq 0 ]; then
    echo "every command does something on $# file(s)"
fi
exit $failed
