#!/bin/sh
# What a formatter has to be true of: its output parses, means the same thing,
# formatting it again changes nothing, and a file it cannot read is left as it
# was found. And what this tree has to be true of: every file in it is already
# in the one form, because a language with one form is written in it.
#
# A file's path has to match what it calls itself for its imports to resolve,
# so the comparison is done in place: the file is formatted where it is, read,
# and put back.
set -u

# A scratch of this run's own. Two of these run at once when the backstops put
# one out of order while another is being asked, and fixed names in `/tmp` are
# two runs writing to one file.
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
kest=./kest
failed=0
backup="$scratch"/fmt-backup

# What this was given. A check that reads the files it is handed passes when it
# is handed none: the loop runs no times and the count at the end is nought,
# which reads like a success. Nothing in the tree is an empty list, so this is
# only ever a caller that lost its own.
if [ "$#" -eq 0 ]; then
    echo "formatting: nothing was given to look at"
    exit 1
fi

# The same, as the compiler reads it. What a comment is is the lexer's to say,
# so this is the reading the comparison rests on, and the one above is the
# second opinion that says when it has stopped seeing anything.
comments() {
    "$kest" lex "$1" --json 2>/dev/null </dev/null | python3 -c '
import json
import sys

for one in json.load(sys.stdin).get("comments", []):
    print(one["text"].rstrip())
'
}

# Where each comment ended up, held to where it belongs. Said in tokens rather
# than in lines, because every line moves: a comment sits above a token, and
# which token that is is the whole of where it is.
where() {
    python3 - "$kest" "$1" "$2" <<'WHERE'
import json
import subprocess
import sys

kest, was_path, now_path = sys.argv[1], sys.argv[2], sys.argv[3]


def read(path):
    ran = subprocess.run([kest, "lex", path, "--json"], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL)
    said = json.loads(ran.stdout)
    tokens = [one for one in said["tokens"] if one["kind"] != "end of line"]
    written = open(path).read().split("\n")
    out = []
    for one in said["comments"]:
        at = (one["line"], one["column"])
        # And that it is where it says it is. Everything below works out where
        # a comment sits from the line and the column it is reported at, and a
        # walk that reported the same place for every one of them would be a
        # comparison of nothing against nothing. See D449.
        holds = (written[one["line"] - 1][one["column"] - 1:]
                 if 0 < one["line"] <= len(written) else "")
        if not holds.startswith(one["text"]):
            print("a comment says it is at %u:%u, where `%s` is"
                  % (one["line"], one["column"], holds[:40]))
            raise SystemExit(1)
        # How many tokens are before it, which is where it sits in the stream.
        above = sum(1 for t in tokens if (t["line"], t["column"]) < at)
        first = next((i for i, t in enumerate(tokens)
                      if t["line"] == one["line"]), None)
        trails = any(t["line"] == one["line"] and t["column"] < one["column"]
                     for t in tokens)
        # What was written after code on a line was written about what is on
        # that line, so it belongs no later than the first thing there. What
        # was alone on its line belongs no later than where it already was.
        # Earlier than that is allowed and happens: a thing the author wrote
        # over several lines is printed on one, and a comment from inside it
        # comes out above the whole.
        out.append({"text": one["text"].rstrip(), "above": above,
                    "belongs": first if trails else above})
    return [t["text"] for t in tokens], out


was_tokens, was = read(was_path)
now_tokens, now = read(now_path)
failed = 0
if was_tokens != now_tokens:
    print("the tokens changed, so where a comment sits cannot be compared")
    failed = 1
elif len(was) != len(now):
    print("%u comment(s) became %u" % (len(was), len(now)))
    failed = 1
else:
    for before, after in zip(was, now):
        if after["above"] > before["belongs"]:
            print("a comment moved past what it was written about: %s"
                  % before["text"][:44])
            print("    belongs above `%s`, came out above `%s`"
                  % (was_tokens[before["belongs"]]
                     if before["belongs"] < len(was_tokens) else "the end",
                     now_tokens[after["above"]]
                     if after["above"] < len(now_tokens) else "the end"))
            failed = 1
sys.exit(failed)
WHERE
}

# The same file written badly, formatted back. Every file here is already in
# the one form, so formatting one changes nothing and the comparisons above it
# compare a file with itself: what they can catch is the formatter ceasing to
# be a no-op, and not much else. This roughs the file up first — a line ended
# wherever one may end and carry on, every line at a different indent, a space
# left at the end of each, every blank line doubled — and requires the one form
# of that to be the file, byte for byte. None of those four is part of a
# program: where the breaks go is the form's to decide, indentation is not read
# here, space nobody can see is not something anybody wrote, and one blank line
# is what any number of them come back as. Which tokens a line may carry on
# after is asked of a run, so a language that gains one gains it here too.
rough() {
    python3 -c '
import json
import subprocess
import sys

kest, path = sys.argv[1], sys.argv[2]

# Where a line may end and carry on, asked of a run rather than worked out: it
# is the one thing about a token that cannot be read off the token, and a
# second copy of the rule here would be a second copy to keep right.
ran = subprocess.run([kest, "lex", path, "--json"], capture_output=True,
                     text=True, stdin=subprocess.DEVNULL)
said = json.loads(ran.stdout)
# On every line, comments included. What is written inside something that
# comes out on one line was written about that thing, so a comment left at the
# end of a statement broken in two belongs above the statement — which is
# where it was when the statement was one line.
breaks = {}
for token in said["tokens"]:
    if token["carries"] and token["kind"] != "end of line":
        breaks.setdefault(token["line"], []).append(
            token["column"] - 1 + len(token["text"]))

broken = []
for i, line in enumerate(open(path).read().split("\n"), 1):
    at = 0
    for column in breaks.get(i, []):
        broken.append(line[at:column])
        at = column
    broken.append(line[at:])

lines = []
for i, line in enumerate(broken):
    if line.strip() == "":
        lines.append("")
        lines.append("")
        continue
    # A space at the end of every line, comments included: what is written in
    # a comment is kept, and a space nobody can see is not something anybody
    # wrote.
    lines.append(" " * (((i * 7) % 5) * 2) + line.strip() + " ")
sys.stdout.write("\n".join(lines))
' "$kest" "$1"
}

# What was said in a file, one comment a line. A `//` inside a string begins
# nothing, so the strings are stepped over first — the same rule the formatter
# reads a file by, and the reason this is not a search for two slashes.
said() {
    python3 -c '
import sys

QUOTE = chr(34)
text = open(sys.argv[1]).read()
at = 0
while at < len(text):
    if text[at] == QUOTE:
        # A hole may hold a string of its own, so the quote that closes this
        # one is the one found outside every brace.
        depth = 0
        at += 1
        while at < len(text):
            if text[at] == "\\":
                at += 1
            elif text[at] == "{":
                depth += 1
            elif text[at] == "}" and depth > 0:
                depth -= 1
            elif text[at] == QUOTE and depth == 0:
                break
            at += 1
        at += 1
        continue
    if text.startswith("//", at):
        end = text.find("\n", at)
        end = len(text) if end < 0 else end
        print(text[at:end].rstrip())
        at = end
        continue
    at += 1
' "$1"
}

