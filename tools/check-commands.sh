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
    # And nothing on the other stream, which is the half that was written down
    # here and not held: what a command says there is what is wrong, so a
    # command that worked says nothing there. A reader who has to tell the
    # answer from the complaints by reading them is a reader a pipe cannot
    # be. See D585.
    if [ -s "$scratch"/cmd-err ]; then
        said_anyway=$(head -1 "$scratch"/cmd-err)
        complain "$command $file: worked and said $said_anyway"
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
        # And a run that worked, which says nothing: what the program writes
        # goes where a reader looks for an answer and what the run has to say
        # about itself goes where the complaints are, so a run with nothing
        # wrong leaves that stream empty. See D585.
        if [ $status -eq 0 ] && [ -s "$mine.err" ]; then
            said_anyway=$(head -1 "$mine.err")
            complain "run $file: worked and said $said_anyway"
        fi

        # The two forms of `check` say the same file's declarations.        # The two forms of `check` say the same file's declarations. One is read
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
    shapes = {}
    laid = {}
    laid_out = None
    summarised = {}
    for line in text.splitlines():
        # To the two spaces the layout begins after, rather than to the
        # first space in it: a copy of a shape over two types is called
        # `Pair<i32, text>`, and a name read to the first space is half of
        # one. No file in this tree had a copy over two types until one was
        # written, which is why a reading that could not spell one held.
        what = re.match(r"(struct|enum|flags) (.+?)  (.*)$", line)
        if what:
            printed.add(what.group(2))
            # And what it is laid out as, which is the rest of that line, with
            # the lines under it kept in order beneath it: a field is a slot
            # and a byte and a name and a type, and a case is a tag and a name
            # and what it carries. See D592.
            laid_out = what.group(2)
            laid.setdefault(laid_out, []).append(what.group(3).rstrip())
        under = re.match(r"  (slot |bit |\d+ )(.*)$", line)
        if under and laid_out is not None:
            laid[laid_out].append(re.sub(r"\s+", " ",
                                         (under.group(1) +
                                          under.group(2)).rstrip()))
        called = re.match(r"(?:extern )?fn ([^(]+)\((.*)$", line)
        if called:
            printed.add(called.group(1))
            # And what it takes and gives back, kept whole rather than split
            # on the commas: a copy of a shape over two types is written
            # `Pair<i32, text>`, and a list read by splitting is one that
            # comes apart on the first of those. A list per name, because a
            # module written in two widths declares one name twice and a
            # reading that keeps the last of them reads half a module.
            # See D591.
            shapes.setdefault(called.group(1), []).append(called.group(2))
        held = re.match(r"const (\S+):", line)
        if held:
            printed.add(held.group(1))
        # And the line a module gets when a file imported it, which is a count
        # of what the object writes out one at a time. It is the third thing
        # this command prints and the only one that is a summary: what a
        # reader is shown instead of two hundred lines of a library they did
        # not write. See D593.
        counted = re.match(r"([^ ]+)  (\d.*)$", line)
        if counted and not line.startswith(("struct ", "enum ", "flags ",
                                            "fn ", "extern fn ", "const ")):
            summarised[counted.group(1)] = counted.group(2).rstrip()

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

    # And what each of them takes and gives back, which the two forms said in
    # two shapes and nothing read together: the words put it after an arrow
    # and leave the arrow off a function that gives nothing, and the object
    # says `nothing` under `gives`. The promise is a word after the type in
    # one and a field in the other. One fact, two spellings, and the object is
    # the one a tool reads.
    of_the_same_name = {}
    for one in json.loads(written or "{}").get("functions", []):
        if one.get("file") != sys.argv[1] or one["name"] not in shapes:
            continue
        gives = one["gives"]
        of_the_same_name.setdefault(one["name"], []).append(
            "%s)%s%s" % (", ".join(one["parameters"]),
                         "" if gives == "nothing" else " -> " + gives,
                         " no.alloc" if one["noAlloc"] else ""))
    # And the same for a shape: what it is laid out as, and what is under it.
    # The words say it in a line and a run of lines beneath, and the object
    # says it in numbers and a list; a reader of one has never been held to
    # what the other says, and a layout is the half of a program a host is
    # written against. See D592.
    def how_it_lies(one):
        many = "" if one["bytes"] == 1 else "s"
        if one["kind"] == "flags":
            said = ["1 slot, %d byte%s over %s" % (one["bytes"], many,
                                                   one["over"])]
            for bit in one["bits"]:
                said.append("bit %d %s" % (bit["bit"], bit["name"]))
            return said
        said = ["%d slot%s, %d byte%s aligned %d"
                % (one["slots"], "" if one["slots"] == 1 else "s",
                   one["bytes"], many, one["align"])]
        if one["kind"] == "struct":
            for field in one["fields"]:
                said.append("slot +%d byte +%d %s: %s"
                            % (field["slot"], field["byte"], field["name"],
                               field["type"]))
            return said
        for case in one["cases"]:
            carries = "".join(" slot +%d byte +%d %s"
                              % (what["slot"], what["byte"], what["type"])
                              for what in case["carries"])
            said.append("%d %s%s" % (case["tag"], case["name"], carries))
        return said

    for one in json.loads(written or "{}").get("types", []):
        if one.get("file") != sys.argv[1] or one["name"] not in laid:
            continue
        if laid[one["name"]] != how_it_lies(one):
            print("%s: printed %s and the JSON says %s"
                  % (one["name"], laid[one["name"]], how_it_lies(one)))

    # And the counts in those lines, worked out from the list the object
    # writes: the words say a module holds so many types and so many functions
    # and how many of those a host provides, and the object says every one of
    # them under its own name. Two readings of one import, and the summary is
    # the one nothing could check.
    everything = json.loads(written or "{}")
    root = ""
    for what in ("types", "functions", "constants"):
        for one in everything.get(what, []):
            if one.get("file") == sys.argv[1] and "." in one["name"]:
                root = one["name"].split(".")[0]
    holds = {}
    for what in ("types", "functions", "constants"):
        for one in everything.get(what, []):
            module = one["name"].split(".")[0] if "." in one["name"] else ""
            if module in ("", root):
                continue
            has = holds.setdefault(module, {"types": 0, "functions": 0,
                                            "foreign": 0})
            if what == "functions":
                has["functions"] += 1
                has["foreign"] += 1 if one["foreign"] else 0
            else:
                has["types"] += 1
    for module in sorted(set(holds) | set(summarised)):
        has = holds.get(module, {"types": 0, "functions": 0, "foreign": 0})
        pieces = []
        if has["types"]:
            pieces.append("%d type%s" % (has["types"],
                                         "" if has["types"] == 1 else "s"))
        if has["functions"]:
            pieces.append("%d function%s"
                          % (has["functions"],
                             "" if has["functions"] == 1 else "s"))
        if has["foreign"]:
            pieces.append("%d the host provides" % has["foreign"])
        if summarised.get(module) != ", ".join(pieces):
            print("%s: printed `%s` and the JSON counts `%s`"
                  % (module, summarised.get(module), ", ".join(pieces)))

    for name in sorted(of_the_same_name):
        if sorted(of_the_same_name[name]) != sorted(shapes[name]):
            print("%s: printed %s and the JSON says %s"
                  % (name, sorted(shapes[name]),
                     sorted(of_the_same_name[name])))
    ' "$file")
        if [ -n "$said" ]; then
            complain "check $file: the two forms disagree"
            printf '%s\n' "$said" | sed 's/^/    /' | head -4
        fi

        # The two forms of `lex`, which is the smallest of these and the one whose        # The two forms of `lex`, which is the smallest of these and the one whose
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
    remarks = []
    for line in text.splitlines():
        step = re.match(r"\s*(\d+):(\d+)\s+(\S+(?: \S+)*?)\s\s+(.*)$", line)
        if step:
            where = (int(step.group(1)), int(step.group(2)), step.group(4))
            # A comment is not a token, and the two forms say the same list of
            # them: it is printed where it was written and the object writes
            # them out on their own. See D595.
            if step.group(3) == "comment":
                remarks.append(where)
            else:
                printed.append((int(step.group(1)), int(step.group(2)),
                                step.group(3), step.group(4)))

    said = json.loads(written or "{}")
    machine = [(one["line"], one["column"], one["kind"], one["text"])
               for one in said.get("tokens", [])]
    aside = [(one["line"], one["column"], one["text"])
             for one in said.get("comments", [])]
    if remarks != aside:
        print("comments: %s printed, %s in the JSON"
              % (remarks[:2], aside[:2]))

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
    layouts = []
    on_its_own = {}
    hosts = []
    needs = None
    for line in text.splitlines():
        lays = re.match(r"layout \d+  (.*)$", line)
        if lays:
            # The whole of the line rather than the count of them: a layout is
            # what a host lays memory out against, and counting them holds
            # that there are as many as there are. What each one is was in
            # both forms and read in neither. See D594.
            layouts.append(lays.group(1).rstrip())
            continue
        if line.startswith("host "):
            hosts.append(line[len("host "):].strip())
            continue
        asked = re.match(r"needs (\d+) slots and (\d+) frames", line)
        if asked:
            needs = (int(asked.group(1)), int(asked.group(2)))
            continue
        # And what one entry wants on its own, which is printed only where it
        # is less than the whole: a host that calls one function is not made to
        # pay for the deepest of the ones it never will, and this is the line
        # that says so. The object lists every one of them either way.
        alone = re.match(r"\s+(\d+) and (\d+) for `(.+)` on its own$", line)
        if alone:
            on_its_own[alone.group(3)] = (int(alone.group(1)),
                                          int(alone.group(2)))
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
        step = re.match(r"\s+(\d+)\s+(\S+)\s*(.*)$", line)
        if step and name is not None:
            # And what the instruction carries. A number a jump is written
            # with is shown twice — the step it takes and the place that
            # reaches — so what is read here is what stands before the arrow,
            # and the numbers a slot or a count is written with carry a `+`
            # or sit beside a `<` or an `of`. See D451.
            carries = []
            for word in re.split(r"\s*(?:;|->)", step.group(3))[0].split():
                word = word.lstrip("+")
                if word.isdigit():
                    carries.append(int(word))
            printed[name]["code"].append((int(step.group(1)), step.group(2),
                                          carries))

    said = json.loads(written or "{}")
    machine = {}
    for one in said.get("functions", []):
        machine[one["name"]] = {
            "wide": (one["parameterSlots"], one["slots"], one["deep"]),
            "promises": one["noAlloc"],
            "code": [(step["at"], step["op"], step["operands"])
                     for step in said and one["code"]],
        }

    written_out = []
    for one in said.get("layouts", []):
        written_out.append(
            "%d byte%s aligned %d%s: %s"
            % (one["bytes"], "" if one["bytes"] == 1 else "s", one["align"],
               ", tagged" if one["tagged"] else "",
               " ".join("+%d %s" % (piece["byte"], piece["is"])
                        for piece in one["pieces"])))
    if layouts != written_out:
        print("layouts: %s printed, %s in the JSON" % (layouts, written_out))
    if hosts != said.get("hosts", []):
        print("hosts: %s printed, %s in the JSON" % (hosts, said.get("hosts")))
    asked = said.get("needs")
    if needs is not None and asked is not None and \
            needs != (asked["slots"], asked["frames"]):
        print("needs: %s printed, %s in the JSON" % (needs, asked))
    # Every entry printed is one the object has with the same two numbers, and
    # every entry the object has that wants less than the whole is printed:
    # the words leave out the ones that want exactly what everything wants,
    # because a line that says the same number twice is a line to read twice.
    if asked is not None:
        listed = dict((one["name"], (one["slots"], one["frames"]))
                      for one in asked.get("entries", []))
        smaller = dict((name_of, numbers) for name_of, numbers in listed.items()
                       if numbers != (asked["slots"], asked["frames"]))
        if on_its_own != smaller:
            print("needs: printed %s on their own, and the JSON has %s"
                  % (sorted(on_its_own.items()), sorted(smaller.items())))
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
        if len(printed[name]["code"]) != len(machine[name]["code"]):
            print("%s: %u instructions printed, %u in the JSON"
                  % (name, len(printed[name]["code"]), len(machine[name]["code"])))
            continue
        for was, now in zip(printed[name]["code"], machine[name]["code"]):
            # Every number the printed form writes plainly is the number the
            # JSON writes in that place. What a jump steps by is the one thing
            # the printed form shows only as where it lands, so the JSON may
            # carry one more than is read here and no fewer.
            if (was[:2] != now[:2] or len(now[2]) < len(was[2])
                    or now[2][:len(was[2])] != was[2]):
                print("%s: %s printed, %s in the JSON" % (name, was, now))
                break
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
# Its own name, because a name that stands for a place and for what a command
# answered is a name that reads right in both loops and holds one of them.
sweeps="$scratch"/sweeps
mkdir "$sweeps"
at=0
for file in "$@"; do
    at=$((at + 1))
    sweep_one "$file" "$sweeps/$(printf %04d $at)" \
        > "$sweeps/$(printf %04d $at)" 2>&1 &
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
status=$?
if [ "$status" -ne 0 ]; then
    complain "run: a reference used with another store named somebody else"
    printf '    it answered %s, which is what that store holds\n' "$status"
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
    answered=$("$kest" call "$values" "$one" 2>&1 </dev/null | head -1)
    machine=$("$kest" call "$values" "$one" --json 2>&1 </dev/null | tail -1 |
           python3 -c 'import json, sys; print(json.load(sys.stdin)["result"])')
    if [ "$printed_line" != "$answered" ] || [ "$printed_line" != "$machine" ]; then
        complain "call: \`$one\` is not written the way the program writes it"
        printf '    printed %s, said %s, told %s\n' "$printed_line" \
               "$answered" "$machine"
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

