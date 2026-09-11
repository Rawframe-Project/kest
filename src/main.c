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
            "  help              this, and `-h` and `--help` are it too\n"
            "\n"
            "options:\n"
            "  --json            everything this command says, as JSON, one\n"
            "                    object a file: the diagnostics, and for\n"
            "                    `check` what the program holds, for `emit`\n"
            "                    the instructions, for `lex` the tokens and\n"
            "                    the comments, for `call` what came back,\n"
            "                    for `tick` the crossings and the heap, and\n"
            "                    for `fmt` whether the file is in the one\n"
            "                    form\n"
            "  -w                fmt writes each file it is given\n"
            "  --check           fmt names the files it would rewrite, without\n"
            "                    writing them, and exits non-zero\n"
            "  --reset           tick throws the heap away between events\n"
            "  --version         print the version\n"
            "\n"
            "exit status is 1 when anything was refused, and otherwise what\n"
            "`main` returned, which has to be a number from 0 to 255. A\n"
            "warning is not a refusal: it is said and the run goes on.\n"
            "\n"
            "KEST_LIB says where the standard library is. Without it the\n"
            "compiler looks beside itself and then where it was installed.\n",
            kest_version());
}

static int usage(bool json) {
    // A tool asking for JSON is not reading the help, and what it is reading
    // is on the other stream: the words go to a person or nowhere.
    if (!json) {
        help(stderr);
    }
    return 1;
}

// What the command line itself refuses, before there is a build to write a
// diagnostic into: a mistake in the words, and a file the one form could not
// be written to. These were bare sentences on the standard error — no code, no
// JSON — so a run asked for JSON answered a tool with a status of 1 and an
// empty stream, which is the one answer nothing can act on. See D437.
//
// The code is the caller's rather than this function's so that it stands
// beside the words it is for, which is where everything that reads this tree
// looks for the pair. Always 1, so a refusal is a `return` of what a command
// answers with.
static int refused_at_the_words(bool json, const char *code,
                                const char *format, ...) {
    char said[512];
    va_list words;
    va_start(words, format);
    vsnprintf(said, sizeof(said), format, words);
    va_end(words);
    kest_diags_say_one(json ? stdout : stderr, json, code, said);
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

// The same stream written for a tool. `lex` is the one command whose whole
// answer is the tokens, and until this it said everything about a file except
// them: a reader saw the stream and anything reading the JSON saw the
// comments and the diagnostics beside a hole where the answer was.
static void dump_tokens_json(KestArena *arena, const KestToken *tokens,
                             uint32_t count, const KestSource *source,
                             FILE *out) {
    fputs(",\"tokens\":[", out);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t line = 0;
        uint32_t column = 0;
        kest_source_locate(source, tokens[i].span.offset, &line, &column);
        fprintf(out, "%s{\"kind\":", i > 0 ? "," : "");
        kest_json_text(kest_token_name(tokens[i].kind), out);
        fprintf(out, ",\"line\":%u,\"column\":%u,\"text\":", line, column);
        char *text = kest_arena_strndup(
            arena, source->text + tokens[i].span.offset, tokens[i].span.length);
        kest_json_text(text == NULL ? "" : text, out);
        // Whether a line ending here carries on to the next. It is the one
        // thing about a token that a tool cannot work out from the token: the
        // rule is the lexer's, and a second copy of it in whatever is reading
        // this is a second copy to keep right. What reads it is anything that
        // writes this language back out. See D397.
        fprintf(out, ",\"carries\":%s}",
                kest_lexer_ends_statement(tokens[i].kind) ? "false" : "true");
    }
    fputc(']', out);
}