for file in "$@"; do
    if ! "$kest" fmt "$file" > "$scratch"/fmt-1 2>/dev/null; then
        continue
    fi
    # A language with one form is written in it. Nothing held this before, and
    # three files had drifted out of it — two of them by being written before
    # the formatter learned what to do with the line they hold.
    if ! cmp -s "$scratch"/fmt-1 "$file"; then
        echo "not in the one form: $file"
        failed=1
    fi
    if ! "$kest" fmt "$scratch"/fmt-1 > "$scratch"/fmt-2 2>/dev/null; then
        echo "output does not format: $file"
        failed=1
        continue
    fi
    if ! cmp -s "$scratch"/fmt-1 "$scratch"/fmt-2; then
        echo "not idempotent: $file"
        failed=1
    fi

    # A file that does not parse has no tree to compare.
    if ! "$kest" parse "$file" > "$scratch"/tree-1 2>/dev/null; then
        continue
    fi

    # On a copy rather than on the file. `fmt -w` is the only thing in this
    # project that writes over somebody's source, and a check that does it to
    # the tree is a check nothing else can run beside: everything here reads
    # these files.
    cp "$file" "$backup" || exit 1
    "$kest" fmt -w "$backup" > /dev/null 2>&1
    "$kest" parse "$backup" > "$scratch"/tree-2 2>/dev/null

    if ! cmp -s "$scratch"/tree-1 "$scratch"/tree-2; then
        echo "tree changed: $file"
        failed=1
    fi

    # And the same file written badly comes back as this one, exactly.
    rough "$file" > "$scratch"/rough.kest
    if ! "$kest" fmt "$scratch"/rough.kest > "$scratch"/rough-out 2>&1; then
        echo "roughed up, it does not format: $file"
        sed 's/^/    /' "$scratch"/rough-out | head -2
        failed=1
    elif ! cmp -s "$scratch"/rough-out "$file"; then
        echo "roughed up, it does not come back: $file"
        diff "$file" "$scratch"/rough-out | sed 's/^/    /' | head -4
        failed=1
    fi

    # And every comment is still there, in the order it was written. The tree
    # says nothing about them: a formatter that dropped one would keep every
    # promise above this and lose what a reader was told.
    said "$file" > "$scratch"/said-1
    said "$scratch"/fmt-1 > "$scratch"/said-2
    # Two readings of what a comment is: this one, and the compiler's. Both are
    # compared, and they are held to each other word for word. What was here
    # counted the compiler's and compared its own, so a comment the compiler
    # saw and this reading did not was counted and never looked at — the two
    # numbers agreeing says nothing about the two lists being the same list.
    # The compiler's reading is the one that decides what a comment is; this
    # one is here to see it go quiet.
    comments "$file" > "$scratch"/theirs-1
    comments "$scratch"/fmt-1 > "$scratch"/theirs-2
    if ! cmp -s "$scratch"/said-1 "$scratch"/theirs-1; then
        echo "read a different comment from the compiler: $file"
        diff "$scratch"/said-1 "$scratch"/theirs-1 | sed 's/^/    /' | head -4
        failed=1
    fi
    if ! cmp -s "$scratch"/said-1 "$scratch"/said-2 ||
       ! cmp -s "$scratch"/theirs-1 "$scratch"/theirs-2; then
        echo "comments changed: $file"
        failed=1
    fi
done

rm -f "$backup"

# A file nobody has formatted yet, with a comment in every place one can be
# written: at the end of a line, inside a signature, inside the value of a
# match arm, in an empty block, and after the last statement. Every file in
# this tree is already in the one form, so none of them is this.
commented="$scratch"/fmt-commented.kest
cat > "$commented" <<'EOF'
module commented

enum Door {
    Shut // trailing a case
    Open(i32)
}

fn act(d: Door) -> i32 { // what it does
    return match d {
        Shut -> 0 // trailing an arm
        Open(w) ->
            // the width matters
            w
    }
}

fn quiet() {
    // nothing to do yet
}

fn main() -> i32 {
    let x = act(Door.Open(1)) // one open door
    // a string may hold two slashes that begin nothing
    let where = "http://kest" // and a comment may follow one
    // and a quote it wrote itself, which does not end it
    let quoted = "a \" // not a comment"
    // and a hole holding a string of its own, whose quotes are not this one's
    let held = "{act(Door.Shut)}: {"// still not a comment"}"
    // the last thing
    return x - len(where) + len(quoted) + len(held) - 35
}
EOF
if ! "$kest" fmt "$commented" > "$scratch"/fmt-commented-1 2>/dev/null; then
    echo "the file with comments in it does not format"
    failed=1
else
    said "$commented" > "$scratch"/commented-1
    said "$scratch"/fmt-commented-1 > "$scratch"/commented-2
    if ! cmp -s "$scratch"/commented-1 "$scratch"/commented-2; then
        echo "comments changed: a file nobody had formatted"
        failed=1
    fi
    if ! "$kest" run "$scratch"/fmt-commented-1 >/dev/null 2>&1 </dev/null; then
        echo "the file with comments in it stopped running once formatted"
        failed=1
    fi
    # And every one of them still above the thing it was written about, which
    # is the half of keeping a comment that comparing the words does not say:
    # the same list in a different order of things is the same list. What was
    # written after code on a line was written about what is on that line, so
    # it belongs above the first thing there; what was alone on its line
    # belongs above whatever it was already above. A comment left where the
    # thing it was about used to be reads as a comment about the next thing.
    where "$commented" "$scratch"/fmt-commented-1 || failed=1
fi
rm -f "$commented" "$scratch"/fmt-commented-1

# A comment in every place a file offers, found rather than thought of. The
# file with comments in it above is written by hand, and what decides whether
# a place is on it is whoever last thought of one — which is how a comment
# written on a closing brace went unasked about for as long as there has been
# a formatter. So this takes a file that uses most of the grammar and writes
# one variant per line with a comment at the end of that line, and one with a
# comment on its own line above it, and holds every one of them to coming back
# with that comment above the thing it was written about.
#
# It found ten places at once: every closing brace in the language. A comment
# written on the line of a `}` came out above whatever followed the block —
# the next declaration, the next statement, or the end of the file.
places="$scratch"/fmt-places.kest
cat > "$places" <<'BASE'
module places

import std.io
import std.math

const LIMIT: i32 = 10

extern fn Engine.decide(health: i32) -> i32 no.alloc

struct Point {
    x: i32
    y: i32
}

enum Door {
    Shut
    Open(i32)
}

flags State: u8 {
    Moving
    Hurt
}

fn width(d: Door) -> i32 {
    return match d {
        Shut -> 0
        Open(w) -> w
    }
}

fn nearest(steps: [i32], want: i32) -> i32? {
    for i in 0..len(steps) {
        if steps[i] < want {
            continue
        }
        if steps[i] == want {
            return i
        }
        break
    }
    return none
}

