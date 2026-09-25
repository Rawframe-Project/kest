#!/bin/sh
# What the machine reads without asking, proved before it runs. A program is
# compiled, then one number in one instruction is changed to one the program
# has not got -- a slot, a constant, a function, a door of the host, a layout,
# a run of slots, a jump past the end, a jump into the middle of the next
# instruction, a jump back past the start -- and the verifier is asked about
# the module. Each has to be refused with its code, `K0408` for a number that
# names what is not there and `K0409` for a jump that lands where no
# instruction starts, and the program as it was compiled has to be held.
# Nothing is run: what is asked is only whether the verifier would let it.
#
# It reads the module a build made, so what it is written in is C against the
# library's own headers as well as the public one, written here rather than
# kept in `tools/` because it is this check's and nothing else's. See D1237.
set -u
cc=${CC:-cc}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cd "$(dirname "$0")/.." || exit 1

if [ ! -f libkest.a ]; then
    echo "the library is not built"
    exit 1
fi

cat > "$scratch"/refuse.c <<'REFUSE'
// The program `check-verifier.sh` builds and runs: chunks the verifier has to
// refuse. A program is compiled, and then one
// number in one instruction is changed to one the program does not have -- a
// slot past the frame, a constant past the body's, a function, a door or a
// layout past the module's, a jump that lands between two instructions or past
// the end -- and `kest_module_prove` is asked about the module. Each has to be
// refused with the code for what it is, and the program as it was compiled has
// to be held. Nothing here runs a chunk: what is asked is only whether the
// verifier would let it run. See D1237.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kest.h"
#include "build.h"
#include "value.h"
#include "verify.h"

static const char *PROGRAM =
    "module refused\n"
    "\n"
    "import std.io\n"
    "\n"
    "struct Pair {\n"
    "    a: i32\n"
    "    b: i32\n"
    "}\n"
    "\n"
    "enum Shape {\n"
    "    Dot\n"
    "    Box(i32, i32)\n"
    "}\n"
    "\n"
    "fn made(n: i32) -> Pair {\n"
    "    return Pair(n, n + 1)\n"
    "}\n"
    "\n"
    "fn wide(s: Shape) -> i32 {\n"
    "    return match s {\n"
    "        Dot -> 0\n"
    "        Box(w, h) -> w * h\n"
    "    }\n"
    "}\n"
    "\n"
    "fn pieces(n: i32) -> i32 {\n"
    "    let a = \"{n}ab\"\n"
    "    let b = \"{n}cde\"\n"
    "    return len(a) + len(b)\n"
    "}\n"
    "\n"
    "fn deeper(n: i32) -> i32 {\n"
    "    if n <= 0 {\n"
    "        return 0\n"
    "    }\n"
    "    return 1 + deeper(n - 1)\n"
    "}\n"
    "\n"
    "fn main() -> i32 {\n"
    "    let xs: [Pair] = array()\n"
    "    let n = 0\n"
    "    while n < 40 {\n"
    "        push(xs, Pair(n, n * 2))\n"
    "        n += 3\n"
    "    }\n"
    "    let p = xs[2]\n"
    "    let total = xs[1].b + deeper(3) + p.a + p.b + pieces(1) +\n"
    "        wide(Shape.Box(2, 3)) + made(4).b\n"
    "    io.print(\"{total}\")\n"
    "    return total % 7\n"
    "}\n";

// Whether the module is held, and if not, whether what was said carries this
// code.
static bool refused_with(KestBuild *build, const char *code) {
    KestDiags said;
    kest_diags_init(&said, build->arena);
    bool held = kest_module_prove(&build->module, build->arena, &said);
    if (code == NULL) {
        return held && said.error_count == 0;
    }
    for (uint32_t i = 0; i < said.count; i++) {
        if (strcmp(said.items[i].code, code) == 0) {
            return !held;
        }
    }
    return false;
}

// The first instruction anywhere in the module carrying an operand of this
// kind, as the chunk, where the instruction is and which operand.
// One that carries nought is passed over when what is written is one fewer.
static bool first_with(KestModule *module, const char *named, uint32_t operand,
                       bool counted, KestChunk **in, uint32_t *at) {
    for (uint32_t f = 0; f < module->count; f++) {
        KestChunk *chunk = module->functions[f];
        for (uint32_t i = 0; i < chunk->code_count;
             i += kest_op_wide(chunk->code[i])) {
            if (strcmp(kest_op_name(chunk->code[i]), named) == 0 &&
                kest_op_wide(chunk->code[i]) >= 3 + 2 * operand &&
                (!counted || chunk->code[i + 1 + 2 * operand] != 0 ||
                 chunk->code[i + 2 + 2 * operand] != 0)) {
                *in = chunk;
                *at = i;
                return true;
            }
        }
    }
    return false;
}

// What is written in place of a number that is one fewer than it was, and in
// place of a jump one byte further than it lands.
#define FEWER 0xFFFE
#define BETWEEN 0xFFFD
// And in place of a slot, the last one the body has.
#define LAST 0xFFFC

typedef struct {
    const char *what;
    const char *op;
    uint32_t operand;
    uint16_t written;
    const char *code;
} Case;