# The four places these two warnings are quiet, which is the whole of what makes
# them worth having: a warning that goes off where nothing is wrong is one a
# reader learns to read past. Counting with a constant is reading it; a shape
# whose field is one of its own is named by that; a file with no `main` is a
# library and not a program; and what a program imported is named by whoever
# imported it. Each is written down in `Diagnostics` and none was held. See
# D537.
mkdir "$scratch"/quiet
cat > "$scratch"/quiet/counted.kest <<'KEST'
const CELLS: i32 = 4

fn main() -> i32 {
    let grid: [i32; CELLS] = [0, 0, 0, 0]
    let room = array(CELLS, 0)
    return len(grid) + len(room) - 8
}
KEST
cat > "$scratch"/quiet/itself.kest <<'KEST'
struct Node {
    n: i32
    next: ref<Node>?
}

fn main() -> i32 {
    return 0
}
KEST
cat > "$scratch"/quiet/library.kest <<'KEST'
const SPARE: i32 = 1

struct Spare {
    x: i32
}
KEST
mkdir "$scratch"/quiet/held
cat > "$scratch"/quiet/held/side.kest <<'KEST'
module held.side

const SPARE: i32 = 1

struct Spare {
    x: i32
}

fn used() -> i32 {
    return 2
}
KEST
cat > "$scratch"/quiet/held/top.kest <<'KEST'
module top

import held.side

fn main() -> i32 {
    return side.used() - 2
}
KEST
for quietly in "a constant counted with:$scratch/quiet/counted.kest" \
               "a constant asked for:$scratch/quiet/asked.kest" \
               "a shape that names itself:$scratch/quiet/itself.kest" \
               "a file with no \`main\`:$scratch/quiet/library.kest" \
               "a file that was imported:$scratch/quiet/held/top.kest"; do
    what_it_is=${quietly%%:*}
    said_about=$("$kest" check "${quietly#*:}" 2>&1 </dev/null |
                 grep '^warning' | head -1)
    if [ -n "$said_about" ]; then
        complain "check: $what_it_is was warned about: \`$said_about\`"
    fi
done

# How a failure while running says it got there. A note per call under the one
# that failed, outermost first, so the notes read as the way in rather than as
# the way back out — and a run deeper than a diagnostic holds says how many
# were left out, because a number is what a reader of a deep one wants. Both
# are written down in `Running` and neither was held by anything. The number
# comes out of the header rather than out of this line. See D533.
most_notes=$(sed -n 's/^#define KEST_MAX_NOTES \([0-9][0-9]*\)$/\1/p' src/diag.h)
if [ -z "$most_notes" ]; then
    complain "check: \`KEST_MAX_NOTES\` is not a number in src/diag.h, so how \
many calls a message shows is held to nothing"
fi
mkdir "$scratch"/deep
cat > "$scratch"/deep/three.kest <<'KEST'
fn inner(n: i32) -> i32 {
    return 10 / n
}

fn middle(n: i32) -> i32 {
    return inner(n)
}

fn outer(n: i32) -> i32 {
    return middle(n)
}

fn main() -> i32 {
    return outer(0)
}
KEST
the_way_in=$("$kest" run "$scratch"/deep/three.kest 2>&1 </dev/null |
             sed -n 's/.*`\([a-z]*\)` was called here.*/\1/p' | tr '\n' ' ')
if [ "$the_way_in" != "outer middle inner " ]; then
    complain "run: a failure three calls deep read as \`$the_way_in\` rather \
than as the way in"
fi

# And one deeper than a message holds, which says how many are under the last
# it shows.
{
    printf 'fn f0(n: i32) -> i32 {\n    return 10 / n\n}\n'
    step=1
    while [ $step -le 12 ]; do
        printf '\nfn f%d(n: i32) -> i32 {\n    return f%d(n)\n}\n' \
               "$step" "$((step - 1))"
        step=$((step + 1))
    done
    printf '\nfn main() -> i32 {\n    return f12(0)\n}\n'
} > "$scratch"/deep/twelve.kest
deeply=$("$kest" run "$scratch"/deep/twelve.kest 2>&1 </dev/null)
shown=$(printf '%s\n' "$deeply" | grep -c "was called here")
if [ "$shown" -ne "$most_notes" ]; then
    complain "run: a message holds $most_notes calls and this one showed $shown"
fi
case "$deeply" in
*"was called here, and 5 more under it"*) ;;
*)
    complain "run: a run deeper than a message holds did not say how many were \
left out"
    printf '%s\n' "$deeply" | sed 's/^/    /' | tail -3
    ;;
esac

# Which of two files is the one whose `main` runs. `help` says the first named
# settles where imports resolve from and is the one `run` and `tick` call, and
# what held that was the half about `check` writing the first file out in full.
# Two files with a `main` each, named both ways round: the answer says which
# one ran. See D532.
whose="$scratch"/check-whose
mkdir -p "$whose"
cat > "$whose/eleven.kest" <<'KEST'
module eleven

fn main() -> i32 {
    return 11
}
KEST
cat > "$whose/twentytwo.kest" <<'KEST'
module twentytwo

fn main() -> i32 {
    return 22
}
KEST
"$kest" run "$whose/eleven.kest" "$whose/twentytwo.kest" >/dev/null 2>&1 </dev/null
first_ran=$?
"$kest" run "$whose/twentytwo.kest" "$whose/eleven.kest" >/dev/null 2>&1 </dev/null
other_ran=$?
if [ "$first_ran" -ne 11 ] || [ "$other_ran" -ne 22 ]; then
    complain "run: the first file named is the one whose \`main\` runs, and \
these answered $first_ran and $other_ran"
fi

# And the three that read each file on its own. `help` says they follow no
# imports, so a file naming a module nothing can read is a file they are still
# able to answer about — and nothing had ever handed them one. See D532.
on_its_own_at="$scratch"/check-alone.kest
cat > "$on_its_own_at" <<'KEST'
import no.such.thing

fn main() -> i32 {
    return 0
}
KEST
for on_its_own in fmt parse lex; do
    said_alone=$("$kest" "$on_its_own" "$on_its_own_at" 2>&1 </dev/null)
    answered=$?
    case "$answered$said_alone" in
    0*"no"*) ;;
    *)
        complain "$on_its_own: a file that imports what cannot be read is one \
this reads on its own, and it answered $answered"
        printf '%s\n' "$said_alone" | sed 's/^/    /' | head -3
        ;;
    esac
done

# And a program with something to warn about, which is a thing said and a run
# that goes on. A warning is not a refusal: what the run answers is what the
# program answered, and the help says so. Three at once, because a program with
# one of each is what says they do not stand in each other's way. See D531.
warned="$scratch"/check-warned.kest
cat > "$warned" <<'KEST'
module warned

extern fn Host.now() -> i32 no.alloc

const N: i32 = 1

struct P {
    x: i32
}

fn main() -> i32 {
    return 7
}
KEST

warned_said=$("$kest" run "$warned" 2>&1 </dev/null)
gave=$?
for code in K0506 K0508 K0509; do
    case "$warned_said" in
    *"$code"*) ;;
    *)
        complain "run: a program with one of each warning did not say $code"
        printf '%s
' "$warned_said" | sed 's/^/    /' | head -4
        ;;
    esac
done
if [ "$gave" -ne 7 ]; then
    complain "run: a warning made the run answer $gave rather than the 7 the \
program did"
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
telling="$scratch"/check-told.kest
cat > "$telling" <<'KEST'
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
ticking="$scratch"/check-crossed.kest
cat > "$ticking" <<'KEST'
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

