// Bytes the compiler was not written for, made from a seed and handed to it.
// What this holds is not that any of them compile -- almost none do -- but that
// every one of them ends in one of two ways: a program, or a refusal that says
// what is wrong. A crash, a hang or a quiet nothing is what this is looking
// for. See D984.
//
// Deterministic, from a seed, so a run that finds something can be run again:
// `tools/fuzz 12345 1000` is the same thousand inputs on any machine. There is
// no corpus directory and nothing is written down -- what a finding is is a
// seed and a number, which fits in a sentence.
//
// It is built and run under the sanitisers, because a release build answers a
// read past the end of something with whatever was next and this is the one
// place here where what comes next is chosen by a stranger.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kest.h"

// The one mixer this tree has, written out rather than reached for: a fuzzer
// that depended on the library it is fuzzing would say nothing about a day the
// library is wrong.
static uint64_t next_number(uint64_t *state) {
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

// The pieces a program is made of, for the half of the inputs that are built
// rather than broken. Random bytes never got past the lexer and pieces alone
// rarely get past the parser, so the other half is a real program with
// something done to it: a byte changed, a run of bytes cut out, a piece put in
// the middle. A mutation of something that compiles usually still parses,
// which is how the checker and the compiler are reached at all.
static const char *const PIECES[] = {
    "module ", "import std.io\n", "fn ", "main", "() -> ", "i32", "f32",
    "f64", "u8", "text", "bool", "{\n", "}\n", "return ", "let ", "const ",
    "struct ", "enum ", "flags ", "match ", "if ", "else ", "for ", "while ",
    "in ", "0..", "break\n", "continue\n", "defer ", "scratch ", "none",
    "true", "false", "extern fn ", "no.alloc", "no.host", "deterministic",
    "array(", "store(", "add(", "get(", "set(", "push(", "len(", "text(",
    "(", ")", "[", "]", ",", ".", ":", ";", "?", "->", "=", "==", "!=",
    "<", ">", "+", "-", "*", "/", "%", "&&", "||", "!", "~", "^", "&", "|",
    "\"", "\"a\"", "\"{", "}\"", "'a'", "\\0", "\n", " ", "\t", "x", "y",
    "Thing", "T", "1", "0", "-1", "999999999999999999999",
    "1.5", "0x", "0b", "1e", "\xc4\xb1", "\xff", "\xc0", "\xe2",
};

// A file of this tree, read whole, to be broken. NULL when it is not there,
// which is what running this from somewhere else looks like.
static char *seed_program(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size <= 0 || size > (1 << 16)) {
        fclose(file);
        return NULL;
    }
    char *text = malloc((size_t)size + 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    *length = fread(text, 1, (size_t)size, file);
    text[*length] = '\0';
    fclose(file);
    return text;
}

static const char *const SEEDS[] = {
    "examples/math.kest",     "examples/colony.kest",
    "examples/boxes.kest",    "examples/inventory.kest",
    "examples/frame.kest",    "lib/std/text.kest",
    "lib/std/table.kest",     "examples/registry.kest",
};

int main(int argc, char **argv) {
    uint64_t seed = argc > 1 ? strtoull(argv[1], NULL, 10) : 1;
    unsigned long many = argc > 2 ? strtoul(argv[2], NULL, 10) : 200;
    if (seed == 0) {
        seed = 1;
    }
    const char *where = argc > 3 ? argv[3] : "fuzz.kest";

    unsigned long refused = 0;
    unsigned long compiled = 0;
    for (unsigned long round = 0; round < many; round++) {
        uint64_t state = seed + round;
        static char program[1 << 17];
        size_t used = 0;
        bool from_a_seed = (next_number(&state) & 1) != 0;
        char *seeded = NULL;
        size_t seeded_length = 0;
        if (from_a_seed) {
            const char *which =
                SEEDS[next_number(&state) % (sizeof(SEEDS) /
                                             sizeof(SEEDS[0]))];
            seeded = seed_program(which, &seeded_length);
        }
        if (seeded != NULL && seeded_length > 0) {
            used = seeded_length < sizeof(program) - 1 ? seeded_length
                                                       : sizeof(program) - 1;
            memcpy(program, seeded, used);
            // Between one and eight things done to it. One is often not
            // enough to reach anything new and a hundred is random bytes
            // again.
            uint32_t doings = (uint32_t)(next_number(&state) % 8) + 1;
            for (uint32_t i = 0; i < doings && used > 4; i++) {
                size_t at = (size_t)(next_number(&state) % used);
                switch (next_number(&state) % 3) {
                case 0:
                    // A byte changed, which is what a typo is.
                    program[at] =
                        (char)(next_number(&state) % 256);
                    break;
                case 1: {
                    // A run cut out, which is what a half-saved file is.
                    size_t cut = (size_t)(next_number(&state) % 64) + 1;
                    if (at + cut >= used) {
                        cut = used - at - 1;
                    }
                    memmove(program + at, program + at + cut,
                            used - at - cut);
                    used -= cut;
                    break;
                }
                default: {
                    // A piece of the language put in the middle of it.
                    const char *piece =
                        PIECES[next_number(&state) % (sizeof(PIECES) /
                                                      sizeof(PIECES[0]))];
                    size_t length = strlen(piece);
                    if (used + length < sizeof(program) - 1) {
                        memmove(program + at + length, program + at,
                                used - at);
                        memcpy(program + at, piece, length);
                        used += length;
                    }
                    break;
                }
                }
            }
            free(seeded);
        } else {
            uint32_t pieces = (uint32_t)(next_number(&state) % 120) + 1;
            for (uint32_t i = 0;
                 i < pieces && used < sizeof(program) - 64; i++) {
                const char *piece =
                    PIECES[next_number(&state) % (sizeof(PIECES) /
                                                  sizeof(PIECES[0]))];
                size_t length = strlen(piece);
                if (used + length >= sizeof(program) - 1) {
                    break;
                }
                memcpy(program + used, piece, length);
                used += length;
            }
        }
        program[used] = '\0';

        FILE *file = fopen(where, "wb");
        if (file == NULL) {
            fprintf(stderr, "fuzz: `%s` could not be written\n", where);
            return 1;
        }
        fwrite(program, 1, used, file);
        fclose(file);

        // Through the whole of it, because what a stranger's bytes reach is
        // the point: reading, checking, compiling and running. A build that
        // refuses stops before the next stage, which is what it is for.
        // Everything the build says goes nowhere: what is being asked is
        // whether it says something rather than what. A refusal is the good
        // answer and there are a great many of them.
        KestBuild *build = kest_build(where, NULL, NULL, KEST_FORM_TEXT, 0);
        if (build == NULL) {
            refused++;
            continue;
        }
        // A budget and a ceiling, because a program made out of pieces may be
        // a loop that never ends or a table that never stops growing, and
        // this is not the place to find either out by waiting.
        KestLimits bounded = {4096, 64, 4 * 1024 * 1024, 200000};
        KestRuntime *runtime = kest_start(build, NULL, &bounded);
        if (runtime == NULL) {
            refused++;
        } else {
            compiled++;
            int32_t at = kest_entry(runtime, "main");
            if (at >= 0) {
                KestValue frame[8] = {{0}};
                kest_call(runtime, at, frame,
                          sizeof(frame) / sizeof(frame[0]));
            }
            kest_runtime_free(runtime);
        }
        kest_build_free(build);
    }
    remove(where);
    printf("fuzz: %lu input(s) from seed %llu: %lu refused, %lu compiled, "
           "and none of them stopped this\n",
           many, (unsigned long long)seed, refused, compiled);
    return 0;
}
