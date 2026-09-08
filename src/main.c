#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
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
            "defined.\n"
            "                    `4,5,6` instead of a count lends those, so a\n"
            "                    program that reads what it was given can be\n"
            "                    given something\n"
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

// Where every comment in the file is, for whatever reads a file to show it:
// what the tokens are is in the text form, and a comment is not a token, so
// this is the one way to ask.
static void dump_comments_json(KestArena *arena, const KestSource *source,
                               FILE *out) {
    uint32_t count = kest_comments(source, NULL, 0);
    KestSpan *spans = count == 0 ? NULL
                                 : KEST_ARENA_ARRAY(arena, KestSpan, count);
    if (count > 0 && spans == NULL) {
        return;
    }
    kest_comments(source, spans, count);

    fputs(",\"comments\":[", out);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t line = 0;
        uint32_t column = 0;
        kest_source_locate(source, spans[i].offset, &line, &column);
        fprintf(out, "%s{\"line\":%u,\"column\":%u,\"text\":", i > 0 ? "," : "",
                line, column);
        char *text = kest_arena_strndup(arena, source->text + spans[i].offset,
                                        spans[i].length);
        kest_json_text(text == NULL ? "" : text, out);
        fputc('}', out);
    }
    fputc(']', out);
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

// The angle of a direction, which is the one thing a program cannot build out
// of the others: everything else `std.math` asks a host for is a rounding or a
// curve it could approximate, and this is the one that turns two numbers into
// where they point.
static void math_atan2(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = atan2(frame[0].real, frame[1].real);
}

// Everything on the standard input, handed over as text. This is the command
// line being a host: `std.io` does not declare it, because a declaration there
// is a thing every host of every program that imports it has to provide, and
// an engine has no standard input. A program that wants this declares it and
// runs under a host that has it.
//
// All of it at once rather than a line at a time, because `std.text` splits
// and a program that reads a line at a time would be asking a host to keep a
// place in a file between calls.
static void io_read(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    size_t room = 4096;
    size_t held = 0;
    char *bytes = malloc(room);
    if (bytes == NULL) {
        frame[0] = kest_text(runtime, "", 0);
        return;
    }
    for (;;) {
        size_t read = fread(bytes + held, 1, room - held, stdin);
        held += read;
        if (held < room) {
            break;
        }
        char *grown = realloc(bytes, room * 2);
        if (grown == NULL) {
            break;
        }
        bytes = grown;
        room *= 2;
    }
    frame[0] = kest_text(runtime, bytes, (uint32_t)held);
    free(bytes);
}

// Which host is running this, handed over as text the machine owns. A pointer
// of this host's own would be a promise to keep it as long as the program
// holds it, and a program holds a piece of text for as long as it likes.
static void engine_name(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    frame[0] = kest_text(runtime, "kest", 4);
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
        !kest_host_bind(host, "Math.atan2", math_atan2, NULL) ||
        !kest_host_bind(host, "Engine.decide", engine_decide, NULL) ||
        !kest_host_bind(host, "Engine.name", engine_name, NULL) ||
        !kest_host_bind(host, "Io.read", io_read, NULL) ||
        !kest_host_bind(host, "Io.write", io_write, output)) {
        kest_host_free(host);
        return NULL;
    }
    return host;
}

