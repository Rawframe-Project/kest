#include <math.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "kest.h"
#include "ast.h"
#include "build.h"
#include "diag.h"
#include "lexer.h"
#include "mem.h"
#include "parser.h"
#include "fmt.h"
#include "loader.h"
#include "vm.h"
#include "types.h"

// The functions this command line calls. `main` is the language's name and is
// in `kest.h`; the two handlers are this command's own, and everything here
// that names one of them names it from these — the lists it asks about, the
// lookups it does, and the line the usage prints.
#define TICK_BULK "onEvents"
#define TICK_SINGLE "onEvent"
static const char *const RUN_CALLS[] = {KEST_MAIN, NULL};
static const char *const TICK_CALLS[] = {TICK_BULK, TICK_SINGLE, NULL};
static const char *const EVERY_CALL[] = {KEST_MAIN, TICK_BULK, TICK_SINGLE,
                                         NULL};


// What the command does, written where a person asking for it will look:
// standard output, and not an error.
static void help(FILE *out) {
    fprintf(out,
            "kest %s\n"
            "\n"
            "usage: kest <command> <file>... [options]\n"
            "\n"
            "These read a program, which is the files named and everything\n"
            "they import. The first settles where imports resolve from, and\n"
            "for `run` and `tick` it is the one whose `main` is called.\n"
            "\n"
            "  check <file>...   resolve everything and report what is wrong\n"
            "  run <file>...     compile and run `main`\n"
            "  emit <file>...    print the bytecode\n"
            "  call <file> <fn> [argument]...\n"
            "                    call one function and print what it gives\n"
            "  tick <file> [n]   call `" TICK_BULK "` once with n events, "
            "and\n"
            "                    `" TICK_SINGLE "` n times, whichever are "
            "defined\n"
            "\n"
            "These read each file on its own and follow no imports, because\n"
            "what a file is does not depend on what it imports.\n"
            "\n"
            "  fmt <file>...     print the file in the one form it has\n"
            "  parse <file>...   print the syntax tree\n"
            "  lex <file>...     print the token stream, whatever is wrong\n"
            "\n"
            "  help              this\n"
            "\n"
            "options:\n"
            "  --json            everything this command says, as JSON, one\n"
            "                    object a file: the diagnostics, and for\n"
            "                    `check` what the program holds, for `emit`\n"
            "                    the instructions, for `call` what came\n"
            "                    back, for `tick` the crossings and the\n"
            "                    heap, and for `fmt` whether the file is in\n"
            "                    the one form\n"
            "  -w                fmt writes each file it is given\n"
            "  --check           fmt names the files it would rewrite, without\n"
            "                    writing them, and exits non-zero\n"
            "  --reset           tick throws the heap away between events\n"
            "  --version         print the version\n"
            "\n"
            "exit status is 1 when anything was reported, and otherwise what\n"
            "`main` returned, which has to be a number from 0 to 255.\n"
            "\n"
            "KEST_LIB says where the standard library is. Without it the\n"
            "compiler looks beside itself and then where it was installed.\n",
            kest_version());
}

static int usage(void) {
    help(stderr);
    return 1;
}

static void dump_tokens(const KestToken *tokens, uint32_t count,
                        const KestSource *source) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t line = 0;
        uint32_t column = 0;
        kest_source_locate(source, tokens[i].span.offset, &line, &column);
        printf("%4u:%-3u %-14s %.*s\n", line, column,
               kest_token_name(tokens[i].kind), (int)tokens[i].span.length,
               source->text + tokens[i].span.offset);
    }
}

static void host_sqrt(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = sqrt(frame[0].real);
}

static void host_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    fputs(frame[0].text, (FILE *)context);
}

// What `std.io` declares. Where it goes is the host's, which is the whole
// point of it being the host's: when the caller asked for JSON on standard
// output, the program's own writing goes to standard error so that what is
// left is JSON.
static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    fputs(frame[0].text, (FILE *)context);
}

// What the standard library declares and every host has to provide. A program
// that never reaches one of these never asks for it.
static void math_sqrt(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = sqrt(frame[0].real);
}

static void math_floor(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = floor(frame[0].real);
}

static void math_ceil(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = ceil(frame[0].real);
}

static void math_sin(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = sin(frame[0].real);
}

static void math_cos(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = cos(frame[0].real);
}

static void math_pow(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = pow(frame[0].real, frame[1].real);
}

static void host_clock(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = (int64_t)clock() * 1000000 / CLOCKS_PER_SEC;
}

// Memory this program owns, handed to Kest without copying it. A real engine
// would hand over its particle positions the same way.
#define HOST_SAMPLE_COUNT 1024
static float host_samples[HOST_SAMPLE_COUNT];

// Reads the host's own array, so a program writing through the view it was
// lent can be shown to have written here.
static void host_sample(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    int64_t index = frame[0].integer;
    frame[0].real = index >= 0 && index < HOST_SAMPLE_COUNT
                        ? host_samples[index]
                        : -1.0f;
}

