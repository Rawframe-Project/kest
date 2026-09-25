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
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kest.h"
// And three of the library's own, for the one boundary here that is not a door:
// what the verifier is handed. See D1253.
#include "build.h"
#include "value.h"
#include "verify.h"

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

// Bytes the compiler was not written for, which is the target this file began
// as and the one that reaches the most code: reading, checking, compiling and
// running, all of it over something nobody wrote.
static int fuzz_source(uint64_t seed, unsigned long many, const char *where) {
    unsigned long refused = 0;
    unsigned long compiled = 0;
    // What every one of them answered, folded into one number. What it is for
    // is the other half of the differential test: the same seeds compiled the
    // other way -- with the lowering's fusions turned off -- have to fold to
    // the same number, and a fusion that changed what a program answers is a
    // fusion that changed the program. Thousands of programs nobody wrote is
    // exactly where a miscompilation hides. See D1013 and D1015.
    uint64_t folded = 1469598103934665603ull;
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
            // Half of them changed only in their numbers, which is a program
            // that still reads as one and so reaches the compiler and the
            // machine rather than the parser's refusal. See D1210.
            bool gently = (next_number(&state) & 1) != 0;
            for (uint32_t i = 0; i < doings && used > 4; i++) {
                size_t at = (size_t)(next_number(&state) % used);
                switch (gently ? 3 : next_number(&state) % 4) {
                case 3: {
                    // A number changed to another, which is the one change
                    // that mostly leaves a program a program: the three
                    // above break what they touch, and of the inputs they
                    // made one in eighty compiled, so the fold below was
                    // over forty programs a gate. This one runs the same
                    // program down another path. See D1210.
                    // A number standing on its own, and not the digits of a
                    // name: `i32` made `i857` is no type at all.
                    size_t digit = at;
                    while (digit < used &&
                           (program[digit] < '0' || program[digit] > '9' ||
                            (digit > 0 &&
                             (isalnum((unsigned char)program[digit - 1]) ||
                              program[digit - 1] == '_' ||
                              program[digit - 1] == '.')))) {
                        digit++;
                    }
                    size_t end = digit;
                    while (end < used && program[end] >= '0' &&
                           program[end] <= '9') {
                        end++;
                    }
                    if (digit == end) {
                        break;
                    }
                    char number[8];
                    int wrote = snprintf(number, sizeof number, "%u",
                                         (unsigned)(next_number(&state) %
                                                    1000));
                    size_t length = (size_t)wrote;
                    if (used - (end - digit) + length >= sizeof(program) - 1) {
                        break;
                    }
                    memmove(program + digit + length, program + end,
                            used - end);
                    memcpy(program + digit, number, length);
                    used = used - (end - digit) + length;
                    break;
                }
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
            folded = (folded ^ 1u) * 1099511628211ull;
            continue;
        }
        // A budget and a ceiling, because a program made out of pieces may be
        // a loop that never ends or a table that never stops growing, and
        // this is not the place to find either out by waiting.
        KestLimits bounded = {4096, 64, 4 * 1024 * 1024, 200000};
        KestRuntime *runtime = kest_start(build, NULL, &bounded);
        if (runtime == NULL) {
            refused++;
            folded = (folded ^ 2u) * 1099511628211ull;
        } else {
            compiled++;
            int32_t at = kest_entry(runtime, "main");
            uint64_t said = 3;
            if (at >= 0) {
                KestValue frame[8] = {{0}};
                bool ran = kest_call(runtime, at, frame,
                                     sizeof(frame) / sizeof(frame[0]));
                said = ran ? (uint64_t)frame[0].integer * 2 + 1 : 0;
            }
            folded = (folded ^ said) * 1099511628211ull;
            kest_runtime_free(runtime);
        }
        kest_build_free(build);
    }
    remove(where);
    printf("fuzz: %lu source input(s) from seed %llu: %lu refused, %lu "
           "compiled, none of them stopped this, and what they answered "
           "folds to %llu\n",
           many, (unsigned long long)seed, refused, compiled,
           (unsigned long long)folded);
    return 0;
}