// How many events a single run will make. Not a limit the language has: it is
// how many this command is willing to lend at once, and it is here so that a
// mistyped number is answered rather than turned into a run nobody wanted.
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
                         const int32_t *given, bool reset, Ticked *out) {
    KestProgram *program = build->program;
    KestArena *arena = build->arena;
    // As many as were asked for. This was a static run of the largest number
    // allowed — a quarter of a megabyte of the command line's own bytes,
    // there whether it was asked for one event or none, and mutable state
    // hanging off nothing, which this project does not keep.
    int32_t *events = count > 0 ? KEST_ARENA_ARRAY(arena, int32_t,
                                                   (uint32_t)count)
                                : NULL;
    if (count > 0 && events == NULL) {
        return;
    }
    for (int32_t i = 0; i < count; i++) {
        // Counted up from nought, unless the caller wrote down which ones
        // they wanted: a program whose answer depends on what it was given
        // has to be able to say what it was given.
        events[i] = given == NULL ? i : given[i];
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
            if (json && what == FILE_LEX) {
                // What a file is made of, which is its tokens and the comments
                // between them. The tokens are what the text form prints; the
                // comments are not tokens and are printed nowhere else.
                fputc('{', stdout);
                kest_diags_write_json(&diags, stdout);
                if (loaded) {
                    dump_comments_json(arena, &units.items[0].source, stdout);
                }
                fputs("}\n", stdout);
            } else if (json) {
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
            // Two reasons and two sentences: a file that was read and does
            // not parse, and a file that was never read at all. Saying the
            // first about the second sends a reader looking for a mistake in
            // a file that is not there.
            if (loaded) {
                fprintf(stderr,
                        "kest: `%s` is not formatted, because what `fmt` "
                        "writes has to be the same program and this one did "
                        "not parse\n",
                        paths[i]);
            } else {
                fprintf(stderr,
                        "kest: `%s` is not formatted, because it was not "
                        "read\n",
                        paths[i]);
            }
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
// A reason built where it is kept, because it names the type the argument did
// not fit in (D193).
static const char *reason_of(KestArena *arena, const char *format, ...) {
    va_list args;
    va_list again;
    va_start(args, format);
    va_copy(again, args);
    int room = vsnprintf(NULL, 0, format, args);
    va_end(args);
    char *out = room < 0 ? NULL : kest_arena_alloc(arena, (size_t)room + 1, 1);
    if (out != NULL) {
        vsnprintf(out, (size_t)room + 1, format, again);
    }
    va_end(again);
    return out == NULL ? "does not fit" : out;
}

// The ends of a whole number of that width and sign. `u64` is cut short at
// what a signed read can carry, which is the widest thing the shell can hand
// over anyway.
static void ends_of(const KestType *type, long long *low, long long *high) {
    if (type->is_signed) {
        *high = type->width >= 64 ? LLONG_MAX
                                  : ((long long)1 << (type->width - 1)) - 1;
        *low = -*high - 1;
        return;
    }
    *low = 0;
    *high = type->width >= 63 ? LLONG_MAX
                              : ((long long)1 << type->width) - 1;
}

static bool read_argument(KestArena *arena, const char *text,
                          const KestType *type, KestValue *into,
                          const char **why) {
    char *end = NULL;
    switch (type->tag) {
    case KEST_T_INT: {
        errno = 0;
        long long value = strtoll(text, &end, 0);
        if (end == text || *end != '\0') {
            *why = "is not a number";
            return false;
        }
        long long low = 0;
        long long high = 0;
        ends_of(type, &low, &high);
        // A number the machine cannot carry, and one it can carry and the
        // type cannot hold. Both were taken before, and what the program got
        // was a number nobody typed.
        if (errno == ERANGE || value < low || value > high) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
            return false;
        }
        into->integer = value;
        return true;
    }
    case KEST_T_FLOAT: {
        errno = 0;
        double value = strtod(text, &end);
        if (end == text || *end != '\0') {
            *why = "is not a number";
            return false;
        }
        if (errno == ERANGE && value != 0.0) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
            return false;
        }
        if (type->width == 32 && value > (double)FLT_MAX) {
            *why = reason_of(arena, "does not fit in `%s`",
                             kest_type_name(arena, type));
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
// What came back, as words. The machine writes it: `kest_gave_text` is what a
// host is given for this, and the command line is one, so it asks the same
// question through the same door rather than reaching past it.
//
// The type is still read here for one thing the answer cannot say: which type
// it was that has no text, which is what the message needs.
static const char *result_text(KestRuntime *runtime, int32_t entry,
                               const KestValue *frame, const KestType *type,
                               KestArena *arena, char *buffer, size_t room,
                               const KestType **without) {
    *without = NULL;
    if (type->tag == KEST_T_VOID) {
        return NULL;
    }
    if (!kest_type_has_text(type, without)) {
        return NULL;
    }
    int64_t needed = kest_gave_text(runtime, entry, frame, buffer, room);
    if (needed < 0) {
        return NULL;
    }
    if ((size_t)needed + 1 <= room) {
        return buffer;
    }
    // Longer than the buffer this command carries, so it is asked again into
    // one that fits, which is what the number is for.
    char *out = kest_arena_alloc(arena, (size_t)needed + 1, 1);
    if (out == NULL) {
        return NULL;
    }
    kest_gave_text(runtime, entry, frame, out, (size_t)needed + 1);
    return out;
}

// Whether what follows the first file is one thing of its own rather than more
// files. `tick <file> [n]` is the only command that takes one, and which
// commands take what was four comparisons against a name spread through the
// reading of the arguments.
static bool takes_a_count(const char *command) {
    return strcmp(command, "tick") == 0;
}

// The events a `tick` was given, written `4,5,6`, and how many there are.
// Nothing to do with the arena: this is read before there is a build, so it is
// the command line's own memory and freed with the rest of it.
static int32_t *read_events(const char *text, int32_t *count) {
    uint32_t found = 1;
    for (const char *at = text; *at != '\0'; at++) {
        found += *at == ',' ? 1 : 0;
    }
    if (found > MAX_EVENTS) {
        fprintf(stderr, "kest: between 0 and %d events\n", MAX_EVENTS);
        return NULL;
    }
    int32_t *events = malloc(sizeof(int32_t) * found);
    if (events == NULL) {
        fprintf(stderr, "kest: out of memory\n");
        return NULL;
    }

    uint32_t used = 0;
    const char *at = text;
    while (used < found) {
        char *end = NULL;
        errno = 0;
        long value = strtol(at, &end, 10);
        if (end == at || (*end != ',' && *end != '\0') || errno == ERANGE ||
            value < INT32_MIN || value > INT32_MAX) {
            fprintf(stderr, "kest: `%s` is not a list of events\n", text);
            free(events);
            return NULL;
        }
        events[used++] = (int32_t)value;
        if (*end == '\0') {
            break;
        }
        at = end + 1;
    }
    *count = (int32_t)used;
    return events;
}

// Whether what was typed is spelled the way a float is. `3` and `3.5` are the
// same characters to `strtod` and are not the same thing to a reader.
static bool spelled_as_float(const char *text) {
    return strpbrk(text, ".eEnN") != NULL;
}

// Which of the functions of that name takes what was typed. The same rule the
// language uses for a literal: any width of the right family, and then the
// width it would have had on its own.
// What a function takes, written the way the language writes it: `(i32, i32)`,
// and `()` for one that takes nothing. In the arena, because a message is as
// long as what it says (D193).
//
// The marks a message puts round a thing go round the whole of it and never
// inside: this is one thing, so it is quoted by whoever says it and not by
// itself.
static const char *takes_written(KestArena *arena, const KestType *fn) {
    size_t room = 3;
    for (uint32_t p = 0; p < fn->param_count; p++) {
        room += strlen(kest_type_name(arena, fn->params[p])) + 2;
    }
    char *out = kest_arena_alloc(arena, room, 1);
    if (out == NULL) {
        return "()";
    }
    size_t used = 1;
    out[0] = '(';
    for (uint32_t p = 0; p < fn->param_count; p++) {
        used += (size_t)snprintf(out + used, room - used, "%s%s",
                                 p == 0 ? "" : ", ",
                                 kest_type_name(arena, fn->params[p]));
    }
    out[used++] = ')';
    out[used] = '\0';
    return out;
}

// Every one of them, in one line: `(i32)`, `(text, i32)`. For a name that is
// more functions than a diagnostic has places to point at, where a list that
// stops is a function somebody could have called and was not shown.
static const char *all_of_them(KestArena *arena, KestSymbol **candidates,
                               uint32_t found) {
    size_t room = 1;
    for (uint32_t i = 0; i < found; i++) {
        room += strlen(takes_written(arena, candidates[i]->type)) + 4;
    }
    char *out = kest_arena_alloc(arena, room, 1);
    if (out == NULL) {
        return "";
    }
    size_t used = 0;
    for (uint32_t i = 0; i < found; i++) {
        used += (size_t)snprintf(out + used, room - used, "%s`%s`",
                                 i == 0 ? "" : ", ",
                                 takes_written(arena, candidates[i]->type));
    }
    out[used] = '\0';
    return out;
}

static const KestSymbol *choose(KestBuild *build, const char *name,
                                char **args, int count) {
    KestSymbol *candidates[16];
    uint32_t found = kest_overloads(build->program, name, strlen(name),
                                    candidates, 16);
    const KestSymbol *chosen = NULL;
    uint32_t matches = 0;
    int refused = -1;
    const char *refusal = NULL;

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
                fits = read_argument(build->arena, args[a], want, &scratch,
                                     &why);
                if (!fits) {
                    // Kept for the one case where it is worth saying: one
                    // function of that name, and an argument it could not
                    // read.
                    refused = a;
                    refusal = why;
                }
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

    // What the command line was asked for and could not do is said the way
    // everything else is said: as a diagnostic, so `--json` has it too and a
    // tool is not left with a status that disagrees with an empty list.
    KestSpan nowhere = {0, 0};
    kest_diags_in(&build->diags, NULL);
    if (matches == 0 && count == 0) {
        // What was typed was nothing, and a message about what was typed
        // reads as though something was.
        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0624", nowhere,
                       "no `%s` takes nothing", name);
    } else if (matches == 0) {
        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0624", nowhere,
                       "no `%s` takes what was typed", name);
    } else {
        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0625", nowhere,
                       "more than one `%s` takes what was typed", name);
    }
    // A name that is more functions than a diagnostic has places to point at.
    // A list that stops is not a place nobody can see, which is what a count
    // is the right answer for: it is a function somebody could have called and
    // was not shown. So they are said instead, all of them, in one line.
    const char *plenty = found > KEST_MAX_NOTES
                             ? all_of_them(build->arena, candidates, found)
                             : NULL;
    if (found == 0) {
        kest_diags_suggest(&build->diags, "nothing in this program is called "
                                          "that");
    } else if (count == 0 && plenty != NULL) {
        kest_diags_suggest(&build->diags,
                           "nothing was written after the name, and they take "
                           "%s",
                           plenty);
    } else if (count == 0) {
        // Nothing was typed at all, which the notes below show the shape of
        // and the message does not: "what was typed" was nothing.
        kest_diags_suggest(&build->diags,
                           "nothing was written after the name");
    } else if (plenty != NULL) {
        kest_diags_suggest(&build->diags, "they take %s", plenty);
    } else if (found == 1 && refused >= 0 && refusal != NULL) {
        // One function of that name, so which argument it was and what was
        // wrong with it are both knowable, and a list of one says neither.
        kest_diags_suggest(&build->diags, "`%s` %s", args[refused], refusal);
    }
    // One note per function of that name, at the line that declares it, which
    // is where somebody picking between them has to look anyway — as long as
    // they all fit. A list of them that stops is not a place nobody can see,
    // which is what a count is the right answer for: it is a function they
    // could have called and were not shown. So past that they are said
    // instead, all of them, in one line.
    if (plenty != NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < found; i++) {
        kest_diags_note(&build->diags, candidates[i]->source,
                        candidates[i]->span, "this one takes `%s`",
                        takes_written(build->arena, candidates[i]->type));
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
               int path_count, bool json, int32_t count, const int32_t *given,
               bool reset) {
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
    // Which function `call` called, so that what it needs can be said beside
    // what it gave back. -1 until one is chosen.
    int32_t called = -1;
    size_t called_heap = 0;
    int64_t exit_code = 0;
    // What the called function gave back, which is written once and then
    // either printed or put in the object.
    char wrote[64];
    const char *gave = NULL;
    // Whether a call was made at all, which is not the same as whether
    // anything came back: a function that gives nothing gives nothing.
    bool called_it = false;
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
                KestSpan nowhere = {0, 0};
                kest_diags_in(&build->diags, NULL);
                kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0626",
                               nowhere, "`call` was given no function to call");
                kest_diags_suggest(&build->diags,
                                   "`kest call <file> <fn> [argument]...`");
            }
            if (chosen != NULL) {
                called = kest_module_find(&build->module, chosen->type->symbol);
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
                    // What comes back and what goes in, because the frame is
                    // both: `chosen->type` is the function, and a function is
                    // one slot however wide the thing it gives.
                    uint16_t width = chosen->type->result == NULL
                                         ? 1
                                         : chosen->type->result->slots;
                    for (uint32_t p = 0; p < chosen->type->param_count; p++) {
                        width += chosen->type->params[p]->slots;
                    }
                    KestValue *frame =
                        KEST_ARENA_ARRAY(build->arena, KestValue, width + 1);
                    uint16_t at = 0;
                    const char *why = NULL;
                    for (uint32_t p = 0; p < chosen->type->param_count; p++) {
                        read_argument(build->arena, paths[2 + p],
                                      chosen->type->params[p], &frame[at],
                                      &why);
                        at += chosen->type->params[p]->slots;
                    }
                    int32_t entry = kest_entry(runtime, chosen->type->symbol);
                    if (entry < 0) {
                        // A function that takes types has no body until a call
                        // asks for one, so there is nothing here to call. The
                        // machine would say there is nothing at -1, which is
                        // true of the table and says nothing about the file.
                        KestSpan nowhere = {0, 0};
                        kest_diags_in(&build->diags, NULL);
                        if (chosen->type->type_param_count > 0) {
                            kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                           "K0627", nowhere,
                                           "`%s` takes types, and a copy of it "
                                           "exists where one is called",
                                           paths[1]);
                            kest_diags_suggest(&build->diags,
                                               "write the call in a file and "
                                               "run that");
                        } else {
                            kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                           "K0628", nowhere,
                                           "nothing in this program compiled "
                                           "`%s`",
                                           paths[1]);
                        }
                        failed_to_choose = true;
                    } else if (kest_call(runtime, entry, frame, width + 1)) {
                        const KestType *without = NULL;
                        gave = result_text(runtime, entry, frame,
                                           chosen->type->result, build->arena,
                                           wrote, sizeof(wrote), &without);
                        if (without != NULL) {
                            // The words the compiler uses for the same rule,
                            // because it is the same rule.
                            KestSpan nowhere = {0, 0};
                            kest_diags_in(&build->diags, NULL);
                            kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                           "K0629", nowhere,
                                           "there is no text for `%s`, which "
                                           "is what `%s` gives",
                                           kest_type_name(build->arena,
                                                          without),
                                           paths[1]);
                            kest_diags_suggest(&build->diags,
                                               "call something that gives a "
                                               "value with text, or write the "
                                               "fields you want to see");
                            // The command is to call and say what came back,
                            // and it did half of that.
                            failed_to_choose = true;
                        } else if (!json) {
                            // A command that says nothing looks like one that
                            // did not run, and this one did: it called
                            // something that gives nothing.
                            printf("%s\n",
                                   gave == NULL ? "nothing came back" : gave);
                        }
                        called_it = without == NULL;
                    }
                    // What the call cost, which is the question a caller of
                    // one function is asking when it asks anything: the same
                    // subtraction a host does on either side of a call, done
                    // here where there is one call and it started at nought.
                    called_heap = kest_heap_used(runtime);
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
                    drive_events(runtime, build, count, given, reset, &ticked);
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
                        // A file with nothing in it has no `main` for a
                        // different reason than a file full of functions, and
                        // the person who just made one wants to be told which
                        // of the two they are looking at.
                        KestSpan nowhere = {0, 0};
                        // Two messages rather than one with a choice in it, so
                        // that each is a code beside the words it is raised
                        // with, which is what `check-docs.sh` reads.
                        if (build->units.items[0].unit.count > 0) {
                            kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                           "K0603", nowhere,
                                           "this file has no `main` to run");
                        } else {
                            kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                           "K0603", nowhere,
                                           "this file declares nothing, so "
                                           "there is nothing to run");
                        }
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
            kest_module_disassemble_json(&build->module, EVERY_CALL, stdout);
        }
        // The one command whose answer is a value says it here rather than
        // beside the JSON, where a person would not look and a tool could not
        // read it.
        if (gave != NULL) {
            fputs(",\"result\":", stdout);
            kest_json_text(gave, stdout);
        } else if (called_it) {
            fputs(",\"result\":null", stdout);
        }
        // What the function this command called needs, which is the question
        // `emit` answers about the three names a command line might call and
        // this one always knows the answer to exactly.
        if (called >= 0) {
            fputs(",\"needs\":{", stdout);
            kest_module_needs_json(&build->module, called, stdout);
            fputc('}', stdout);
        }
        // What it cost to answer, beside what it needed to. A machine that
        // never started has nothing to say here and says nought, which is
        // what it allocated.
        if (called_it) {
            fprintf(stdout, ",\"heap\":%zu", called_heap);
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
        // The door a host outside the library uses, which is why the command
        // line uses it: a way in that only ever runs when something has gone
        // wrong for somebody else is a way in nobody has walked through. In
        // JSON the object carries more than the diagnostics, so that one is
        // written here.
        kest_build_report(build, stderr, KEST_FORM_TEXT);
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
    // The events themselves, when `tick` was given a list rather than a count.
    int32_t *given = NULL;
    bool told_it = false;
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
        } else if (takes_a_count(argv[1]) && path_count > 0 && told_it) {
            fprintf(stderr, "kest: `%s` takes one count, and was given `%s` "
                            "as well\n",
                    argv[1], argv[i]);
            free(paths);
            free(given);
            return 1;
        } else if (takes_a_count(argv[1]) && path_count > 0 &&
                   strchr(argv[i], ',') != NULL) {
            // The events written down: `tick file 4,5,6` lends those three and
            // hands each of them over. A program whose answer depends on what
            // it was given is measured against what it was given, rather than
            // against a run counted up from nought that nobody chose.
            given = read_events(argv[i], &count);
            if (given == NULL) {
                free(paths);
                return 1;
            }
            told_it = true;
        } else if (takes_a_count(argv[1]) && path_count > 0) {
            // `tick <file> [n]`, so after the file what is left is how many
            // events, whatever it is spelt like. Reading only what begins with
            // a digit made `-3` a second file and `2x` a two.
            char *end = NULL;
            errno = 0;
            long value = strtol(argv[i], &end, 10);
            if (end == argv[i] || *end != '\0') {
                fprintf(stderr, "kest: `%s` is not a number of events\n",
                        argv[i]);
                free(paths);
                return 1;
            }
            if (errno == ERANGE || value < 0 || value > MAX_EVENTS) {
                fprintf(stderr, "kest: between 0 and %d events\n", MAX_EVENTS);
                free(paths);
                return 1;
            }
            count = (int32_t)value;
            told_it = true;
        } else {
            // Everything else is a file. For `call` that is the file, the
            // function and what to call it with, in that order, which the
            // command reads for itself.
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
        free(given);
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
            run(argv[1], argv[0], paths, path_count, json, count, given,
                reset);
        free(paths);
        free(given);
        return status;
    }

    free(paths);
    free(given);
    fprintf(stderr, "kest: unknown command '%s'\n", argv[1]);
    return usage();
}