static void host_samples_view(KestValue *frame, KestRuntime *runtime,
                              void *context) {
    (void)context;
    for (uint32_t i = 0; i < HOST_SAMPLE_COUNT; i++) {
        host_samples[i] = (float)i * 0.5f;
    }
    frame[0] = kest_borrow(runtime, host_samples, HOST_SAMPLE_COUNT, "f32",
                           sizeof(float));
}

// A host with no engine in it. `examples/embed` binds this to something that
// asks the program; the command line has nothing to ask with, and says so by
// answering the same thing every time.
static void engine_decide(KestValue *frame, KestRuntime *runtime,
                          void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = 1;
}

static KestHost *make_host(FILE *output) {
    KestHost *host = kest_host_new();
    if (host == NULL) {
        return NULL;
    }
    if (!kest_host_bind(host, "Host.sqrt", host_sqrt, NULL) ||
        !kest_host_bind(host, "Host.write", host_write, output) ||
        !kest_host_bind(host, "Host.clock", host_clock, NULL) ||
        !kest_host_bind(host, "Host.samples", host_samples_view, NULL) ||
        !kest_host_bind(host, "Host.sample", host_sample, NULL) ||
        !kest_host_bind(host, "Math.sqrt", math_sqrt, NULL) ||
        !kest_host_bind(host, "Math.floor", math_floor, NULL) ||
        !kest_host_bind(host, "Math.ceil", math_ceil, NULL) ||
        !kest_host_bind(host, "Math.sin", math_sin, NULL) ||
        !kest_host_bind(host, "Math.cos", math_cos, NULL) ||
        !kest_host_bind(host, "Math.pow", math_pow, NULL) ||
        !kest_host_bind(host, "Engine.decide", engine_decide, NULL) ||
        !kest_host_bind(host, "Io.write", io_write, output)) {
        kest_host_free(host);
        return NULL;
    }
    return host;
}

#define MAX_EVENTS 65536

// The host calling into the program, in both shapes W11 measured. One call
// carrying the batch is the shape D007 makes the default; one call per event
// is kept because it has to remain expressible.
// Whether the program's entry takes what this host has to hand it and gives
// back something this host can read. `tick` carries a batch of `i32`, and a
// program whose `onEvents` takes something else is told rather than handed the
// wrong bytes. What it gives is the other direction of the same rule: tick
// reads a slot as a whole number, so a handler giving `text` has its pointer
// added up and printed as a total, which is not a measurement of anything.
//
// Giving nothing is allowed and is not a number: `gives` says which, because
// a handler that answers nothing and one that answers nought are two things
// and this prints them the same way otherwise.
static bool takes_events(KestProgram *program, const char *name,
                         const char *shape, KestArena *arena, bool *gives,
                         bool *there) {
    KestSymbol *entry =
        kest_lookup_global(program, name, strlen(name));
    *there = entry != NULL && entry->type->tag == KEST_T_FN;
    if (!*there) {
        return false;
    }
    // What is wrong is with the declaration, so it is said where the
    // declaration is: a name qualified by its module is how this host found
    // it and not how the file reads.
    const char *plain = strrchr(name, '.');
    plain = plain == NULL ? name : plain + 1;
    kest_diags_in(program->diags, entry->source);

    if (entry->type->type_param_count > 0) {
        // Nothing calls a handler from inside the file, so a generic one has
        // no copy to run and `kest_entry` finds nothing. Saying why is here,
        // where the declaration is, rather than there, where there is only a
        // name that is not in the module.
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0622",
                       entry->span,
                       "`%s` is generic, and tick has no type to make the "
                       "copy from",
                       plain);
        kest_diags_suggest(program->diags, "take `%s` and nothing else", shape);
        return false;
    }
    if (entry->type->param_count != 1) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0619",
                       entry->span,
                       "`%s` takes %u parameters, and tick passes one", plain,
                       entry->type->param_count);
        kest_diags_suggest(program->diags, "take `%s`", shape);
        return false;
    }
    const char *written = kest_type_name(arena, entry->type->params[0]);
    if (strcmp(written, shape) != 0) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0619",
                       entry->span, "`%s` takes `%s`, and tick has `%s` to "
                                    "give it",
                       plain, written, shape);
        kest_diags_suggest(program->diags, "take `%s`", shape);
        return false;
    }
    const KestType *result = entry->type->result;
    *gives = result != NULL && result->tag != KEST_T_VOID;
    if (*gives && result->tag != KEST_T_INT) {
        kest_diags_add(program->diags, KEST_SEVERITY_ERROR, "K0620",
                       entry->span,
                       "`%s` gives `%s`, and tick reads what comes back as a "
                       "whole number",
                       plain, kest_type_name(arena, result));
        kest_diags_suggest(program->diags, "give an integer, or give nothing");
        return false;
    }
    return true;
}