// The program the boundary targets below drive. It is written here rather than
// read from `examples/` because what they are about is the doors a host uses,
// and a door is asked the same question whatever program is behind it: a small
// world with places in it, and the four things a host can do to one.
static const char PROBING[] =
    "module probing\n"
    "\n"
    "struct Thing {\n"
    "    id: i32\n"
    "    name: text\n"
    "}\n"
    "\n"
    "fn world(many: i32) -> store<Thing> {\n"
    "    let made: store<Thing> = store(many)\n"
    "    for i in 0..many {\n"
    "        add(made, Thing(i, \"one\"))\n"
    "    }\n"
    "    return made\n"
    "}\n"
    "\n"
    "fn look(here: store<Thing>, which: ref<Thing>) -> i32 {\n"
    "    if let one = get(here, which) {\n"
    "        return one.id\n"
    "    }\n"
    "    return 0 - 1\n"
    "}\n"
    "\n"
    "fn drop(here: store<Thing>, which: ref<Thing>) -> i32 {\n"
    "    if remove(here, which) {\n"
    "        return 1\n"
    "    }\n"
    "    return 0\n"
    "}\n"
    "\n"
    "fn put(here: store<Thing>, which: ref<Thing>, id: i32) -> i32 {\n"
    "    if let one = get(here, which) {\n"
    "        let changed = one\n"
    "        changed.id = id\n"
    "        if set(here, which, changed) {\n"
    "            return 1\n"
    "        }\n"
    "    }\n"
    "    return 0\n"
    "}\n"
    "\n"
    "fn grow(here: store<Thing>, id: i32) -> i32 {\n"
    "    let made = add(here, Thing(id, \"more\"))\n"
    "    if get(here, made) == none {\n"
    "        return 0 - 1\n"
    "    }\n"
    "    return 1\n"
    "}\n"
    "\n"
    "fn counted(here: store<Thing>) -> i32 {\n"
    "    return len(here)\n"
    "}\n"
    "\n"
    "fn main() -> i32 {\n"
    "    return 0\n"
    "}\n";

// What every target below needs: a program written down, built and started.
// A machine of its own each time, because what a target is looking for is a
// door that reads memory it was not given, and a machine carried between rounds
// would let one round's mistake be read as the next round's.
static bool a_machine_for(const char *source, const char *where,
                          KestBuild **build, KestRuntime **runtime) {
    FILE *file = fopen(where, "wb");
    if (file == NULL) {
        return false;
    }
    fwrite(source, 1, strlen(source), file);
    fclose(file);
    *build = kest_build(where, NULL, NULL, KEST_FORM_TEXT, 0);
    if (*build == NULL) {
        return false;
    }
    KestLimits bounded = {4096, 64, 8 * 1024 * 1024, 2000000};
    *runtime = kest_start(*build, NULL, &bounded);
    if (*runtime == NULL) {
        kest_build_free(*build);
        return false;
    }
    return true;
}