int main(int argc, char **argv) {
    const char *where = argc > 1 ? argv[1] : "refused.kest";
    FILE *out = fopen(where, "w");
    if (out == NULL) {
        fprintf(stderr, "refuse: cannot write %s\n", where);
        return 2;
    }
    fputs(PROGRAM, out);
    fclose(out);
    KestBuild *build = kest_build(where, NULL, stderr, KEST_FORM_TEXT, 0);
    remove(where);
    if (build == NULL) {
        fprintf(stderr, "refuse: the program did not compile\n");
        return 2;
    }
    if (!refused_with(build, NULL)) {
        printf("refuse: the program as it was compiled is not held\n");
        return 1;
    }
    Case cases[] = {
        {"a slot past the frame", "store", 0, 0xFFFF, "K0408"},
        {"a constant past the body's", "const", 0, 0xFFFF, "K0408"},
        {"a function past the module's", "call", 0, 0xFFFF, "K0408"},
        {"a door past the module's", "call.host", 0, 0xFFFF, "K0408"},
        {"a layout past the module's", "make.array", 0, 0xFFFF, "K0408"},
        {"a run of slots past the frame", "load.n", 1, 0xFFFF, "K0408"},
        {"a jump past the end", "jump.false.lt.k", 2, 0xFFFF, "K0409"},
        {"a jump between two instructions", "jump.false.lt.k", 2, BETWEEN,
         "K0409"},
        {"a jump back to before the body", "loop", 0, 0xFFFF, "K0409"},
        {"a return one slot short of the declaration", "return", 0, FEWER,
         "K0410"},
        {"a call handing a function another number of slots", "call", 1,
         0xFFFF, "K0410"},
        {"a concat of more pieces than there are", "concat", 0, 0xFFFF,
         "K0410"},
        {"a concat of no pieces, leaving two slots nothing reads", "concat",
         0, 0, "K0410"},
        {"an element written past the end of the frame", "index.to", 1, LAST,
         "K0408"},
        {"a slot read before anything wrote it", "load.k", 0, 2, "K0411"},
        {"a number handed over as an array", "load.k", 0, 1, "K0411"},
        {"an array read as a number", "mod.i.k", 0, 0, "K0411"},
        {"a rotation of none", "rotate", 0, 0, "K0410"},
        {"a piece taken past the end of its value", "field", 0, 0xFFFF,
         "K0410"},
    };
    uint32_t refused = 0;
    uint32_t missed = 0;
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        KestChunk *chunk = NULL;
        uint32_t at = 0;
        if (!first_with(&build->module, cases[c].op, cases[c].operand,
                        cases[c].written == FEWER, &chunk, &at)) {
            printf("refuse: no `%s` in the program for %s\n", cases[c].op,
                   cases[c].what);
            missed++;
            continue;
        }
        uint32_t place = at + 1 + 2 * cases[c].operand;
        uint8_t low = chunk->code[place];
        uint8_t high = chunk->code[place + 1];
        uint16_t written = cases[c].written;
        if (written == BETWEEN) {
            // Between two instructions: one byte further than it lands.
            written = (uint16_t)((low | (high << 8)) + 1);
        } else if (written == FEWER) {
            written = (uint16_t)((low | (high << 8)) - 1);
        } else if (written == LAST) {
            written = (uint16_t)(chunk->slot_count - 1);
        }
        chunk->code[place] = (uint8_t)(written & 0xFF);
        chunk->code[place + 1] = (uint8_t)(written >> 8);
        if (refused_with(build, cases[c].code)) {
            refused++;
        } else {
            printf("refuse: %s was held\n", cases[c].what);
            missed++;
        }
        chunk->code[place] = low;
        chunk->code[place + 1] = high;
    }
    // A piece of text and another piece's length, side by side and each
    // what it says it is: `load.n` of the first text's two slots made a
    // `load2` of its text and the second text's length, which is as wide.
    {
        KestChunk *chunk = NULL;
        uint32_t at = 0;
        uint8_t load2 = 0;
        while (load2 < 255 && strcmp(kest_op_name(load2), "load2") != 0) {
            load2++;
        }
        for (uint32_t f = 0; chunk == NULL && f < build->module.count; f++) {
            KestChunk *one = build->module.functions[f];
            if (strstr(one->name, "pieces") == NULL) {
                continue;
            }
            for (uint32_t i = 0; i < one->code_count;
                 i += kest_op_wide(one->code[i])) {
                if (strcmp(kest_op_name(one->code[i]), "load.n") == 0 &&
                    one->code[i + 3] == 2 && one->code[i + 4] == 0) {
                    chunk = one;
                    at = i;
                    break;
                }
            }
        }
        if (chunk == NULL) {
            printf("refuse: no text loaded whole for a piece and another's "
                   "length\n");
            missed++;
        } else {
            uint8_t was[5];
            memcpy(was, chunk->code + at, 5);
            uint32_t first = (uint32_t)(was[1] | (was[2] << 8));
            uint32_t other = first + 3;
            chunk->code[at] = load2;
            chunk->code[at + 3] = (uint8_t)(other & 0xFF);
            chunk->code[at + 4] = (uint8_t)(other >> 8);
            if (refused_with(build, "K0411")) {
                refused++;
            } else {
                printf("refuse: a piece of text and another's length was "
                       "held\n");
                missed++;
            }
            memcpy(chunk->code + at, was, 5);
        }
    }
    if (!refused_with(build, NULL)) {
        printf("refuse: the program as it was compiled is not held once "
               "every case was put back\n");
        missed++;
    }
    kest_build_free(build);
    if (missed > 0) {
        return 1;
    }
    printf("%u way(s) a chunk can name what it has not got, move the stack "
           "wrong or read a slot as what it does not hold, each refused, and "
           "the program as it was compiled held\n",
           refused);
    return 0;
}
REFUSE

if ! $cc -std=c11 -Wall -Wextra -Wshadow -Wconversion -Werror -O2 -Iinclude \
        -Isrc -o "$scratch"/refuse "$scratch"/refuse.c libkest.a -lm \
        2>"$scratch"/why; then
    echo "the verifier's own test does not build:"
    sed 's/^/    /' "$scratch"/why | head -5
    exit 1
fi
"$scratch"/refuse "$scratch"/refused.kest