// What driving a program with events found. It is filled rather than printed,
// because the same numbers are read by a person and by whatever asked for
// JSON, and two writers of one answer come apart.
typedef struct {
    bool ran;
    // A name that is there, whether or not it could be driven. What is wrong
    // with one that cannot has already been said by the time this is read, so
    // saying there is nothing here would be the second and wrong answer.
    bool named;
    // The nearest name to the two it looked for, when neither is there. A
    // program that misspells its own entry point is told that nothing takes
    // events, which is true and is not the answer.
    const char *near;
    bool bulk;
    bool bulk_gives;
    int64_t bulk_gave;
    bool single;
    bool single_gives;
    int32_t crossings;
    int64_t single_gave;
    size_t peak;
    size_t heap;
} Ticked;

static void drive_events(KestRuntime *runtime, KestBuild *build, int32_t count,
                         bool reset, Ticked *out) {
    KestProgram *program = build->program;
    KestArena *arena = build->arena;
    static int32_t events[MAX_EVENTS];
    for (int32_t i = 0; i < count; i++) {
        events[i] = i;
    }

    // The name the file registered them under, which is the one thing a
    // caller has to ask for and does not otherwise know.
    const char *bulk = kest_build_name(build, TICK_BULK);
    const char *single = kest_build_name(build, TICK_SINGLE);

    // Whether a handler is there and whether it can be driven is one question
    // asked in one place. It used to be two — the compiled name and the
    // declared one — and two ways of asking leave a path where neither
    // answers: a name one of them has and the other does not drove nothing and
    // said nothing.
    bool bulk_there = false;
    bool single_there = false;
    bool bulk_gives = false;
    bool single_gives = false;
    bool drive_bulk =
        takes_events(program, bulk, "[i32]", arena, &bulk_gives, &bulk_there);
    bool drive_single =
        takes_events(program, single, "i32", arena, &single_gives,
                     &single_there);
    out->named = bulk_there || single_there;
    if (!out->named) {
        out->near = kest_nearest_global(program, single, strlen(single));
        if (out->near == NULL) {
            out->near = kest_nearest_global(program, bulk, strlen(bulk));
        }
        return;
    }

    // Found once. What a name means is a search, and a per-event crossing is
    // the shape that would pay for it a thousand times a frame. A name that
    // is declared and has nothing to call is `kest_entry`'s to explain.
    int32_t bulk_at = drive_bulk ? kest_entry(runtime, bulk) : -1;
    int32_t single_at = drive_single ? kest_entry(runtime, single) : -1;

    bool gives = bulk_gives;
    if (bulk_at >= 0) {
        KestValue frame[1];
        frame[0] = kest_borrow(runtime, events, (uint32_t)count, "i32",
                               sizeof(int32_t));
        if (kest_call(runtime, bulk_at, frame, 1)) {
            out->bulk = true;
            out->bulk_gives = gives;
            out->bulk_gave = gives ? frame[0].integer : 0;
        }
    }

    gives = single_gives;
    if (single_at >= 0) {
        int64_t total = 0;
        size_t peak = 0;
        for (int32_t i = 0; i < count; i++) {
            KestValue frame[1];
            frame[0].integer = events[i];
            if (!kest_call(runtime, single_at, frame, 1)) {
                return;
            }
            if (gives) {
                total += frame[0].integer;
            }
            if (kest_heap_used(runtime) > peak) {
                peak = kest_heap_used(runtime);
            }
            // Nothing of the program's survives a call, so between two of
            // them there is nothing left pointing at the heap.
            if (reset && !kest_heap_reset(runtime)) {
                return;
            }
        }
        out->single = true;
        out->single_gives = gives;
        out->crossings = count;
        out->single_gave = total;
        out->peak = peak;
    }
}

typedef enum {
    FORMAT_PRINT,
    FORMAT_WRITE,
    FORMAT_CHECK,
} FormatMode;

// Writes through a file beside the target and renames over it, so a program
// that stops half way leaves the file it was given rather than half of it.
static bool replace_file(const char *path, const char *text, size_t length) {
    size_t room = strlen(path) + 16;
    char *temporary = malloc(room);
    if (temporary == NULL) {
        return false;
    }
    snprintf(temporary, room, "%s.kest-fmt", path);

    FILE *file = fopen(temporary, "wb");
    if (file == NULL) {
        free(temporary);
        return false;
    }
    bool written = fwrite(text, 1, length, file) == length;
    if (fclose(file) != 0 || !written || rename(temporary, path) != 0) {
        remove(temporary);
        free(temporary);
        return false;
    }
    free(temporary);
    return true;
}

typedef enum {
    FILE_LEX,
    FILE_PARSE,
    FILE_FORMAT,
} FileCommand;