// Handles nobody made: random words, near misses of real ones, and handles
// from another machine, put through every door a host hands a handle to. What
// is held is that each of them answers rather than reads: a handle is four
// bytes at the front of something, and any four bytes can be those four.
static int fuzz_handles(uint64_t seed, unsigned long many, const char *where) {
    KestBuild *build = NULL;
    KestRuntime *runtime = NULL;
    KestBuild *elsewhere = NULL;
    KestRuntime *other = NULL;
    char second[512];
    snprintf(second, sizeof(second), "%s.other", where);
    if (!a_machine_for(PROBING, where, &build, &runtime) ||
        !a_machine_for(PROBING, second, &elsewhere, &other)) {
        fprintf(stderr, "fuzz: the probing program would not start\n");
        return 1;
    }
    // Two real handles to make near misses of: one this machine made and one
    // another did. A handle from another machine is the case a pointer alone
    // cannot answer, and it is the one that used to be read.
    int32_t making = kest_entry(runtime, "world");
    KestValue mine[8] = {{0}};
    mine[0].integer = 8;
    bool made = making >= 0 && kest_call(runtime, making, mine, 8);
    KestValue theirs[8] = {{0}};
    theirs[0].integer = 8;
    int32_t making_there = kest_entry(other, "world");
    bool made_there =
        making_there >= 0 && kest_call(other, making_there, theirs, 8);
    unsigned char block[64];
    memset(block, 0, sizeof(block));
    KestValue lent = kest_borrow(runtime, block, 4, "u8", 1);

    unsigned long answered = 0;
    for (unsigned long round = 0; round < many; round++) {
        uint64_t state = seed + round * 2654435761u + 1;
        KestValue handed = {0};
        switch (next_number(&state) % 6) {
        case 0:
            // Whatever a word of noise is when it is read as an address.
            handed.integer = (int64_t)next_number(&state);
            break;
        case 1:
            handed = made ? mine[0] : handed;
            break;
        case 2:
            // A near miss: a real handle moved by a little, which is what an
            // arithmetic mistake in a host looks like.
            handed.object =
                made ? (char *)mine[0].object +
                           (int64_t)(next_number(&state) % 64) - 32
                     : NULL;
            break;
        case 3:
            handed = made_there ? theirs[0] : handed;
            break;
        case 4:
            handed = lent;
            break;
        default:
            handed.object = block + (next_number(&state) % sizeof(block));
            break;
        }
        // Every door a host hands one to. None of them may read what is at an
        // address this machine did not hand out.
        (void)kest_array_length(runtime, handed, NULL);
        uint32_t room = 0;
        (void)kest_array_length(runtime, handed, &room);
        (void)kest_still_holds(runtime, handed);
        (void)kest_kept_where(runtime, handed);
        (void)kest_keeps(runtime, handed);
        (void)kest_lets_go(runtime, handed);
        (void)kest_lend_ends(runtime, handed);
        // Text is two slots and this door reads both of them, so it is handed
        // two: the address in the first and, in the second, whatever a host
        // might have left in it -- including a length that has nothing to do
        // with the bytes. What is held is that it answers with the number it
        // was given rather than reading to find one. See D964.
        KestValue said[2] = {{0}, {0}};
        said[0] = handed;
        said[1].integer = (int64_t)next_number(&state);
        uint32_t length = 0;
        const char *bytes = kest_text_bytes(&said[0], &length);
        if (bytes != NULL && length != (uint32_t)said[1].integer) {
            fprintf(stderr,
                    "fuzz: text of %lld bytes was read back as %u\n",
                    (long long)said[1].integer, length);
            return 1;
        }
        answered++;
    }
    kest_runtime_free(runtime);
    kest_build_free(build);
    kest_runtime_free(other);
    kest_build_free(elsewhere);
    remove(where);
    remove(second);
    printf("fuzz: %lu handle(s) from seed %llu put through every door a host "
           "hands one to, and none of them stopped this\n",
           answered, (unsigned long long)seed);
    return 0;
}

// The life of a lend, in whatever order a host puts it: begun, read, ended,
// ended again, read after ending, begun inside another, and the header handed
// out again to the next one. The order is what this is for -- every one of
// these is right on its own, and the class of mistake it looks for is a
// descriptor that comes back to life. See D283 and D352.
static int fuzz_lends(uint64_t seed, unsigned long many, const char *where) {
    KestBuild *build = NULL;
    KestRuntime *runtime = NULL;
    if (!a_machine_for(PROBING, where, &build, &runtime)) {
        fprintf(stderr, "fuzz: the probing program would not start\n");
        return 1;
    }
    // The host's own blocks, which outlast every lend over them. Sized
    // differently so that a handle read after its lend ended is read as
    // something of the wrong length if it is read at all.
    static unsigned char blocks[8][256];
    KestValue held[8];
    // How long each lend said it was, or nought where there is no lend. While
    // one is open the machine has to answer with the number the host gave it;
    // once it has ended the handle names whatever was lent next and nothing
    // can be asked of it, which is D352 and is why this goes back to nought.
    uint32_t lending[8];
    memset(blocks, 0, sizeof(blocks));
    memset(held, 0, sizeof(held));
    for (int i = 0; i < 8; i++) {
        lending[i] = 0;
    }
    unsigned long steps = 0;
    for (unsigned long round = 0; round < many; round++) {
        uint64_t state = seed + round * 40503u + 7;
        int which = (int)(next_number(&state) % 8);
        switch (next_number(&state) % 7) {
        case 0: {
            // Begun. A length the block has and, sometimes, one it has not:
            // a host that says more than it owns is the mistake the door is
            // there for.
            uint32_t count = (uint32_t)(next_number(&state) % 300) + 1;
            held[which] = kest_borrow(runtime, blocks[which], count, "u8", 1);
            lending[which] =
                held[which].object != NULL && count <= sizeof(blocks[which])
                    ? count
                    : 0;
            break;
        }
        case 1:
            // Ended, whether it was begun or not.
            if (kest_lend_ends(runtime, held[which])) {
                lending[which] = 0;
            }
            break;
        case 2:
            // Ended twice, which is the same call and a different answer.
            (void)kest_lend_ends(runtime, held[which]);
            (void)kest_lend_ends(runtime, held[which]);
            lending[which] = 0;
            break;
        case 3: {
            // Read, which is the one that would read the host's block after
            // the host has moved on from it.
            uint32_t room = 0;
            uint32_t count = kest_array_length(runtime, held[which], &room);
            if (lending[which] != 0 && count != lending[which]) {
                fprintf(stderr,
                        "fuzz: a lend of %u bytes says it is %u long\n",
                        lending[which], count);
                return 1;
            }
            if (count > sizeof(blocks[which])) {
                fprintf(stderr,
                        "fuzz: a lend inside %zu bytes says it is %u long\n",
                        sizeof(blocks[which]), count);
                return 1;
            }
            break;
        }
        case 4:
            (void)kest_still_holds(runtime, held[which]);
            (void)kest_kept_where(runtime, held[which]);
            break;
        case 5: {
            // One begun inside another, over the same block: two handles and
            // one block, and ending either ends both. See D283.
            KestValue again =
                kest_borrow(runtime, blocks[which], 16, "u8", 1);
            (void)kest_array_length(runtime, again, NULL);
            (void)kest_lend_ends(runtime, again);
            (void)kest_array_length(runtime, held[which], NULL);
            lending[which] = 0;
            break;
        }
        default:
            // The heap thrown away under every lend there is, which takes the
            // headers and the list with it.
            if ((next_number(&state) % 32) == 0) {
                (void)kest_heap_reset(runtime);
                for (int i = 0; i < 8; i++) {
                    lending[i] = 0;
                }
            }
            break;
        }
        steps++;
    }
    kest_runtime_free(runtime);
    kest_build_free(build);
    remove(where);
    printf("fuzz: %lu step(s) of lending from seed %llu -- begun, read, ended, "
           "ended again, nested and thrown away -- and none of them stopped "
           "this\n",
           steps, (unsigned long long)seed);
    return 0;
}