ticked=$( { "$kest" tick "$ticking" 3 2>&1 </dev/null;
            echo "----";
            "$kest" tick "$ticking" 3 --json 2>&1 </dev/null; } |
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
            continue
        made = re.match(r"machine\s+(\d+) bytes, (\d+) slots and "
                        r"(\d+) frames?$", line)
        if made:
            said["machine"] = {"bytes": int(made.group(1)),
                               "slots": int(made.group(2)),
                               "frames": int(made.group(3))}
            continue
        spent = re.match(r"cost\s+(\d+) bytes to compile$", line)
        if spent:
            said["cost"] = int(spent.group(1))

    try:
        written = json.loads(machine.splitlines()[-1] if machine.strip()
                             else "{}")
    except ValueError:
        print("what was said as JSON is not JSON")
        raise SystemExit(0)
    for what in ("onEvents", "onEvent", "events", "cost", "machine", "heap",
                 "thrown"):
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
    # And what the machine cost, which is a number a host pays once against a
    # number it pays every frame. The command line asks the program what it
    # needs and hands that over, so a machine here is smaller than one nobody
    # asked about. See D576.
    if said["machine"]["slots"] >= 65536 or said["machine"]["frames"] >= 1024:
        print("a machine the program was asked about took %d slots and %d "
              "frames" % (said["machine"]["slots"], said["machine"]["frames"]))
    ' 3)
if [ -n "$ticked" ]; then
    complain "tick: what a frame cost is one thing in words and another in JSON"
    printf '%s\n' "$ticked" | sed 's/^/    /' | head -4
fi

# And the two numbers apart from each other. What a machine is made of is paid
# once and does not move with the work; what the heap holds is paid by the
# frames and moves with every one of them. A run of three events and a run of
# nine say so together: the same machine, a bigger heap. A machine that answers
# with the program's heap says the first number twice and neither of them is
# what a host would put in its budget. See D576.
of_three=$("$kest" tick "$ticking" 3 --json 2>&1 </dev/null |
           sed -n 's/.*"machine":{"bytes":\([0-9]*\).*"heap":\([0-9]*\).*/\1 \2/p')
of_nine=$("$kest" tick "$ticking" 9 --json 2>&1 </dev/null |
          sed -n 's/.*"machine":{"bytes":\([0-9]*\).*"heap":\([0-9]*\).*/\1 \2/p')
if [ -z "$of_three" ] || [ -z "$of_nine" ] ||
   [ "${of_three%% *}" != "${of_nine%% *}" ] ||
   [ "${of_three##* }" = "${of_nine##* }" ]; then
    complain "tick: machine and heap are $of_three over three events and $of_nine over nine"
fi

# The two streams under `--json`, which is the form that tells them apart. The
# object is the answer and what the program wrote is beside it: a tool reads
# one and a person reads the other, and a refusal goes in the object rather
# than into the middle of what the program was saying. In words they are one
# stream on purpose — there the measurement is the answer, and what a program
# prints goes where it cannot be mistaken for a number. See D586.
cat > "$scratch"/talking.kest <<'KEST'
module talking

import std.io

fn onEvent(n: i32) -> i32 {
    io.print("event")
    return n
}

fn main() -> i32 {
    return 0
}
KEST
sed 's|    return n|    let some: [i32; 2] = [1, 2]\
    return some[n + 5]|' "$scratch"/talking.kest |
    sed 's|^module talking$|module falling|' > "$scratch"/falling.kest
apart=$( { "$kest" tick --json "$scratch"/talking.kest 2 2>"$scratch"/talking.err
           echo "----"
           cat "$scratch"/talking.err
           echo "----"
           "$kest" tick --json "$scratch"/falling.kest 2 2>"$scratch"/falling.err
           echo "----"
           cat "$scratch"/falling.err; } | python3 -c '
    import json
    import sys

    # Padded rather than unpacked, because a stream holding what another one
    # is for is exactly what this is looking for and a run of the wrong shape
    # is that: read as four, whatever came.
    parts = [part.strip() for part in sys.stdin.read().split("\n----\n")]
    worked, quiet, fell, said = (parts + ["", "", "", ""])[:4]


    def object_of(text):
        try:
            return json.loads(text)
        except ValueError:
            return None


    # One sentence for all of it, because every way this can be wrong is the
    # same way: something is in the stream the other one is for.
    ran = object_of(worked)
    stopped = object_of(fell)
    if (ran is None or stopped is None or ran["diagnostics"]
            or not stopped["diagnostics"] or quiet != "event\nevent"
            or said != "event"):
        print("tick: a tick answered %r and %r, and the program wrote %r "
              "and %r" % (worked[:40], fell[:40], quiet, said))
    ')
if [ -n "$apart" ]; then
    complain "$apart"
fi

# And the same two streams under `call`, where the answer is a value rather than
# a measurement — so the value goes where a shell reads it in both forms, and
# everything else goes beside it. A function that prints while it works out what
# to answer is the one that says both, and this tree had none: what a shell
# reading `kest call` gets has to be the value and nothing else, in words as
# well as in an object. See D587.
cat > "$scratch"/answering.kest <<'KEST'
module answering

import std.io

fn twice(n: i32) -> i32 {
    io.print("working it out")
    return n * 2
}

fn main() -> i32 {
    return twice(1)
}
KEST
beside=$( { "$kest" call "$scratch"/answering.kest twice 21 \
                2>"$scratch"/answering.err
            echo "----"
            cat "$scratch"/answering.err
            echo "----"
            "$kest" call --json "$scratch"/answering.kest twice 21 \
                2>"$scratch"/answering-json.err
            echo "----"
            cat "$scratch"/answering-json.err; } | python3 -c '
    import json
    import sys

    parts = [part.strip() for part in sys.stdin.read().split("\n----\n")]
    plainly, wrote, asked, wrote_again = (parts + ["", "", "", ""])[:4]
    try:
        object_said = json.loads(asked)
    except ValueError:
        object_said = None
    # One sentence again: every way this is wrong is one stream holding what
    # the other one is for.
    if (plainly != "42" or object_said is None
            or object_said.get("result") != "42"
            or wrote != "working it out" or wrote_again != "working it out"):
        print("call: a call answered %r and %r, and the program wrote %r and %r"
              % (plainly[:40], asked[:40], wrote, wrote_again))
    ')
if [ -n "$beside" ]; then
    complain "$beside"
fi

# And what a run answered, which is the one thing a `run` says that a tool could
# not read: the words are what the program wrote and the answer is the exit
# status, which is eight bits and is the same nought for a program that answered
# nought and one that answers nothing at all. The object says which. Held
# against the status, because two readings of one answer are what keeps either
# of them honest. See D588.
cat > "$scratch"/answering-run.kest <<'KEST'
module answers

import std.io

fn main() -> i32 {
    io.print("and wrote as well")
    return 7
}
KEST
sed 's|^fn main() -> i32 {|fn main() {|; s|^    return 7||' "$scratch"/answering-run.kest |
    sed 's|^module answers$|module quietly|' > "$scratch"/quiet-run.kest
replied=$( { "$kest" run --json "$scratch"/answering-run.kest 2>/dev/null
             echo "$?"
             echo "----"
             "$kest" run --json "$scratch"/quiet-run.kest 2>/dev/null
             echo "$?"
             echo "----"
             "$kest" check --json "$scratch"/answering-run.kest 2>/dev/null
             echo "----"
             "$kest" check --json "$scratch"/quiet-run.kest 2>/dev/null; } |
           python3 -c '
    import json
    import sys

    parts = [part.strip() for part in sys.stdin.read().split("\n----\n")]
    answering, quiet, declared, declared_quiet = (parts + [""] * 4)[:4]


    def object_and_status(said):
        lines = said.splitlines()
        try:
            return json.loads("\n".join(lines[:-1])), lines[-1]
        except (ValueError, IndexError):
            return None, None


    def gives_back(said):
        try:
            functions = json.loads(said)["functions"]
        except (ValueError, KeyError):
            return None
        for one in functions:
            if one["name"].split(".")[-1] == "main":
                return one["gives"] != "nothing"
        return None


    # Three readings of one fact and one sentence about all of them: what the
    # checker says `main` gives back, what a run answered, and what it exited
    # with. The checker knew before there was a machine, the run found out
    # after the call, and a status says the same nought for a program that
    # answered nought and one that answers nothing at all. See D589.
    gave, status = object_and_status(answering)
    nothing, quiet_status = object_and_status(quiet)
    if (gave is None or nothing is None or gave.get("answered") != 7
            or status != "7" or "answered" not in nothing
            or nothing["answered"] is not None or quiet_status != "0"
            or gives_back(declared) is not True
            or gives_back(declared_quiet) is not False):
        print("run: a run answered %r and exited %r where the checker said "
              "%r, and one that gives nothing answered %r and exited %r "
              "where the checker said %r"
              % (None if gave is None else gave.get("answered"), status,
                 gives_back(declared),
                 None if nothing is None else nothing.get("answered"),
                 quiet_status, gives_back(declared_quiet)))
    ')
if [ -n "$replied" ]; then
    complain "$replied"
fi

# And the same for a file that is not in the one form yet, which is the half no
# file in this tree can show: every one of them is already formatted, so the
# text an object carries and the file it was made from are the same bytes and
# a formatter that answered with what it was given would look right. Written
# badly on purpose here, the way `check-fmt.sh` writes one. See D596.
cat > "$scratch"/untidy.kest <<'KEST'
module untidy



fn   twice( n:i32 )->i32{
        return n*2
}
KEST
made=$( { "$kest" fmt "$scratch"/untidy.kest 2>/dev/null </dev/null
          echo "----"
          "$kest" fmt --json "$scratch"/untidy.kest 2>/dev/null </dev/null; } |
        python3 -c '
    import json
    import sys

    parts = sys.stdin.read().split("\n----\n")
    printed, written = (parts + ["", ""])[:2]
    printed = printed + "\n"
    try:
        said = json.loads(written)
    except ValueError:
        said = None
    was = open(sys.argv[1]).read()
    if (said is None or said.get("text") != printed or printed == was
            or said.get("formed")):
        print("fmt: a file written badly printed %d bytes, the object carries "
              "%s, and it says formed %r"
              % (len(printed),
                 "nothing" if said is None or said.get("text") is None else
                 str(len(said["text"])) + " bytes",
                 None if said is None else said.get("formed")))
    ' "$scratch"/untidy.kest)
if [ -n "$made" ]; then
    complain "$made"
fi

# And the same over a tick that was told which events to run, because what was
# lent is the half a counted run never says.
lent=$("$kest" tick "$ticking" 4,5,6 2>&1 </dev/null | sed -n 's/^events *//p')
lent_json=$("$kest" tick "$ticking" 4,5,6 --json 2>&1 </dev/null |
            tail -1 |
            python3 -c 'import json, sys; print(json.load(sys.stdin)["events"])')
case "$lent:$lent_json" in
"3 lent: 4, 5, 6:{'count': 3, 'lent': [4, 5, 6]}") ;;
*)
    complain "tick: what it was lent is one thing in words and another in JSON"
    printf '    words %s\n    json  %s\n' "$lent" "$lent_json"
    ;;
esac

two_ways "check" check "$telling"

# Which of the two streams each half goes to. What is wrong with a program goes
# where a shell keeps errors and what a program holds goes where a shell keeps
# answers, so `kest check x.kest > held` writes the answer and shows the errors
# — and in JSON everything is on one stream, because a tool reads one thing and
# an object split over two is neither.
alone=$("$kest" check "$telling" 2>/dev/null </dev/null)
if [ -n "$alone" ]; then
    complain "check: what is wrong with a program was written where its answer goes"
    printf '%s\n' "$alone" | sed 's/^/    /' | head -3
fi
aside=$("$kest" check "$telling" 2>&1 >/dev/null </dev/null)
case "$aside" in
*K0304*) ;;
*)
    complain "check: what is wrong with a program was not written where errors go"
    printf '%s\n' "$aside" | sed 's/^/    /' | head -3
    ;;
esac
aside=$("$kest" check "$telling" --json 2>&1 >/dev/null </dev/null)
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
unfinished=$("$kest" check "$telling" 2>&1 </dev/null)
case "$unfinished" in
*"fn told."*)
    complain "check: a program that did not check was written out anyway"
    printf '%s\n' "$unfinished" | sed 's/^/    /' | head -3
    ;;