// `lex`, `parse` and `fmt` read a file and follow nothing: what a file is does
// not depend on what it imports, and each is its own answer, so one that
// cannot be read does not stop the rest.
static int per_file(char **paths, int count, FileCommand what, FormatMode mode,
                    bool json) {
    int status = 0;

    for (int i = 0; i < count; i++) {
        KestArena *arena = kest_arena_new();
        if (arena == NULL) {
            fprintf(stderr, "kest: out of memory\n");
            return 1;
        }

        KestDiags diags;
        kest_diags_init(&diags, arena);
        KestUnits units = {0};
        bool loaded =
            kest_load_alone(arena, &diags, paths[i], &units) && units.count > 0;
        bool read = loaded && diags.error_count == 0;

        // A token stream is whole whatever was wrong with the file: the lexer
        // makes a token for what it could not read and carries on. A tree is
        // not — what was refused is missing from it — so it is shown with a
        // line saying so. What that line answers is the objection to showing
        // it at all, which was that it would say the file is something it is
        // not.
        bool show = loaded;

        if (what != FILE_FORMAT) {
            if (show && !json) {
                if (count > 1) {
                    printf("// %s\n", paths[i]);
                }
                if (what == FILE_LEX) {
                    uint32_t found = 0;
                    // Lexed again to show it, and muted while it is: what is
                    // wrong with the file was said when it was read, and
                    // saying it twice is worse than not saying it once.
                    kest_diags_mute(&diags, true);
                    KestToken *tokens = kest_lex_all(
                        arena, &units.items[0].source, &diags, &found);
                    kest_diags_mute(&diags, false);
                    dump_tokens(tokens, found, &units.items[0].source);
                } else {
                    if (!read) {
                        printf("// this is what parsed; %u thing%s refused\n",
                               diags.error_count,
                               diags.error_count == 1 ? "" : "s");
                    }
                    kest_ast_dump(&units.items[0].unit,
                                  &units.items[0].source, stdout);
                }
            }
            kest_diags_sort(&diags);
            if (json) {
                kest_diags_render_json(&diags, stdout);
            } else {
                kest_diags_render(&diags, stderr);
            }
            if (diags.error_count > 0) {
                status = 1;
            }
            kest_arena_free(arena);
            continue;
        }

        size_t length = 0;
        const char *text = NULL;
        if (read) {
            text = kest_format(&units.items[0].unit, &units.items[0].source,
                               arena, &length);
        }

        const KestSource *source = loaded ? &units.items[0].source : NULL;
        bool same = text != NULL && source != NULL &&
                    length == source->length &&
                    memcmp(text, source->text, length) == 0;

        // What this command says, for whatever is reading it rather than for a
        // person: one object a file, saying whether it is already in the one
        // form and what was wrong with it if anything was. The formatted text
        // is not said, it is printed, and a stream that is JSON and a file's
        // contents at once is neither.
        if (json) {
            kest_diags_sort(&diags);
            fputc('{', stdout);
            kest_diags_write_json(&diags, stdout);
            fputs(",\"file\":", stdout);
            kest_json_text(paths[i], stdout);
            // Whether a file is in the one form is a question about a
            // program, and a file that did not parse is not one: null is the
            // answer that says there was none, which is a different thing
            // from a file that is not in the form yet. One of the two is
            // fixed by running `-w` and the other is not.
            fprintf(stdout, ",\"formed\":%s}\n",
                    text == NULL ? "null" : same ? "true" : "false");
            if (text == NULL || (!same && mode == FORMAT_CHECK)) {
                status = 1;
            }
            if (text != NULL && !same && mode == FORMAT_WRITE &&
                !replace_file(paths[i], text, length)) {
                status = 1;
            }
            kest_arena_free(arena);
            continue;
        }

        if (text == NULL) {
            kest_diags_sort(&diags);
            // The list `--check` prints is the files `-w` would rewrite, so
            // that what answers the question can be acted on. A file that did
            // not parse is not one of them: running `-w` over it does nothing,
            // and a name in that list that nothing fixes is a name a tool
            // comes back to. It is on the standard error instead, with the
            // diagnostics that say what is wrong, and the status is 1 either
            // way.
            kest_diags_render(&diags, stderr);
            // What this prints is meant to go back over the file, so it is
            // the one command that shows nothing after a mistake — a form of
            // half a program would delete the other half. Saying so is the
            // difference between refusing and appearing to do nothing.
            fprintf(stderr,
                    "kest: `%s` is not formatted, because what `fmt` writes "
                    "has to be the same program and this one did not parse\n",
                    paths[i]);
            status = 1;
            kest_arena_free(arena);
            continue;
        }

        if (mode == FORMAT_PRINT) {
            fwrite(text, 1, length, stdout);
        } else if (same) {
            // Nothing to say about a file that is already right, and nothing
            // to write to it either.
        } else if (mode == FORMAT_CHECK) {
            printf("%s\n", paths[i]);
            status = 1;
        } else if (replace_file(paths[i], text, length)) {
            printf("%s\n", paths[i]);
        } else {
            fprintf(stderr, "kest: cannot write '%s'\n", paths[i]);
            status = 1;
        }

        kest_arena_free(arena);
    }
    return status;
}