// References nobody handed out, into a world that is being changed underneath
// them: made, dropped, made again, and read at every point. A reference is a
// number a host can write, so this writes them -- and what is held is that a
// stale one names nothing rather than naming whoever is there now. See D934.
static int fuzz_refs(uint64_t seed, unsigned long many, const char *where) {
    KestBuild *build = NULL;
    KestRuntime *runtime = NULL;
    if (!a_machine_for(PROBING, where, &build, &runtime)) {
        fprintf(stderr, "fuzz: the probing program would not start\n");
        return 1;
    }
    int32_t making = kest_entry(runtime, "world");
    int32_t looking = kest_entry(runtime, "look");
    int32_t dropping = kest_entry(runtime, "drop");
    int32_t putting = kest_entry(runtime, "put");
    int32_t growing = kest_entry(runtime, "grow");
    int32_t counting = kest_entry(runtime, "counted");
    if (making < 0 || looking < 0 || dropping < 0 || putting < 0 ||
        growing < 0 || counting < 0) {
        fprintf(stderr, "fuzz: the probing program is missing a name\n");
        return 1;
    }
    KestValue world[8] = {{0}};
    world[0].integer = 16;
    if (!kest_call(runtime, making, world, 8) ||
        !kest_keeps(runtime, world[0])) {
        fprintf(stderr, "fuzz: a world would not be made\n");
        return 1;
    }
    // The references this machine really handed out, kept so that a stale one
    // is a real one that has been dropped rather than a number that never was.
    int64_t real[32];
    uint32_t reals = 0;
    unsigned long asked = 0;
    for (unsigned long round = 0; round < many; round++) {
        uint64_t state = seed + round * 2246822519u + 3;
        KestValue frame[8] = {{0}};
        frame[0] = world[0];
        switch (next_number(&state) % 3) {
        case 0:
            frame[1].integer = (int64_t)next_number(&state);
            break;
        case 1:
            frame[1].integer =
                reals > 0 ? real[next_number(&state) % reals] : 0;
            break;
        default:
            // A real one moved by a little, which reaches the neighbouring
            // place and the next stamp of the same place.
            frame[1].integer =
                (reals > 0 ? real[next_number(&state) % reals] : 0) +
                (int64_t)(next_number(&state) % 8) - 4;
            break;
        }
        int32_t entry = looking;
        switch (next_number(&state) % 5) {
        case 0: entry = looking; break;
        case 1: entry = dropping; break;
        case 2: entry = putting; frame[2].integer = (int64_t)next_number(&state); break;
        case 3:
            entry = growing;
            frame[1].integer = (int64_t)(next_number(&state) % 1000);
            break;
        default: entry = counting; break;
        }
        if (!kest_call(runtime, entry, frame, 8)) {
            // A refusal is an answer. What would not be one is a machine that
            // could not carry on, which the next round finds out.
            continue;
        }
        if (entry == growing && frame[0].integer < 0) {
            fprintf(stderr, "fuzz: a place made could not be read back\n");
            return 1;
        }
        if (entry == counting && frame[0].integer < 0) {
            fprintf(stderr, "fuzz: a world says it holds %lld\n",
                    (long long)frame[0].integer);
            return 1;
        }
        // And a real one to keep, now and again, so that the stale ones above
        // are references this machine did hand out.
        if ((next_number(&state) % 8) == 0 && reals < 32) {
            KestValue fresh[8] = {{0}};
            fresh[0] = world[0];
            fresh[1].integer = (int64_t)(next_number(&state) % 1000);
            if (kest_call(runtime, growing, fresh, 8)) {
                real[reals++] = fresh[1].integer;
            }
        }
        asked++;
    }
    kest_runtime_free(runtime);
    kest_build_free(build);
    remove(where);
    printf("fuzz: %lu reference(s) from seed %llu asked of a world being "
           "changed underneath them, and none of them stopped this\n",
           asked, (unsigned long long)seed);
    return 0;
}