esac
held_anyway=$("$kest" check "$telling" --json 2>&1 </dev/null | python3 -c '
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
carrying="$scratch"/carried/carried.kest
carried_said=$("$kest" check "$carrying" 2>&1 </dev/null)
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
# The two shapes of a crossing, over the same events. D007 makes the batch the
# default and D016 quotes W11 on why — one crossing per event was the widest
# figure in that workload — and what makes it a default rather than a second
# feature is that it answers what one at a time answers. `kest tick` drives both
# and prints what each gave; nothing compared them. See D556.
both_ways=$("$kest" tick examples/events.kest 7 --json 2>/dev/null </dev/null)
in_one=$(printf '%s' "$both_ways" |
         sed -n 's/.*"onEvents":{"crossings":\([0-9]*\),"gave":\([0-9]*\)}.*/\1 \2/p')
one_at_a_time=$(printf '%s' "$both_ways" |
                sed -n 's/.*"onEvent":{"crossings":\([0-9]*\),"gave":\([0-9]*\).*/\1 \2/p')
if [ -z "$in_one" ] || [ -z "$one_at_a_time" ]; then
    complain "tick: what the two shapes gave is not a number either of them \
wrote"
    printf '%s\n' "$both_ways" | sed 's/^/    /' | head -2
elif [ "${in_one#* }" != "${one_at_a_time#* }" ]; then
    complain "tick: a batch gave ${in_one#* } and one at a time gave \
${one_at_a_time#* } for the same events"
elif [ "${in_one%% *}" -ne 1 ] || [ "${one_at_a_time%% *}" -ne 7 ]; then
    complain "tick: seven events crossed ${in_one%% *} time(s) in one batch \
and ${one_at_a_time%% *} one at a time"
fi

# A slot holds one value, which is what D554 decided to keep and what nothing
# was holding. The type says a `Vec3` is three slots; the compiler lays a
# parameter out with `type_slots`, which is a different reader of the same
# thing, and a function taking a `Vec3` and an `f32` takes one more than the
# `Vec3` does. Packed — a slot holding whatever fitted — it would take one
# fewer, and the answers would be wrong rather than the count being small.
# See D555.
mkdir "$scratch"/oneslot
cat > "$scratch"/oneslot/vec.kest <<'KEST'
struct Vec3 {
    x: f32
    y: f32
    z: f32
}

fn scaled(v: Vec3, by: f32) -> Vec3 no.alloc {
    return Vec3(v.x * by, v.y * by, v.z * by)
}

fn main() -> i32 {
    let v = Vec3(1.0, 2.0, 3.0)
    let s = scaled(v, 2.0)
    return i32(s.x) - 2
}
KEST
of_a_vec=$("$kest" check "$scratch"/oneslot/vec.kest --json 2>/dev/null </dev/null |
           sed -n 's/.*"name":"Vec3","kind":"struct","slots":\([0-9]*\).*/\1/p')
laid_out=$("$kest" emit "$scratch"/oneslot/vec.kest 2>/dev/null </dev/null |
           sed -n 's/^fn scaled#Vec3,f32  \([0-9]*\) parameter slots.*/\1/p')
if [ -z "$of_a_vec" ] || [ -z "$laid_out" ]; then
    complain "emit: what a `Vec3` takes is not a number either the types or \
the compiler said"
elif [ "$laid_out" -ne "$((of_a_vec + 1))" ]; then
    complain "emit: a \`Vec3\` is $of_a_vec slots and a function taking one \
and an \`f32\` lays out $laid_out"
fi

# A type name inside a run of a written length, which is the one shape that
# carries one and was not walked: `[T; 3]` says what `T` is as plainly as `[T]`
# does, and a generic over one used to be a function nothing could call. Asked
# for here as well as in `examples/boxes.kest`, because what it answered before
# was a refusal rather than a wrong answer, and a refusal is a thing to ask
# about by name. See D546.
mkdir "$scratch"/written
cat > "$scratch"/written/run.kest <<'KEST'
struct Box<T> {
    it: T
}

fn middleOf<T>(run: [T; 3]) -> T no.alloc {
    return run[1]
}

fn countIn<T>(s: store<T>) -> i32 no.alloc {
    return len(s)
}

fn stillThere<T>(s: store<T>, r: ref<T>) -> bool no.alloc {
    if let one = get(s, r) {
        return true
    }
    return false
}

fn isThere<T>(o: T?) -> bool no.alloc {
    if let one = o {
        return true
    }
    return false
}

fn howManyBoxes<T>(boxed: [Box<T>]) -> i32 no.alloc {
    return len(boxed)
}

fn main() -> i32 {
    let numbers: [i32; 3] = [4, 5, 6]
    let words: [text; 3] = ["a", "bb", "ccc"]
    if middleOf(numbers) != 5 {
        return 1
    }
    if middleOf(words) != "bb" {
        return 2
    }
    let held: store<Box<i32>> = store()
    let at = add(held, Box(1))
    if countIn(held) != 1 {
        return 3
    }
    if !stillThere(held, at) {
        return 4
    }
    let maybe: text? = "here"
    if !isThere(maybe) {
        return 5
    }
    if howManyBoxes([Box(1), Box(2)]) != 2 {
        return 6
    }
    return 0
}
KEST
"$kest" run "$scratch"/written/run.kest >/dev/null 2>"$scratch"/written/why </dev/null
ran_written=$?
if [ "$ran_written" -ne 0 ]; then
    complain "run: a name written in one shape and nowhere else is one a call \
cannot work out, and the run answered $ran_written"
    sed 's/^/    /' "$scratch"/written/why | head -4
fi

# What a hole holds and what it does not. Seven kinds write themselves and six
# do not, and which is which was in `kest_type_has_text` and in no document
# until now: a reader met the rule one refusal at a time. Both halves are asked
# for here, because a kind that quietly gained text would be as wrong as one
# that lost it. See D540.
mkdir "$scratch"/holes
cat > "$scratch"/holes/writes.kest <<'KEST'
flags S: u8 {
    A
}

enum E {
    One(i32)
}

fn main() -> i32 {
    let n: i32 = 1
    let f: f32 = 1.0
    let b: bool = true
    let t: text = "a"
    let s = S.A
    let e = E.One(1)
    let o: i32? = 1
    return len("{n} {f} {b} {t} {s} {e} {o}")
}
KEST
if ! "$kest" check "$scratch"/holes/writes.kest >/dev/null 2>&1 </dev/null; then
    complain "check: the seven kinds that write themselves did not all fit in \
a hole"
    "$kest" check "$scratch"/holes/writes.kest 2>&1 </dev/null |
        sed 's/^/    /' | head -4
fi
for without in "P:struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let it = P(1)\n    return len(\"{it}\")\n}" \
               "[i32]:fn main() -> i32 {\n    let it = [1]\n    return len(\"{it}\")\n}" \
               "[i32; 1]:fn main() -> i32 {\n    let it: [i32; 1] = [1]\n    return len(\"{it}\")\n}" \
               "store<P>:struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let it: store<P> = store()\n    return len(\"{it}\")\n}" \
               "ref<P>:struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let st: store<P> = store()\n    let it = add(st, P(1))\n    return len(\"{it}\")\n}" \
               "fn(i32) -> i32:fn twice(n: i32) -> i32 {\n    return n + n\n}\n\nfn main() -> i32 {\n    let it: fn(i32) -> i32 = twice\n    return len(\"{it}\")\n}"; do
    named=${without%%:*}
    printf '%b\n' "${without#*:}" > "$scratch"/holes/one.kest
    said_none=$("$kest" check "$scratch"/holes/one.kest 2>&1 </dev/null)
    case "$said_none" in
    *"K0324"*"there is no text for \`$named\`"*) ;;
    *)
        complain "check: a hole holding a \`$named\` said \
\`$(printf '%s' "$said_none" | head -1)\`"
        ;;
    esac
done

# A caret under a span that runs onto the next line. It used to be drawn to
# where the span ended, which is forty characters of `^` under a line fourteen
# long: what is on the next line is on the next line, and a caret there points
# at nothing. See D539.
mkdir "$scratch"/wide
cat > "$scratch"/wide/wide.kest <<'KEST'
fn main() -> i32 {
    return [1,
            2,
            3]
}
KEST
drawn=$("$kest" check "$scratch"/wide/wide.kest 2>&1 </dev/null)
under=$(printf '%s\n' "$drawn" | sed -n 's/^ *| *\(\^\^*\)$/\1/p' | head -1)
if [ "${#under}" -ne 3 ]; then
    complain "check: a span over more than one line drew ${#under} caret(s) \
under a line with three characters left on it"
    printf '%s\n' "$drawn" | sed 's/^/    /' | head -5
fi

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
wrote=$("$kest" check --json "$carrying" 2>&1 </dev/null)
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

# What a shape is, asked of each of the three things it holds. A function value
# is what it takes, what it gives back and what it promises, and every one of
# those has to match where the value is handed over. Nothing in this tree ever
# hands one of the wrong shape — a program that did would not compile, and
# every program here compiles — so loosening any of the three refused nothing
# and `make check` stayed green. These are written here because the tree cannot
# hold them. See D412.
mkdir "$scratch"/shapes
for shape in "fn other(word: text, also: text) -> bool no.alloc|takes two" \
             "fn other(word: text) -> i32 no.alloc|gives a number back" \
             "fn other(count: i32) -> bool no.alloc|takes a number"; do
    body=${shape%%|*}
    what=${shape#*|}
    cat > "$scratch"/shapes/shape.kest <<KEST
$body {
    return true
}

fn howMany(items: [text], keep: fn(text) -> bool no.alloc) -> i32 no.alloc {
    let found = 0
    for one in items {
        if keep(one) {
            found += 1
        }
    }
    return found
}

fn main() -> i32 {
    let words: [text] = array()
    push(words, "herald")
    return howMany(words, other) - 1
}
KEST
    shaped=$("$kest" check "$scratch"/shapes/shape.kest 2>&1 </dev/null)
    case "$shaped" in
    *"K0310"*"expects \`fn(text) -> bool no.alloc\`"*) ;;
    *)
        complain "check: a function that $what was taken for one that does \
not: \`$shaped\`"
        ;;
    esac
done

# And the shapes that are not functions. What is inside an array, a store, a
# reference or an optional is one line of the same reading, and how many a
# fixed one holds is the next: loosening either refused nothing in this tree.
# The same reason again — every program here compiles, so a refusal is held by
# a program written where it can be refused. See D413.
for shape in "fn howMany(items: [text]) -> i32 no.alloc|of one thing for one of another|expects \`[text]\`, found \`[i32]\`" \
             "fn howMany(items: [i32; 8]) -> i32 no.alloc|of eight for one of four|expects \`[i32; 8]\`, found \`[i32; 4]\`"; do
    body=${shape%%|*}
    rest=${shape#*|}
    what=${rest%%|*}
    wanted=${rest#*|}
    cat > "$scratch"/shapes/held.kest <<KEST
$body {
    return len(items)
}

fn main() -> i32 {
    let four: [i32; 4] = [1, 2, 3, 4]
    let numbers: [i32] = array()
    push(numbers, 1)
    if len(four) == 0 {
        return howMany(numbers)
    }
    return howMany(four) - 4
}
KEST
    held=$("$kest" check "$scratch"/shapes/held.kest 2>&1 </dev/null)
    case "$held" in
    *"K0310"*"$wanted"*) ;;
    *)
        complain "check: an array $what was taken: \`$held\`"
        ;;
    esac
done

# The one conversion this language does. A value standing where an optional is
# wanted becomes one, and what makes that safe is that it has to be a value of
# what the optional holds. Loosening that let a piece of text stand where an
# `i32?` was wanted: the program compiled, and `main` answered
# 105265126565552, which is where the text was. Nothing in this tree noticed,
# because every program here compiles. See D414.
cat > "$scratch"/shapes/maybe.kest <<'KEST'
fn maybe() -> i32? {
    return "text"
}

fn main() -> i32 {
    if let n = maybe() {
        return n
    }
    return 0
}
KEST
became=$("$kest" check "$scratch"/shapes/maybe.kest 2>&1 </dev/null)
case "$became" in
*"K0310"*"expects \`i32?\`, found \`text\`"*) ;;
*)
    complain "check: text stood where a number that may be nothing was \
wanted: \`$became\`"
    ;;
esac

# Every refusal a file can meet before it means anything, and every one the
# checker has for a program that parses and does not mean anything: the
# lexer's, the parser's and the checker's. A hundred and thirty-nine codes this
# compiler can say, and thirty-three were named in no document and in no check
# — a message nobody has seen is a message nobody knows is there. These are the
# ones a reader meets first, where a file is refused for what it is rather than
# for what it says, so they are the ones to hold first. See D415.
#
# One line each, written into a whole file, because what refuses them is
# reading rather than meaning. `%b` turns the `\n` in the table into lines.
mkdir "$scratch"/refused
while IFS='|' read -r code body words; do
    printf '%b\n' "$body" > "$scratch"/refused/one.kest
    refused=$("$kest" check "$scratch"/refused/one.kest 2>&1 </dev/null)
    printf '%s\n' "$refused" >> "$scratch"/said
    case "$refused" in
    *"$code"*"$words"*) ;;
    *)
        complain "check: $code said \`$(printf '%s' "$refused" | head -1)\`"
        ;;
    esac