// A value written the way the command line was given one. Anything that is
// not a number, a truth or a piece of text cannot be typed at a shell, and
// saying so beats guessing.
static bool read_argument(const char *text, const KestType *type,
                          KestValue *into, const char **why) {
    char *end = NULL;
    switch (type->tag) {
    case KEST_T_INT: {
        long long value = strtoll(text, &end, 0);
        if (end == text || *end != '\0') {
            *why = "is not a number";
            return false;
        }
        into->integer = value;
        return true;
    }
    case KEST_T_FLOAT: {
        double value = strtod(text, &end);
        if (end == text || *end != '\0') {
            *why = "is not a number";
            return false;
        }
        into->real = type->width == 32 ? (double)(float)value : value;
        return true;
    }
    case KEST_T_BOOL:
        if (strcmp(text, "true") == 0 || strcmp(text, "false") == 0) {
            into->integer = strcmp(text, "true") == 0;
            return true;
        }
        *why = "is not `true` or `false`";
        return false;
    case KEST_T_TEXT:
        into->text = text;
        return true;
    default:
        *why = "cannot be written at a shell";
        return false;
    }
}

// What came back, written the way the language writes it.
// What the language writes for a value, which is what a hole in a string is
// filled with and nothing else: a value printed here reads the same as one
// printed by the program. NULL when there is nothing to write — a function
// that gives nothing back, and then `without` is NULL, or one that gives back
// something the language has no text for, and then `without` names it.
static const char *result_text(const KestValue *frame, const KestType *type,
                               KestArena *arena, char *buffer, size_t room,
                               const KestType **without) {
    *without = NULL;
    if (type->tag == KEST_T_VOID) {
        return NULL;
    }
    if (!kest_type_has_text(type, without)) {
        return NULL;
    }
    // Text on its own is the content and not the source that spells it, which
    // is the exception D035 names and the reason a hole holding one is not
    // written through this at all.
    if (type->tag == KEST_T_TEXT) {
        return frame[0].text;
    }
    size_t needed = kest_write_value(NULL, 0, type, frame);
    char *out = needed + 1 <= room ? buffer
                                   : kest_arena_alloc(arena, needed + 1, 1);
    if (out == NULL) {
        return NULL;
    }
    kest_write_value(out, needed, type, frame);
    out[needed] = '\0';
    return out;
}

// Whether what was typed is spelled the way a float is. `3` and `3.5` are the
// same characters to `strtod` and are not the same thing to a reader.
static bool spelled_as_float(const char *text) {
    return strpbrk(text, ".eEnN") != NULL;
}

// Which of the functions of that name takes what was typed. The same rule the
// language uses for a literal: any width of the right family, and then the
// width it would have had on its own.
static const KestSymbol *choose(KestBuild *build, const char *name,
                                char **args, int count) {
    KestSymbol *candidates[16];
    uint32_t found = kest_overloads(build->program, name, strlen(name),
                                    candidates, 16);
    const KestSymbol *chosen = NULL;
    uint32_t matches = 0;

    for (int pass = 0; pass < 2 && matches != 1; pass++) {
        chosen = NULL;
        matches = 0;
        for (uint32_t i = 0; i < found; i++) {
            if (candidates[i]->type->param_count != (uint32_t)count) {
                continue;
            }
            bool fits = true;
            for (int a = 0; a < count && fits; a++) {
                const KestType *want = candidates[i]->type->params[a];
                KestValue scratch = {0};
                const char *why = NULL;
                fits = read_argument(args[a], want, &scratch, &why);
                if (fits && pass == 1 &&
                    (want->tag == KEST_T_INT || want->tag == KEST_T_FLOAT)) {
                    bool real = spelled_as_float(args[a]);
                    fits = want->tag == (real ? KEST_T_FLOAT : KEST_T_INT) &&
                           want->width == 32 &&
                           (real || want->is_signed);
                }
            }
            if (fits) {
                chosen = candidates[i];
                matches++;
            }
        }
    }
    if (matches == 1) {
        return chosen;
    }

    fprintf(stderr, "kest: %s `%s` takes what was typed\n",
            matches == 0 ? "no" : "more than one", name);
    for (uint32_t i = 0; i < found; i++) {
        fprintf(stderr, "  %s(", candidates[i]->name);
        for (uint32_t p = 0; p < candidates[i]->type->param_count; p++) {
            fprintf(stderr, "%s%s", p == 0 ? "" : ", ",
                    kest_type_name(build->arena, candidates[i]->type->params[p]));
        }
        fprintf(stderr, ") -> %s\n",
                kest_type_name(build->arena, candidates[i]->type->result));
    }
    if (found == 0) {
        fprintf(stderr, "  nothing is called that\n");
    }
    return NULL;
}