// Bytes a host hands over as text: any of them, including the one that used to
// end a piece of text and the ones that begin no character. What is held is
// that every one of them is taken and read back as the bytes it was, or
// refused -- and that what comes back is as long as what went in. See D964 and
// D971.
static int fuzz_text(uint64_t seed, unsigned long many, const char *where) {
    KestBuild *build = NULL;
    KestRuntime *runtime = NULL;
    if (!a_machine_for(PROBING, where, &build, &runtime)) {
        fprintf(stderr, "fuzz: the probing program would not start\n");
        return 1;
    }
    // The shapes a character can be cut into: a whole one, one cut short, and
    // a byte that begins none. A run of these is what a host's own buffer is
    // when it has been sliced by length rather than by character.
    // Whole characters first and broken ones after, so that a run can be asked
    // for either: one that is nought before a nought, a character of every
    // width, and then every way a character can be cut short or written as
    // something no character is -- a lead byte with nothing after it, a
    // surrogate, a value past the last one there is, and a byte that only ever
    // follows another.
    static const char *const CHARACTERS[] = {
        "a", "\0", "\n", "\x7f", "\xc4\xb1", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
        "\xc4", "\xe2\x82", "\xe2", "\xf0\x9f\x98", "\xf0", "\xff", "\xc0\x80",
        "\xed\xa0\x80", "\xf4\x90\x80\x80", "\x80",
    };
    static const uint32_t WIDTHS[] = {1, 1, 1, 1, 2, 3, 4,
                                      1, 2, 1, 3, 1, 1, 2, 3, 4, 1};
    unsigned long taken = 0;
    unsigned long refused = 0;
    for (unsigned long round = 0; round < many; round++) {
        uint64_t state = seed + round * 1103515245u + 11;
        static char bytes[4096];
        uint32_t used = 0;
        uint32_t pieces = (uint32_t)(next_number(&state) % 200);
        // Mostly whole characters, because a run that is refused at its first
        // byte says nothing about the bytes after it: what is being asked is
        // that text the machine takes comes back as what went in, and a
        // generator that is refused every time never asks it. One round in
        // four is allowed anything.
        bool anything = (next_number(&state) % 4) == 0;
        uint32_t whole = 7;
        for (uint32_t i = 0; i < pieces; i++) {
            uint32_t which =
                anything ? (uint32_t)(next_number(&state) %
                                      (sizeof(WIDTHS) / sizeof(WIDTHS[0])))
                         : (uint32_t)(next_number(&state) % whole);
            if (used + WIDTHS[which] >= sizeof(bytes)) {
                break;
            }
            memcpy(bytes + used, CHARACTERS[which], WIDTHS[which]);
            used += WIDTHS[which];
        }
        KestValue into[2] = {{0}, {0}};
        if (!kest_text(runtime, bytes, used, into)) {
            // Refused, which is the right answer for bytes that begin no
            // character. What is held is that it still answered with an empty
            // piece of text rather than with nothing at all. See D436.
            if (into[0].text == NULL || into[1].integer != 0) {
                fprintf(stderr,
                        "fuzz: text refused came back as %p of %lld\n",
                        (const void *)into[0].text,
                        (long long)into[1].integer);
                return 1;
            }
            refused++;
            continue;
        }
        // Taken, so it has to be the same bytes and as many of them -- read
        // through the door a host reads text through rather than through the
        // member, because the two are the same bytes today and the door is the
        // one that survives them not being.
        uint32_t length = 0;
        const char *back = kest_text_bytes(&into[0], &length);
        if (length != used || (used > 0 && memcmp(back, bytes, used) != 0)) {
            fprintf(stderr,
                    "fuzz: %u byte(s) of text came back as %u and not the "
                    "same\n",
                    used, length);
            return 1;
        }
        if (!kest_still_holds(runtime, into[0])) {
            fprintf(stderr, "fuzz: text this machine made is not its own\n");
            return 1;
        }
        taken++;
        if ((next_number(&state) % 64) == 0) {
            (void)kest_heap_reset(runtime);
        }
    }
    kest_runtime_free(runtime);
    kest_build_free(build);
    remove(where);
    printf("fuzz: %lu run(s) of bytes from seed %llu handed over as text, %lu "
           "of them refused for beginning no character, and none of them "
           "stopped this\n",
           taken + refused, (unsigned long long)seed, refused);
    return 0;
}

