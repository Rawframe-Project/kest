"""Writes the programs `bench/compile.sh` times, the same program in every
language it is timed in, so what differs between two rows is the language and
not the program.

Two shapes. A long program: functions of a loop, a branch and a call each,
as many as make the lines asked for, and a `main` that calls the last of them
-- which is what reading, checking where a language checks, and compiling a
game's scripts before the first frame costs. And a generic taken 3,200 ways:
one function over a type parameter, called with 3,200 shapes of its own, which
is what a language that writes a copy per type pays for each copy. The Lua
family has no types to copy over and is not in the second; C++, Rust and
daslang each write a copy per type as Kest does. See D1273.

    python3 bench/compile/write.py <directory> <lines> <copies>
"""

import os
import sys


def functions(lines):
    return max(1, lines // 14)


def kest(many):
    out = ["module long", ""]
    for i in range(many):
        call = "        s += w%d(a, b - 1)\n" % (i - 1) if i > 0 else \
               "        s += a\n"
        out.append("fn w%d(a: i64, b: i64) -> i64 {\n"
                   "    let s: i64 = a\n"
                   "    for k in 0..b {\n"
                   "        if k %% 3 == 0 {\n"
                   "            s += k * %d\n"
                   "        } else {\n"
                   "            s -= 1\n"
                   "        }\n"
                   "    }\n"
                   "    if b > 100 {\n"
                   "%s"
                   "    }\n"
                   "    return s\n"
                   "}\n" % (i, i % 7 + 1, call))
    out.append("fn main() -> i32 {\n"
               "    return i32(w%d(1, 3) + 1)\n"
               "}\n" % (many - 1))
    return "\n".join(out)


def lua(many):
    out = []
    for i in range(many):
        call = "        s = s + f%d(a, b - 1)\n" % (i - 1) if i > 0 else \
               "        s = s + a\n"
        out.append("function f%d(a, b)\n"
                   "    local s = a\n"
                   "    for k = 0, b - 1 do\n"
                   "        if k %% 3 == 0 then\n"
                   "            s = s + k * %d\n"
                   "        else\n"
                   "            s = s - 1\n"
                   "        end\n"
                   "    end\n"
                   "    if b > 100 then\n"
                   "%s"
                   "    end\n"
                   "    return s\n"
                   "end\n" % (i, i % 7 + 1, call))
    out.append("local answer = f%d(1, 3) + 1\n" % (many - 1))
    return "\n".join(out)


def js(many):
    out = []
    for i in range(many):
        call = "        s += f%d(a, b - 1);\n" % (i - 1) if i > 0 else \
               "        s += a;\n"
        out.append("function f%d(a, b) {\n"
                   "    let s = a;\n"
                   "    for (let k = 0; k < b; k++) {\n"
                   "        if (k %% 3 === 0) {\n"
                   "            s += k * %d;\n"
                   "        } else {\n"
                   "            s -= 1;\n"
                   "        }\n"
                   "    }\n"
                   "    if (b > 100) {\n"
                   "%s"
                   "    }\n"
                   "    return s;\n"
                   "}\n" % (i, i % 7 + 1, call))
    out.append("const answer = f%d(1, 3) + 1;\n" % (many - 1))
    return "\n".join(out)


def das(many):
    out = ["options gen2", ""]
    for i in range(many):
        call = "        s += f%d(a, b - 1l);\n" % (i - 1) if i > 0 else \
               "        s += a;\n"
        out.append("def f%d(a : int64; b : int64) : int64 {\n"
                   "    var s = a;\n"
                   "    for (k in range64(b)) {\n"
                   "        if (k %% 3l == 0l) {\n"
                   "            s += k * %dl;\n"
                   "        } else {\n"
                   "            s -= 1l;\n"
                   "        }\n"
                   "    }\n"
                   "    if (b > 100l) {\n"
                   "%s"
                   "    }\n"
                   "    return s;\n"
                   "}\n" % (i, i % 7 + 1, call))
    out.append("[export]\n"
               "def main() {\n"
               "    let answer = f%d(1l, 3l) + 1l;\n"
               "}\n" % (many - 1))
    return "\n".join(out)


def kest_copies(many):
    out = ["module copies", "",
           "fn pick<T>(x: T, y: T, first: bool) -> T {\n"
           "    if first {\n"
           "        return x\n"
           "    }\n"
           "    return y\n"
           "}\n"]
    for i in range(many):
        out.append("struct S%d {\n    a: i64\n}\n" % i)
    out.append("fn main() -> i32 {\n    let t: i64 = 0")
    for i in range(many):
        out.append("    t += pick(S%d(%d), S%d(1), true).a" % (i, i, i))
    out.append("    if t < 0 {\n        return 1\n    }\n    return 0\n}\n")
    return "\n".join(out)


def cpp_copies(many):
    out = ["template <typename T> T pick(T x, T y, bool first) {\n"
           "    if (first) {\n"
           "        return x;\n"
           "    }\n"
           "    return y;\n"
           "}\n"]
    for i in range(many):
        out.append("struct S%d {\n    long long a;\n};\n" % i)
    out.append("int main() {\n    long long t = 0;")
    for i in range(many):
        out.append("    t += pick(S%d{%d}, S%d{1}, true).a;" % (i, i, i))
    out.append("    return (int)(t % 100);\n}\n")
    return "\n".join(out)


def rust_copies(many):
    out = ["fn pick<T>(x: T, y: T, first: bool) -> T {\n"
           "    if first {\n"
           "        return x;\n"
           "    }\n"
           "    y\n"
           "}\n"]
    for i in range(many):
        out.append("struct S%d {\n    a: i64,\n}\n" % i)
    out.append("fn main() {\n    let mut t: i64 = 0;")
    for i in range(many):
        out.append("    t += pick(S%d { a: %d }, S%d { a: 1 }, true).a;"
                   % (i, i, i))
    out.append("    std::process::exit((t % 100) as i32);\n}\n")
    return "\n".join(out)


def das_copies(many):
    out = ["options gen2", "",
           "def pick(x, y : auto(T); first : bool) : T {\n"
           "    if (first) {\n"
           "        return x;\n"
           "    }\n"
           "    return y;\n"
           "}\n"]
    for i in range(many):
        out.append("struct S%d {\n    a : int64;\n}\n" % i)
    out.append("[export]\ndef main() {\n    var t = 0l;")
    for i in range(many):
        out.append("    t += pick(S%d(a = %dl), S%d(a = 1l), true).a;"
                   % (i, i, i))
    out.append("}\n")
    return "\n".join(out)


def main():
    where, lines, copies = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    os.makedirs(where, exist_ok=True)
    many = functions(lines)
    for name, body in (("long.kest", kest(many)), ("long.lua", lua(many)),
                       ("long.js", js(many)), ("long.das", das(many)),
                       ("copies.kest", kest_copies(copies)),
                       ("copies.cpp", cpp_copies(copies)),
                       ("copies.rs", rust_copies(copies)),
                       ("copies.das", das_copies(copies))):
        with open(os.path.join(where, name), "w") as out:
            out.write(body)


if __name__ == "__main__":
    main()