fn walk(p: Point, times: i32) -> i32 {
    let total = 0
    let open = true
    let shut = false
    let steps: [i32] = array()
    defer push(steps, 0)
    for i in 0..times {
        push(steps, i)
    }
    while total < LIMIT {
        total += math.abs(p.x) + p.y + Engine.decide(1)
    }
    if open && !shut {
        total = total - 1
    } else {
        total = 0
    }
    return total + len(steps)
}

fn hurt(s: State) -> bool no.alloc {
    return s & State.Hurt == State.Hurt
}

fn main() -> i32 {
    let here = Point(1, 2)
    let moving = State.Moving
    let said = "a line"
    if let found = nearest(array(), 1) {
        return found
    }
    if hurt(moving) {
        return 1
    }
    io.print("{width(Door.Open(2))} wide, {walk(here, 3)} walked")
    // A statement the one form writes over several lines, so that a comment
    // put at the end of one of them is a comment inside a statement rather
    // than after one.
    let held = math.clamp(
        walk(here, 3) + width(Door.Open(1)) + len(said),
        LIMIT - LIMIT,
        LIMIT + LIMIT
    )
    if held < 0 {
        return 1
    }
    return walk(here, 3) + width(Door.Open(1)) + len(said) - 21
}
BASE
everywhere=$(python3 - "$kest" "$places" "$scratch" <<'PLACES'
import json
import re
import subprocess
import sys

kest, base_path, scratch = sys.argv[1], sys.argv[2], sys.argv[3]
with_it = scratch + "/fmt-places-one.kest"
formatted = scratch + "/fmt-places-two.kest"


def read(path):
    ran = subprocess.run([kest, "lex", path, "--json"], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL)
    if ran.returncode != 0:
        return None, None
    said = json.loads(ran.stdout)
    tokens = [t for t in said["tokens"] if t["kind"] != "end of line"]
    out = []
    for c in said["comments"]:
        at = (c["line"], c["column"])
        above = sum(1 for t in tokens if (t["line"], t["column"]) < at)
        first = next((i for i, t in enumerate(tokens)
                      if t["line"] == c["line"]), None)
        trails = any(t["line"] == c["line"] and t["column"] < c["column"]
                     for t in tokens)
        out.append({"above": above, "belongs": first if trails else above})
    return [t["text"] for t in tokens], out


def answer(path):
    ran = subprocess.run([kest, "run", path], capture_output=True, text=True,
                         stdin=subprocess.DEVNULL)
    return ran.returncode, ran.stdout


# What the file does, which is the one thing about it that does not go through
# the tree. The tree is what says a formatted file means the same, and it is
# also what the formatter prints from, so both sides of that comparison agree
# about anything the tree cannot hold. Running it agrees with nobody.
was_answer = answer(base_path)


def wrong(lines):
    open(with_it, "w").write("\n".join(lines))
    was_tokens, was = read(with_it)
    if was_tokens is None:
        return "does not lex"
    ran = subprocess.run([kest, "fmt", with_it], capture_output=True, text=True,
                         stdin=subprocess.DEVNULL)
    if ran.returncode != 0:
        why = ran.stderr.strip().splitlines()
        return "fmt refused: %s" % (why[0][:70] if why else "saying nothing")
    open(formatted, "w").write(ran.stdout)
    now_tokens, now = read(formatted)
    if now_tokens != was_tokens:
        return "the tokens changed"
    if len(now) != len(was):
        return "%u comment(s) became %u" % (len(was), len(now))
    for before, after in zip(was, now):
        if after["above"] > before["belongs"]:
            return ("belongs above `%s`, came out above `%s`"
                    % (was_tokens[before["belongs"]]
                       if before["belongs"] < len(was_tokens) else "the end",
                       now_tokens[after["above"]]
                       if after["above"] < len(now_tokens) else "the end"))
    for what, path in (("with the comment in it", with_it),
                       ("once formatted", formatted)):
        said = answer(path)
        if said != was_answer:
            return ("%s it answers %r and says %r, where it answered %r and "
                    "said %r" % (what, said[0], said[1][:40], was_answer[0],
                                 was_answer[1][:40]))
    return None


base_text = open(base_path).read()
base = base_text.split("\n")
tried = 0
failed = 0

# And the file has to offer every place there is, which is decided by what it
# uses of the language rather than by what somebody remembered to put in it. A
# keyword nothing here writes is a construct nothing here puts a comment in, so
# the keywords are read from the lexer and every one of them has to be in this
# file. A pattern that stops matching finds nothing and nothing agrees with
# everything, so an empty list is a failure and not a pass.
keywords = re.findall(r'\{"([a-z]+)", KEST_TOK_', open("src/lexer.c").read())
if not keywords:
    print("nothing in the lexer is where the keywords are read from",
          file=sys.stderr)
    failed = 1

# And what a file can hold, which is not the same list. `flags` declares a type
# where a declaration begins and is a name everywhere else, so it is no keyword
# and holding this file to the keywords does not reach it. What the words are
# is asked of a run rather than read out of the source: it is the list a reader
# is given when a file holds something else, so it is the list that is true.
stray = scratch + "/fmt-places-stray.kest"
open(stray, "w").write("what\n")
told = subprocess.run([kest, "check", stray], capture_output=True, text=True,
                      stdin=subprocess.DEVNULL)
holds = re.findall(r"`([a-z ]+)`", (told.stdout + told.stderr)
                   .partition("a file holds")[2].partition("\n")[0])
if not holds:
    print("nothing a run says is where the declarations are read from",
          file=sys.stderr)
    failed = 1

words = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", base_text))
wanted = set(keywords)
for phrase in holds:
    wanted.update(phrase.split())
for keyword in sorted(wanted):
    if keyword not in words:
        print("the file a comment is put in every place of does not use `%s`"
              % keyword, file=sys.stderr)
        failed = 1
for i, line in enumerate(base):
    if line.strip() == "":
        continue
    trailing = list(base)
    trailing[i] = line + " // probe"
    indent = line[:len(line) - len(line.lstrip())]
    above = list(base)
    above.insert(i, indent + "// probe")
    for what, lines in (("after line %u" % (i + 1), trailing),
                        ("above line %u" % (i + 1), above)):
        tried += 1
        said = wrong(lines)
        if said is not None:
            # Said with words of its own rather than as a place and a
            # colon. A sentence that is two blanks and a mark is one nothing
            # can be shown to have caused: what a hole quotes to say it was
            # caught would be almost all of it what somebody else wrote.
            # See D469.
            print("a comment %s was not kept as it was written: %s"
                  % (what, said), file=sys.stderr)
            failed = 1
print(tried)
sys.exit(failed)
PLACES
) || failed=1
rm -f "$places" "$scratch"/fmt-places-one.kest "$scratch"/fmt-places-two.kest

# A file bigger than the numbers the formatter used to carry: more comments
# than the run it kept them in, and a chain longer than the one it collected.
# Both were quiet — the comments past the end were dropped and the chain past
# the end came out in a shape nobody asked for — and no file in this tree is
# either.
big="$scratch"/fmt-big.kest
{
    echo "module big"
    echo
    i=0
    while [ $i -lt 4200 ]; do
        echo "// said $i"
        i=$((i + 1))
    done
    echo "fn main() -> i32 {"
    printf "    let n = 1"
    i=1
    while [ $i -lt 40 ]; do
        printf " + 1"
        i=$((i + 1))
    done
    echo
    echo "    return n - 40"
    echo "}"
} > "$big"
if ! "$kest" fmt "$big" > "$scratch"/fmt-big-1 2>/dev/null; then
    echo "the big file does not format"
    failed=1