// A program edited underneath a world that is already running: a field added,
// taken away, moved, renamed, or given another type. What is held is the half
// a host cannot do for itself -- that the machine it is already driving is
// still there and still answers, whether the edited program built or not. See
// D985.
static int fuzz_migrate(uint64_t seed, unsigned long many, const char *where) {
    static const char *const EDITS[] = {
        // A field added, which is what a program grows.
        "    id: i32\n    name: text\n    weight: f32\n",
        // One taken away, which is what it loses.
        "    id: i32\n",
        // The same two the other way round, which is the same shape laid out
        // somewhere else.
        "    name: text\n    id: i32\n",
        // One renamed, which nothing about the layout says.
        "    id: i32\n    called: text\n",
        // And one whose type moved under its name, which is the edit a layout
        // mark is for.
        "    id: text\n    name: text\n",
        // A run of things inside a thing, which is a payload inside a payload.
        "    id: i32\n    name: text\n    tags: [i32]\n",
        // And a shape that names itself, which is what a world of references
        // is made of.
        "    id: i32\n    name: text\n    next: ref<Thing>?\n",
    };
    KestBuild *build = NULL;
    KestRuntime *runtime = NULL;
    if (!a_machine_for(PROBING, where, &build, &runtime)) {
        fprintf(stderr, "fuzz: the probing program would not start\n");
        return 1;
    }
    int32_t making = kest_entry(runtime, "world");
    int32_t counting = kest_entry(runtime, "counted");
    KestValue world[8] = {{0}};
    world[0].integer = 8;
    if (making < 0 || counting < 0 || !kest_call(runtime, making, world, 8) ||
        !kest_keeps(runtime, world[0])) {
        fprintf(stderr, "fuzz: a world would not be made\n");
        return 1;
    }
    char edited[512];
    snprintf(edited, sizeof(edited), "%s.edited", where);
    unsigned long built = 0;
    unsigned long turned_away = 0;
    for (unsigned long round = 0; round < many; round++) {
        uint64_t state = seed + round * 69069u + 13;
        const char *fields =
            EDITS[next_number(&state) % (sizeof(EDITS) / sizeof(EDITS[0]))];
        static char source[1 << 14];
        const char *whole = PROBING;
        const char *opened = strstr(whole, "struct Thing {\n");
        const char *closed = strstr(whole, "}\n\nfn world");
        if (opened == NULL || closed == NULL) {
            fprintf(stderr, "fuzz: the probing program has no shape in it\n");
            return 1;
        }
        size_t front = (size_t)(opened - whole) + strlen("struct Thing {\n");
        size_t used = 0;
        memcpy(source, whole, front);
        used = front;
        memcpy(source + used, fields, strlen(fields));
        used += strlen(fields);
        memcpy(source + used, closed, strlen(closed) + 1);
        // Built beside the one that is running, which is what a reload is: the
        // machine under this host is not touched until the candidate has
        // answered, and a candidate that will not build leaves it alone.
        KestBuild *candidate = NULL;
        KestRuntime *fresh = NULL;
        if (a_machine_for(source, edited, &candidate, &fresh)) {
            int32_t there = kest_entry(fresh, "world");
            KestValue theirs[8] = {{0}};
            theirs[0].integer = 4;
            if (there >= 0) {
                (void)kest_call(fresh, there, theirs, 8);
            }
            // And the old world asked of the new machine, which is the one
            // thing a host must not do and the machine must refuse.
            KestValue crossed[8] = {{0}};
            crossed[0] = world[0];
            int32_t counted_there = kest_entry(fresh, "counted");
            if (counted_there >= 0 &&
                kest_call(fresh, counted_there, crossed, 8)) {
                fprintf(stderr,
                        "fuzz: a world of one machine was counted by "
                        "another\n");
                return 1;
            }
            kest_runtime_free(fresh);
            kest_build_free(candidate);
            built++;
        } else {
            turned_away++;
        }
        // Whatever happened to the candidate, the world this host is driving
        // is still there and still answers with what it holds.
        KestValue asking[8] = {{0}};
        asking[0] = world[0];
        if (!kest_call(runtime, counting, asking, 8) ||
            asking[0].integer != 8) {
            fprintf(stderr,
                    "fuzz: the world this host was driving answered %lld "
                    "after a reload was tried\n",
                    (long long)asking[0].integer);
            return 1;
        }
    }
    kest_runtime_free(runtime);
    kest_build_free(build);
    remove(where);
    remove(edited);
    printf("fuzz: %lu edit(s) from seed %llu tried under a world that was "
           "running, %lu of them refused, the world still answering after "
           "every one, and none of them stopped this\n",
           built + turned_away, (unsigned long long)seed, turned_away);
    return 0;
}