done <<'REFUSED'
K0101|fn main() -> i32 {\n    let s = "unterminated\n    return 0\n}|string is not terminated
K0103|fn main() -> i32 {\n    let b = '\\q'\n    return 0\n}|unknown escape
K0105|fn main() -> i32 {\n    let a = 1; let b = 2\n    return a + b\n}|are not separated
K0106|fn main() -> i32 {\n    let b = 'a\n    return 0\n}|closing quote
K0203|fn main() -> 3 {\n    return 0\n}|expected a type
K0206|fn Host.name() -> i32 {\n    return 0\n}|names a receiver
K0301|fn main() -> i32 {\n    let x: Nope = 1\n    return x\n}|unknown type `Nope`
K0302|struct Box<T> {\n    it: T\n}\n\nfn main() -> i32 {\n    let b: Box<i32, i32> = Box(1)\n    return b.it + 0\n}|takes 1 type, found 2
K0302|fn main() -> i32 {\n    let b: Nope<i32> = 1\n    return 0\n}|unknown generic type `Nope`
K0309|struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let p = P(1, 2)\n    return p.x\n}|has 1 field, found 2
K0309|enum Door {\n    Open(i32, i32)\n}\n\nfn main() -> i32 {\n    let d = Door.Open(1, 2)\n    return match d {\n        Open(a) -> a\n    }\n}|carries 2 things, and 1 name was given
K0310|fn main() -> i32 {\n    let n = 1\n    return len(rest(n, 0))\n}|`rest` works on text, found `i32`
K0310|fn main() -> i32 {\n    let n = 1\n    if matches(n, 0, "a") {\n        return 1\n    }\n    return 0\n}|`matches` works on text, found `i32`
K0310|fn main() -> i32 {\n    let n = 1\n    return len(slice(n, 0, 1))\n}|`slice` works on text, found `i32`
K0207|fn main() -> i32 {\n    let s = "{}"\n    return len(s)\n}|hole is empty
K0209|extern fn Host.of<T>(one: T) -> T\n\nfn main() -> i32 {\n    return 0\n}|takes no types
K0210|fn main() -> i32 {\n    defer 1\n    return 0\n}|and this is not a call
K0210|fn main() -> i32 {\n    defer {\n        return 1\n    }\n    return 0\n}|and this is not a call
K0102|fn main() -> i32 {\n    let a = 1 $ 2\n    return a\n}|unexpected character
K0104|fn main() -> i32 {\n    let a = 0x\n    return a\n}|literal has no digits
K0107|fn main() -> i32 {\n    let s = "\0377"\n    return len(s)\n}|starts no character
K0108|fn main() -> i32 {\n    let s = "\0357\0273\0277hi"\n    return len(s)\n}|a mark with no width
K0201|fn main() -> i32 \n    return 0\n}|expected
K0202|what\n|expected a declaration
K0204|fn main() -> i32 {\n    let a = \n    return 0\n}|expected an expression
K0205|fn main() -> i32 {\n    1 = 2\n    return 0\n}|cannot be assigned to
K0208|enum Door {\n    Shut\n    Open(i32)\n}\n\nfn main() -> i32 {\n    let d = Door.Shut\n    match d {\n        Shut -> 0\n        Open(w) {\n            return w\n        }\n    }\n    return 0\n}|every arm gives a value
K0211|fn firstOf<T>(a: T, b: T) -> T {\n    return a\n}\n\nfn main() -> i32 {\n    return firstOf<i32>(1, 2)\n}|is not given its types
K0212|flags State {\n    Moving\n}\n\nfn main() -> i32 {\n    return 0\n}|says how wide it is
K0213|fn main() -> i32 {\n    if (1 < 3) {\n        return 0\n    }\n    return 1\n}|without brackets round the whole of it
K0214|fn main() -> i32 {\n    let n = 5\n    n %= 2\n    return n\n}|is not one of the four this language has
K0303|struct P {\n    x: i32\n    x: i32\n}\n\nfn main() -> i32 {\n    return 0\n}|declared twice
K0305|fn f(a: i32, a: i32) -> i32 {\n    return a\n}\n\nfn main() -> i32 {\n    return f(1, 2)\n}|declared twice
K0311|const N: i32 = 1\n\nfn main() -> i32 {\n    N = 2\n    return N\n}|is a constant
K0312|fn main() -> i32 {\n    if 1 {\n        return 1\n    }\n    return 0\n}|must be `bool`
K0313|fn main() -> i32 {\n    break\n    return 0\n}|outside a loop
K0315|fn main() -> i32 {\n    let a: [i32] = array()\n    return a["x"]\n}|must be an integer
K0316|fn f(n: i32) -> i32 {\n    if n > 0 {\n        return 1\n    }\n}\n\nfn main() -> i32 {\n    return f(1)\n}|can end without returning
K0318|fn main() -> i32 {\n    let a = 1\n    let a = 2\n    return a\n}|already declared
K0319|struct P {\n    p: P\n}\n\nfn main() -> i32 {\n    return 0\n}|contains itself
K0320|fn main() -> i32 {\n    let a = []\n    return len(a)\n}|no element type
K0322|fn main() -> i32 {\n    let s = store()\n    return 0\n}|has no type here
K0323|fn main() -> i32 {\n    if let x = 1 {\n        return x\n    }\n    return 0\n}|opens an optional
K0324|struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let p = P(1)\n    let s = "{p}"\n    return len(s)\n}|no text for
K0329|import std.math\n\nfn main() -> i32 {\n    return math.min(1, "a")\n}|no `math.min` takes these
K0329|fn f(n: i32) -> i32 {\n    return n\n}\n\nfn f(n: i32?) -> i32 {\n    return 1\n}\n\nfn main() -> i32 {\n    return f(none) - 1\n}|more than one `f` takes these
K0336|fn main() -> i32 {\n    return 1 << 1.0\n}|a shift counts
K0337|flags S: i8 {\n    A\n}\n\nfn main() -> i32 {\n    return 0\n}|unsigned integer
K0340|enum Door {\n    Shut\n    Open(i32)\n}\n\nfn main() -> i32 {\n    let d = Door.Shut\n    return match d {\n        Shut, Shut -> 0\n        Open(w) -> w\n    }\n}|chooses between
K0341|fn main() -> i32 {\n    let t = 0\n    let one: f32 = 1.0\n    for i in one..one {\n        t += 1\n    }\n    return t\n}|runs between integers
K0342|extern fn Host.now() -> i32 no.alloc\n\nfn main() -> i32 {\n    let f = Host.now\n    return 0\n}|is the host's, so it is called and not named
K0704|module one\n\nimport one\n\nfn main() -> i32 {\n    return 0\n}|imports itself
K0302|struct Box<T> {\n    it: T\n}\n\nfn main() -> i32 {\n    let b: Box = Box(1)\n    return b.it\n}|none are written here
K0307|struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let p = P(1)\n    return p.y\n}|has no field
K0308|fn main() -> i32 {\n    let a = 1\n    return a(2)\n}|is not a function
K0309|enum Door {\n    Open(i32)\n}\n\nfn main() -> i32 {\n    let d = Door.Open(1, 2)\n    return 0\n}|carries 1 thing, found 2
K0314|fn main() -> i32 {\n    let a = "x" - "y"\n    return len(a)\n}|does not apply to
K0317|fn main() -> i32 {\n    let t = 0\n    for i, j in 0..3 {\n        t += i\n    }\n    return t\n}|no positions to walk by
K0321|fn main() -> i32 {\n    let t = 0\n    for i in 0..3 {\n        i = 1\n    }\n    return t\n}|is the loop's own
K0331|fn main() -> i32 {\n    let a = 1\n    return match a {\n        else -> 0\n    }\n}|chooses between the cases
K0332|enum Door {\n    Shut\n    Open(i32)\n}\n\nfn main() -> i32 {\n    let d = Door.Shut\n    return match d {\n        else -> 0\n        else -> 1\n    }\n}|two `else` arms
K0334|fn main() -> i32 {\n    let a = if true -> 1\n    return a\n}|needs an `else`
K0335|fn main() -> i32 {\n    let a = array()\n    return len(a)\n}|has no type here
K0345|fn main() -> i32 {\n    1 + 2\n    return 0\n}|nothing takes it
K0347|fn main() -> f32 {\n    return 1.0\n}|is the exit
K0348|fn main(n: i32) -> i32 {\n    return n\n}|calls it with
K0350|fn one(n: i32) -> i32 {\n    return n\n}\n\nfn two(n: i32) -> i32 {\n    return n + 1\n}\n\nfn main() -> i32 {\n    let f = one\n    f = two\n    return f(1)\n}|a name for one is not
K0352|fn main() -> i32 {\n    let s = rest("abc", -1)\n    return len(s)\n}|is before it
K0353|import std.text\n\nfn main() -> i32 {\n    return text.nothing("a")\n}|has nothing called
K0355|fn main(n: i32) -> i32 {\n    return n\n}\n\nfn main() -> i32 {\n    return 0\n}|this file declares
K0330|enum Door {\n    Shut\n}\n\nfn main() -> i32 {\n    let d = Door.Open\n    return 0\n}|has no
K0338|flags S: u8 {\n    A\n    B\n    C\n    D\n    E\n    F\n    G\n    H\n    I\n}\n\nfn main() -> i32 {\n    return 0\n}|is flag 9, and a `u8` holds 8
K0327|flags Marks: u8 {\n    Seen\n    Read\n}\n\nfn main() -> i32 {\n    let m = Marks(1)\n    return 0\n}|is made from a `u8`, found `i32`
K0327|enum Door {\n    Shut\n    Open\n}\n\nfn main() -> i32 {\n    let d = Door(1)\n    return 0\n}|there is no way to make a `Door` from a value
K0327|fn main() -> i32 {\n    let xs: [i32] = array()\n    let n = i32(xs)\n    return n\n}|there is no `i32` for `[i32]`
K0327|fn main() -> i32 {\n    let t = text(1)\n    return 0\n}|text is made from `[u8]`, found `i32`
K0327|flags A: u8 {\n    One\n}\n\nfn main() -> i32 {\n    let n = u16(A.One)\n    return 0\n}|`A` is 8 bits, and `u16` is not
K0327|struct Big {\n    cells: [i32; 20000]\n}\n\nfn take(b: Big) -> i32 {\n    return b.cells[0]\n}\n\nfn main() -> i32 {\n    return 0\n}|is 80000 bytes, and a value is at most 65535
K0343|fn firstOf<T>(a: T) -> T {\n    return a\n}\n\nfn main() -> i32 {\n    let f = firstOf\n    return 0\n}|takes a type
K0349|fn main<T>() -> i32 {\n    return 0\n}|is generic
K0351|fn main() -> i32 {\n    let s: store<i32> = store(-1)\n    return 0\n}|cannot have room for
K0402|fn careful(f: fn(i32) -> i32, n: i32) -> i32 no.alloc {\n    return f(n)\n}\n\nfn one(n: i32) -> i32 {\n    return n\n}\n\nfn main() -> i32 {\n    return careful(one, 1) - 1\n}|nothing promises about what this calls
K0201|fn main() -> i32 {\n    let a = 1 let b = 2\n    return a + b\n}|expected end of line, found `let`
K0201|fn main() -> i32 {\n    let a: i32\n    return a\n}|a `let` gives its value where it is written
K0201|fn main() -> i32 {\n    let a = 1\n    return match a {\n        1 -> 1\n        else -> 0\n    }\n}|a `match` arm names a case of an enum, and `else` answers the rest
K0201|fn main() -> i32 {\n    let door: i32? = 1\n    if let 1 = door {\n        return 1\n    }\n    return 0\n}|`if let` names what is held rather than comparing with it
K0201|fn main() -> i32 {\n    let door: i32? = 1\n    if let Some(x) = door {\n        return 1\n    }\n    return 0\n}|`if let` names what an optional holds: `if let held = ...`
K0201|fn next() -> text? {\n    return "x"\n}\n\nfn main() -> i32 {\n    while let "x" = next() {\n        return 1\n    }\n    return 0\n}|`while let` names what is held rather than comparing with it
K0201|fn main() -> i32 {\n    for 1 in [1, 2] {\n        return 1\n    }\n    return 0\n}|a `for` names what it walks over: `for one in ...`
K0201|fn main() -> i32 {\n    for x, 1 in [1, 2] {\n        return 1\n    }\n    return 0\n}|a `for` names the position first and what it walks over second
K0201|struct P {\n    x i32\n}|a field is written `name: type`
K0201|const N = 1|a `const` is written with its type: `const N: i32 = 1`
K0201|const N: i32|a `const` gives its value where it is written
K0201|fn main() -> i32 {\n    let a = 1\n    return a.0\n}|a field is named, so there is nothing at a position to read
K0201|flags S u8 {\n    A\n}|a flag set says how wide it is: `flags Name: u8 {`
K0203|fn f(a: *i32) -> i32 {\n    return 0\n}|there are no pointers here: what names a slot in a store is `ref<T>`
K0203|fn f(a: (i32, i32)) -> i32 {\n    return 0\n}|there are no tuples here: a `struct` is what holds several things
K0203|fn f(a: 3) -> i32 {\n    return 0\n}|a type is a name, `[T]`, `[T; N]` or `fn(...)`, and `?` after any of them
K0204|fn main() -> i32 {\n    let a = {\n        1\n    }\n    return a\n}|a block is not a value: an `if` gives one with `->`
K0345|fn main() -> i32 {\n    let a = if true { 1 } else { 2 }\n    return a\n}|this `if` gives nothing, and both its arms end in a value
K0356|fn note(n: i32) {\n}\n\nfn main() -> i32 {\n    let a = note(1)\n    return 0\n}|this gives nothing back, and a `let` names a value
K0356|fn note(n: i32) {\n}\n\nfn main() -> i32 {\n    let a = [note(1)]\n    return len(a)\n}|this gives nothing back, and an array holds values
K0357|fn f() -> void {\n}\n\nfn main() -> i32 {\n    f()\n    return 0\n}|`void` is not a type this language writes
K0345|enum D {\n    A\n    B\n}\n\nfn main() -> i32 {\n    let d = D.A\n    let a = match d {\n        A { 1 }\n        B { 2 }\n    }\n    return a\n}|this `match` gives nothing, and every arm ends in a value
K0302|fn main() -> i32 {\n    let r: ref<i32, i32> = 0\n    return 0\n}|`ref` takes one type argument, found 2
K0303|enum D {\n    A\n    A\n}\n\nfn main() -> i32 {\n    let d = D.A\n    return 0\n}|case `A` is declared twice in `D`
K0303|flags S: u8 {\n    A\n    A\n}\n\nfn main() -> i32 {\n    let s = S.A\n    return 0\n}|flag `A` is declared twice in `S`
K0307|struct E {\n}\n\nfn main() -> i32 {\n    let e = E()\n    return e.x\n}|`E` has no field `x`
K0309|fn f(a: i32, b: i32) -> i32 {\n    return a + b\n}\n\nfn main() -> i32 {\n    return f(1)\n}|`f` takes 2 arguments, found 1
K0309|fn main() -> i32 {\n    let a: [i32] = array()\n    push(a)\n    return 0\n}|`push` takes 2 arguments, found 1
K0309|enum D {\n    A(i32)\n}\n\nfn main() -> i32 {\n    let d = D.A\n    return 0\n}|`A` carries 1 thing and was named with none
K0309|fn one(n: i32) -> i32 {\n    return n\n}\n\nfn main() -> i32 {\n    let f: fn(i32) -> i32 = one\n    return f(1, 2)\n}|expected 1 argument, found 2
K0310|struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let p = P(1)\n    p.x = "a"\n    return p.x\n}|this assignment expects `i32`, found `text`
K0310|fn f(a: i32) -> i32 {\n    return a\n}\n\nfn main() -> i32 {\n    return f("x")\n}|`a` expects `i32`, found `text`
K0310|fn main() -> i32 {\n    let n = 1\n    return len(get(n, 0))\n}|`get` works on a store, found `i32`
K0310|fn main() -> i32 {\n    let n = 1\n    return len(pop(n))\n}|`pop` works on an array, found `i32`
K0310|fn main() -> i32 {\n    let n = 1\n    push(n, 1)\n    return 0\n}|`push` puts something on an array, found `i32`
K0310|fn main() -> i32 {\n    let a: [i32] = array()\n    let h = hash(a)\n    return i32(h)\n}|`hash` stands for what compares, and `[i32]` does not
K0310|fn main() -> i32 {\n    return len(1)\n}|`len` counts an array, a store or text, found `i32`
K0310|fn main() -> i32 {\n    let a: [i32] = array("x", 0)\n    return len(a)\n}|a count is an integer, found `text`
K0310|fn f() -> i32 {\n    return\n}\n\nfn main() -> i32 {\n    return f()\n}|this function returns `i32`, so `return` needs a value
K0310|fn f() {\n    return 1\n}\n\nfn main() -> i32 {\n    f()\n    return 0\n}|this function returns nothing, so `return` takes no value
K0310|fn main() -> i32 {\n    let a: [i32] = array()\n    return hash(a) + 0\n}|this return expects `i32`, found `u64`
K0314|fn main() -> i32 {\n    let a = 1.0\n    let b = 2.0\n    return i32(a % b)\n}|`%` does not apply to `f32`
K0314|fn main() -> i32 {\n    let a = 1.0\n    return i32(~a)\n}|`~` does not apply to `f32`
K0314|fn main() -> i32 {\n    let a: i32 = 1\n    let b: i64 = 2\n    return i32(a + b)\n}|`+` needs both sides to have one type, found `i32` and `i64`
K0315|fn main() -> i32 {\n    let v: [i32; 2] = [1, 2]\n    return v[5]\n}|5 is outside 2 of them
K0315|fn main() -> i32 {\n    let a = 1\n    return a[0]\n}|`i32` cannot be indexed
K0317|fn main() -> i32 {\n    let a = 1\n    for x in a {\n        return x\n    }\n    return 0\n}|`for` walks an array, text, a store or a set of bits, found `i32`
K0317|flags S: u8 {\n    A\n    B\n}\n\nfn main() -> i32 {\n    let s = S.A\n    for i, x in s {\n        return i\n    }\n    return 0\n}|a set of bits has no positions to walk by
K0317|struct N {\n    n: i32\n}\n\nfn main() -> i32 {\n    let w: store<N> = store()\n    for i, x in w {\n        return i\n    }\n    return 0\n}|a store has no positions to walk by
K0320|fn main() -> i32 {\n    let v: [i32; 2] = [1, 2, 3]\n    return v[0]\n}|this holds 2 and 3 are written
K0323|fn main() -> i32 {\n    let a = 1\n    while let x = a {\n        return x\n    }\n    return 0\n}|`while let` opens an optional, found `i32`
K0332|enum D {\n    A\n    B\n}\n\nfn main() -> i32 {\n    let d = D.A\n    return match d {\n        A -> 0\n        A -> 1\n        B -> 2\n    }\n}|this arm is already answered above
K0343|fn pair<A>(a: A, b: A) -> i32 {\n    return 1\n}\n\nfn main() -> i32 {\n    return pair(1, "x")\n}|two arguments disagree about what a type name is
K0343|struct Pair<A> {\n    a: A\n    b: A\n}\n\nfn main() -> i32 {\n    let p = Pair(1, "x")\n    return 0\n}|two fields disagree about what a type name is
K0343|struct Box<T> {\n    it: T\n}\n\nfn main() -> i32 {\n    let b = Box()\n    return 0\n}|what `T` is here cannot be told from what this is built with
K0343|fn only<T>(n: i32) -> i32 {\n    return n\n}\n\nfn main() -> i32 {\n    return only(1)\n}|what `T` is here cannot be told from what was passed
K0351|fn main() -> i32 {\n    return len(slice("abc", 0, -1))\n}|a piece of text cannot be -1 bytes long
K0352|fn main() -> i32 {\n    let a: [i32] = array()\n    return a[-1]\n}|an index is nought or more, and -1 is not
K0307|fn main() -> i32 {\n    let n = 1\n    return n.x\n}|`i32` has no fields
K0307|struct Thing {\n    n: i32\n}\n\nfn main() -> i32 {\n    let s: store<Thing> = store()\n    let a = add(s, Thing(1))\n    for t in s {\n        return t.n\n    }\n    return 0\n}|read what it names with `get` and take `n` off that
K0307|struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let p: P? = P(1)\n    return p.x\n}|take what it holds out with `if let`
K0314|fn main() -> i32 {\n    let a: i32? = 1\n    return a + 1\n}|take what it holds out with `if let`
K0314|fn main() -> i32 {\n    let a: i32? = 1\n    return a % 2\n}|take what it holds out with `if let`
K0314|fn main() -> i32 {\n    let a: i32? = 1\n    return a & 2\n}|take what it holds out with `if let`
K0310|fn f(n: i32) -> i32 {\n    return n\n}\n\nfn main() -> i32 {\n    let a: i32? = 1\n    return f(a)\n}|take what it holds out with `if let`
K0310|fn main() -> i32 {\n    let a: [i32]? = [1]\n    return len(a)\n}|take what it holds out with `if let`
K0317|fn main() -> i32 {\n    let a: [i32]? = [1]\n    for x in a {\n        return x\n    }\n    return 0\n}|take what it holds out with `if let`
K0315|fn main() -> i32 {\n    let a: [i32]? = [1]\n    return a[0]\n}|take what it holds out with `if let`
K0351|fn main() -> i32 {\n    let a: [i32] = array(-1, 0)\n    return len(a)\n}|an array cannot have -1 elements
K0314|fn main() -> i32 {\n    let a = "x"\n    return len(-a)\n}|`-` does not apply to `text`
K0326|fn main() -> i32 {\n    let x: i8 = 300\n    return i32(x)\n}|300 does not fit in `i8`
K0326|fn main() -> i32 {\n    let x: u8 = -1\n    return i32(x)\n}|`u8` holds no negative numbers
K0326|fn main() -> i32 {\n    let n = 2\n    let v: [i32; n] = [1, 2]\n    return v[0]\n}|a count is a number or a constant that is one
K0326|fn wide() -> i32 {\n    return 2\n}\n\nconst N: i32 = wide()\n\nfn main() -> i32 {\n    let v: [i32; N] = [1, 2]\n    return v[0]\n}|this count is not worked out where it is written
K0333|enum D {\n    A\n    B\n}\n\nfn main() -> i32 {\n    let d = D.A\n    return match d {\n        A -> 0\n    }\n}|this `match` does not answer `B`
K0401|fn grow() -> i32 no.alloc {\n    let xs: [i32] = array()\n    let ys: [i32] = array()\n    push(xs, 1)\n    return len(xs) + len(ys)\n}\n\nfn main() -> i32 {\n    return grow()\n}|and here: `push` grows what it is given
K0401|fn grow() -> i32 no.alloc {\n    let a0: [i32] = array()\n    let a1: [i32] = array()\n    let a2: [i32] = array()\n    let a3: [i32] = array()\n    let a4: [i32] = array()\n    let a5: [i32] = array()\n    let a6: [i32] = array()\n    let a7: [i32] = array()\n    let a8: [i32] = array()\n    let a9: [i32] = array()\n    let a10: [i32] = array()\n    let a11: [i32] = array()\n    return len(a0)\n}\n\nfn main() -> i32 {\n    return grow()\n}|and here, and 4 more places
K0333|enum D {\n    A\n    B\n    C\n}\n\nfn main() -> i32 {\n    let d = D.A\n    return match d {\n        A -> 0\n    }\n}|does not answer `B`, `C`
K0333|enum D {\n    A\n    B\n    C\n    D\n    E\n    F\n    G\n    H\n    I\n    J\n}\n\nfn main() -> i32 {\n    let d = D.A\n    return match d {\n        A -> 0\n    }\n}|`I` and 1 more
K0344|fn main() -> i32 {\n    let b = 'ab'\n    return i32(b)\n}|a byte literal holds one byte, and this is 2
K0344|fn main() -> i32 {\n    let b = ''\n    return i32(b)\n}|a byte literal holds one byte
K0326|const N: i32 = 2000000000\n\nfn main() -> i32 {\n    let v: [i64; N] = [1]\n    return i32(v[0])\n}|an array of that many has no size: 2000000000
K0344|struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    let a = P\n    return 0\n}|`P` is a type, and this wants a value
K0333|enum Four {\n    C0\n    C1\n    C2\n    C3\n}\n\nfn main() -> i32 {\n    let a = Four.C0\n    return match a, a, a, a, a {\n        _, _, _, _, _ -> 0\n    }\n}|combinations to answer, which is more than
REFUSED