// The stream as a reader sees it, and the comments in their places. A comment
// is not a token — the parser never sees one — and it was in the object and
// nowhere else, so a reader of this looked at a file with every comment in it
// missing and nothing saying so. What keeps a formatter honest is that a
// comment is read twice, by the compiler and by the check, and neither reading
// was one a person could look at. See D595.
static void dump_tokens(KestArena *arena, const KestToken *tokens,
                        uint32_t count, const KestSource *source) {
    uint32_t written = kest_comments(source, NULL, 0);
    KestSpan *comments = written == 0
                             ? NULL
                             : KEST_ARENA_ARRAY(arena, KestSpan, written);
    if (comments == NULL) {
        written = 0;
    } else {
        kest_comments(source, comments, written);
    }
    uint32_t said = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t line = 0;
        uint32_t column = 0;
        kest_source_locate(source, tokens[i].span.offset, &line, &column);
        // In their places, which is where they were written: a comment before
        // the token it was written above, and one at the end of a line after
        // the last token of it.
        while (said < written &&
               comments[said].offset < tokens[i].span.offset) {
            uint32_t at = 0;
            uint32_t from = 0;
            kest_source_locate(source, comments[said].offset, &at, &from);
            printf("%4u:%-3u %-14s %.*s\n", at, from, "comment",
                   (int)comments[said].length,
                   source->text + comments[said].offset);
            said++;
        }
        printf("%4u:%-3u %-14s %.*s\n", line, column,
               kest_token_name(tokens[i].kind), (int)tokens[i].span.length,
               source->text + tokens[i].span.offset);
    }
    for (; said < written; said++) {
        uint32_t at = 0;
        uint32_t from = 0;
        kest_source_locate(source, comments[said].offset, &at, &from);
        printf("%4u:%-3u %-14s %.*s\n", at, from, "comment",
               (int)comments[said].length,
               source->text + comments[said].offset);
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
// Where the program's words went. `Io.write` gives nothing back, so a program
// cannot be told that its writing failed and does not know; the host is the
// one that finds out, and here the host is this command line. What it is asked
// is the stream's own memory of it — a write that failed is remembered by the
// stream until somebody asks — so this is one question at the end rather than
// a flag kept by hand at every write. See D344.
static FILE *program_wrote_to = NULL;

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
// Whether what the program asked to read was there to read. `Io.read` gives
// back text and has no way to say `this failed`, so a read that went wrong
// hands over an empty piece and a program cannot tell that from an empty
// input: a closed stream and a directory both read as nothing at all. The
// host is the one that finds out, the same way it does about writing.
static bool program_could_not_read = false;

static void io_read(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    size_t room = 4096;
    size_t held = 0;
    char *bytes = malloc(room);
    if (bytes == NULL) {
        program_could_not_read = true;
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
            // What was read so far is a piece of the input, and a piece
            // handed over as the whole of it is the quiet truncation this
            // project refuses everywhere else. Nothing, and the run is told.
            program_could_not_read = true;
            held = 0;
            break;
        }
        bytes = grown;
        room *= 2;
    }
    if (ferror(stdin)) {
        program_could_not_read = true;
        held = 0;
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
    program_wrote_to = output;
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
    // What the machine that ran them is made of, beside what the frames cost.
    // A host reading this is choosing two things at once — how much a frame
    // costs it and how much having a machine at all costs it — and only one of
    // them was here. See D576.
    size_t machine;
    uint32_t slots;
    uint32_t frames;
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
    // How many times the heap was thrown away between events. Without it, a
    // run that allocated nothing and a run that threw everything away say the
    // same thing: nought bytes and none of them freed, which is true of the
    // first and the opposite of the second.
    int32_t thrown;
    // What it was run over: how many, and which ones when a caller wrote them
    // down. A run of `0,1,2` and a run of `4,5,6` are two measurements with the
    // same shape, and what a program answers may depend on which it was.
    int32_t count;
    const int32_t *given;
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
    out->count = count;
    out->given = given;

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
            if (reset) {
                if (!kest_heap_reset(runtime)) {
                    return;
                }
                out->thrown++;
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
            kest_diags_say_one(json ? stdout : stderr, json,
                               KEST_STARVED_CODE, KEST_STARVED_SAYS);
            return 1;
        }

        KestDiags diags;
        kest_diags_init(&diags, arena);
        KestUnits units = {0};
        // `lex` answers with the tokens, so it reads the file and lexes it,
        // once. Parsing to reach a token stream is work nobody asked for, and
        // what a parser has to say about a file is what `parse` and `check`
        // are for.
        KestSource alone = {0};
        KestToken *tokens = NULL;
        uint32_t found = 0;
        bool loaded;
        if (what == FILE_LEX) {
            loaded = kest_read_source(arena, &diags, paths[i], &alone);
            if (loaded) {
                kest_diags_in(&diags, &alone);
                tokens = kest_lex_all(arena, &alone, &diags, &found);
            }
        } else {
            loaded = kest_read_unit(arena, &diags, paths[i], &units) &&
                     units.count > 0;
        }
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
                    dump_tokens(arena, tokens, found, &alone);
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
                // What a file is made of, which is its tokens and the comments
                // between them: the same stream the text form prints, and the
                // comments, which are not tokens and are printed nowhere else.
                //
                // And what reading it cost, the way `check` and `emit` say
                // what they cost (D572). These two stop where they stop —
                // `lex` at the tokens and `parse` at the tree — so the three
                // numbers beside each other are what each stage of reading a
                // file costs, which nothing could be asked before. Read before
                // what follows is written, because writing it allocates. See
                // D640.
                fputc('{', stdout);
                kest_diags_write_json(&diags, stdout);
                fprintf(stdout, ",\"cost\":%zu", kest_arena_used(arena));
                // And what a tree is made of, which is where most of that
                // went: every expression, statement and declaration the parser
                // made. A tool that has the cost and the count has what a node
                // of this compiler weighs. See D641.
                if (what == FILE_PARSE && loaded && units.count > 0) {
                    fprintf(stdout, ",\"nodes\":%u", units.items[0].unit.nodes);
                }
                if (what == FILE_LEX && loaded) {
                    dump_tokens_json(arena, tokens, found, &alone, stdout);
                    dump_comments_json(arena, &alone, stdout);
                }
                fputs("}\n", stdout);
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

        // Read back before it is handed over. What `fmt` writes has to be the
        // same program, which this command has said in those words for as
        // long as it has existed and nothing has ever asked: the formatter
        // broke a line where a line may end, printed it, and answered nought,
        // and `-w` would have written it over somebody's file. Reading it back
        // is the only thing that can tell. Twice over, because a form that is
        // not the form again is a formatter nobody can leave running on save.
        // See D386.
        const char *unreadable = NULL;
        if (text != NULL) {
            KestSource again;
            KestDiags back;
            kest_diags_init(&back, arena);
            KestUnit twice_over = {0};
            if (!kest_source_init(&again, arena, paths[i], text, length) ||
                !kest_parse(arena, &again, &back, &twice_over)) {
                unreadable = "ran out of memory reading it back";
            } else if (back.error_count > 0) {
                unreadable = "does not parse";
            } else {
                size_t twice_length = 0;
                const char *twice =
                    kest_format(&twice_over, &again, arena, &twice_length);
                if (twice == NULL) {
                    unreadable = "ran out of memory reading it back";
                } else if (twice_length != length ||
                           memcmp(twice, text, length) != 0) {
                    unreadable = "is not itself in the one form";
                }
            }
            if (unreadable != NULL) {
                // Whose mistake it is, said in the message and said whatever
                // form the rest of this command is answering in: a file that
                // does not parse is the program's mistake, and what the
                // formatter wrote is this project's. A reader who cannot tell
                // them apart goes looking in the wrong place. It goes to the
                // standard error, where the other two refusals go, so a run
                // asked for JSON still writes JSON and nothing else.
                fprintf(stderr,
                        "kest: `%s` is not formatted, because what `fmt` "
                        "writes has to be the same program and what it wrote "
                        "%s, which is a fault in the formatter\n",
                        paths[i], unreadable);
                text = NULL;
            }
        }

        const KestSource *source = loaded ? &units.items[0].source : NULL;
        bool same = text != NULL && source != NULL &&
                    length == source->length &&
                    memcmp(text, source->text, length) == 0;

        // What this command says, for whatever is reading it rather than for a
        // person: one object a file, saying whether it is already in the one
        // form, what was wrong with it if anything was, and — where the
        // command is the one that answers with a file — the file. A stream
        // that is JSON and a file's contents at once is neither, and a string
        // inside an object is not that: it is the answer where a tool reads
        // one, which is what asking for JSON is for. See D596.
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
            fprintf(stdout, ",\"formed\":%s",
                    text == NULL ? "null" : same ? "true" : "false");
            // The one form of the file, where that is what was asked for.
            // `-w` put it in the file and `--check` was asked a question
            // rather than for a file, so neither of those says it: what a
            // reader of this gets is what the words form would have printed.
            if (mode == FORMAT_PRINT && text != NULL) {
                fputs(",\"text\":", stdout);
                kest_json_text(text, stdout);
                // And where the two differ, which is what a tool that formats
                // on save wants: the bytes before it are the same in both and
                // so are the bytes after it, so what it has to replace is
                // that much and its reader keeps their cursor. Found in one
                // pass from each end over what was read and what was written
                // — no second reading of the file, and nothing kept but two
                // numbers. See D597.
                size_t head = 0;
                while (!same && head < length && head < source->length &&
                       text[head] == source->text[head]) {
                    head++;
                }
                size_t tail = 0;
                while (!same && tail < length - head &&
                       tail < source->length - head &&
                       text[length - 1 - tail] ==
                           source->text[source->length - 1 - tail]) {
                    tail++;
                }
                if (same) {
                    fputs(",\"edit\":null", stdout);
                } else {
                    uint32_t line = 0;
                    uint32_t column = 0;
                    kest_source_locate(source, (uint32_t)head, &line, &column);
                    fprintf(stdout,
                            ",\"edit\":{\"offset\":%zu,\"length\":%zu,"
                            "\"line\":%u,\"column\":%u,\"text\":",
                            head, source->length - tail - head, line, column);
                    char *put = kest_arena_strndup(arena, text + head,
                                                   length - tail - head);
                    kest_json_text(put == NULL ? "" : put, stdout);
                    fputc('}', stdout);
                }
            }
            fputs("}\n", stdout);
            if (text == NULL || (!same && mode == FORMAT_CHECK)) {
                status = 1;
            }
            if (text != NULL && !same && mode == FORMAT_WRITE &&
                !replace_file(paths[i], text, length)) {
                // Said rather than left to the status. In this form the object
                // above has already gone out saying the file is not in the one
                // form, which is true and is not what happened.
                refused_at_the_words(json, "K0706",
                                     "`%s` could not be written", paths[i]);
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
            if (loaded && unreadable == NULL) {
                fprintf(stderr,
                        "kest: `%s` is not formatted, because what `fmt` "
                        "writes has to be the same program and this one did "
                        "not parse\n",
                        paths[i]);
            } else if (unreadable == NULL) {
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
            refused_at_the_words(json, "K0706",
                                 "`%s` could not be written", paths[i]);
            status = 1;
        }

        kest_arena_free(arena);
    }
    return status;
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
static int32_t *read_events(const char *text, int32_t *count,
                            bool json) {
    uint32_t found = 1;
    for (const char *at = text; *at != '\0'; at++) {
        found += *at == ',' ? 1 : 0;
    }
    if (found > MAX_EVENTS) {
        refused_at_the_words(json, "K0649",
                             "an event count is between 0 and %d", MAX_EVENTS);
        return NULL;
    }
    int32_t *events = malloc(sizeof(int32_t) * found);
    if (events == NULL) {
        kest_diags_say_one(json ? stdout : stderr, json, KEST_STARVED_CODE,
                           KEST_STARVED_SAYS);
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
            refused_at_the_words(json, "K0649",
                                 "`%s` is not a list of events", text);
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
                fits = kest_value_read(build->arena, args[a], want, &scratch,
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
    const char *plenty = found > KEST_MOST_PLACES
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
        // number is picked, which is what a host without one does. Read as a
        // list with nothing else in it, the way the other host in this tree
        // reads every answer this header gives: a reason added to the four
        // stops this command line compiling rather than being taken for one
        // of them. See D568.
        switch (why.reach) {
        case KEST_REACH_NO_NAME:
            continue;
        case KEST_REACH_KNOWN:
        case KEST_REACH_ITSELF:
        case KEST_REACH_VALUE:
        case KEST_REACH_NO_ROOM:
        case KEST_REACH_UNASKED:
            return NULL;
        }
    }
    // Nothing named, or nothing found: the whole program then, which is what a
    // host that has not said which function it calls is given.
    if (!asked && !kest_needs(build, least, &why)) {
        return NULL;
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
        // Before there is anywhere to write a diagnostic down, which is what
        // this door is for: the words are the ones every other refusal is
        // written with, because they are written beside them.
        kest_diags_say_one(json ? stdout : stderr, json, KEST_STARVED_CODE,
                           KEST_STARVED_SAYS);
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
    // Whether the program answered, which is not the same as what it answered:
    // a `main` that gives nothing back is a shape this language has, and a
    // status of nought is what a run of one is. A tool reading the object
    // reads the difference; a shell reading the status cannot. See D588.
    bool answered = false;
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
                // What this command answers with is on standard output, so
                // what the program says while it runs is not: a shell reading
                // `kest call` wants the value and gets the program's writing
                // above it otherwise, with nothing to say which line is which.
                // `--json` has always done this; the words do it too. See
                // D343.
                KestHost *host = make_host(stderr);
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
                    int32_t entry = kest_entry(runtime, chosen->type->symbol);
                    // Through the door a host outside this library uses. What
                    // was typed is words, and the machine reads a word as the
                    // type the declaration says and copies what has to be
                    // copied — a piece of text made out of `argv` would be a
                    // pointer of this host's own, which is the thing that door
                    // exists so that nobody hands over.
                    //
                    // Which of them it is has been settled already, so a word
                    // that does not fit here cannot happen: it would have been
                    // `K0624` before anything started.
                    if (entry >= 0 &&
                        !kest_takes_text(runtime, entry, frame, width + 1,
                                         (const char *const *)&paths[2],
                                         chosen->type->param_count)) {
                        kest_report(runtime, stderr, KEST_FORM_TEXT);
                        failed_to_choose = true;
                    }
                    if (failed_to_choose) {
                        // Said already, and the shape below is what to do
                        // about a name rather than about a word.
                    } else if (entry < 0) {
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
                        } else if (!chosen->type->is_foreign) {
                            // What is left is a function this program
                            // declared, did not take types, and has no body
                            // in the module: the compiler lost a chunk it
                            // made. A name the host answers is not that, and
                            // the machine has already said so where the
                            // `extern` line is — saying it again here would
                            // be the same news in two voices, and this one
                            // has nowhere to point.
                            kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                           "K0628", nowhere,
                                           "nothing in this program compiled "
                                           "`%s`",
                                           paths[1]);
                            kest_diags_fault(&build->diags,
                                             "a function that was declared "
                                             "and not compiled");
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
            // `run` answers with what the program says, so that goes where a
            // reader looks; `tick` answers with what a frame cost, and a
            // program writing into the middle of that is the same mixing as
            // above.
            KestHost *host = make_host(json || ticking ? stderr : stdout);
            if (host == NULL) {
                kest_diags_say_one(stderr, json, KEST_STARVED_CODE,
                                   KEST_STARVED_SAYS);
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
                    // And what the machine itself cost, which is a number a
                    // host pays once and a frame budget is measured against:
                    // the command line asks the program what it needs, so
                    // this is what asking gets you.
                    ticked.machine = kest_runtime_cost(runtime);
                    KestLimits given_room = {0, 0, 0};
                    kest_allowed(runtime, &given_room);
                    ticked.slots = given_room.stack_slots;
                    ticked.frames = given_room.call_depth;
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
                        // What it was run over, before what that cost: two
                        // runs of the same shape over different events are two
                        // measurements, and a reader with one line of numbers
                        // and no idea which events made them has half of it.
                        if (ticked.given != NULL) {
                            printf("events    %d lent:", ticked.count);
                            for (int32_t i = 0; i < ticked.count; i++) {
                                printf("%s %d", i == 0 ? "" : ",",
                                       ticked.given[i]);
                            }
                            printf("\n");
                        } else {
                            printf("events    %d, counted up from nought\n",
                                   ticked.count);
                        }
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
                        // The three numbers a host pays, in the order it
                        // pays them: what reading the program cost, what
                        // having a machine costs, and what a frame leaves
                        // behind. The first was in the JSON and nowhere in the
                        // words, so a reader of the words had two of the
                        // three. See D577.
                        printf("cost      %zu bytes to compile\n",
                               kest_build_cost(build));
                        printf("machine   %zu bytes, %u slots and %u "
                               "frame%s\n", ticked.machine, ticked.slots,
                               ticked.frames, ticked.frames == 1 ? "" : "s");
                        if (ticked.thrown > 0) {
                            printf("heap      %zu bytes, thrown away %d "
                                   "time%s\n",
                                   ticked.heap, ticked.thrown,
                                   ticked.thrown == 1 ? "" : "s");
                        } else {
                            printf("heap      %zu bytes, none of it freed\n",
                                   ticked.heap);
                        }
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
                        answered = kest_frame_gives(runtime, at) != NULL;
                        exit_code = answered ? frame[0].integer : 0;
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

    // And whether what the program said got where it was sent. A buffer takes
    // what a full disk will not, so this is asked after the last write as well
    // as at each one: the failure a program cannot be told about is one the
    // command line has to say, because a run that wrote nothing and answered
    // nought is a script that carries on with an empty file.
    if (program_could_not_read) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(&build->diags, NULL);
        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0642", nowhere,
                       "what the program asked to read could not be read");
        kest_diags_suggest(&build->diags,
                           "the standard input would not be read: a stream "
                           "that is not open, or something that is not a file");
    }
    if (program_wrote_to != NULL &&
        (fflush(program_wrote_to) == EOF || ferror(program_wrote_to))) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(&build->diags, NULL);
        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0641", nowhere,
                       "what the program said could not be written");
        kest_diags_suggest(&build->diags,
                           "the stream it was told to write to would not take "
                           "it: a disk with nothing left, or a reader that has "
                           "gone");
    }

    kest_diags_sort(&build->diags);
    if (json) {
        // One object, with whatever the command has to add beside what it
        // found wrong.
        fputc('{', stdout);
        kest_diags_write_json(&build->diags, stdout);
        // What reading and checking this program cost the compiler, which is
        // the one number about itself this project has never printed: a goal
        // written down as *cost is visible and provable* had nothing to say
        // about the compiler's own. Read here rather than at the end, because
        // writing what follows allocates too and a number that counted the
        // writing would grow with how much a tool asked to be told. See D572.
        fprintf(stdout, ",\"cost\":%zu", kest_build_cost(build));
        // The name this file puts its own declarations under, which is not the
        // line it wrote: a file that says `module examples.math` declares
        // `math.factorial`, and a tool that read the line and put it in front
        // of a name would ask about one the program has not got. Everything
        // else here is written under it, so it is the one thing a reader needs
        // to go from a name in the file to a name in this. See D598.
        if (checking) {
            // Read from the file rather than from the module: `check` does not
            // compile, so the module this build would make has no name yet,
            // and the name is the file's own — the last piece of what its
            // `module` line says.
            const char *alias = build->units.count > 0
                                    ? build->units.items[0].alias
                                    : NULL;
            fputs(",\"module\":", stdout);
            if (alias == NULL || alias[0] == '\0') {
                fputs("null", stdout);
            } else {
                kest_json_text(alias, stdout);
            }
        }
        if (checking && build->program != NULL) {
            fputc(',', stdout);
            kest_program_dump_json(build->program, build->arena, stdout);
        }
        if (emitting && build->compiled) {
            fputc(',', stdout);
            kest_module_disassemble_json(&build->module, EVERY_CALL, stdout);
        }
        // And what a run answered, which nothing but the exit status carried:
        // a status is eight bits and a byte of it is the whole answer, so a
        // tool that wanted the number had to start a process to read one. It
        // is null for a `main` that gives nothing back, because nothing and
        // nought are two answers and a status says the same thing for both.
        // The words do not say it: what a run writes is what the program
        // wrote, and a number of this command's own put in the middle of that
        // is a line nobody asked for. See D588.
        if (running && !ticking) {
            if (answered) {
                fprintf(stdout, ",\"answered\":%lld", (long long)exit_code);
            } else {
                fputs(",\"answered\":null", stdout);
            }
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
            fputs(",\"events\":{\"count\":", stdout);
            fprintf(stdout, "%d,\"lent\":", ticked.count);
            if (ticked.given == NULL) {
                // Counted up from nought, which is a thing to say rather than
                // a list to write out: a tool that wants them has them.
                fputs("null}", stdout);
            } else {
                for (int32_t i = 0; i < ticked.count; i++) {
                    fprintf(stdout, "%s%d", i == 0 ? "[" : ",",
                            ticked.given[i]);
                }
                fputs(ticked.count == 0 ? "[]}" : "]}", stdout);
            }
            fprintf(stdout,
                    ",\"machine\":{\"bytes\":%zu,\"slots\":%u,"
                    "\"frames\":%u}",
                    ticked.machine, ticked.slots, ticked.frames);
            fprintf(stdout, ",\"heap\":%zu,\"thrown\":%d", ticked.heap,
                    ticked.thrown);
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
        // No words at all, so no form to answer in but the one a person
        // reads: `--json` is a word, and there are none.
        refused_at_the_words(false, "K0649",
                             "there is no command in what was typed");
        return usage(false);
    }

    if (strcmp(argv[1], "--version") == 0) {
        // And whether this build checks itself, because there is one build
        // here whose whole job is those checks and nothing about it says so:
        // a guard that stopped matching would be that build with none of them
        // in it, running everything and finding nothing, and the run would
        // read exactly as it does now.
        printf("kest %s%s\n", kest_version(), KEST_CHECKED ? " checked" : "");
        return 0;
    }

    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        help(stdout);
        return 0;
    }

    // Found before anything is read, because a mistake in the words is
    // refused while they are being read and the form to say it in is one of
    // them: `tick f.kest 2x --json` used to answer a tool with prose, because
    // the word that said which form to answer in came after the one that was
    // wrong. See D437.
    bool json = false;
    for (int i = 2; i < argc; i++) {
        json = json || strcmp(argv[i], "--json") == 0;
    }
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
        // Whether this run was going to be asked for JSON is in the words
        // this could not gather, so it is said the way a person reads it.
        kest_diags_say_one(json ? stdout : stderr, json, KEST_STARVED_CODE,
                           KEST_STARVED_SAYS);
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            // Read above, before anything here could be refused.
        } else if (strcmp(argv[i], "-w") == 0) {
            mode = FORMAT_WRITE;
        } else if (strcmp(argv[i], "--check") == 0) {
            mode = FORMAT_CHECK;
        } else if (strcmp(argv[i], "--reset") == 0) {
            reset = true;
        } else if (takes_a_count(argv[1]) && path_count > 0 && told_it) {
            free(paths);
            free(given);
            return refused_at_the_words(json, "K0649",
                                        "`%s` takes one count, and was given "
                                        "`%s` as well",
                                        argv[1], argv[i]);
        } else if (takes_a_count(argv[1]) && path_count > 0 &&
                   strchr(argv[i], ',') != NULL) {
            // The events written down: `tick file 4,5,6` lends those three and
            // hands each of them over. A program whose answer depends on what
            // it was given is measured against what it was given, rather than
            // against a run counted up from nought that nobody chose.
            given = read_events(argv[i], &count, json);
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
                free(paths);
                return refused_at_the_words(
                    json, "K0649", "`%s` is not a number of events", argv[i]);
            }
            if (errno == ERANGE || value < 0 || value > MAX_EVENTS) {
                free(paths);
                return refused_at_the_words(
                    json, "K0649", "an event count is between 0 and %d",
                    MAX_EVENTS);
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
            refused_at_the_words(json, "K0649", "`%s` needs a file", argv[1]);
            free(paths);
            return usage(json);
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
            refused_at_the_words(json, "K0649", "`%s` needs a file", argv[1]);
            free(paths);
            return usage(json);
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
    refused_at_the_words(json, "K0649", "unknown command `%s`", argv[1]);
    return usage(json);
}