// Which boundary to put through it. `source` is the one this file began as and
// stays the one a run with nothing said gets, so every reader of this that was
// written before the others is reading what it always read.
// Instructions nobody's compiler wrote. A program is compiled, a few bytes of
// one of its bodies are changed -- an instruction for another, an operand for
// any number, a bit turned over -- and the verifier is asked about the module.
// What this holds is the verifier's half of the first promise in
// `SECURITY.md`: whatever it lets through, the machine runs without reading or
// writing what it does not own. So a module the verifier refuses is an answer,
// and one it lets through is run, under the sanitisers and with ceilings, and
// has to answer or be refused in words the way anything else does. A report
// from the sanitisers here is a chunk the verifier should have refused. See
// D1253.
static const char *const PROVEN[] = {
    "examples/math.kest",   "examples/vectors.kest", "examples/boxes.kest",
    "examples/words.kest",  "examples/tree.kest",    "examples/state.kest",
    "examples/queue.kest",  "examples/player.kest",
};

// What the programs above write is not what is asked about, so it goes
// nowhere; the door has to be there for a machine to start at all.
static void written_nowhere(KestValue *frame, KestRuntime *runtime,
                            void *context) {
    (void)frame;
    (void)runtime;
    (void)context;
}

static int fuzz_chunks(uint64_t seed, unsigned long many, const char *where) {
    (void)where;
    KestHost *host = kest_host_new();
    if (host == NULL ||
        !kest_host_bind(host, "Io.write", written_nowhere, NULL)) {
        fprintf(stderr, "fuzz: no host to start a machine with\n");
        return 1;
    }
    // Each program is built once and its bytes are put back after every
    // round. Neither the verifier nor the machine writes into a module, so a
    // round sees the program as compiled with only its own changes in it --
    // and building it again every round was most of what this boundary cost:
    // thirty-three minutes on the arm64 runner, for rounds the verifier
    // refused in a moment. See D1260.
    enum { PROGRAMS = sizeof(PROVEN) / sizeof(PROVEN[0]) };
    KestBuild *built[PROGRAMS];
    for (size_t i = 0; i < PROGRAMS; i++) {
        built[i] = kest_build(PROVEN[i], NULL, NULL, KEST_FORM_TEXT, 0);
        if (built[i] == NULL) {
            fprintf(stderr, "fuzz: `%s` does not build, so there is nothing "
                            "to break\n", PROVEN[i]);
            return 1;
        }
    }
    unsigned long refused = 0;
    unsigned long held = 0;
    unsigned long ran = 0;
    int status = 0;
    for (unsigned long round = 0; round < many && status == 0; round++) {
        uint64_t state = (seed << 20) + round + 1;
        KestBuild *build = built[next_number(&state) % PROGRAMS];
        KestModule *module = &build->module;
        KestChunk *chunk = NULL;
        for (int tries = 0; tries < 8 && chunk == NULL; tries++) {
            KestChunk *one =
                module->functions[next_number(&state) % module->count];
            chunk = one->code_count > 2 ? one : NULL;
        }
        if (chunk == NULL) {
            continue;
        }
        uint8_t *kept = malloc(chunk->code_count);
        KestArena *scratch = kest_arena_new();
        if (kept == NULL || scratch == NULL) {
            free(kept);
            kest_arena_free(scratch);
            fprintf(stderr, "fuzz: no room to keep what a chunk was\n");
            status = 1;
            break;
        }
        memcpy(kept, chunk->code, chunk->code_count);
        // Between one and four things done to it. The bytes are the chunk's
        // own, so a change is made where the machine would read it.
        uint32_t doings = (uint32_t)(next_number(&state) % 4) + 1;
        for (uint32_t d = 0; d < doings; d++) {
            uint32_t at = (uint32_t)(next_number(&state) % chunk->code_count);
            switch (next_number(&state) % 4) {
            case 0:
                chunk->code[at] = (uint8_t)(next_number(&state) % KEST_OP_COUNT);
                break;
            case 1:
                chunk->code[at] = (uint8_t)next_number(&state);
                break;
            case 2:
                chunk->code[at] ^= (uint8_t)(1u << (next_number(&state) % 8));
                break;
            default: {
                // An operand near what it was, which is where a verifier that
                // checks against one number too many lets one through.
                if (at + 1 < chunk->code_count) {
                    uint16_t near = (uint16_t)(chunk->code[at] |
                                               (chunk->code[at + 1] << 8));
                    near = (uint16_t)(near + (next_number(&state) % 5) - 2);
                    chunk->code[at] = (uint8_t)(near & 0xff);
                    chunk->code[at + 1] = (uint8_t)(near >> 8);
                }
                break;
            }
            }
        }
        KestDiags said;
        kest_diags_init(&said, scratch);
        if (!kest_module_prove(module, scratch, &said)) {
            refused++;
        } else {
            held++;
            KestLimits bounded = {4096, 64, 4 * 1024 * 1024, 200000};
            KestRuntime *runtime = kest_start(build, host, &bounded);
            if (runtime != NULL) {
                int32_t entry = kest_entry(runtime, "main");
                if (entry >= 0) {
                    KestValue frame[16];
                    memset(frame, 0, sizeof frame);
                    kest_call(runtime, entry, frame,
                              sizeof frame / sizeof frame[0]);
                    ran++;
                }
                kest_runtime_free(runtime);
            }
        }
        memcpy(chunk->code, kept, chunk->code_count);
        free(kept);
        kest_arena_free(scratch);
    }
    for (size_t i = 0; i < PROGRAMS; i++) {
        kest_build_free(built[i]);
    }
    kest_host_free(host);
    if (status != 0) {
        return status;
    }
    printf("fuzz: %lu chunk(s) changed from seed %llu: %lu refused by the "
           "verifier, %lu let through and %lu of those run, and none of them "
           "stopped this\n",
           many, (unsigned long long)seed, refused, held, ran);
    return 0;
}