# And the one a command is refused for rather than a file: `call` with nothing
# to call.
cat > "$scratch"/refused/calling.kest <<'KEST'
fn main() -> i32 {
    return 0
}
KEST
nothing_named=$("$kest" call "$scratch"/refused/calling.kest 2>&1 </dev/null)
case "$nothing_named" in
*"K0626"*"no function to call"*) ;;
*)
    complain "check: \`call\` with no function said \
\`$(printf '%s' "$nothing_named" | head -1)\`"
    ;;
esac

# And the ones a command is refused for before there is a file at all: a
# mistake in the words themselves. These were bare sentences with no code and
# no JSON, so a run asked for JSON answered with a status and an empty stream.
# Each is asked in both forms, because the whole of what was wrong with them
# was that one of the two said nothing. See D437.
# And no words at all, which is the one of the seven with no `--json` to it:
# `--json` is a word, and there are none. See D445.
nothing_typed=$("$kest" 2>&1 </dev/null)
case "$nothing_typed" in
*"K0649"*"there is no command in what was typed"*) ;;
*)
    complain "check: \`kest\` with nothing after it said \
\`$(printf '%s' "$nothing_typed" | head -1)\`"
    ;;
esac

# The words each of them is refused with as well as the code, because `K0649`
# says seven things and a check that reads only the code reads none of them.
# See D445.
#
# How many events this command will lend is read out of the source rather than
# written here, and the whole sentence is asked for rather than the front of it.
# This stopped at `between 0 and`, so a refusal that stopped saying how many
# would have gone on passing — which is what D521 and D522 found one table over,
# in the check that holds the language's own ceilings. See D524.
most_events=$(sed -n 's/^#define MAX_EVENTS \([0-9][0-9]*\)$/\1/p' src/main.c)
if [ -z "$most_events" ]; then
    complain "check: \`MAX_EVENTS\` is not a number in src/main.c, so how many \
events a run may ask for is held to nothing"
fi
for asking in "nonsense@unknown command \`nonsense\`" \
        "check@\`check\` needs a file" \
        "fmt@\`fmt\` needs a file" \
        "tick $scratch/refused/calling.kest 2x@\`2x\` is not a number of events" \
        "tick $scratch/refused/calling.kest 99999999999@an event count is between 0 and $most_events" \
        "tick $scratch/refused/calling.kest 1,2 3@takes one count, and was given \`3\` as well" \
        "tick $scratch/refused/calling.kest 1,x@\`1,x\` is not a list of events"; do
    words=${asking%%@*}
    refused_with=${asking#*@}
    answered=$("$kest" $words 2>&1 </dev/null)
    case "$answered" in
    *"K0649"*"$refused_with"*) ;;
    *)
        complain "check: \`kest $words\` said \
\`$(printf '%s' "$answered" | head -1)\`"
        ;;
    esac
    # And the same words with `--json`, where the object is on the standard
    # output because that is where a tool is reading.
    answered=$("$kest" $words --json 2>/dev/null </dev/null)
    case "$answered" in
    '{"diagnostics":[{"severity":"error","code":"K0649"'*) ;;
    *)
        complain "check: \`kest $words --json\` wrote \
\`$(printf '%s' "$answered" | head -1)\`"
        ;;
    esac