// What to give the machine. This command line is a host like any other, and it
// is one with the answer in front of it: a program that says how much room it
// needs gets that much, and one that cannot say gets what a host that says
// nothing gets. Never less than that, because what is measured is the least
// and this host prints from inside the call it makes.
static const KestLimits *room_for(KestBuild *build, const char *const *entries,
                                  KestLimits *least) {
    KestReason why = {KEST_REACH_UNASKED, NULL};
    bool asked = false;
    for (uint32_t i = 0; entries != NULL && entries[i] != NULL; i++) {
        KestLimits one = {0, 0, 0};
        if (kest_needs_of(build, entries[i], &one, &why)) {
            asked = true;
            if (one.stack_slots > least->stack_slots) {
                least->stack_slots = one.stack_slots;
            }
            if (one.call_depth > least->call_depth) {
                least->call_depth = one.call_depth;
            }
            continue;
        }
        // A name the program does not have is a name this host will not call
        // either. One it has and cannot answer for is the whole answer: a
        // number is picked, which is what a host without one does.
        if (why.reach != KEST_REACH_UNASKED) {
            return NULL;
        }
    }
    // Nothing named, or nothing found: the whole program then, which is what a
    // host that has not said which function it calls is given.
    if (!asked && !kest_needs(build, least, &why)) {
        return NULL;
    }
    if (least->stack_slots < KEST_STACK_SLOTS) {
        least->stack_slots = KEST_STACK_SLOTS;
    }
    if (least->call_depth < KEST_CALL_DEPTH) {
        least->call_depth = KEST_CALL_DEPTH;
    }
    return least;
}

