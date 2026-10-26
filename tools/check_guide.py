#!/usr/bin/env python3
"""Hold the user guide to the compiler.

Every ```kest block in docs/user is a whole file, and `kest check` accepts it
with nothing to say. A ```kest block followed directly by a ```text block is a
program: `kest run` answers nought and prints exactly the text. A ```c block
with a `main` in it is a whole host, and it compiles against include/kest.h and
links against libkest.a.

Run from the root of the tree after `make` and `make libkest.a`.
"""

import os
import re
import subprocess
import sys
import tempfile

FENCE = re.compile(r"^```([a-z]*)\n(.*?)^```\n", re.M | re.S)
MODULE = re.compile(r"^module ([a-z][A-Za-z0-9_.]*)$", re.M)


def blocks(text):
    """Each fenced block as (language, body, line, the block right after)."""
    found = list(FENCE.finditer(text))
    for i, one in enumerate(found):
        line = text.count("\n", 0, one.start()) + 1
        after = None
        if i + 1 < len(found):
            between = text[one.end():found[i + 1].start()]
            if between.strip() == "":
                after = found[i + 1]
        yield one.group(1), one.group(2), line, after


def place(work, body):
    """Where a file has to be for its module name to resolve."""
    named = MODULE.search(body)
    if named is None:
        path = os.path.join(work, "program.kest")
    else:
        path = os.path.join(work, *named.group(1).split(".")) + ".kest"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as out:
        out.write(body)
    return path


def kest(*words):
    return subprocess.run(["./kest", *words], capture_output=True, text=True,
                          stdin=subprocess.DEVNULL, timeout=60)


def main():
    if not os.access("kest", os.X_OK) or not os.path.exists("libkest.a"):
        print("guide: build `kest` and `libkest.a` first")
        return 1
    pages = sorted(os.path.join(root, name)
                   for root, _, names in os.walk(os.path.join("docs", "user"))
                   for name in names if name.endswith(".md"))
    if not pages:
        print("guide: nothing was given: docs/user has no pages")
        return 1
    failed = 0
    checked = ran = hosts = 0
    with tempfile.TemporaryDirectory() as scratch:
        for page in pages:
            text = open(page).read()
            for at, (language, body, line, after) in enumerate(blocks(text)):
                where = "%s:%u" % (page, line)
                if language == "kest":
                    work = os.path.join(scratch, "%s-%u" % (
                        page.replace(os.sep, "-"), at))
                    path = place(work, body)
                    said = kest("check", path)
                    checked += 1
                    if said.returncode != 0 or said.stderr.strip():
                        print("%s: `kest check` says:\n%s" % (
                            where, said.stderr.strip()))
                        failed = 1
                        continue
                    if after is not None and after.group(1) == "text":
                        said = kest("run", path)
                        ran += 1
                        if said.returncode != 0:
                            print("%s: the program answered %d:\n%s" % (
                                where, said.returncode, said.stderr.strip()))
                            failed = 1
                        elif said.stdout != after.group(2):
                            print("%s: the program printed\n%s\nand the page "
                                  "says\n%s" % (where, said.stdout,
                                                after.group(2)))
                            failed = 1
                elif language == "c" and re.search(r"\bint main\(", body):
                    work = os.path.join(scratch, "host-%u" % hosts)
                    os.makedirs(work)
                    source = os.path.join(work, "host.c")
                    with open(source, "w") as out:
                        out.write(body)
                    built = subprocess.run(
                        [os.environ.get("CC", "cc"), "-std=c11", "-Wall",
                         "-Wextra", "-Werror", "-Iinclude", source,
                         "libkest.a", "-lm", "-o",
                         os.path.join(work, "host")],
                        capture_output=True, text=True)
                    hosts += 1
                    if built.returncode != 0:
                        print("%s: the host does not build:\n%s" % (
                            where, built.stderr.strip()))
                        failed = 1
    if checked == 0:
        print("guide: docs/user has no ```kest blocks, so nothing was checked")
        return 1
    if not failed:
        print("guide: %u Kest file(s) checked, %u of them run for what the "
              "page says they print, and %u host(s) built, over %u page(s)"
              % (checked, ran, hosts, len(pages)))
    return failed


if __name__ == "__main__":
    sys.exit(main())