done

# And a file the one form could not be written into, which is not a file that
# is in the wrong form: the object for it says `false` either way, so what
# happened is said beside it or nowhere.
mkdir -p "$scratch"/refused/sealed
printf 'fn  main( ) -> i32 {\n  return 0\n}\n' > "$scratch"/refused/sealed/badly.kest
# `fmt -w` writes beside the file and renames over it, so a directory standing
# where that file goes is a write nothing can make -- which is what this needs,
# because a run as the owner of everything can write through any permission.
mkdir -p "$scratch"/refused/sealed/badly.kest.kest-fmt
for form in "" "--json"; do
    sealed=$("$kest" fmt -w $form "$scratch"/refused/sealed/badly.kest 2>&1 \
        </dev/null)
    case "$sealed" in
    *"K0706"*) ;;
    *)
        complain "check: \`fmt -w $form\` where it cannot write said \
\`$(printf '%s' "$sealed" | head -1)\`"
        ;;
    esac
done
rmdir "$scratch"/refused/sealed/badly.kest.kest-fmt

# And the one that takes two files: a name is reachable from a module this file
# asked for, so being told it is not needs a module that was loaded by somebody
# else. The helper imports `std.text` and the program names `text` without
# asking for it.
mkdir -p "$scratch"/refused/asking
cat > "$scratch"/refused/asking/helper.kest <<'KEST'
module asking.helper

import std.text

fn wide(word: text) -> i32 no.alloc {
    return text.chars(word)
}
KEST
cat > "$scratch"/refused/asking/main.kest <<'KEST'
module asking.main

import asking.helper

fn main() -> i32 {
    return text.chars("a") + helper.wide("b")
}
KEST
unasked=$("$kest" check "$scratch"/refused/asking/main.kest 2>&1 </dev/null)
case "$unasked" in
*"K0325"*"does not import"*) ;;
*)
    complain "check: a name from a module this file did not ask for said \
\`$(printf '%s' "$unasked" | head -1)\`"
    ;;
esac

# And the refusals a program meets while it runs, or when a command asks it for
# something it has not got. These need a program that runs rather than one that
# is refused, so each says which command reaches it. See D418.
#
# The last nine are one sentence each that the machine can say and nothing had
# ever made it say. A code is asked for by something; a code the machine says
# in more than one place is asked for at one of them, and the others are
# reached by nothing -- an unsigned divide by nought, a shift by a negative
# count in each of its three widths, three walks over text that begin past the
# end of it, and a count of less than nothing for an array and for a store.
# See D440.
while IFS='|' read -r code command body words; do
    printf '%b\n' "$body" > "$scratch"/refused/running.kest
    # The file comes between the command and whatever the command is given, so
    # a command that takes a name carries it after the file rather than before.
    given=${command#* }
    [ "$given" = "$command" ] && given=""
    # shellcheck disable=SC2086
    ran=$("$kest" ${command%% *} "$scratch"/refused/running.kest $given \
          2>&1 </dev/null)
    printf '%s\n' "$ran" >> "$scratch"/said
    case "$ran" in
    *"$code"*"$words"*) ;;
    *)
        complain "$command: $code said \`$(printf '%s' "$ran" | head -1)\`"
        ;;
    esac
done <<'RUNNING'
K0506|check|extern fn Host.now() -> i32 no.alloc\n\nfn main() -> i32 {\n    return 0\n}|no host is asked for it
K0504|emit|const N: i32 = M + 1\nconst M: i32 = N + 1\n\nfn main() -> i32 {\n    return N\n}|`N` is not worked out where it is written
K0508|check|const N: i32 = 1\n\nfn main() -> i32 {\n    return 0\n}|nothing in this program reads
K0509|check|struct P {\n    x: i32\n}\n\nfn main() -> i32 {\n    return 0\n}|nothing in this program names
K0346|check|struct P {\n    x: i32\n}\n\nfn touch(p: P) {\n    p.x = 1\n}\n\nfn main() -> i32 {\n    let q = P(0)\n    touch(q)\n    return q.x\n}|is a value here, so this is discarded
K0627|call count 3|fn count<T>(n: i32) -> i32 {\n    return n\n}\n\nfn main() -> i32 {\n    return 0\n}|takes types, and a copy of it exists where one is called
K0601|run|fn main() -> i32 {\n    let z = 0\n    return 1 / z\n}|division by zero
K0606|run|extern fn Host.now() -> i32 no.alloc\n\nfn main() -> i32 {\n    return Host.now()\n}|does not provide
K0629|call shape|fn shape() -> [i32] {\n    let a: [i32] = array()\n    return a\n}\n\nfn main() -> i32 {\n    return len(shape())\n}|there is no text for
K0620|tick 2|fn onEvents(events: [i32]) -> f32 {\n    return 1.0\n}\n\nfn main() -> i32 {\n    return 0\n}|as a whole number
K0622|tick 2|fn onEvents<T>(events: [T]) -> i32 {\n    return len(events)\n}\n\nfn main() -> i32 {\n    return 0\n}|and tick has no type
K0619|tick 2|fn onEvents(events: [i32], more: i32) -> i32 {\n    return len(events) + more\n}\n\nfn main() -> i32 {\n    return 0\n}|and tick passes one
K0625|call wide 1|fn wide(n: i32) -> i32 {\n    return n\n}\n\nfn wide(word: text) -> i32 {\n    return len(word)\n}\n\nfn main() -> i32 {\n    return wide(1) + wide("") - 1\n}|more than one
K0601|run|fn main() -> i32 {\n    let a: u32 = 1\n    let z: u32 = 0\n    return i32(a / z)\n}|division by zero
K0604|run|fn main() -> i32 {\n    let by = 0 - 1\n    return 1 << by\n}|a shift of -1 is not a count
K0604|run|fn main() -> i32 {\n    let by = 0 - 1\n    let v: i64 = 8\n    return i32(v >> by)\n}|a shift of -1 is not a count
K0604|run|fn main() -> i32 {\n    let by = 0 - 1\n    let v: u64 = 8\n    return i32(v >> by)\n}|a shift of -1 is not a count
K0604|run|fn main() -> i32 {\n    let at = 5\n    return len(rest("ab", at))\n}|the rest from 5 is outside text of 2 bytes
K0604|run|fn main() -> i32 {\n    let at = 5\n    if matches("ab", at, "c") {\n        return 1\n    }\n    return 0\n}|looking at 5, which is outside text of 2 bytes
K0604|run|fn main() -> i32 {\n    let from = 5\n    if let at = find("ab", "b", from) {\n        return 1\n    }\n    return 0\n}|looking from 5, which is outside text of 2 bytes
K0604|run|fn main() -> i32 {\n    let n = 0 - 1\n    let a: [i32] = array(n, 0)\n    return len(a)\n}|an array cannot have -1 elements
K0604|run|fn main() -> i32 {\n    let n = 0 - 1\n    let s: store<i32> = store(n)\n    return 0\n}|a store cannot have room for -1
K0604|run|fn main() -> i32 {\n    let at = 1\n    return len(slice("ab", at, 9))\n}|9 bytes from 1 is outside text of 2 bytes
K0604|run|fn main() -> i32 {\n    let b: [u8] = array()\n    push(b, 0)\n    return len(text(b))\n}|byte 0 is zero, and text ends at a zero byte
K0604|run|fn main() -> i32 {\n    let v: [i32; 2] = [1, 2]\n    let i = 5\n    return v[i]\n}|index 5 is outside 2 of them
K0604|run|fn main() -> i32 {\n    let a: [i32] = array()\n    let i = 5\n    return a[i]\n}|index 5 is outside an array of length 0
K0604|run|fn main() -> i32 {\n    let i = 9\n    return i32("ab"[i])\n}|index 9 is outside text of 2 bytes
K0619|tick 2|fn onEvent(e: text) -> i32 {\n    return 0\n}\n\nfn main() -> i32 {\n    return 0\n}|`onEvent` takes `text`, and tick has `i32` to give it
RUNNING