static int run(const char *command, const char *executable, char **paths,
               int path_count, bool json, int32_t count, bool reset) {
    KestBuild *build = kest_build_open(kest_library_path(NULL, executable),
                                       paths,
                                       strcmp(command, "call") == 0
                                           ? 1
                                           : path_count);
    if (build == NULL) {
        fprintf(stderr, "kest: out of memory\n");
        return 1;
    }

    bool ticking = strcmp(command, "tick") == 0;
    bool running = strcmp(command, "run") == 0 || ticking;
    bool emitting = strcmp(command, "emit") == 0;
    bool checking = strcmp(command, "check") == 0;
    bool calling = strcmp(command, "call") == 0;
    bool failed_to_choose = false;
    int64_t exit_code = 0;
    // What the called function gave back, which is written once and then
    // either printed or put in the object.
    char wrote[64];
    const char *gave = NULL;
    Ticked ticked = {0};

    if (build->units.count > 0 && build->diags.error_count == 0) {
        const KestSource *root = &build->units.items[0].source;
        if (checking) {
            if (kest_build_check(build) && !json) {
                // The file that was named, which `check` knows without
                // having compiled anything.
                kest_program_dump(build->program, build->arena,
                                  build->units.items[0].alias, stdout);
            }
        } else if (emitting) {
            if (kest_build_emit(build) && !json) {
                kest_module_disassemble(&build->module, EVERY_CALL, stdout);
            }
            // In JSON it goes inside the object below, because a stream that
            // is an object and a listing at once is neither.
        } else if (calling && kest_build_emit(build)) {
            // The first path is the file; the second is what to call, and the
            // rest are what to call it with.
            const KestSymbol *chosen =
                path_count < 2
                    ? NULL
                    : choose(build, kest_build_name(build, paths[1]),
                             paths + 2, path_count - 2);
            if (path_count < 2) {
                fprintf(stderr, "kest: call needs a function\n");
            }
            if (chosen != NULL) {
                KestHost *host = make_host(json ? stderr : stdout);
                KestLimits least = {0, 0, 0};
                KestRuntime *runtime =
                    host == NULL
                        ? NULL
                        : kest_start(build, host,
                                     room_for(build, (const char *[]){paths[1],
                                                                      NULL},
                                              &least));
                if (runtime != NULL) {
                    uint16_t width = chosen->type->slots;
                    for (uint32_t p = 0; p < chosen->type->param_count; p++) {
                        width += chosen->type->params[p]->slots;
                    }
                    KestValue *frame =
                        KEST_ARENA_ARRAY(build->arena, KestValue, width + 1);
                    uint16_t at = 0;
                    const char *why = NULL;
                    for (uint32_t p = 0; p < chosen->type->param_count; p++) {
                        read_argument(paths[2 + p], chosen->type->params[p],
                                      &frame[at], &why);
                        at += chosen->type->params[p]->slots;
                    }
                    if (kest_call(runtime,
                                  kest_entry(runtime, chosen->type->symbol),
                                  frame, width + 1)) {
                        const KestType *without = NULL;
                        gave = result_text(frame, chosen->type->result,
                                           build->arena, wrote, sizeof(wrote),
                                           &without);
                        if (without != NULL) {
                            // The words the compiler uses for the same rule,
                            // because it is the same rule.
                            fprintf(stderr,
                                    "kest: there is no text for `%s`, which is "
                                    "what `%s` gives\n",
                                    kest_type_name(build->arena, without),
                                    paths[1]);
                            fprintf(stderr, "      call something that gives a "
                                            "value with text, or write the "
                                            "fields you want to see\n");
                            // The command is to call and say what came back,
                            // and it did half of that.
                            failed_to_choose = true;
                        } else if (!json && gave != NULL) {
                            printf("%s\n", gave);
                        }
                    }
                    // What running found, sorted with what compiling did. A
                    // host reads this with `kest_report`; one command says
                    // everything it has to say at once, so it takes the set.
                    kest_diags_absorb(&build->diags,
                                      kest_runtime_said(runtime));
                    kest_runtime_free(runtime);
                }
                kest_host_free(host);
            } else {
                failed_to_choose = true;
            }
        } else if (running && kest_build_emit(build)) {
            KestHost *host = make_host(json ? stderr : stdout);
            if (host == NULL) {
                fprintf(stderr, "kest: out of memory\n");
                kest_build_free(build);
                return 1;
            }
            // `run` calls `main` and nothing else, and `tick` calls whichever
            // of the two handlers the file has. Asking about the ones this
            // host will call is asking about what will run.
            KestLimits least = {0, 0, 0};
            KestRuntime *runtime = kest_start(
                build, host,
                room_for(build, ticking ? TICK_CALLS : RUN_CALLS, &least));
            if (runtime != NULL) {
                if (ticking) {
                    drive_events(runtime, build, count, reset, &ticked);
                    // What the program allocated and nothing freed, which is
                    // D012's cost with a number on it.
                    ticked.heap = kest_heap_used(runtime);
                    ticked.ran = true;
                    if (!ticked.bulk && !ticked.single && !ticked.named) {
                        // Driving a program that takes no events looks the
                        // same as driving one that took them and did nothing.
                        // A program that has one and cannot be driven by it
                        // has already been told which, so this stays quiet.
                        KestSpan nowhere = {0, 0};
                        kest_diags_in(&build->diags, root);
                        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                       "K0621", nowhere,
                                       "nothing here takes events");
                        if (ticked.near != NULL) {
                            // Under the name the file wrote, not the one this
                            // host looked it up by.
                            const char *near = strrchr(ticked.near, '.');
                            near = near == NULL ? ticked.near : near + 1;
                            kest_diags_suggest(
                                &build->diags,
                                "`%s` is the nearest name this program has; "
                                "write `onEvents(events: [i32])` or "
                                "`onEvent(event: i32)`",
                                near);
                        } else {
                            kest_diags_suggest(&build->diags,
                                               "write `onEvents(events: "
                                               "[i32])` or `onEvent(event: "
                                               "i32)`");
                        }
                    }
                    if (!json) {
                        if (ticked.bulk) {
                            if (ticked.bulk_gives) {
                                printf("onEvents  1 crossing   returned %lld\n",
                                       (long long)ticked.bulk_gave);
                            } else {
                                printf("onEvents  1 crossing\n");
                            }
                        }
                        if (ticked.single) {
                            if (ticked.single_gives) {
                                printf("onEvent   %d crossings returned %lld, "
                                       "peak %zu bytes\n",
                                       ticked.crossings,
                                       (long long)ticked.single_gave,
                                       ticked.peak);
                            } else {
                                printf("onEvent   %d crossings, peak %zu "
                                       "bytes\n",
                                       ticked.crossings, ticked.peak);
                            }
                        }
                        printf("heap      %zu bytes, none of it freed\n",
                               ticked.heap);
                    }
                } else {
                    KestValue frame[1] = {{0}};
                    const char *entry = kest_build_name(build, KEST_MAIN);
                    kest_diags_in(&build->diags, root);
                    int32_t at = kest_entry(runtime, entry);
                    if (at < 0) {
                        KestSpan nowhere = {0, 0};
                        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                       "K0603", nowhere,
                                       "this file has no `main` to run");
                        kest_diags_suggest(&build->diags, "add `fn main() { }`");
                    } else if (kest_call(runtime, at, frame, 1)) {
                        exit_code = frame[0].integer;
                        if (exit_code < 0 || exit_code > 255) {
                            // A process answers in eight bits. Cutting the
                            // number down to fit turns 256 into nought, which
                            // is the one answer that means nothing went wrong,
                            // so it is said rather than cut.
                            KestSpan nowhere = {0, 0};
                            kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                           "K0618", nowhere,
                                           "`main` answered %lld, and an exit "
                                           "status carries 0 to 255",
                                           (long long)exit_code);
                            kest_diags_suggest(&build->diags,
                                               "answer inside that range, and "
                                               "print what does not fit");
                        }
                    }
                }
                kest_diags_absorb(&build->diags, kest_runtime_said(runtime));
                kest_runtime_free(runtime);
            }
            kest_host_free(host);
        } else if (running) {
            // Nothing to say: the diagnostics below say why.
        }
    }

    kest_diags_sort(&build->diags);
    if (json) {
        // One object, with whatever the command has to add beside what it
        // found wrong.
        fputc('{', stdout);
        kest_diags_write_json(&build->diags, stdout);
        if (checking && build->program != NULL) {
            fputc(',', stdout);
            kest_program_dump_json(build->program, build->arena, stdout);
        }
        if (emitting && build->compiled) {
            fputc(',', stdout);
            kest_module_disassemble_json(&build->module, stdout);
        }
        // The one command whose answer is a value says it here rather than
        // beside the JSON, where a person would not look and a tool could not
        // read it.
        if (gave != NULL) {
            fputs(",\"result\":", stdout);
            kest_json_text(gave, stdout);
        }
        if (ticked.ran) {
            if (ticked.bulk) {
                // A handler that gives nothing gave nothing, which is not
                // nought: the field stays so a reader can rely on it and says
                // null so it cannot be added up.
                fputs(",\"onEvents\":{\"crossings\":1,\"gave\":", stdout);
                if (ticked.bulk_gives) {
                    fprintf(stdout, "%lld}", (long long)ticked.bulk_gave);
                } else {
                    fputs("null}", stdout);
                }
            }
            if (ticked.single) {
                fprintf(stdout, ",\"onEvent\":{\"crossings\":%d,\"gave\":",
                        ticked.crossings);
                if (ticked.single_gives) {
                    fprintf(stdout, "%lld", (long long)ticked.single_gave);
                } else {
                    fputs("null", stdout);
                }
                fprintf(stdout, ",\"peak\":%zu}", ticked.peak);
            }
            fprintf(stdout, ",\"heap\":%zu", ticked.heap);
        }
        fputs("}\n", stdout);
    } else {
        kest_diags_render(&build->diags, stderr);
    }

    int status = build->diags.error_count > 0 || failed_to_choose
                     ? 1
                     : (int)(exit_code & 0xff);
    kest_build_free(build);
    return status;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return usage();
    }

    if (strcmp(argv[1], "--version") == 0) {
        printf("kest %s\n", kest_version());
        return 0;
    }

    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        help(stdout);
        return 0;
    }

    bool json = false;
    int32_t count = 1024;
    bool reset = false;
    FormatMode mode = FORMAT_PRINT;
    // Gathered rather than sliced out of argv, because a number among them is
    // how many events to send and not a file to read.
    char **paths = calloc((size_t)argc, sizeof(char *));
    int path_count = 0;
    if (paths == NULL) {
        fprintf(stderr, "kest: out of memory\n");
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json = true;
        } else if (strcmp(argv[i], "-w") == 0) {
            mode = FORMAT_WRITE;
        } else if (strcmp(argv[i], "--check") == 0) {
            mode = FORMAT_CHECK;
        } else if (strcmp(argv[i], "--reset") == 0) {
            reset = true;
        } else if (strcmp(argv[1], "call") == 0) {
            // Everything after the command is the file, the function and what
            // to call it with, in that order.
            paths[path_count++] = argv[i];
        } else if (strcmp(argv[1], "tick") == 0 && path_count > 0 &&
                   argv[i][0] >= '0' && argv[i][0] <= '9') {
            count = atoi(argv[i]);
            if (count < 0 || count > MAX_EVENTS) {
                fprintf(stderr, "kest: between 0 and %d events\n", MAX_EVENTS);
                free(paths);
                return 1;
            }
        } else {
            paths[path_count++] = argv[i];
        }
    }

    bool per_file_command = strcmp(argv[1], "fmt") == 0 ||
                            strcmp(argv[1], "lex") == 0 ||
                            strcmp(argv[1], "parse") == 0;
    if (per_file_command) {
        if (path_count == 0) {
            fprintf(stderr, "kest: %s needs a file\n", argv[1]);
            free(paths);
            return usage();
        }
        FileCommand what = strcmp(argv[1], "fmt") == 0   ? FILE_FORMAT
                           : strcmp(argv[1], "lex") == 0 ? FILE_LEX
                                                         : FILE_PARSE;
        int status = per_file(paths, path_count, what, mode, json);
        free(paths);
        return status;
    }

    if (strcmp(argv[1], "check") == 0 || strcmp(argv[1], "emit") == 0 ||
        strcmp(argv[1], "run") == 0 || strcmp(argv[1], "tick") == 0 ||
        strcmp(argv[1], "call") == 0) {
        if (path_count == 0) {
            fprintf(stderr, "kest: %s needs a file\n", argv[1]);
            free(paths);
            return usage();
        }
        int status =
            run(argv[1], argv[0], paths, path_count, json, count, reset);
        free(paths);
        return status;
    }

    free(paths);
    fprintf(stderr, "kest: unknown command '%s'\n", argv[1]);
    return usage();
}
