#!/usr/bin/env python3
# Writes what the playground page reads beside kest.wasm: std.json, every
# module of lib/std by file name, and examples.json, the programs its menu
# offers, a greeting first and then the examples named on the command line.
#
# Usage: python3 tools/playground.py OUT EXAMPLE...  (from the repository root)

import glob
import json
import os
import sys

HELLO = 'import std.io\n\nfn main() -> i32 {\n    io.print("hello from kest")\n    return 0\n}\n'


def main():
    out, played = sys.argv[1], sys.argv[2:]
    library = {os.path.basename(path): open(path, encoding="utf-8").read()
               for path in sorted(glob.glob("lib/std/*.kest"))}
    if not library or not played:
        sys.exit("playground: no library or no examples to offer")
    with open(os.path.join(out, "std.json"), "w", encoding="utf-8") as file:
        json.dump(library, file)
    examples = [("hello", HELLO)] + [
        (name, open("examples/%s.kest" % name, encoding="utf-8").read())
        for name in played]
    with open(os.path.join(out, "examples.json"), "w", encoding="utf-8") as file:
        json.dump(dict(examples), file)


if __name__ == "__main__":
    main()