int main(int argc, char **argv) {
    uint64_t seed = argc > 1 ? strtoull(argv[1], NULL, 10) : 1;
    unsigned long many = argc > 2 ? strtoul(argv[2], NULL, 10) : 200;
    if (seed == 0) {
        seed = 1;
    }
    const char *where = argc > 3 ? argv[3] : "fuzz.kest";
    const char *what = argc > 4 ? argv[4] : "source";

    if (strcmp(what, "source") == 0) {
        return fuzz_source(seed, many, where);
    }
    if (strcmp(what, "handles") == 0) {
        return fuzz_handles(seed, many, where);
    }
    if (strcmp(what, "lends") == 0) {
        return fuzz_lends(seed, many, where);
    }
    if (strcmp(what, "refs") == 0) {
        return fuzz_refs(seed, many, where);
    }
    if (strcmp(what, "text") == 0) {
        return fuzz_text(seed, many, where);
    }
    if (strcmp(what, "migrate") == 0) {
        return fuzz_migrate(seed, many, where);
    }
    if (strcmp(what, "chunks") == 0) {
        return fuzz_chunks(seed, many, where);
    }
    fprintf(stderr,
            "fuzz: `%s` is not a boundary this puts anything through; they "
            "are source, handles, lends, refs, text, migrate and chunks\n",
            what);
    return 2;
}
