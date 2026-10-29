#!/bin/sh
# A project under a path whose characters no system code page has -- a CJK
# directory under an emoji one -- made, checked, run, built and tested, and
# answered over by the language server, with the URIs written the way VS Code
# writes them. On Windows a narrow path is read in the process's code page,
# and the manifest the build embeds (`cmake/kest.manifest`) is what makes that
# UTF-8; a `kest.exe` without it is refused here before anything runs.
#
# And a project reached through a link into a directory whose name has an `a`
# with a bar over it, beside one named with the plain `a` that holds a
# dependency: a rename keeps to the project, which a path read in a code page
# without that character does not -- the two names come back as one.
#
#   tools/check-paths.sh [PYTHON]
set -u

build_dir=${KEST_BUILD:-out/dev}
case $build_dir in /*) ;; [A-Za-z]:*) ;; *) build_dir=$(pwd)/$build_dir ;; esac
python=${1:-python3}
suffix=
if [ -f "$build_dir/kest.exe" ]; then
    suffix=.exe
fi
kest=$build_dir/kest$suffix
if [ ! -f "$kest" ]; then
    echo "the command line is not built: $kest"
    exit 1
fi
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
# A path as Python on Windows reads one, which Git's shell does not write.
native() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s\n' "$1"
    fi
}

said=$("$python" - "$(native "$kest")" "$(native "$scratch")" <<'PY'
import json, os, re, subprocess, sys, urllib.parse

kest, top = sys.argv[1], os.path.abspath(sys.argv[2])
windows = os.name == "nt"
wrong = []

# The manifest, as the linker embedded it: the platform reads it out of the
# binary's resources, where it is the XML it was written as, with whatever
# prefixes the tool that merged it gave the elements.
if windows:
    with open(kest, "rb") as binary:
        carried = binary.read()
    if not re.search(rb"activeCodePage[^>]*>\s*UTF-8\s*<", carried):
        print("%s carries no manifest saying its code page is UTF-8, so a "
              "path is read in the system's" % kest)
        sys.exit(0)


def ran(words, where=None):
    return subprocess.run(words, cwd=where, capture_output=True, timeout=300)


def told(answer):
    return (answer.stdout + answer.stderr).decode("utf-8", "replace").strip()


def uri_of(path):
    path = path.replace("\\", "/")
    if re.match(r"[A-Za-z]:", path):
        path = "/" + path[0].lower() + path[1:]
    return "file://" + urllib.parse.quote(path, safe="/")


def same_path(uri, path):
    if not uri.startswith("file:///"):
        return False
    read = urllib.parse.unquote(uri[len("file://"):])
    if re.match(r"/[A-Za-z]:", read):
        read = read[1:]
    return (os.path.normcase(os.path.normpath(read)) ==
            os.path.normcase(os.path.normpath(path)))


def talk(messages):
    body = b""
    for one in messages:
        written = json.dumps(one).encode()
        body += b"Content-Length: %d\r\n\r\n" % len(written) + written
    answer = subprocess.run([kest, "lsp"], input=body, capture_output=True,
                            timeout=300)
    heard, published, at = {}, [], 0
    while True:
        head = answer.stdout.find(b"Content-Length: ", at)
        blank = answer.stdout.find(b"\r\n\r\n", head) if head >= 0 else -1
        if blank < 0:
            break
        many = int(answer.stdout[head + 16:blank])
        one = json.loads(answer.stdout[blank + 4:blank + 4 + many])
        at = blank + 4 + many
        if "id" in one:
            heard[one["id"]] = one
        elif one.get("method") == "textDocument/publishDiagnostics":
            published.append(one["params"])
    return heard, published


def opened(uri, text, version=1):
    return {"jsonrpc": "2.0", "method": "textDocument/didOpen",
            "params": {"textDocument": {"uri": uri, "languageId": "kest",
                                        "version": version, "text": text}}}


def asked(number, method, uri, line, column, **more):
    params = {"textDocument": {"uri": uri},
              "position": {"line": line, "character": column}}
    params.update(more)
    return {"jsonrpc": "2.0", "id": number, "method": method,
            "params": params}


def edited_files(answer):
    changes = answer.get("result", {}) or {}
    return changes.get("changes", {})


START = {"jsonrpc": "2.0", "id": 0, "method": "initialize", "params": {}}
SHAPE = "module src.shape\n\nstruct Point {\n    x: i32\n}\n"
MAIN = ("module src.main\n\nimport src.shape\nimport std.io\n\n"
        "fn main() -> i32 {\n    let p: shape.Point = shape.Point(2)\n"
        "    io.print(\"hello\")\n    return p.x - 2\n}\n")

# Made where no code page reaches, and every command over it.
far = os.path.join(top, "项目", "\U0001F98A")
os.makedirs(far)
made = ran([kest, "new", "游戏"], far)
project = os.path.join(far, "游戏")
main = os.path.join(project, "src", "main.kest")
shape = os.path.join(project, "src", "shape.kest")
if made.returncode != 0 or not os.path.isfile(main):
    wrong.append("new: %s" % told(made))
else:
    with open(shape, "w", encoding="utf-8", newline="\n") as written:
        written.write(SHAPE)
    with open(main, "w", encoding="utf-8", newline="\n") as written:
        written.write(MAIN)
    for words, where, wanted in (
            ([kest, "check", main], None, None),
            ([kest, "run", main], None, "hello"),
            ([kest, "build"], project, None),
            ([kest, "test"], project, "1 of 1 passed")):
        answer = ran(words, where)
        if answer.returncode != 0 or (wanted is not None and
                                      wanted not in told(answer)):
            wrong.append("%s: %s" % (words[1], told(answer)))

    # What is wrong, read through the import, and what is right after a change.
    uri = uri_of(main)
    broken = MAIN.replace("shape.Point(2)", "shape.Point(true)")
    _, published = talk([
        START, opened(uri, broken),
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": MAIN}]}}])
    mine = [one["diagnostics"] for one in published
            if same_path(one["uri"], main)]
    if len(mine) != 2 or [d["range"]["start"]["line"] for d in mine[0]] != \
            [6] or mine[1] != []:
        wrong.append("diagnostics: %s" % json.dumps(published)[:400])

    # Where `Point` was declared, and every file that names it renamed.
    column = MAIN.split("\n")[6].rindex("Point")
    heard, _ = talk([
        START, opened(uri, MAIN),
        asked(1, "textDocument/definition", uri, 6, column),
        asked(2, "textDocument/rename", uri, 6, column, newName="Spot")])
    found = heard.get(1, {}).get("result")
    found = found[0] if isinstance(found, list) and found else found
    if not isinstance(found, dict) or not same_path(found.get("uri", ""),
                                                    shape) or \
            found["range"]["start"]["line"] != 2:
        wrong.append("definition: %s" % json.dumps(heard.get(1)))
    changes = edited_files(heard.get(2, {}))
    lines = sorted((os.path.basename(urllib.parse.unquote(u)),
                    sorted(e["range"]["start"]["line"] for e in edits))
                   for u, edits in changes.items())
    if lines != [("main.kest", [6, 6]), ("shape.kest", [2])] or \
            not all(same_path(u, main) or same_path(u, shape)
                    for u in changes):
        wrong.append("rename: %s" % json.dumps(heard.get(2)))

# The layout two names that look alike make: `p/n` + a with a bar, the
# project, reached through the link `p/j`, beside `p/na`, which holds a
# dependency the project names with `source ../na`. Reached through the link,
# `..` is `p`, so the dependency is found; resolved, it is outside.
alike = os.path.join(top, "p")
barred = os.path.join(alike, "nā")
plain = os.path.join(alike, "na")
link = os.path.join(alike, "j")
os.makedirs(os.path.join(barred, "src"))
os.makedirs(os.path.join(plain, "lib"))
with open(os.path.join(barred, "kest.project"), "w", newline="\n") as written:
    written.write("project alike\nsource src\nsource ../na\n")
with open(os.path.join(barred, "src", "main.kest"), "w",
          newline="\n") as written:
    written.write("module main\n\nimport lib.far\n\nfn main() -> i32 {\n"
                  "    let n = 1\n    return far.f(n) - n\n}\n")
with open(os.path.join(plain, "lib", "far.kest"), "w",
          newline="\n") as written:
    written.write("module lib.far\n\nfn f(n: i32) -> i32 {\n"
                  "    return n\n}\n")
if windows:
    # In the platform's separators: `cmd` reads a `/` as a switch.
    linked = subprocess.run(["cmd", "/c", "mklink", "/J",
                             os.path.normpath(link), os.path.normpath(barred)],
                            capture_output=True)
    if linked.returncode != 0:
        wrong.append("the junction was not made: %s" % told(linked))
else:
    os.symlink(barred, link)
through = os.path.join(link, "src", "main.kest")
uri = uri_of(through)
with open(through, encoding="utf-8") as reading:
    text = reading.read()
returned = text.split("\n")[6]
heard, _ = talk([
    START, opened(uri, text),
    asked(1, "textDocument/rename", uri, 6, returned.index("far.f") + 4,
          newName="g"),
    asked(2, "textDocument/rename", uri, 6, returned.index("(n)") + 1,
          newName="m")])
error = heard.get(1, {}).get("error", {})
if error.get("code") != -32803 or "outside the project" not in \
        error.get("message", ""):
    wrong.append("a name declared beside the project was answered %s" %
                 json.dumps(heard.get(1)))
local = edited_files(heard.get(2, {}))
if len(local) != 1 or not same_path(next(iter(local)), through) or sorted(
        e["range"]["start"]["line"] for e in local[next(iter(local))]) != \
        [5, 6, 6]:
    wrong.append("a local through the link was answered %s" %
                 json.dumps(heard.get(2)))

print("; ".join(wrong[:3]) if wrong else "yes")
PY
)
if [ "$said" != "yes" ]; then
    echo "a path no code page has: $said"
    exit 1
fi
echo "a project under a CJK and an emoji directory made, checked, run, built" \
    "and tested, answered over by the language server, and renamed through a" \
    "link without reaching the directory beside it"