# Every way a refusal can be worded, held to having been seen. The tables above
# name a code and some of the words, and a code is a name the compiler chooses:
# `K0310` says fourteen different things and a check that asks for one of them
# leaves the other thirteen said by nothing. So the sentences are read out of
# the source and each is held against what these runs actually printed, which
# is the one comparison with nothing to guess about — a rendered message either
# reads as a wording or it does not. See D444.
#
# Only the codes these tables ask for. What a host is refused and what a
# ceiling says are somebody else's to hold, and they are held where they
# happen.
seen_said=$(python3 - "$scratch"/said <<'SEEING'
import glob
import re
import sys

LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# A conversion, in the shape C writes one. Reading `%zu` as `%z` and a `u`
# after it made every message with a size in it fail to read as itself.
CONVERSION = re.compile(
    r'%(?:%|[-+ #0]*[0-9*]*(?:\.[0-9*]+)?(?:hh|h|ll|l|j|z|t|L)?[a-zA-Z])')


def plain(piece):
    return (piece.replace('\\"', '"').replace('\\n', '\n')
            .replace('\\t', '\t').replace('\\\\', '\\'))


def sentences():
    """Every way each code can be worded, out of the source that says it."""
    out = {}
    for path in sorted(glob.glob('src/*.c')):
        text = re.sub(r'//[^\n]*', '', open(path).read())
        found = list(LITERAL.finditer(text))
        for i, one in enumerate(found):
            if not re.fullmatch(r'K0\d{3}', one.group(1)) or i + 1 >= len(found):
                continue
            words, j = found[i + 1].group(1), i + 1
            # Two literals with nothing but space between them are one string.
            while (j + 1 < len(found)
                   and text[found[j].end():found[j + 1].start()].strip() == ''):
                words += found[j + 1].group(1)
                j += 1
            out.setdefault(one.group(1), set()).add(plain(words))
            # And a colon between two of them is a message written as a choice,
            # which is two things one code can say. See D443.
            if (j + 1 < len(found)
                    and text[found[j].end():found[j + 1].start()].strip() == ':'):
                out[one.group(1)].add(plain(found[j + 1].group(1)))
    return out


def reads_as(form, line):
    """Whether a line printed reads as this wording, values and all."""
    pattern, at = '', 0
    for one in CONVERSION.finditer(form):
        pattern += re.escape(form[at:one.start()])
        pattern += re.escape('%') if one.group(0) == '%%' else '.*?'
        at = one.end()
    return re.fullmatch(pattern + re.escape(form[at:]), line, re.S) is not None


# Two nothing here can make happen, each beside the reason. A hole that is
# never closed runs to the end of the line, and a piece of text that runs to
# the end of the line is refused by the lexer before the parser reads a hole
# at all. A generic with no declaration behind it is a copy asked for from
# somewhere its own source is not, which is a fault's shape rather than a
# program's.
NOT_SEEN = (("K0207", "this hole is not closed"),
            ("K0343", "`%s` cannot be made here"))

SAID = re.compile(r'^(?:error|warning)\[(K0\d{3})\]: (.*)$', re.M)
printed = {}
for said in SAID.finditer(open(sys.argv[1], errors='replace').read()):
    printed.setdefault(said.group(1), set()).add(said.group(2).strip())

table = open('tools/check-commands.sh').read()
asked = set()
for kind in ('REFUSED', 'RUNNING'):
    body = re.search(r"<<'%s'\n(.*?)\n%s\n" % (kind, kind), table, re.S)
    if body is not None:
        asked |= set(re.findall(r'^(K0\d{3})\|', body.group(1), re.M))

says = sentences()
if not says or not printed or not asked:
    print("commands: nothing here reads as a table of refusals")
    raise SystemExit(1)

wrong = 0
held = 0
for code in sorted(asked):
    for form in sorted(says.get(code, ())):
        if (code, form) in NOT_SEEN:
            continue
        held += 1
        if not any(reads_as(form, line) for line in printed.get(code, ())):
            print("commands: %s can say `%s`, and nothing here has made it"
                  % (code, form))
            wrong = 1
print("%u wording(s) of %u refusal(s) seen, and 2 written down"
      % (held, len(asked)))
raise SystemExit(wrong)
SEEING
) || failed=1

# What a run says about a declaration, held to being so. `check --json` writes
# a `named` beside every function, shape, constant, case and bit, and what
# reads it is `check-dead.sh`, which holds a library to naming everything it
# declares. A compiler that said everything was named would make that rule
# pass over a library with a hole in it, and nothing said the flag was right —
# nor what a function takes, nor whether it is the host's. Each of them is
# written here with both answers in one file, because a flag that is always
# true and a flag that is right read the same until something is false.
# See D450.
mkdir -p "$scratch"/named
cat > "$scratch"/named/named.kest <<'KEST'
module named

extern fn Host.now() -> i32 no.alloc

struct Held {
    n: i32
}

struct Alone {
    n: i32
}

const READ: i32 = 1

const UNREAD: i32 = 2

enum Door {
    Shut
    Open
}

flags Marks: u8 {
    Seen
    Unseen
}

fn used(a: i32, b: text) -> i32 no.alloc {
    return a + len(b)
}

fn unused(a: i32) -> i32 {
    return a
}

fn main() -> i32 {
    let h = Held(READ)
    let d = Door.Shut
    let m = Marks.Seen
    return used(h.n, "x") + Host.now() + i32(u8(m)) - 2
}
KEST
if ! "$kest" check "$scratch"/named/named.kest --json 2>/dev/null </dev/null |
        python3 -c '
import json
import sys

said = json.load(sys.stdin)
WANTED = {
    "named.Host.now": (True, True, []),
    "named.used": (True, False, ["i32", "text"]),
    "named.unused": (False, False, ["i32"]),
    "named.main": (False, False, []),
}
SHAPES = {"named.Held": True, "named.Alone": False, "named.Door": True,
          "named.Marks": True}
HOLDS = {"named.READ": True, "named.UNREAD": False}
PARTS = {"Shut": True, "Open": False, "Seen": True, "Unseen": False}
wrong = 0
seen = 0
for one in said.get("functions", []):
    if one["name"] not in WANTED:
        continue
    seen += 1
    named, foreign, takes = WANTED[one["name"]]
    if one["named"] != named:
        print("%s says named is %s" % (one["name"], one["named"]))
        wrong = 1
    if one["foreign"] != foreign:
        print("%s says the host provides it: %s" % (one["name"], one["foreign"]))
        wrong = 1
    if one["parameters"] != takes:
        print("%s says it takes %s" % (one["name"], one["parameters"]))
        wrong = 1
for what, holds in (("types", SHAPES), ("constants", HOLDS)):
    for one in said.get(what, []):
        if one["name"] not in holds:
            continue
        seen += 1
        if one["named"] != holds[one["name"]]:
            print("%s says named is %s" % (one["name"], one["named"]))
            wrong = 1
        for part in one.get("cases", []) + one.get("bits", []):
            seen += 1
            if part["named"] != PARTS.get(part["name"]):
                print("%s.%s says named is %s"
                      % (one["name"], part["name"], part["named"]))
                wrong = 1
if seen != 14:
    print("a run said %u of the fourteen things this asks about" % seen)
    wrong = 1
raise SystemExit(wrong)
'; then
    complain "check: what a run says about a declaration is not what is so"
fi

# And a file with no `module` line, which only another file can find out: a
# name has nowhere to live until a file says where it lives, and the file that
# imports it is where that is met.
mkdir -p "$scratch"/refused/nameless "$scratch"/refused/pack
cat > "$scratch"/refused/pack/helper.kest <<'KEST'
fn wide(n: i32) -> i32 {
    return n
}
KEST
cat > "$scratch"/refused/nameless/main.kest <<'KEST'
module nameless.main

import pack.helper

fn main() -> i32 {
    return helper.wide(1)
}
KEST
nameless=$("$kest" check "$scratch"/refused/nameless/main.kest 2>&1 </dev/null)
case "$nameless" in
*"K0702"*"names no module"*) ;;
*)
    complain "check: a file with no module line said \
\`$(printf '%s' "$nameless" | head -1)\`"
    ;;
esac

# And two modules that put their names in the same place. Where a module's
# names go is the program's rather than the file's — `text.own` is one entry
# however many modules end in `text` — so a file importing one of them would
# find the other's names without asking for them. It takes two files and one of
# them is the library's, which is why it is asked here rather than in the table
# above.
mkdir "$scratch"/refused/twice
cat > "$scratch"/refused/twice/text.kest <<'KEST'
module text

fn own() -> i32 {
    return 1
}
KEST
cat > "$scratch"/refused/twice/main.kest <<'KEST'
module main

import text
import std.text

fn main() -> i32 {
    return text.own() - 1
}
KEST
shared=$("$kest" check "$scratch"/refused/twice/main.kest 2>&1 </dev/null)
case "$shared" in
*"K0328"*"both put their names under"*) ;;
*)
    complain "check: two modules under one name said \
\`$(printf '%s' "$shared" | head -1)\`"
    ;;
esac

# A value written where a statement belongs is a `return` with the word left
# off, and the function is told exactly that. What the arms are measured
# against is then what the function gives back rather than nothing, so `none`
# has a type there: a reader who forgot the word is told what to write and not
# also told that what they meant to give back is not a value at all. Asked for
# by what is missing, because a message that should not be said is held by
# nothing else.
mkdir "$scratch"/refused/forgot
cat > "$scratch"/refused/forgot/forgot.kest <<'KEST'
fn pick(a: i32) -> i32? {
    if a > 0 -> a else -> none
}

fn main() -> i32 {
    return 0
}
KEST
forgot=$("$kest" check "$scratch"/refused/forgot/forgot.kest 2>&1 </dev/null)
case "$forgot" in
*"K0322"*)
    complain "check: a value where a statement belongs was told about none as well"
    printf '%s\n' "$forgot" | sed 's/^/    /' | head -4
    ;;
esac

# And a copy of a generic struct, which exists because something asked for it:
# asking for it is naming it. Without that a program that declares one of its
# own and uses it is warned that nothing names the copy it has just made — and
# no file in this tree can show it, because the one generic struct here is the
# library's and that warning is about the file that was named.
mkdir "$scratch"/refused/copy
cat > "$scratch"/refused/copy/copy.kest <<'KEST'
struct Box<T> {
    it: T
}

fn main() -> i32 {
    let b: Box<i32> = Box(1)
    return b.it - 1
}
KEST
copied=$("$kest" check "$scratch"/refused/copy/copy.kest 2>&1 </dev/null)
case "$copied" in
*"K0509"*)
    complain "check: a copy of a generic was said to be named by nothing"
    printf '%s\n' "$copied" | sed 's/^/    /' | head -4
    ;;
esac

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
greeting="$scratch"/aside/aside.kest
answered=$("$kest" call "$greeting" aside.greet world 2>/dev/null </dev/null)
if [ "$answered" != "5" ]; then
    complain "call: what a program said is on the answer's stream: \
\`$answered\`"
fi
beside=$("$kest" call "$greeting" aside.greet world 2>&1 >/dev/null </dev/null)
if [ "$beside" != "hello world" ]; then
    complain "call: what a program said while it ran is not beside the \
answer: \`$beside\`"
fi

# And the other two, which answer with different things: what `run` answers
# with is what the program said, so that stays where a reader looks, and what
# `tick` answers with is a frame's cost, which a program writing into the
# middle of would spoil the same way.
ran=$("$kest" run "$greeting" 2>/dev/null </dev/null)
if [ "$ran" != "hello x" ]; then
    complain "run: what a program said is not what this answered: \`$ran\`"
fi
ticked=$("$kest" tick "$greeting" 2 2>/dev/null </dev/null)
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
worlds="$scratch"/crossed/world.kest
if ! python3 - "$kest" "$carrying" "$worlds" <<'NOTES' >"$scratch"/carried-notes 2>&1
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

# Counted again rather than counted on: `at` is set two thousand lines above
# this and anything between here and there that walks something is holding one
# of them. That is what a sweep read from the wrong place looks like, and it
# looks like nothing. See D533.
read_back=0
for file in "$@"; do
    read_back=$((read_back + 1))
    mine="$sweeps/$(printf %04d $read_back)"
    if [ ! -f "$mine" ]; then
        complain "check: the sweep of \`$file\` was written to somewhere this \
is not reading"
        continue
    fi
    if [ -s "$mine" ]; then
        cat "$mine"
        failed=1
    fi
done
# And that every one of them was read. What went wrong here once was a reading
# that started from the wrong number and found nothing, which is a check saying
# less rather than a check failing — the worse of the two, because what it
# looks like is everything being fine. See D534.
# The four digits and not what a sweep wrote beside them: `sweep_one` keeps
# what a run said on its error stream in `$mine.err`, which is a file in the
# same place and not a sweep.
swept=$(ls "$sweeps" | grep -vc "[.]err")
if [ "$read_back" -ne "$swept" ]; then
    complain "check: $swept file(s) were swept and $read_back were read back"
fi
rm -rf "$carried_said"

rm -f "$scratch"/cmd-err
if [ $failed -eq 0 ]; then
    echo "every command does something on $# file(s), and $seen_said"
else
    printf '%s\n' "$seen_said" | grep -v "seen, and" | sed '/^$/d'
fi
exit $failed