else
    said "$big" > "$scratch"/said-1
    said "$scratch"/fmt-big-1 > "$scratch"/said-2
    if ! cmp -s "$scratch"/said-1 "$scratch"/said-2; then
        echo "comments changed: a file with more of them than fitted"
        failed=1
    fi
    if ! "$kest" fmt "$scratch"/fmt-big-1 > "$scratch"/fmt-big-2 2>/dev/null ||
       ! cmp -s "$scratch"/fmt-big-1 "$scratch"/fmt-big-2; then
        echo "not idempotent: a file with a chain longer than the line"
        failed=1
    fi
    if ! "$kest" run "$scratch"/fmt-big-1 >/dev/null 2>&1 </dev/null; then
        echo "the big file stopped running once formatted"
        failed=1
    fi
fi
rm -f "$big" "$scratch"/fmt-big-1 "$scratch"/fmt-big-2

# `--check` is the one a build runs: it names what it would rewrite, writes
# nothing, and answers with its status. Nothing in this tree had ever run it in
# anger, so all three of those were promises.
if [ $# -gt 0 ]; then
    # shellcheck disable=SC2086
    if ! out=$("$kest" fmt --check "$@" 2>&1); then
        echo "fmt --check: refused a tree that is in the one form"
        printf '%s\n' "$out" | head -3
        failed=1
    elif [ -n "$out" ]; then
        echo "fmt --check: named a file in a tree that is in the one form"
        printf '%s\n' "$out" | head -3
        failed=1
    fi
fi

crooked="$scratch"/fmt-crooked.kest
cat > "$crooked" <<'EOF'
module crooked
fn  main( )->i32 {
  let x=1
   return x-1 }
EOF
cp "$crooked" "$crooked.was" || exit 1
if "$kest" fmt --check "$crooked" > "$scratch"/fmt-named 2>&1; then
    echo "fmt --check: said nothing about a file that is not in the one form"
    failed=1
elif ! grep -q "$crooked" "$scratch"/fmt-named; then
    echo "fmt --check: refused without naming the file"
    failed=1
fi
if ! cmp -s "$crooked" "$crooked.was"; then
    echo "fmt --check: wrote the file it was only asked about"
    failed=1
fi
rm -f "$crooked" "$crooked.was" "$scratch"/fmt-named

# Lines longer than the one form allows, of every kind the formatter can break
# — a comparison whose operator is `>`, a match arm whose value goes onto a
# line of its own, a call whose arguments go one to a line, and an `if` that
# gives a value. No file in the tree has one of these, which is the whole
# reason the two mistakes found here lived as long as they did: a check reads
# the files there are.
#
# `>` is the one operator a line may end after, because `ref<Npc>` ends in one,
# so a comparison holding one stays on the line it is on however long that is:
# breaking after it gives two statements, and breaking before it ends the line
# on a value. And an arm whose value was put on a line of its own is two lines
# where it was one, so the arm after it looked a line further down than it was
# and gained a blank line every time the file was formatted again.
wide="$scratch"/fmt-wide.kest
cat > "$wide" <<'EOF'
enum Shape {
    Round(i32)
    Square(i32)
}

fn addingUpAllOfTheseNumbersTogether(first: i32, second: i32, third: i32) -> i32 {
    return first + second + third
}

fn deciding(what: Shape, which: i32) -> i32 {
    let picked = match what {
        Round(radius) -> addingUpAllOfTheseNumbersTogether(1000000, 20000, radius)
        Square(side) -> side
    }
    let chosen = if which > 0 -> addingUpAllOfTheseNumbersTogether(11111111, 2222, 3) else -> 0
    if 1000000000000000000000000.0 / 100000000000000000000.0 - 1.4142135 > 0.0001 {
        return picked + chosen
    }
    return 0
}

fn main() -> i32 {
    return deciding(Shape.Square(1), 1)
}
EOF
once="$scratch"/fmt-wide-once.kest
twice="$scratch"/fmt-wide-twice.kest
# What `fmt` says for itself is what this asks first. It reads back what it
# wrote before handing it over, so a formatter that broke one of these lines
# refuses here rather than printing something the next command chokes on — and
# the two steps under this one are what would catch it if that reading back
# were taken out, in different words, which is a check that has stopped
# holding what it says.
if ! "$kest" fmt "$wide" > "$once" 2>"$scratch"/fmt-wide-refused; then
    echo "fmt: refused lines longer than the one form allows"
    sed 's/^/    /' "$scratch"/fmt-wide-refused | head -2
    failed=1
elif ! "$kest" check "$once" > "$scratch"/fmt-wide-said 2>&1; then
    echo "fmt: what it made of a long line does not parse"
    sed 's/^/    /' "$scratch"/fmt-wide-said | head -3
    failed=1
elif ! "$kest" fmt "$once" > "$twice" 2>&1 || ! cmp -s "$once" "$twice"; then
    echo "fmt: what it made of a long line is not in the one form"
    diff "$once" "$twice" | sed 's/^/    /' | head -4
    failed=1
fi
rm -f "$wide" "$once" "$twice" "$scratch"/fmt-wide-said \
   "$scratch"/fmt-wide-refused

# A file written on a machine that ends its lines with two characters. The
# formatter reads it and writes the one form, which ends lines with one, so
# what it gives back is a file that differs everywhere — and then it has to be
# stable, or every run would differ again.
crlf="$scratch"/fmt-crlf.kest
printf 'module crlf\r\n\r\nfn main() -> i32 {\r\n    return 0\r\n}\r\n' > "$crlf"
if ! "$kest" fmt "$crlf" > "$scratch"/fmt-crlf-once 2>&1; then
    echo "fmt: refused a file whose lines end with two characters"
    failed=1
else
    # Written by `printf` rather than as a dollar-quote, which is a shell
    # this one is not: under `/bin/sh` those four characters are themselves,
    # so the sweep looked for a byte no file has and this could never have
    # said a word. See D465.
    returned=$(printf '\r')
    if grep -q "$returned" "$scratch"/fmt-crlf-once; then
        echo "fmt: kept a carriage return in the one form"
        failed=1
    fi
    cp "$scratch"/fmt-crlf-once "$crlf.once" || exit 1
    if ! "$kest" fmt "$crlf.once" > "$scratch"/fmt-crlf-twice 2>&1 ||
       ! cmp -s "$scratch"/fmt-crlf-once "$scratch"/fmt-crlf-twice; then
        echo "fmt: what it made of a file with two-character line ends is not "\
             "in the one form"
        failed=1
    fi
fi
rm -f "$crlf" "$crlf.once" "$scratch"/fmt-crlf-once "$scratch"/fmt-crlf-twice

# And a file from an older machine still, which ends its lines with the other
# one of the two. What that costs if a comment does not end there is the whole
# file: everything after the first `//` is one comment, and a program that
# says something is read as a file that declares nothing.
returns="$scratch"/fmt-returns.kest
printf '// a note\rfn main() -> i32 {\r    return 0\r}\r' > "$returns"
if ! "$kest" fmt "$returns" > "$scratch"/fmt-returns-out 2>&1; then
    echo "fmt: refused a file whose lines end with a carriage return"
    failed=1
elif ! grep -q "^// a note$" "$scratch"/fmt-returns-out ||
     ! grep -q "^fn main() -> i32 {$" "$scratch"/fmt-returns-out; then
    echo "fmt: lost what a file with carriage returns said"
    sed 's/^/    /' "$scratch"/fmt-returns-out | head -3
    failed=1
fi
rm -f "$returns" "$scratch"/fmt-returns-out

# A file it cannot read is one it must not write. `fmt -w` is the only thing
# in this project that replaces somebody's source, and half a program written
# over the whole of one deletes the other half.
broken="$scratch"/fmt-broken.kest
cat > "$broken" <<'EOF'
module broken

fn main() -> i32 {
    let n = (1 +
    return n
}
EOF
cp "$broken" "$broken.was" || exit 1
if "$kest" fmt -w "$broken" > /dev/null 2>&1; then
    echo "formatted a file that does not parse"
    failed=1
fi
if ! cmp -s "$broken" "$broken.was"; then
    echo "wrote over a file that does not parse"
    failed=1
fi
rm -f "$broken" "$broken.was"

# A name longer than a line. Nothing in this tree has one — every name here is
# short enough to read — and what a formatter does with a line it cannot make
# fit is a thing to decide rather than to discover: it breaks what can break
# and leaves what cannot. A name is one thing; breaking it in half makes a
# different name.
lengthy="$scratch"/fmt-lengthy.kest
huge=$(printf 'a%.0s' $(seq 90))
{
    printf 'module lengthy\n\n'
    printf 'fn %s(one: i32, two: i32, three: i32) -> i32 {\n' "$huge"
    printf '    return one + two + three\n}\n\n'
    printf 'fn main() -> i32 {\n    return %s(1, 2, 3) + %s(4, 5, 6)\n}\n' \
           "$huge" "$huge"
} > "$lengthy"
if ! "$kest" fmt "$lengthy" > "$scratch"/fmt-lengthy-once 2>&1; then
    echo "a file with a name longer than a line was not written"
    sed 's/^/    /' "$scratch"/fmt-lengthy-once | head -3
    failed=1
elif ! "$kest" fmt "$scratch"/fmt-lengthy-once > "$scratch"/fmt-lengthy-twice \
        2>&1 ||
     ! cmp -s "$scratch"/fmt-lengthy-once "$scratch"/fmt-lengthy-twice; then
    echo "a file with a name longer than a line is not in the one form"
    failed=1
else
    # Every line that is over the limit holds the name that cannot be broken,
    # and nothing else was left long because of it.
    over=$(awk -v name="$huge" 'length($0) > 80 && index($0, name) == 0' \
           "$scratch"/fmt-lengthy-once)
    if [ -n "$over" ]; then
        echo "a line that could have been broken was left long"
        printf '%s\n' "$over" | cut -c1-60 | sed 's/^/    /' | head -3
        failed=1
    fi
    # And what could break did: a list that does not fit goes one item to a
    # line, and a line it could never have fitted on is not a reason to stop
    # trying. Without this, a formatter that gives up when a line is over the
    # limit anyway writes the same file back and every other rule holds.
    if ! grep -qx '        1,' "$scratch"/fmt-lengthy-once; then
        echo "a list beside a name too long to break was left on one line"
        grep -n "$(printf '%s' "$huge" | cut -c1-20)" \
             "$scratch"/fmt-lengthy-once | cut -c1-60 | sed 's/^/    /' |
            head -3
        failed=1
    fi
fi
rm -f "$lengthy" "$scratch"/fmt-lengthy-once "$scratch"/fmt-lengthy-twice

# A file with nothing in it but a comment. Every other rule this holds is about
# what a declaration looks like, and a file with no declarations has none of
# them to be true of: a formatter that wrote nothing at all for one would parse
# the same, mean the same, and come out the same twice. What it may not do is
# lose what somebody wrote.
saying="$scratch"/fmt-saying.kest
printf '\n\n// what this file is for\n\n\n' > "$saying"
if ! "$kest" fmt "$saying" > "$scratch"/fmt-saying-once 2>&1; then
    echo "a file of nothing but a comment was not written"
    sed 's/^/    /' "$scratch"/fmt-saying-once | head -3
    failed=1
elif ! grep -qx '// what this file is for' "$scratch"/fmt-saying-once; then
    echo "a file of nothing but a comment came back without it"
    sed 's/^/    /' "$scratch"/fmt-saying-once | head -3
    failed=1
elif [ "$(wc -l < "$scratch"/fmt-saying-once)" -ne 1 ]; then
    echo "a file of nothing but a comment came back with more than it"
    cat -A "$scratch"/fmt-saying-once | sed 's/^/    /' | head -4
    failed=1
elif ! "$kest" fmt "$scratch"/fmt-saying-once > "$scratch"/fmt-saying-twice \
        2>&1 ||
     ! cmp -s "$scratch"/fmt-saying-once "$scratch"/fmt-saying-twice; then
    echo "a file of nothing but a comment is not in the one form"
    failed=1
fi
rm -f "$saying" "$scratch"/fmt-saying-once "$scratch"/fmt-saying-twice

rm -f "$scratch"/said-1 "$scratch"/said-2 "$scratch"/theirs-1 \
   "$scratch"/theirs-2 "$scratch"/rough.kest "$scratch"/rough-out

# What says the formatter kept the meaning is the tree the `parse` command
# prints: this check formats a file, prints the tree of what came back, and
# holds it to the tree of what went in. So everything that comparison is worth
# rests on the tree telling two programs apart, and nothing said it could. A
# tree that stopped printing the promise on a function would leave a formatter
# free to drop it, and every file here would still be called faithful.
#
# So: pairs of programs differing in one thing each, one thing of every kind a
# tree carries — a promise, a type, a name, an order, how a number was spelled,
# which operator, the shape of what runs. Both have to parse and the two trees
# have to differ. A pair that stops parsing is this check gone quiet, which is
# why that is a failure rather than something skipped. It is a sample, and what
# it is a sample of is the things a formatter could drop with nothing noticing.
pairs=$(python3 - "$kest" "$scratch" <<'TREES'
import subprocess
import sys

kest, scratch = sys.argv[1], sys.argv[2]
PAIRS = [
    ("the promise on a function",
     "fn f() -> i32 no.alloc {\n    return 1\n}",
     "fn f() -> i32 {\n    return 1\n}"),
    ("the type written on a `let`",
     "fn f() -> i32 {\n    let x: i32 = 1\n    return x\n}",
     "fn f() -> i32 {\n    let x = 1\n    return x\n}"),
    ("a name",
     "fn f(a: i32) -> i32 {\n    return a\n}",
     "fn f(b: i32) -> i32 {\n    return b\n}"),
    ("what a module calls itself",
     "module one\n\nfn f() -> i32 {\n    return 1\n}",
     "module two\n\nfn f() -> i32 {\n    return 1\n}"),
    ("the order of a struct's fields",
     "struct P {\n    x: i32\n    y: i32\n}\n\nfn f(p: P) -> i32 {"
     "\n    return p.x\n}",
     "struct P {\n    y: i32\n    x: i32\n}\n\nfn f(p: P) -> i32 {"
     "\n    return p.x\n}"),
    ("how a number was spelled",
     "fn f() -> f32 {\n    return 1.50\n}",
     "fn f() -> f32 {\n    return 1.5\n}"),
    ("which operator",
     "fn f(a: bool, b: bool) -> bool {\n    return a && b\n}",
     "fn f(a: bool, b: bool) -> bool {\n    return a || b\n}"),
    ("an escape inside text",
     "fn f() -> text {\n    return \"a\\nb\"\n}",
     "fn f() -> text {\n    return \"a\\tb\"\n}"),
    ("which way out of a loop",
     "fn f() -> i32 {\n    while true {\n        break\n    }\n   "
     " return 0\n}",
     "fn f() -> i32 {\n    while true {\n        continue\n    }\n"
     "    return 0\n}"),
    ("whether something waits until the end",
     "fn g() -> i32 {\n    return 0\n}\n\nfn f() -> i32 {\n    def"
     "er g()\n    return 0\n}",
     "fn g() -> i32 {\n    return 0\n}\n\nfn f() -> i32 {\n    g()"
     "\n    return 0\n}"),
    ("which name a hole in a string reads",
     "fn f() -> text {\n    let a = 1\n    let b = 2\n    return "
     "\"{a}{b}\"\n}",
     "fn f() -> text {\n    let a = 1\n    let b = 2\n    return "
     "\"{b}{a}\"\n}"),
    ("what an `if` does",
     "fn f(n: i32) -> i32 {\n    let a = 0\n    if n > 0 {\n      "
     "  a = 1\n    }\n    return a\n}",
     "fn f(n: i32) -> i32 {\n    let a = 0\n    if n > 0 {\n      "
     "  a = 2\n    }\n    return a\n}"),
    ("what an `else` does",
     "fn f(n: i32) -> i32 {\n    let a = 0\n    if n > 0 {\n      "
     "  a = 1\n    } else {\n        a = 2\n    }\n    return a\n}",
     "fn f(n: i32) -> i32 {\n    let a = 0\n    if n > 0 {\n      "
     "  a = 1\n    } else {\n        a = 3\n    }\n    return a\n}"),
    ("what an arm of a `match` does",
     "enum D {\n    A\n    B\n}\n\nfn f(d: D) -> i32 {\n    let a "
     "= 0\n    match d {\n        A {\n            a = 1\n        "
     "}\n        B {\n            a = 2\n        }\n    }\n    ret"
     "urn a\n}",
     "enum D {\n    A\n    B\n}\n\nfn f(d: D) -> i32 {\n    let a "
     "= 0\n    match d {\n        A {\n            a = 1\n        "
     "}\n        B {\n            a = 3\n        }\n    }\n    ret"
     "urn a\n}"),
    ("whether an answer may be nothing",
     "fn f() -> i32? {\n    return 1\n}",
     "fn f() -> i32 {\n    return 1\n}"),
    # And what each head of the tree carries beside itself. The fifteen
    # above were a sample; these are the rest, one for every span, type,
    # name and value the printer writes. Each differs in one place and no
    # other, which is what makes it a pair for that one thing: two
    # programs differing twice hold neither of the two. See D448.
    ("how many an array holds",
     "fn f(a: [i32; 2]) -> i32 {\n    return 0\n}",
     "fn f(a: [i32; 3]) -> i32 {\n    return 0\n}"),
    ("the promise on a function value",
     "fn f(g: fn(i32) -> i32 no.alloc) -> i32 {\n    return 0\n}",
     "fn f(g: fn(i32) -> i32) -> i32 {\n    return 0\n}"),
    ("the type of a field",
     "struct P {\n    x: i32\n}\n\nfn f() -> i32 {\n    return 0\n"
     "}",
     "struct P {\n    x: i64\n}\n\nfn f() -> i32 {\n    return 0\n"
     "}"),
    ("the type of a parameter",
     "fn f(a: i32) -> i32 {\n    return 0\n}",
     "fn f(a: i64) -> i32 {\n    return 0\n}"),
    ("the type on a constant",
     "const A: i32 = 1\n\nfn f() -> i32 {\n    return 0\n}",
     "const A: i64 = 1\n\nfn f() -> i32 {\n    return 0\n}"),
    ("the types a function takes",
     "fn f<T>(a: i32) -> i32 {\n    return 0\n}",
     "fn f<U>(a: i32) -> i32 {\n    return 0\n}"),
    ("the types a shape takes",
     "struct P<T> {\n    x: i32\n}\n\nfn f() -> i32 {\n    return "
     "0\n}",
     "struct P<U> {\n    x: i32\n}\n\nfn f() -> i32 {\n    return "
     "0\n}"),
    ("what a `match` chooses on",
     "enum D {\n    A\n}\n\nfn f(c: D, d: D) -> i32 {\n    return "
     "match c {\n        A -> 1\n    }\n}",
     "enum D {\n    A\n}\n\nfn f(c: D, d: D) -> i32 {\n    return "
     "match d {\n        A -> 1\n    }\n}"),
    ("what a `while` asks",
     "fn f(a: bool, b: bool) -> i32 {\n    while a {\n        retu"
     "rn 1\n    }\n    return 0\n}",
     "fn f(a: bool, b: bool) -> i32 {\n    while b {\n        retu"
     "rn 1\n    }\n    return 0\n}"),
    ("what a bit is called",
     "flags S: u8 {\n    A\n}\n\nfn f() -> i32 {\n    return 0\n}",
     "flags S: u8 {\n    B\n}\n\nfn f() -> i32 {\n    return 0\n}"),
    ("what a block on its own does",
     "fn f() -> i32 {\n    {\n        let x = 1\n    }\n    return"
     " 0\n}",
     "fn f() -> i32 {\n    {\n        let x = 2\n    }\n    return"
     " 0\n}"),
    ("what a call is called with",
     "fn g(n: i32) -> i32 {\n    return n\n}\n\nfn f() -> i32 {\n "
     "   return g(1)\n}",
     "fn g(n: i32) -> i32 {\n    return n\n}\n\nfn f() -> i32 {\n "
     "   return g(2)\n}"),
    ("what a case carries",
     "enum D {\n    A(i32)\n}\n\nfn f() -> i32 {\n    return 0\n}",
     "enum D {\n    A(i64)\n}\n\nfn f() -> i32 {\n    return 0\n}"),
    ("what a case is called",
     "enum D {\n    A\n}\n\nfn f() -> i32 {\n    return 0\n}",
     "enum D {\n    B\n}\n\nfn f() -> i32 {\n    return 0\n}"),
    ("what a choice is called",
     "enum D {\n    A\n}\n\nfn f() -> i32 {\n    return 0\n}",
     "enum E {\n    A\n}\n\nfn f() -> i32 {\n    return 0\n}"),
    ("what a constant is",
     "const A: i32 = 1\n\nfn f() -> i32 {\n    return 0\n}",
     "const A: i32 = 2\n\nfn f() -> i32 {\n    return 0\n}"),
    ("what a constant is called",
     "const A: i32 = 1\n\nfn f() -> i32 {\n    return 0\n}",
     "const B: i32 = 1\n\nfn f() -> i32 {\n    return 0\n}"),
    ("what a field is read out of",
     "struct P {\n    x: i32\n}\n\nfn f(p: P, q: P) -> i32 {\n    "
     "return p.x\n}",
     "struct P {\n    x: i32\n}\n\nfn f(p: P, q: P) -> i32 {\n    "
     "return q.x\n}"),
    ("what a file reads",
     "module one\n\nimport std.io\n\nfn f() -> i32 {\n    return 0"
     "\n}",
     "module one\n\nimport std.text\n\nfn f() -> i32 {\n    return"
     " 0\n}"),
    ("what a function gives back",
     "fn f() -> i32 {\n    return 0\n}",
     "fn f() -> i64 {\n    return 0\n}"),
    ("what a function is called",
     "fn one() -> i32 {\n    return 0\n}",
     "fn two() -> i32 {\n    return 0\n}"),
    ("what a function value gives back",
     "fn f(g: fn(i32) -> i32) -> i32 {\n    return 0\n}",
     "fn f(g: fn(i32) -> i64) -> i32 {\n    return 0\n}"),
    ("what a function value takes",
     "fn f(g: fn(i32) -> i32) -> i32 {\n    return 0\n}",
     "fn f(g: fn(i64) -> i32) -> i32 {\n    return 0\n}"),
    ("what a generic type is",
     "struct A {\n    n: i32\n}\n\nfn f(w: store<A>) -> i32 {\n   "
     " return 0\n}",
     "struct A {\n    n: i32\n}\n\nfn f(w: ref<A>) -> i32 {\n    r"
     "eturn 0\n}"),
    ("what a generic type is made with",
     "struct A {\n    n: i32\n}\n\nfn f(w: store<A>) -> i32 {\n   "
     " return 0\n}",
     "struct A {\n    n: i32\n}\n\nfn f(w: store<B>) -> i32 {\n   "
     " return 0\n}"),
    ("what a minus is in front of",
     "fn f(a: i32, b: i32) -> i32 {\n    return -a\n}",
     "fn f(a: i32, b: i32) -> i32 {\n    return -b\n}"),
    ("what a name is called",
     "fn f() -> i32 {\n    let x = 1\n    return 1\n}",
     "fn f() -> i32 {\n    let y = 1\n    return 1\n}"),
    ("what a name is given",
     "fn f() -> i32 {\n    let x = 1\n    return 0\n}",
     "fn f() -> i32 {\n    let x = 2\n    return 0\n}"),
    ("what a named type is",
     "fn f() -> i32 {\n    let x: i32 = 1\n    return 0\n}",
     "fn f() -> i32 {\n    let x: i64 = 1\n    return 0\n}"),
    ("what a parameter is called",
     "fn f(a: i32) -> i32 {\n    return 0\n}",
     "fn f(b: i32) -> i32 {\n    return 0\n}"),
    ("what a run written down holds",
     "fn f() -> i32 {\n    let a: [i32; 2] = [1, 3]\n    return 0"
     "\n}",
     "fn f() -> i32 {\n    let a: [i32; 2] = [2, 3]\n    return 0"
     "\n}"),
    ("what a set of bits is called",
     "flags S: u8 {\n    A\n}\n\nfn f() -> i32 {\n    return 0\n}",
     "flags T: u8 {\n    A\n}\n\nfn f() -> i32 {\n    return 0\n}"),
    ("what a set of bits is written over",
     "flags S: u8 {\n    A\n}\n\nfn f() -> i32 {\n    return 0\n}",
     "flags S: u16 {\n    A\n}\n\nfn f() -> i32 {\n    return 0\n}"),
    ("what a statement works out",
     "fn g(n: i32) -> i32 {\n    return n\n}\n\nfn f() -> i32 {\n "
     "   g(1)\n    return 0\n}",
     "fn g(n: i32) -> i32 {\n    return n\n}\n\nfn f() -> i32 {\n "
     "   g(2)\n    return 0\n}"),
    ("what a struct is called",
     "struct P {\n    x: i32\n}\n\nfn f() -> i32 {\n    return 0\n"
     "}",
     "struct Q {\n    x: i32\n}\n\nfn f() -> i32 {\n    return 0\n"
     "}"),
    ("what a walk calls its place",
     "fn f(a: [i32]) -> i32 {\n    for i, x in a {\n        return"
     " 1\n    }\n    return 0\n}",
     "fn f(a: [i32]) -> i32 {\n    for j, x in a {\n        return"
     " 1\n    }\n    return 0\n}"),
    ("what a walk calls what it holds",
     "fn f(a: [i32]) -> i32 {\n    for x in a {\n        return 1"
     "\n    }\n    return 0\n}",
     "fn f(a: [i32]) -> i32 {\n    for y in a {\n        return 1"
     "\n    }\n    return 0\n}"),
    ("what a walk does",
     "fn f(a: [i32]) -> i32 {\n    for x in a {\n        return 1"
     "\n    }\n    return 0\n}",
     "fn f(a: [i32]) -> i32 {\n    for x in a {\n        return 2"
     "\n    }\n    return 0\n}"),
    ("what a walk walks",
     "fn f(a: [i32], b: [i32]) -> i32 {\n    for x in a {\n       "
     " return 1\n    }\n    return 0\n}",
     "fn f(a: [i32], b: [i32]) -> i32 {\n    for x in b {\n       "
     " return 1\n    }\n    return 0\n}"),
    ("what an `else if` asks",
     "fn f(a: bool, b: bool, c: bool) -> i32 {\n    return if a ->"
     " 1 else if b -> 2 else -> 3\n}",
     "fn f(a: bool, b: bool, c: bool) -> i32 {\n    return if a ->"
     " 1 else if c -> 2 else -> 3\n}"),
    ("what an `else` gives",
     "fn f(a: bool) -> i32 {\n    return if a -> 1 else -> 2\n}",
     "fn f(a: bool) -> i32 {\n    return if a -> 1 else -> 3\n}"),
    ("what an `if` asks",
     "fn f(a: bool, b: bool) -> i32 {\n    if a {\n        return "
     "1\n    }\n    return 0\n}",
     "fn f(a: bool, b: bool) -> i32 {\n    if b {\n        return "
     "1\n    }\n    return 0\n}"),
    ("what an `if` gives",
     "fn f(a: bool) -> i32 {\n    return if a -> 1 else -> 2\n}",
     "fn f(a: bool) -> i32 {\n    return if a -> 3 else -> 2\n}"),
    ("what an arm gives",
     "enum D {\n    A\n}\n\nfn f(d: D) -> i32 {\n    return match "
     "d {\n        A -> 1\n    }\n}",
     "enum D {\n    A\n}\n\nfn f(d: D) -> i32 {\n    return match "
     "d {\n        A -> 2\n    }\n}"),
    ("what an arm names",
     "enum D {\n    A(i32)\n}\n\nfn f(d: D) -> i32 {\n    return m"
     "atch d {\n        A(n) -> 1\n    }\n}",
     "enum D {\n    A(i32)\n}\n\nfn f(d: D) -> i32 {\n    return m"
     "atch d {\n        A(m) -> 1\n    }\n}"),
    ("what an array holds",
     "fn f(a: [i32]) -> i32 {\n    return 0\n}",
     "fn f(a: [i64]) -> i32 {\n    return 0\n}"),
    ("what an optional holds",
     "fn f(a: i32?) -> i32 {\n    return 0\n}",
     "fn f(a: i64?) -> i32 {\n    return 0\n}"),
    ("what is assigned to",
     "fn f(a: i32, b: i32) -> i32 {\n    a = 1\n    return 0\n}",
     "fn f(a: i32, b: i32) -> i32 {\n    b = 1\n    return 0\n}"),
    ("what is called",
     "fn g() -> i32 {\n    return 1\n}\n\nfn h() -> i32 {\n    ret"
     "urn 1\n}\n\nfn f() -> i32 {\n    return g()\n}",
     "fn g() -> i32 {\n    return 1\n}\n\nfn h() -> i32 {\n    ret"
     "urn 1\n}\n\nfn f() -> i32 {\n    return h()\n}"),
    ("what is indexed",
     "fn f(a: [i32], b: [i32]) -> i32 {\n    return a[0]\n}",
     "fn f(a: [i32], b: [i32]) -> i32 {\n    return b[0]\n}"),
    ("what is written between two holes",
     "fn f(a: i32) -> text {\n    return \"x{a}\"\n}",
     "fn f(a: i32) -> text {\n    return \"y{a}\"\n}"),
    ("what waits until the end",
     "fn g(n: i32) -> i32 {\n    return n\n}\n\nfn f() -> i32 {\n "
     "   defer g(1)\n    return 0\n}",
     "fn g(n: i32) -> i32 {\n    return n\n}\n\nfn f() -> i32 {\n "
     "   defer g(2)\n    return 0\n}"),
    ("where a count stops",
     "fn f() -> i32 {\n    for i in 0..2 {\n        return 1\n    "
     "}\n    return 0\n}",
     "fn f() -> i32 {\n    for i in 0..3 {\n        return 1\n    "
     "}\n    return 0\n}"),
    ("where something is indexed",
     "fn f(a: [i32]) -> i32 {\n    return a[0]\n}",
     "fn f(a: [i32]) -> i32 {\n    return a[1]\n}"),
    ("whether a `while` opens an optional",
     "fn f(g: i32?) -> i32 {\n    while let x = g {\n        retur"
     "n 1\n    }\n    return 0\n}",
     "fn f(g: i32?) -> i32 {\n    while let y = g {\n        retur"
     "n 1\n    }\n    return 0\n}"),
    ("whether an `if` opens an optional",
     "fn f(g: i32?) -> i32 {\n    if let x = g {\n        return 1"
     "\n    }\n    return 0\n}",
     "fn f(g: i32?) -> i32 {\n    if let y = g {\n        return 1"
     "\n    }\n    return 0\n}"),
    ("whether the host provides it",
     "extern fn Host.now() -> i32 no.alloc\n\nfn f() -> i32 {\n   "
     " return 0\n}",
     "fn now() -> i32 no.alloc {\n    return 0\n}\n\nfn f() -> i32"
     " {\n    return 0\n}"),
    ("which case an arm answers",
     "enum D {\n    A\n    B\n}\n\nfn f(d: D) -> i32 {\n    return"
     " match d {\n        A -> 1\n        else -> 2\n    }\n}",
     "enum D {\n    A\n    B\n}\n\nfn f(d: D) -> i32 {\n    return"
     " match d {\n        B -> 1\n        else -> 2\n    }\n}"),
    ("which field is read",
     "struct P {\n    x: i32\n    y: i32\n}\n\nfn f(p: P) -> i32 {"
     "\n    return p.x\n}",
     "struct P {\n    x: i32\n    y: i32\n}\n\nfn f(p: P) -> i32 {"
     "\n    return p.y\n}"),
    ("which host a name is under",
     "extern fn Host.now() -> i32 no.alloc\n\nfn f() -> i32 {\n   "
     " return 0\n}",
     "extern fn Clock.now() -> i32 no.alloc\n\nfn f() -> i32 {\n  "
     "  return 0\n}"),
    ("which of two a `bool` is",
     "fn f() -> bool {\n    return true\n}",
     "fn f() -> bool {\n    return false\n}"),
    ("which operator stands in front of a value",
     "fn f(a: i32) -> i32 {\n    return -a\n}",
     "fn f(a: i32) -> i32 {\n    return ~a\n}"),
    ("which side of an operator",
     "fn f(a: i32, b: i32, c: i32) -> i32 {\n    return a - b\n}",
     "fn f(a: i32, b: i32, c: i32) -> i32 {\n    return c - b\n}"),
    ("which side of an operator a name is on",
     "fn f(a: i32, b: i32, c: i32) -> i32 {\n    return a - b\n}",
     "fn f(a: i32, b: i32, c: i32) -> i32 {\n    return a - c\n}"),
    ("which way something is assigned",
     "fn f(a: i32) -> i32 {\n    a = 1\n    return a\n}",
     "fn f(a: i32) -> i32 {\n    a += 1\n    return a\n}"),
]


def tree(source, where):
    open(where, "w").write(source)
    ran = subprocess.run([kest, "parse", where], capture_output=True,
                         text=True, stdin=subprocess.DEVNULL)
    return ran.returncode, ran.stdout, ran.stderr


failed = 0
for what, one, other in PAIRS:
    was, said, why = tree(one, scratch + "/tree-one.kest")
    also, other_said, other_why = tree(other, scratch + "/tree-other.kest")
    if was != 0 or also != 0:
        told = (why or other_why).splitlines()
        # On the standard error, because what this hands back on the other one
        # is the count the last line of this check reads.
        print("a pair about %s does not parse: %s"
              % (what, told[0] if told else "nothing said"), file=sys.stderr)
        failed = 1
    elif said == other_said:
        print("two programs differing in %s have one tree" % what,
              file=sys.stderr)
        failed = 1
print(len(PAIRS))
sys.exit(failed)
TREES
) || failed=1
rm -f "$scratch"/tree-one.kest "$scratch"/tree-other.kest

if [ $failed -eq 0 ]; then
    echo "$# file(s) are in the one form, and are what the one form of the same file written badly is, which is faithful, keeps what was said, names what it would rewrite, refuses what it cannot read, and rests on a tree that tells $pairs pair(s) of programs apart, with a comment tried in each of $everywhere place(s) a file that uses every keyword and every kind of declaration offers"
fi
exit $failed
