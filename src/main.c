// This file is a host, not the library. The library is ISO C11 and nothing
// else; a command line has to read a clock that measures elapsed time, and the
// one C itself has measures processor time. Asked for here so that the request
// is where the reason is. There are two platforms and each has a real
// monotonic clock, so there is no third branch and no fallback that is not one:
// see `host_microseconds`. D935, and D970 for the Windows half.
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#if defined(_WIN32)
// And two more: making a directory, which `kest new` has to, and reading what
// is in one, which `kest test` has to -- a project says where its tests are
// and not which they are. There is no way in ISO C to do either. The library
// has no such need and has neither of them; this is the command line, which is
// where this compiler meets the platform. Through the headers rather than
// declared here, because both are the C library's rather than the platform's
// and how they are linked is the library's business.
#include <direct.h>
#include <io.h>
#define KEST_MAKE_DIRECTORY(path) _mkdir(path)
#else
#include <dirent.h>
#include <sys/stat.h>
#define KEST_MAKE_DIRECTORY(path) mkdir((path), 0755)
#endif

#if defined(_WIN32)
// Two doors out of the platform, declared here rather than by including
// `<windows.h>`. That header defines `near` and `far` as nothing -- names from
// a memory model this machine has not had for thirty years and names this file
// uses -- and brings in some tens of thousands of lines for two functions. The
// types are what the platform documents: `BOOL` is `int`, `WINAPI` is
// `__stdcall`, and a `LARGE_INTEGER` is a union whose whole is a signed
// sixty-four-bit count, which is what is read out of it. See D970.
__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long *count);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long *rate);

// And the two streams put in the mode where a byte written is the byte that
// comes out. Windows writes two bytes for a line end in the mode it starts in,
// so what a program wrote and what came out were different files -- and a
// language whose gate holds two platforms to saying the same thing byte for
// byte cannot have one of them adding a byte. What a program writes is what it
// writes. See D970.
#include <fcntl.h>
#include <io.h>
#define KEST_BYTES_OUT()                                                     \
    do {                                                                     \
        _setmode(_fileno(stdout), _O_BINARY);                                \
        _setmode(_fileno(stderr), _O_BINARY);                                \
    } while (0)
#else
#define KEST_BYTES_OUT()                                                     \
    do {                                                                     \
    } while (0)
#endif

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
#include "debug.h"
#include "lsp.h"
#include "project.h"
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
            "  profile <file>    run it and say what it did: how many steps\n"
            "                    of a budget, how many calls of each body,\n"
            "                    how many crossings into the host, what the\n"
            "                    heap holds and what the budget cost. Counts\n"
            "                    and no durations: a clock is the host's\n"
            "  new <name>        a directory with a project, a program and a\n"
            "                    test in it, ready to run\n"
            "  build [file]      compile it and say nothing if it compiles.\n"
            "                    Without a file, what the project says to\n"
            "                    build. There is no artifact: bytecode is not\n"
            "                    a format anything else reads, and what ships\n"
            "                    is the source beside the runtime\n"
            "  test <files>      run each one and read what it answered. A\n"
            "                    test here is a program that checks itself\n"
            "                    and answers with which check failed\n"
            "  doctor [dir]      what this command line is, where it looks\n"
            "                    for the library, whether it found it, and\n"
            "                    what the project here says about itself\n"
            "  debug <file>      run it with breakpoints: `break <line>`,\n"
            "                    `run`, `continue`, `step`, `next`, `where`\n"
            "                    and `locals`. A breakpoint is written into\n"
            "                    the code and taken out again, so a machine\n"
            "                    nobody is debugging pays nothing for it\n"
            "  lsp               answer an editor over the standard streams:\n"
            "                    what is wrong, what a name is, where it was\n"
            "                    declared, what else names it, what a file\n"
            "                    declares and the one form\n"
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
            "  --c               emit writes the bodies as C rather than as\n"
            "                    instructions: one translation unit, with a\n"
            "                    `main` when the program has one and a line\n"
            "                    naming every body this backend has no C for\n"
            "                    yet, which the machine runs instead\n"
            "  --cost            check says what it proved about each body:\n"
            "                    whether it reaches the heap, whether it\n"
            "                    crosses to the host, whether anything in it\n"
            "                    is outside the deterministic profile, and\n"
            "                    which promise it keeps and does not make.\n"
            "                    The same facts are in --json either way\n"
    "  --room <amount>   the most this command may ask this machine for,\n"
    "                    all of it: reading and compiling the program and\n"
    "                    the heap it runs on. A number of bytes, or one\n"
    "                    with K, M or G after it. Without this it asks for\n"
    "                    whatever it needs\n"
    "  --               everything after this is the program's own, which\n"
    "                    `std.os` hands it. Nothing before it is\n"
    "  --fuel <steps>    the most steps run, tick and call may take, where\n"
    "                    a step is a jump that goes back or a call. A\n"
    "                    number, or one with K, M or G after it. Without\n"
    "                    this a program runs until it is done, which a\n"
    "                    program with a loop that never ends never is\n"
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
        char *text = kest_arena_strndup(
            arena, kest_span_text(source, spans[i]), spans[i].length);
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
            arena, kest_span_text(source, tokens[i].span),
            tokens[i].span.length);
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
                   kest_span_text(source, comments[said]));
            said++;
        }
        printf("%4u:%-3u %-14s %.*s\n", line, column,
               kest_token_name(tokens[i].kind), (int)tokens[i].span.length,
               kest_span_text(source, tokens[i].span));
    }
    for (; said < written; said++) {
        uint32_t at = 0;
        uint32_t from = 0;
        kest_source_locate(source, comments[said].offset, &at, &from);
        printf("%4u:%-3u %-14s %.*s\n", at, from, "comment",
               (int)comments[said].length,
               kest_span_text(source, comments[said]));
    }
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

// Bound twice, under `Io.write` and under `Host.write`: a program may declare
// the crossing itself or reach the library's, and what happens is the same
// thing. One body, because two with one body is a body that can be changed in
// one of them. See D769.
static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(frame, &length);
    // Written by its length rather than to a nought: a piece of text cut out
    // of the middle of another does not end in one. See D964.
    if (bytes != NULL && length > 0) {
        fwrite(bytes, 1, length, (FILE *)context);
    }
}

// What `std.os` declares: a file, the words this command was started with, and
// a clock. They are bound here because this host is a command line and a
// command line may do those things. A host that is a game engine binds the ones
// it wants a program to have and refuses the rest by not binding them, which is
// the whole of the capability model. See D925.
//
// What was after `--` on the command line, which is what a program asks for
// when it asks what it was started with. Kept here rather than threaded through
// every call, because a bound door is handed a frame and a context and this is
// neither: it is what the process was started with, and there is one process.
static char **program_args = NULL;
static int program_arg_count = 0;

static void os_arg_count(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = program_arg_count;
}

// An optional is the value and a byte after it saying whether the value is
// there, which is two slots in a frame. Nothing is the byte set to nought, and
// what is written where the value would be is not read.
static void os_arg(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    int32_t at = (int32_t)frame[0].integer;
    if (program_args == NULL || at < 0 || at >= program_arg_count) {
        kest_text(runtime, "", 0, frame);
        frame[2].integer = 0;
        return;
    }
    kest_text(runtime, program_args[at],
              (uint32_t)strlen(program_args[at]), frame);
    frame[2].integer = 1;
}

// Reading a whole file. Nothing rather than empty text when it cannot be read:
// a file that is not there and a file with nothing in it are different answers.
static void os_file_read(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    (void)context;
    char path[4096];
    uint32_t wide = 0;
    const char *named = kest_text_bytes(frame, &wide);
    if (named == NULL || wide >= sizeof path) {
        kest_text(runtime, "", 0, frame);
        frame[2].integer = 0;
        return;
    }
    memcpy(path, named, wide);
    path[wide] = '\0';
    FILE *reading = fopen(path, "rb");
    if (reading == NULL) {
        kest_text(runtime, "", 0, frame);
        frame[2].integer = 0;
        return;
    }
    size_t room = 4096;
    size_t held = 0;
    char *bytes = malloc(room);
    while (bytes != NULL) {
        size_t read = fread(bytes + held, 1, room - held, reading);
        held += read;
        if (held < room) {
            break;
        }
        char *grown = realloc(bytes, room * 2);
        if (grown == NULL) {
            free(bytes);
            bytes = NULL;
            break;
        }
        bytes = grown;
        room *= 2;
    }
    bool wrong = bytes == NULL || ferror(reading);
    fclose(reading);
    if (wrong) {
        free(bytes);
        kest_text(runtime, "", 0, frame);
        frame[2].integer = 0;
        return;
    }
    // A nought among the bytes would make text that stops early, and text that
    // stops early is a file read as less than it is. Said as nothing rather
    // than handed over short. See D344's rule, applied to a file.
    if (memchr(bytes, 0, held) != NULL) {
        free(bytes);
        kest_text(runtime, "", 0, frame);
        frame[2].integer = 0;
        return;
    }
    kest_text(runtime, bytes, (uint32_t)held, frame);
    frame[2].integer = 1;
    free(bytes);
}

static void os_file_write(KestValue *frame, KestRuntime *runtime,
                          void *context) {
    (void)runtime;
    (void)context;
    char path[4096];
    uint32_t wide = 0;
    const char *named = kest_text_bytes(frame, &wide);
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(frame + 2, &length);
    if (named == NULL || bytes == NULL || wide >= sizeof path) {
        frame[0].integer = 0;
        return;
    }
    memcpy(path, named, wide);
    path[wide] = '\0';
    FILE *writing = fopen(path, "wb");
    if (writing == NULL) {
        frame[0].integer = 0;
        return;
    }
    bool wrote = fwrite(bytes, 1, length, writing) == length;
    frame[0].integer = (fclose(writing) == 0 && wrote) ? 1 : 0;
}

static void os_file_exists(KestValue *frame, KestRuntime *runtime,
                           void *context) {
    (void)runtime;
    (void)context;
    char path[4096];
    uint32_t wide = 0;
    const char *named = kest_text_bytes(frame, &wide);
    if (named == NULL || wide >= sizeof path) {
        frame[0].integer = 0;
        return;
    }
    memcpy(path, named, wide);
    path[wide] = '\0';
    FILE *there = fopen(path, "rb");
    if (there != NULL) {
        fclose(there);
        frame[0].integer = 1;
        return;
    }
    frame[0].integer = 0;
}

// What the standard library declares and every host has to provide. A program
// that never reaches one of these never asks for it.
// And this one under `Math.sqrt` and `Host.sqrt`, for the same reason.
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
        kest_text(runtime, "", 0, frame);
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
    kest_text(runtime, bytes, (uint32_t)held, frame);
    free(bytes);
}

// Which host is running this, handed over as text the machine owns. A pointer
// of this host's own would be a promise to keep it as long as the program
// holds it, and a program holds a piece of text for as long as it likes.
static void engine_name(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    kest_text(runtime, "kest", 4, frame);
}

// A clock that measures elapsed time and only goes forwards. `clock()` is the
// processor time this process has used, which is not that: a program that waits
// for anything reads a clock that stopped, and `make time` was measuring how
// busy the processor had been rather than how long a frame took. See D935.
//
// Every platform this is built for has a real monotonic clock and this reads
// it: `QueryPerformanceCounter` on Windows, `CLOCK_MONOTONIC` elsewhere. There
// is no third case. A wall clock is not a monotonic clock and `clock()` is
// neither, so neither is written here under that name — a fallback that is not
// what the function says it is is worse than a platform that will not build,
// because a build that will not build is read by whoever ports it and a clock
// that goes backwards is read by nobody until a frame time comes out negative.
// See D970.
// The same reading in nanoseconds, which is what this is and what
// `host_microseconds` is a thousandth of. Two of them because what a program
// is handed is microseconds and what weighing a compile wants is finer: a
// stage of compiling a small program is tens of microseconds, and a number
// that could only be told in whole ones would be told in threes and fours.
static int64_t host_nanoseconds(void) {
#if defined(_WIN32)
    // The counter is a count of ticks and the frequency is fixed while the
    // system is running, so it is asked for once. Seconds and remainder are
    // taken apart before scaling, because ticks times a million overflows a
    // signed 64-bit count after about two and a half hours at 10 MHz.
    static long long per_second;
    if (per_second == 0 && !QueryPerformanceFrequency(&per_second)) {
        return 0;
    }
    long long now = 0;
    if (!QueryPerformanceCounter(&now)) {
        return 0;
    }
    int64_t ticks = (int64_t)now;
    int64_t rate = (int64_t)per_second;
    return ticks / rate * 1000000000 + ticks % rate * 1000000000 / rate;
#else
    struct timespec at;
    if (clock_gettime(CLOCK_MONOTONIC, &at) == 0) {
        return (int64_t)at.tv_sec * 1000000000 + at.tv_nsec;
    }
    return 0;
#endif
}

static int64_t host_microseconds(void) { return host_nanoseconds() / 1000; }

// The same clock again in the shape a build asks for. A build is weighed only
// when somebody asked, and what asks is the environment rather than an option,
// because what it answers is a number about this compiler rather than about
// the program somebody named. See D1026.
static uint64_t compiling_now(void *context) {
    (void)context;
    return (uint64_t)host_nanoseconds();
}

// What each stage of compiling took, in nanoseconds, where `copies` is a share
// of the four before it rather than a stage of its own.
static void say_what_compiling_took(const KestSpent *spent) {
    fprintf(stderr,
            "spent reading %llu naming %llu bodies %llu promises %llu "
            "writing %llu verifying %llu optimizing %llu lowering %llu "
            "finishing %llu of-which-copies %llu\n",
            (unsigned long long)spent->reading,
            (unsigned long long)spent->naming,
            (unsigned long long)spent->bodies,
            (unsigned long long)spent->promises,
            (unsigned long long)spent->writing,
            (unsigned long long)spent->verifying,
            (unsigned long long)spent->optimizing,
            (unsigned long long)spent->lowering,
            (unsigned long long)spent->finishing,
            (unsigned long long)spent->copies);
}

static void host_clock(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = host_microseconds();
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

// A crossing handed a shape rather than a number: three `f32` in three slots,
// which is what the layout of a `Point` says the frame holds. The command line
// is a host like any other and the examples are what it is a host for, so a
// crossing an example declares is one this has to provide. See D699.
// A number kept narrower than the slot it comes back in, which is what an
// `f32` is: what this writes is what `f32(x)` would have made of it, because a
// host answering anything else is answering a number the program cannot make.
// See D838.
static void engine_weigh(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    (void)runtime;
    (void)context;
    frame[0].real = (double)(float)0.5;
}

static void engine_rank(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    double sum = frame[0].real + frame[1].real + frame[2].real;
    frame[0].integer = (int64_t)sum;
}

// And one handed a value with a tag in it. The tag is slot nought and an `i32`
// whatever the case is; what is in the slots after it depends on which case the
// tag names, and a host that reads those asks `kest_case_of` about a layout it
// kept from binding. The command line keeps none — it is a host for whatever
// example it was handed — so it answers the one slot of a tagged value that can
// be read without being told anything: the tag. See D704.
static void engine_hurt(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    int32_t tag = (int32_t)frame[0].integer;
    frame[0].integer = tag;
}

// And one that answers a value with a tag in it. Which tag is a decision about
// what the answer means, and the command line has no engine to make one with —
// so it answers the tag every enum that has a case has, and writes nothing after
// it. A host with an opinion writes the tag its case is and then what that case
// carries, which `kest_case_of` says and this keeps no layout to ask. See D706.
static void engine_blame(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = 0;
}

// And one that answers with a shape: a name and a number. The command line has
// no engine to be a name of, so it answers with the name it calls itself and a
// number that is not a claim about anything — what matters here is that the
// name is text the machine made, which is the one thing a host answering with a
// shape can get wrong that nothing else would see. See D719.
static void engine_who(KestValue *frame, KestRuntime *runtime, void *context) {
    // The same name the program is given when it asks what it is running
    // under, in a shape rather than on its own: one spelling of it, so a host
    // that renames itself renames itself once.
    engine_name(frame, runtime, context);
    frame[1].integer = 0;
}

static KestHost *make_host(FILE *output) {
    program_wrote_to = output;
    KestHost *host = kest_host_new();
    if (host == NULL) {
        return NULL;
    }
    if (!kest_host_bind(host, "Host.sqrt", math_sqrt, NULL) ||
        !kest_host_bind(host, "Host.write", io_write, output) ||
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
        !kest_host_bind(host, "Engine.rank", engine_rank, NULL) ||
        !kest_host_bind(host, "Engine.hurt", engine_hurt, NULL) ||
        !kest_host_bind(host, "Engine.blame", engine_blame, NULL) ||
        !kest_host_bind(host, "Engine.who", engine_who, NULL) ||
        !kest_host_bind(host, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(host, "Host.fileRead", os_file_read, NULL) ||
        !kest_host_bind(host, "Host.fileWrite", os_file_write, NULL) ||
        !kest_host_bind(host, "Host.fileExists", os_file_exists, NULL) ||
        !kest_host_bind(host, "Host.argCount", os_arg_count, NULL) ||
        !kest_host_bind(host, "Host.arg", os_arg, NULL) ||
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
    // And every byte the run was ever handed, which is what a frame cost: a
    // world that makes a name a frame and lets it go holds nothing at the end
    // of it and paid for every one. See D996.
    size_t taken;
    // And what the machine was allowed to put on the heap, which is a number
    // the command line works out rather than one a program reaches: what it
    // was given, less what reading and compiling took and less what a machine
    // for it costs. A reader holding the three against what the command was
    // given is holding this command's own arithmetic, which is the one thing
    // a run that never reaches its ceiling cannot say. See D849 and D996.
    size_t allowed;
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
    // The command line's own memory, because a lend is the host's block and
    // this command is a host. It used to take the arena the program was
    // compiled into, which is the machine's own — convenient, and the one
    // address a lend may not have (D720), because what a program holds of text
    // is a pointer into that arena and a lend is memory a program may write
    // into. A host owns what it lends. See D721.
    int32_t *events = count > 0 ? calloc((size_t)count, sizeof(int32_t)) : NULL;
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
        free(events);
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
        KestValue lent = kest_borrow(runtime, events, (uint32_t)count, "i32",
                                     sizeof(int32_t));
        frame[0] = lent;
        if (kest_call(runtime, bulk_at, frame, 1)) {
            out->bulk = true;
            out->bulk_gives = gives;
            out->bulk_gave = gives ? frame[0].integer : 0;
        }
        // Given back before the block is, which is what a host that lends its
        // own memory has to do in that order.
        kest_lend_ends(runtime, lent);
    }

    gives = single_gives;
    if (single_at >= 0) {
        int64_t total = 0;
        size_t peak = 0;
        for (int32_t i = 0; i < count; i++) {
            KestValue frame[1];
            frame[0].integer = events[i];
            if (!kest_call(runtime, single_at, frame, 1)) {
                free(events);
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
                    free(events);
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
    free(events);
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
                    bool json, size_t room) {
    int status = 0;

    for (int i = 0; i < count; i++) {
        KestArena *arena = kest_arena_new();
        // Each file is its own arena, so each is given the whole of what this
        // command may have rather than a share of it: what `--room` says is
        // the most this command asks the machine for at once, and these do not
        // overlap.
        kest_arena_cap(arena, room);
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
                fprintf(stdout, ",\"cost\":%zu,\"held\":%zu,\"askings\":%zu",
                        kest_arena_used(arena), kest_arena_held(arena),
                        kest_arena_askings(arena));
                // And what a tree is made of, which is where most of that
                // went: every expression, statement and declaration the parser
                // made. A tool that has the cost and the count has what a node
                // of this compiler weighs. See D641.
                if (what == FILE_PARSE && loaded && units.count > 0) {
                    fprintf(stdout, ",\"nodes\":%u", units.items[0].unit.nodes);
                    // And what one weighs on the machine this ran on, which is
                    // a number a tool would otherwise write down and hold: a
                    // node is fifty-six bytes here and something else where a
                    // pointer is another width. Said beside the count so that
                    // what is compared with what is measured the same way.
                    // See D688.
                    fprintf(stdout,
                            ",\"nodeBytes\":{\"expression\":%zu"
                            ",\"statement\":%zu,\"declaration\":%zu}",
                            sizeof(KestExpr), sizeof(KestStmt),
                            sizeof(KestDecl));
                }
                if (what == FILE_LEX && loaded) {
                    // What one token weighs here, for the same reason a node
                    // says what it weighs: a tool that has the cost, the count
                    // and the weight can say whether reading a file cost the
                    // tokens or the sizes the array grew through. See D746.
                    fprintf(stdout, ",\"tokenBytes\":%zu,\"tokenRoom\":%u",
                            sizeof(KestToken), kest_lex_room());
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
// How much room this command may have, written the way somebody says an
// amount of memory rather than as the nine digits it is: `64M` is a number
// anybody reads back, and `67108864` is a number nobody checks. One sentence
// for every way of writing it wrong, because what a reader does about any of
// them is write it again.
static bool read_room(const char *text, size_t *room, bool json) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    unsigned long long scale = 1;
    if (end != text && errno != ERANGE) {
        switch (*end) {
        case 'K':
        case 'k':
            scale = 1024;
            end++;
            break;
        case 'M':
        case 'm':
            scale = 1024ull * 1024;
            end++;
            break;
        case 'G':
        case 'g':
            scale = 1024ull * 1024 * 1024;
            end++;
            break;
        default:
            break;
        }
    }
    if (end == text || *end != '\0' || errno == ERANGE || value == 0 ||
        value > (unsigned long long)SIZE_MAX / scale) {
        refused_at_the_words(json, "K0649",
                             "`%s` is not an amount of room; write a number of "
                             "bytes, or one with `K`, `M` or `G` after it",
                             text);
        return false;
    }
    *room = (size_t)(value * scale);
    return true;
}

// And how many instructions a run may have. Written the same way room is, with
// the same scales, because a reader who has learnt one has learnt the other --
// and a budget is the same kind of number: a ceiling somebody picked rather
// than one the program asked for. See D921.
static bool read_fuel(const char *text, uint64_t *fuel, bool json) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 10);
    unsigned long long scale = 1;
    if (end != text && errno != ERANGE) {
        switch (*end) {
        case 'K':
        case 'k':
            scale = 1000;
            end++;
            break;
        case 'M':
        case 'm':
            scale = 1000ull * 1000;
            end++;
            break;
        case 'G':
        case 'g':
            scale = 1000ull * 1000 * 1000;
            end++;
            break;
        default:
            break;
        }
    }
    if (end == text || *end != '\0' || errno == ERANGE || value == 0 ||
        value > UINT64_MAX / scale) {
        refused_at_the_words(json, "K0649",
                             "`%s` is not a number of instructions; write one, "
                             "or one with `K`, `M` or `G` after it",
                             text);
        return false;
    }
    *fuel = (uint64_t)(value * scale);
    return true;
}

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
// What a command keeps back out of what it may have, so that a program stopped
// while it runs can be told what stopped it in the shape everything else is
// told in: the words, the line, and the caret under it. A run with none of this
// still says what it was about to say — that is kept in the list of
// diagnostics itself — but it says it with no file around it.
//
// The list a machine keeps comes first and is the dearest thing in it: sixteen
// places at two hundred and forty-eight bytes is nearly four kilobytes before a
// word is written. So it is counted rather than guessed at — four thousand was
// guessed at, and it was sixty-eight bytes short of the list alone. What is
// added to it is room for the words themselves, several times over.
//
// The same number twice over, because the same thing happens twice: the build
// says what it has to say in its arena and a machine says what it has to say in
// its own, and both lists are this one. See D849 and D851.
#define ENOUGH_TO_SAY (KEST_MOST_UNREAD * sizeof(KestDiag) + 4096)

// What is left of what this command may have once reading and compiling have
// taken theirs, which is the heap the program runs on. Taken rather than read:
// the build's own ceiling comes down to what it has spent and what it keeps
// back, so that the rest belongs to the program alone. `--room` says it is the
// most this command asks the machine for, all of it, and a build that keeps a
// ceiling of the whole number while the program runs is a second purse the same
// size as the first — twenty thousand bytes allowed and twenty-four thousand
// spent, which is a wall somebody walks through. One byte where there is
// nothing left, because nought is what no ceiling is and this is not no
// ceiling. See D849.
static size_t take_the_rest(KestBuild *build, size_t room) {
    if (room == 0) {
        return 0;
    }
    size_t keeping = kest_build_cost(build) + ENOUGH_TO_SAY;
    // Never above what it was allowed: a build already over that number is one
    // whose ceiling this would be raising rather than lowering.
    if (keeping > room) {
        keeping = room;
    }
    kest_arena_cap(build->arena, keeping);
    return room > keeping ? room - keeping : 1;
}

// And what to give a machine when the program cannot say what it needs. Nought
// slots and nought frames is the machine working those out for itself, which is
// what a host with no answer has always got; the heap is the number this
// command was given. Answering nothing at all here is what used to happen, and
// what it meant was that a ceiling asked for stopped applying the moment the
// question under it could not be answered — a program too big for the sizing
// walk to finish inside its own ceiling ran with no ceiling at all, and took
// thirty times what it had been allowed. See D846.
static const KestLimits *only_the_heap(KestLimits *least, size_t left) {
    if (left == 0) {
        return NULL;
    }
    least->stack_slots = 0;
    least->call_depth = 0;
    least->heap_bytes = left;
    return least;
}

static const KestLimits *room_for(KestBuild *build, const char *const *entries,
                                  KestLimits *least, size_t room) {
    KestReason why = {KEST_REACH_UNASKED, NULL};
    bool asked = false;
    for (uint32_t i = 0; entries != NULL && entries[i] != NULL; i++) {
        KestLimits one = {0, 0, 0, 0};
        // The least where the name has one and a bound where it has not, which
        // is one question rather than two: a name that reaches itself used to
        // throw away the answers for the names beside it and for itself, and
        // what the machine did instead was bound the whole file. Nought frames
        // is as many as usual, which is the ceiling this command line has
        // always run with. See D822.
        if (kest_bound_of(build, entries[i], 0, &one, &why)) {
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
            return only_the_heap(least, take_the_rest(build, room));
        }
    }
    // Nothing named, or nothing found: the whole program then, which is what a
    // host that has not said which function it calls is given.
    if (!asked && !kest_needs(build, least, &why)) {
        return only_the_heap(least, take_the_rest(build, room));
    }
    // And what is left of what this command was allowed, which is the heap the
    // program runs on. One number covers the whole of what this command asks
    // the machine for, so what compiling has already taken comes off it: a
    // ceiling that meant one thing while compiling and another while running
    // would be two ceilings with one name. See D843.
    least->heap_bytes = take_the_rest(build, room);
    return least;
}

// A machine, weighed against what this command may have. What a machine takes
// is a third thing beside reading a program and the heap it runs on, and it is
// taken before the program runs: sixty-four kilobytes allowed and a machine of
// fifty for a program that calls itself three times, which used to be made and
// used and never counted.
//
// Weighing it means making one. What a machine costs is the arithmetic a
// machine is made with, and a second copy of that arithmetic here would be two
// numbers that agree until somebody changes one — a shape this project has
// been caught by often enough to write down. So the machine is made with a
// heap of a single byte, asked what it cost, and then told how much of what is
// left the program may have. One machine rather than two: a machine made to be
// weighed and thrown away costs a list of diagnostics of its own, and what
// that showed up as was a program that cost two hundred and sixty bytes more
// to compile when a ceiling was named. See D850.
static KestRuntime *a_machine_within(KestBuild *build, KestHost *host,
                                     const char *const *entries,
                                     KestLimits *least, size_t room) {
    const KestLimits *asked = room_for(build, entries, least, room);
    if (room == 0 || asked == NULL) {
        return kest_start(build, host, asked);
    }
    size_t rest = least->heap_bytes;
    KestLimits weighing = *least;
    weighing.heap_bytes = 1;
    KestRuntime *runtime = kest_start(build, host, &weighing);
    if (runtime == NULL) {
        return NULL;
    }
    // What it costs, and what saying things costs it. A machine writes what it
    // says in its own room: the list first, which is the dearest thing in it,
    // and the words after — and hands the room back when a host reads the
    // report. So what a machine may grow to is what it was made at plus that,
    // and the same number this command keeps back for its own saying is what a
    // machine may spend on its. Weighed at what it can reach rather than at
    // what it starts as, because a wall against the first is a wall against
    // nothing. See D851.
    size_t costs = kest_runtime_cost(runtime) + ENOUGH_TO_SAY;
    if (costs >= rest) {
        KestSpan nowhere = {0, 0};
        kest_diags_in(&build->diags, NULL);
        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, KEST_CRAMPED_CODE,
                       nowhere,
                       "a machine for this program costs %zu bytes and %zu "
                       "are left of what this command was given",
                       costs, rest);
        kest_diags_suggest(&build->diags,
                           "a machine is made before a program runs, so what "
                           "it costs comes out of the same number");
        kest_runtime_free(runtime);
        return NULL;
    }
    least->heap_bytes = rest - costs;
    if (!kest_heap_allow(runtime, least->heap_bytes)) {
        kest_runtime_free(runtime);
        return NULL;
    }
    return runtime;
}

// One body and how many times it was entered, kept past the machine that
// counted them: the object a run writes is written after the machine is gone.
typedef struct {
    const char *name;
    uint64_t calls;
} Entered;

// What a run did, said to a person. Counts and no durations: the machine
// counted what it did and a clock is the host's, so a number of nanoseconds
// here would be this command line's clock read out as though it were the
// program's cost. `make time` and `kest tick` are where a duration comes from,
// and both run something.
//
// On the error stream, because what a program wrote is the program's answer
// and a measurement written into the middle of it is a line nobody asked for.
// See D979.
static void say_profile(const KestCounted *counted, const Entered *bodies,
                        uint32_t body_count) {
    fprintf(stderr,
            "%llu step(s), %llu call(s), %llu crossing(s) into the host, "
            "%zu byte(s) of heap and %zu at most",
            (unsigned long long)counted->steps,
            (unsigned long long)counted->calls,
            (unsigned long long)counted->crossings, counted->heap,
            counted->most);
    if (counted->fuel_given > 0) {
        fprintf(stderr, ", %llu of %llu step(s) of budget spent",
                (unsigned long long)(counted->fuel_given - counted->fuel_left),
                (unsigned long long)counted->fuel_given);
    }
    fputc('\n', stderr);
    for (uint32_t which = 0; which < body_count; which++) {
        fprintf(stderr, "  %-40s %llu call(s)\n", bodies[which].name,
                (unsigned long long)bodies[which].calls);
    }
}

// A project made, checked, run against its own programs, and looked over. Four
// commands that are about a project rather than about a file, and the project
// is a manifest of `name value` lines. See D982.
static bool write_file(const char *path, const char *bytes, bool json) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        refused_at_the_words(json, "K0701", "`%s` could not be written", path);
        return false;
    }
    size_t length = strlen(bytes);
    bool wrote = fwrite(bytes, 1, length, file) == length;
    wrote = fclose(file) == 0 && wrote;
    if (!wrote) {
        refused_at_the_words(json, "K0701", "`%s` could not be written", path);
    }
    return wrote;
}

static int make_project(const char *name, bool json) {
    if (name == NULL || name[0] == '\0') {
        refused_at_the_words(json, "K0649", "`new` needs a name");
        return 1;
    }
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        refused_at_the_words(json, "K0605", "out of memory");
        return 1;
    }
    char path[1024];
    // The directory, and the two inside it. A directory that is already there
    // is said about rather than written into: making a project over one that
    // exists is how somebody loses a project.
    if (KEST_MAKE_DIRECTORY(name) != 0) {
        kest_arena_free(arena);
        refused_at_the_words(json, "K0701",
                             "`%s` is already there, or could not be made",
                             name);
        return 1;
    }
    snprintf(path, sizeof(path), "%s/src", name);
    KEST_MAKE_DIRECTORY(path);
    snprintf(path, sizeof(path), "%s/tests", name);
    KEST_MAKE_DIRECTORY(path);

    const char *manifest = kest_project_written(arena, name);
    snprintf(path, sizeof(path), "%s/%s", name, KEST_PROJECT_FILE);
    bool wrote = manifest != NULL && write_file(path, manifest, json);

    snprintf(path, sizeof(path), "%s/src/main.kest", name);
    wrote = wrote && write_file(path,
                                "module src.main\n"
                                "\n"
                                "import std.io\n"
                                "\n"
                                "fn main() -> i32 {\n"
                                "    io.print(\"hello\")\n"
                                "    return 0\n"
                                "}\n",
                                json);

    // A test is a program that checks itself and answers with which check
    // failed, which is what every example in this language is. There is no
    // framework: a number is a place in a file.
    snprintf(path, sizeof(path), "%s/tests/adding.kest", name);
    wrote = wrote && write_file(path,
                                "module tests.adding\n"
                                "\n"
                                "fn main() -> i32 {\n"
                                "    if 1 + 1 != 2 {\n"
                                "        return 1\n"
                                "    }\n"
                                "    return 0\n"
                                "}\n",
                                json);
    kest_arena_free(arena);
    if (!wrote) {
        return 1;
    }
    if (json) {
        printf("{\"schema\":%u,\"made\":\"%s\"}\n", (unsigned)KEST_JSON_SCHEMA,
               name);
    } else {
        printf("made %s: a project, a program and a test\n", name);
        printf("  cd %s && kest run src/main.kest\n", name);
    }
    return 0;
}

// What a project says, read back. `doctor` is the command somebody runs when
// something is wrong and they do not know what: it says what this command line
// is, where it looks for the library, whether it found it, what the project
// says about itself and whether what the project names is there.
static int look_over(const char *executable, const char *where, bool json) {
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        refused_at_the_words(json, "K0605", "out of memory");
        return 1;
    }
    const char *library = kest_library_path(arena, executable);
    const char *why = NULL;
    KestProject *project = kest_project_read(arena, where == NULL ? "" : where,
                                             &why);
    uint32_t profile = 0;
    const char *named = kest_profile(&profile);

    bool wrong = false;
    if (json) {
        printf("{\"schema\":%u,\"version\":\"%s\",\"abi\":%u,\"json\":%u,"
               "\"profile\":\"%s %u\",\"library\":",
               (unsigned)KEST_JSON_SCHEMA, kest_version(), kest_abi_version(),
               (unsigned)KEST_JSON_SCHEMA, named, profile);
        kest_json_text(library, stdout);
    } else {
        printf("kest %s%s, abi %u, json %u, profile %s %u\n", kest_version(),
               kest_checked() ? " checked" : "", kest_abi_version(),
               (unsigned)KEST_JSON_SCHEMA, named, profile);
        printf("library    %s\n", library);
    }

    // Whether the library is where it says it is, asked by reading one of its
    // files rather than by looking at the directory: what matters is whether
    // an import resolves.
    char probe[1024];
    snprintf(probe, sizeof(probe), "%sstd/io.kest", library);
    FILE *file = fopen(probe, "rb");
    bool found = file != NULL;
    if (file != NULL) {
        fclose(file);
    }
    if (!found) {
        wrong = true;
    }
    if (json) {
        printf(",\"libraryFound\":%s", found ? "true" : "false");
    } else {
        printf("           %s\n",
               found ? "found, and `import std.io` will resolve"
                     : "NOT found: set KEST_LIB, or install");
    }

    if (why != NULL) {
        wrong = true;
        if (json) {
            printf(",\"project\":null,\"projectSaid\":");
            kest_json_text(why, stdout);
        } else {
            printf("project    %s\n", why);
        }
    } else if (project == NULL) {
        if (json) {
            printf(",\"project\":null,\"projectSaid\":null");
        } else {
            printf("project    none here, which is fine: a file is a program\n");
        }
    } else {
        FILE *entry = project->entry[0] == '\0' ? NULL
                                                : fopen(project->entry, "rb");
        bool entry_there = entry != NULL;
        if (entry != NULL) {
            fclose(entry);
        }
        if (!entry_there) {
            wrong = true;
        }
        if (json) {
            printf(",\"project\":{\"name\":");
            kest_json_text(project->name, stdout);
            printf(",\"entry\":");
            kest_json_text(project->entry, stdout);
            printf(",\"entryFound\":%s,\"kest\":", entry_there ? "true"
                                                              : "false");
            kest_json_text(project->needs_kest, stdout);
            printf(",\"profile\":");
            kest_json_text(project->profile, stdout);
            printf(",\"sources\":[");
            for (uint32_t i = 0; i < project->source_count; i++) {
                fputs(i == 0 ? "" : ",", stdout);
                kest_json_text(project->sources[i], stdout);
            }
            printf("]},\"projectSaid\":null");
        } else {
            printf("project    %s, from %s\n", project->name, project->path);
            printf("entry      %s%s\n", project->entry,
                   entry_there ? "" : "  NOT there");
            printf("written    against kest %s, profile %s\n",
                   project->needs_kest, project->profile);
            for (uint32_t i = 0; i < project->source_count; i++) {
                printf("source     %s\n", project->sources[i]);
            }
        }
    }
    if (json) {
        printf(",\"wrong\":%s}\n", wrong ? "true" : "false");
    } else if (!wrong) {
        printf("nothing here is wrong\n");
    }
    kest_arena_free(arena);
    return wrong ? 1 : 0;
}

static int run(const char *command, const char *executable, char **paths,
               int path_count, bool json, int32_t count, const int32_t *given,
               bool reset, size_t room, uint64_t fuel, bool costing,
               bool writing_c) {
    // Asked once, because a compiler that asked the environment twice could
    // give two answers about one run.
    static int weighing = -1;
    if (weighing < 0) {
        weighing = getenv("KEST_SPENT") == NULL ? 0 : 1;
    }
    int64_t opened = weighing ? host_nanoseconds() : 0;
    KestBuild *build = kest_build_open(kest_library_path(NULL, executable),
                                       paths,
                                       strcmp(command, "call") == 0
                                           ? 1
                                           : path_count,
                                       room);
    if (build != NULL && weighing) {
        kest_build_clock(build, compiling_now, NULL,
                         (uint64_t)(host_nanoseconds() - opened));
    }
    // A profile counts the calls the program makes, and a debugger stops in
    // them and says which frame it is in, so both are compiled with every one
    // of them a call. See D1156.
    if (build != NULL && (strcmp(command, "profile") == 0 ||
                          strcmp(command, "debug") == 0)) {
        kest_build_calls_as_written(build);
    }
    if (build == NULL) {
        // Before there is anywhere to write a diagnostic down, which is what
        // this door is for: the words are the ones every other refusal is
        // written with, because they are written beside them.
        //
        // And which of the two it was, which is known here without asking
        // anything: a build given a ceiling and refused before it had a list
        // to write in was refused by the ceiling, and the arena that would
        // have said so is already gone.
        if (room > 0) {
            char said[120];
            snprintf(said, sizeof said, KEST_CRAMPED_START, room);
            kest_diags_say_one(json ? stdout : stderr, json,
                               KEST_CRAMPED_CODE, said);
            return 1;
        }
        kest_diags_say_one(json ? stdout : stderr, json, KEST_STARVED_CODE,
                           KEST_STARVED_SAYS);
        return 1;
    }

    // Asked before anything is compiled, because the two backends read one
    // body each as it is made: a build told afterwards would have nothing
    // left to read.
    if (writing_c) {
        kest_build_writes_c(build, true);
    }

    bool ticking = strcmp(command, "tick") == 0;
    // `profile` is a run with the machine counting what it does. It is a run
    // rather than a thing of its own so that what is measured is the program
    // as it is run, and not a program run some other way. See D979.
    bool profiling = strcmp(command, "profile") == 0;
    // `debug` is a run with somebody asking it questions. It is a run rather
    // than a thing of its own so that what is debugged is the program as it is
    // run, with the same host bound and the same limits. See D991.
    bool debugging = strcmp(command, "debug") == 0;
    bool running = strcmp(command, "run") == 0 || ticking || profiling ||
                   debugging;
    KestCounted counted = {0};
    bool was_counted = false;
    Entered *bodies = NULL;
    uint32_t body_count = 0;
    // `build` is `emit` with nothing printed: what a build produces is the
    // knowledge that it compiles, because the bytecode is not a format
    // anything else reads and what ships is the source. See D982.
    bool building = strcmp(command, "build") == 0;
    bool emitting = strcmp(command, "emit") == 0 || building;
    bool checking = strcmp(command, "check") == 0;
    bool calling = strcmp(command, "call") == 0;
    bool failed_to_choose = false;
    // Which function `call` called, so that what it needs can be said beside
    // what it gave back. -1 until one is chosen.
    int32_t called = -1;
    size_t called_heap = 0;
    size_t called_took = 0;
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
                // having compiled anything. A listing it had no room to work
                // out is not written half-way and is not written wrongly: it
                // is a run that ran out, said where every other one is said.
                // See D845.
                if (!kest_program_dump(build->program, build->arena,
                                       build->units.items[0].module, stdout)) {
                    kest_diags_starve(&build->diags);
                }
            }
        } else if (emitting) {
            if (kest_build_emit(build) && !json && !building) {
                // The instructions, or the C the same bodies were written as
                // when that was asked for. Not both: a listing and a
                // translation unit on one stream are neither.
                const char *written = writing_c ? kest_build_c(build) : NULL;
                if (written != NULL) {
                    fputs(written, stdout);
                } else if (!writing_c) {
                    kest_module_disassemble(&build->module, EVERY_CALL,
                                            stdout);
                }
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
                KestLimits least = {0, 0, 0, 0};
                KestRuntime *runtime =
                    host == NULL
                        ? NULL
                        : a_machine_within(build, host,
                                           (const char *[]){paths[1], NULL},
                                           &least, room);
                kest_fuel_set(runtime, fuel);
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
                    called_took = kest_heap_taken(runtime);
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
            KestLimits least = {0, 0, 0, 0};
            KestRuntime *runtime = a_machine_within(
                build, host, ticking ? TICK_CALLS : RUN_CALLS, &least, room);
            kest_fuel_set(runtime, fuel);
            if (runtime != NULL) {
                if (ticking) {
                    drive_events(runtime, build, count, given, reset, &ticked);
                    // What the program is holding at the end of it, and what
                    // it was ever handed on the way: the first settles where a
                    // world settles and the second is what a frame cost. See
                    // D996.
                    ticked.heap = kest_heap_used(runtime);
                    ticked.taken = kest_heap_taken(runtime);
                    // And what the machine itself cost, which is a number a
                    // host pays once and a frame budget is measured against:
                    // the command line asks the program what it needs, so
                    // this is what asking gets you.
                    ticked.machine = kest_runtime_cost(runtime);
                    KestLimits given_room = {0, 0, 0, 0};
                    kest_allowed(runtime, &given_room);
                    ticked.slots = given_room.stack_slots;
                    ticked.frames = given_room.call_depth;
                    ticked.allowed = given_room.heap_bytes;
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
                            printf("heap      %zu bytes, %zu taken\n",
                                   ticked.heap, ticked.taken);
                        }
                    }
                } else {
                    KestValue frame[1] = {{0}};
                    const char *entry = kest_build_name(build, KEST_MAIN);
                    kest_diags_in(&build->diags, root);
                    if (profiling && !kest_count(runtime, true)) {
                        KestSpan nowhere = {0, 0};
                        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                       "K0605", nowhere,
                                       "there was no room to count what this "
                                       "run does");
                    }
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
                    } else if (debugging) {
                        exit_code = kest_debug_serve(build, runtime, entry,
                                                     stdin, stdout);
                        answered = false;
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
                if (profiling) {
                    was_counted = kest_counted(runtime, &counted);
                    // Read out while the machine is still there, because the
                    // object a run writes is written after it is gone.
                    if (was_counted) {
                        bodies = KEST_ARENA_ARRAY(build->arena, Entered,
                                                  build->module.count == 0
                                                      ? 1
                                                      : build->module.count);
                        for (uint32_t which = 0;
                             bodies != NULL && which < build->module.count;
                             which++) {
                            uint64_t times =
                                kest_counted_entry(runtime, (int32_t)which);
                            if (times == 0) {
                                continue;
                            }
                            bodies[body_count].name =
                                build->module.functions[which]->name;
                            bodies[body_count].calls = times;
                            body_count++;
                        }
                        if (!json) {
                            say_profile(&counted, bodies, body_count);
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
        fprintf(stdout, ",\"cost\":%zu,\"held\":%zu,\"askings\":%zu",
                kest_build_cost(build), kest_build_held(build),
                kest_arena_askings(build->arena));
        // And what of that cost was working values out where they are written,
        // which is a thing every stage after reading does some of: the checker
        // asks about numbers a program wrote down, so that a count below
        // nought is refused where it is written, and the compiler works out
        // every constant once. Said by every command that builds, so the two
        // can be read against each other. See D677.
        if (build->program != NULL) {
            fprintf(stdout, ",\"folds\":%u,\"asked\":%u",
                    build->program->folds,
                    build->program->asked_for_nothing);
        }
        // And what one body written for many types costs this program, which
        // is the one rule in this language whose price is paid per call site
        // rather than per line written. Three numbers because one is not
        // enough to divide by: how many chunks are copies of something, how
        // many bodies those came from, and how many bytes of code the ones
        // past the first are. A reader with all three can say what the rule
        // cost here rather than what it costs in general. See D778.
        {
            uint32_t from_bodies = 0;
            uint32_t bytes = 0;
            uint32_t made = kest_module_copied(&build->module, &from_bodies, &bytes);
            fprintf(stdout, ",\"copies\":%u,\"copiedBodies\":%u"
                            ",\"copiedBytes\":%u",
                    made, from_bodies, bytes);
            // And what the module itself is still holding, which is the part
            // of `held` that has somewhere to be looked up rather than being
            // a number with nothing under it. See D784.
            uint32_t of_code = 0;
            uint32_t of_origins = 0;
            uint32_t of_constants = 0;
            uint32_t of_layouts = 0;
            uint32_t of_chunks = 0;
            uint32_t of_files = 0;
            uint32_t of_lines = 0;
            uint32_t of_paths = 0;
            uint32_t of_names = 0;
            kest_units_hold(&build->units, &of_files, &of_lines, &of_paths,
                            &of_names);
            kest_module_holds(&build->module, &of_code, &of_origins,
                              &of_constants, &of_layouts, &of_chunks);
            uint32_t of_types = 0;
            uint32_t of_globals = 0;
            uint32_t of_found_by = 0;
            uint32_t of_composed = 0;
            if (build->program != NULL) {
                kest_program_holds(build->program, &of_types, &of_globals,
                                   &of_found_by, &of_composed);
            }
            fprintf(stdout, ",\"holds\":{\"code\":%u,\"origins\":%u"
                            ",\"constants\":%u,\"layouts\":%u"
                            ",\"types\":%u,\"globals\":%u"
                            ",\"foundBy\":%u,\"composed\":%u,\"chunks\":%u"
                            ",\"files\":%u,\"lines\":%u,\"paths\":%u"
                            ",\"names\":%u}",
                    of_code, of_origins, of_constants, of_layouts,
                    of_types, of_globals, of_found_by, of_composed,
                    of_chunks, of_files, of_lines, of_paths, of_names);
        }
        // And what that cost was paid for: every file this build read, and how
        // many bytes each of them is. A cost on its own is a number with
        // nothing to divide it by — a program that imports the library costs
        // what the library costs, and a tool dividing by the file it named
        // would say the program is fifteen times dearer per byte than it is.
        // What a reader wants is bytes of source against bytes of memory, and
        // only this side knows which files were read to get there. See D656.
        fputs(",\"read\":[", stdout);
        for (uint32_t at = 0;; at++) {
            const char *from = kest_build_read(build, at);
            if (from == NULL) {
                break;
            }
            fprintf(stdout, "%s{\"file\":", at > 0 ? "," : "");
            kest_json_text(from, stdout);
            fprintf(stdout, ",\"bytes\":%zu,\"mark\":\"%016llx\"}",
                    kest_build_read_bytes(build, at),
                    (unsigned long long)kest_build_read_mark(build, at));
        }
        fprintf(stdout, "],\"source\":%zu,\"mark\":\"%016llx\"",
                kest_build_source(build),
                (unsigned long long)kest_build_mark(build));
        // The name this file puts its own declarations under, which is not the
        // line it wrote: a file that says `module examples.math` declares
        // `math.factorial`, and a tool that read the line and put it in front
        // of a name would ask about one the program has not got. Everything
        // else here is written under it, so it is the one thing a reader needs
        // to go from a name in the file to a name in this. See D598.
        if (checking) {
            // Read from the file rather than from the module: `check` does not
            // compile, so the module this build would make has no name yet,
            // and the name is the file's own — the whole of what its `module`
            // line says, which is what every name in it lives under. See
            // D1039.
            const char *alias = build->units.count > 0
                                    ? build->units.items[0].module
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
        if (emitting && !building && build->compiled) {
            // And what the machine will run, as one number. Said where the
            // instructions are said, because it is those and not the file they
            // came from. See D659.
            fprintf(stdout, ",\"codeMark\":\"%016llx\"",
                    (unsigned long long)kest_build_code_mark(build));
            fputc(',', stdout);
            kest_module_disassemble_json(&build->module, EVERY_CALL, stdout);
            // And the same bodies as C, for a tool that asked for them. It is
            // a string rather than a stream of its own for the reason
            // everything else here is one: what a command says is one object.
            if (writing_c && kest_build_c(build) != NULL) {
                fputs(",\"c\":", stdout);
                kest_json_text(kest_build_c(build), stdout);
            }
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
        // What the run did, inside the object the run writes rather than
        // beside it: one run is one object, and a second object on another
        // stream is two things for a tool to put back together. See D979.
        if (was_counted) {
            fprintf(stdout,
                    ",\"profile\":{\"steps\":%llu,\"calls\":%llu,"
                    "\"crossings\":%llu,\"heap\":%zu,\"most\":%zu,"
                    "\"fuelGiven\":%llu,"
                    "\"fuelLeft\":%llu,\"bodies\":[",
                    (unsigned long long)counted.steps,
                    (unsigned long long)counted.calls,
                    (unsigned long long)counted.crossings, counted.heap,
                    counted.most,
                    (unsigned long long)counted.fuel_given,
                    (unsigned long long)counted.fuel_left);
            for (uint32_t which = 0; which < body_count; which++) {
                fprintf(stdout, "%s{\"name\":", which == 0 ? "" : ",");
                kest_json_text(bodies[which].name, stdout);
                fprintf(stdout, ",\"calls\":%llu}",
                        (unsigned long long)bodies[which].calls);
            }
            fputs("]}", stdout);
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
            fprintf(stdout, ",\"heap\":%zu,\"taken\":%zu", called_heap,
                    called_took);
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
            fprintf(stdout,
                    ",\"heap\":%zu,\"taken\":%zu,\"allowed\":%zu,"
                    "\"thrown\":%d",
                    ticked.heap, ticked.taken, ticked.allowed, ticked.thrown);
        }
        fputs("}\n", stdout);
    } else {
        // The door a host outside the library uses, which is why the command
        // line uses it: a way in that only ever runs when something has gone
        // wrong for somebody else is a way in nobody has walked through. In
        // JSON the object carries more than the diagnostics, so that one is
        // written here.
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        // What the compiler proved about each body, which is a different thing
        // from what the body says about itself. Four columns and no times:
        // a static count of instructions is not a duration and saying one in
        // nanoseconds would be a number nobody measured. What is here is what
        // was walked -- the heap, the host, and whether every operation is in
        // the deterministic profile -- and `could` for a promise the body
        // keeps and does not make. See D976.
        if (costing && checking && build->program != NULL &&
            build->diags.error_count == 0) {
            kest_program_costs(build->program, stdout);
        }
    }

    if (weighing) {
        say_what_compiling_took(kest_build_spent(build));
    }
    int status = build->diags.error_count > 0 || failed_to_choose
                     ? 1
                     : (int)(exit_code & 0xff);
    kest_build_free(build);
    return status;
}

// Every program named, run for its answer. A test in this language is a
// program that checks itself and answers with which check failed, which is
// what every example here is: there is no framework, and a number is a place
// in a file. See D982.
// Two names, the way a name sorts: what a directory hands back is in whatever
// order it kept, and what a run of tests says has to be the same every time.
static int by_name(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

// Every `.kest` directly under a directory, in that order. This is the one
// place this compiler asks what is in a directory, and `kest test` is what
// asks: a project says where its tests are and not which they are. Answers how
// many, with the paths in `into` for the caller to free, and nought for a
// directory that is not there or has none -- which the caller tells apart,
// because the second is a project saying something that is not so. See D1082.
static uint32_t tests_under(const char *where, char ***into) {
    *into = NULL;
    char **found = NULL;
    uint32_t count = 0;
    uint32_t room = 0;
#if defined(_WIN32)
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*.kest", where);
    struct _finddata_t entry;
    intptr_t walking = _findfirst(pattern, &entry);
    if (walking == -1) {
        return 0;
    }
    do {
        const char *name = entry.name;
#else
    DIR *walking = opendir(where);
    if (walking == NULL) {
        return 0;
    }
    for (const struct dirent *entry = readdir(walking); entry != NULL;
         entry = readdir(walking)) {
        const char *name = entry->d_name;
        size_t length = strlen(name);
        if (length < 6 || strcmp(name + length - 5, ".kest") != 0) {
            continue;
        }
#endif
        if (count == room) {
            uint32_t bigger = room == 0 ? 8 : room * 2;
            char **grown = realloc(found, (size_t)bigger * sizeof(char *));
            if (grown == NULL) {
                break;
            }
            found = grown;
            room = bigger;
        }
        size_t wide = strlen(where) + strlen(name) + 2;
        char *path = malloc(wide);
        if (path == NULL) {
            break;
        }
        snprintf(path, wide, "%s/%s", where, name);
        found[count++] = path;
#if defined(_WIN32)
    } while (_findnext(walking, &entry) == 0);
    _findclose(walking);
#else
    }
    closedir(walking);
#endif
    if (count > 1) {
        qsort(found, count, sizeof(char *), by_name);
    }
    *into = found;
    return count;
}

static int run_tests(const char *executable, char **paths, int path_count,
                     bool json, size_t room, uint64_t fuel) {
    int failed = 0;
    if (json) {
        printf("{\"schema\":%u,\"tests\":[", (unsigned)KEST_JSON_SCHEMA);
    }
    for (int i = 0; i < path_count; i++) {
        char *one[1] = {paths[i]};
        // Each on its own, because a program that will not compile is one
        // program that will not compile and the rest still run.
        int status = run("run", executable, one, 1, false, 0, NULL, false,
                         room, fuel, false, false);
        if (status != 0) {
            failed++;
        }
        if (json) {
            printf("%s{\"file\":", i == 0 ? "" : ",");
            kest_json_text(paths[i], stdout);
            printf(",\"answered\":%d}", status);
        } else {
            printf("%-40s %s\n", paths[i],
                   status == 0 ? "passed" : "FAILED");
            if (status != 0) {
                printf("    it answered %d, which is the check that failed\n",
                       status);
            }
        }
    }
    if (json) {
        printf("],\"failed\":%d,\"ran\":%d}\n", failed, path_count);
    } else if (path_count == 0) {
        printf("no programs were named, so nothing ran\n");
    } else {
        printf("%d of %d passed\n", path_count - failed, path_count);
    }
    return failed == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    KEST_BYTES_OUT();
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
        uint32_t profile = 0;
        const char *named = kest_profile(&profile);
        // What a host and a tool have to agree with, said where a person and a
        // script both read it: the language's own version, the shape the doors
        // are in, the shape the JSON is in, and which deterministic profile
        // this build holds `deterministic` to. Four numbers because they move
        // for four different reasons. See D974.
        printf("kest %s%s, abi %u, json %u, profile %s %u\n", kest_version(),
               kest_checked() ? " checked" : "", kest_abi_version(),
               (unsigned)KEST_JSON_SCHEMA, named, profile);
        return 0;
    }

    // The language server, which is this compiler answering an editor rather
    // than a person. It reads and writes one stream each and takes no files:
    // which file it is about is what the editor says. See D977.
    if (strcmp(argv[1], "lsp") == 0) {
        return kest_lsp_serve(kest_library_path(NULL, argv[0]), stdin, stdout);
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
    // The most this command may ask the machine for, and nought for as much as
    // there is, which is what it has always asked for. See D843.
    size_t room = 0;
    // And how many instructions it may run, nought for as many as it takes,
    // which is what this command has always allowed. See D921.
    uint64_t fuel = KEST_FUEL_UNLIMITED;
    // The events themselves, when `tick` was given a list rather than a count.
    int32_t *given = NULL;
    bool told_it = false;
    bool reset = false;
    // `check --cost`: what the compiler proved about each body, said to a
    // person. The same facts are in `--json` whether this was asked for or
    // not, because a tool reads one shape. See D976.
    bool costing = false;
    // `emit --c`: the same bodies written as C rather than as instructions,
    // for the compiler a release is built with. See D1093.
    bool writing_c = false;
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
    // What is after `--` is the program's rather than this command's: a file
    // named there would otherwise be read as another file to compile, and a
    // count as how many events to send. `std.os` is what a program asks for it
    // through. See D925.
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            program_args = &argv[i + 1];
            program_arg_count = argc - i - 1;
            argc = i;
            break;
        }
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
        } else if (strcmp(argv[i], "--cost") == 0) {
            costing = true;
        } else if (strcmp(argv[i], "--c") == 0) {
            writing_c = true;
        } else if (strcmp(argv[i], "--fuel") == 0) {
            // The count is the word after, for the reason `--room`'s is.
            if (i + 1 >= argc) {
                free(paths);
                free(given);
                return refused_at_the_words(json, "K0649",
                                            "`--fuel` says how many "
                                            "instruction(s), and there is "
                                            "nothing after it");
            }
            if (!read_fuel(argv[++i], &fuel, json)) {
                free(paths);
                free(given);
                return 1;
            }
        } else if (strcmp(argv[i], "--room") == 0) {
            // The amount is the word after, the way a count is: an option
            // written `--room=64M` is one word this command line would have
            // taken for a file, and a file is what everything it does not
            // know is.
            if (i + 1 >= argc) {
                free(paths);
                free(given);
                return refused_at_the_words(json, "K0649",
                                            "`--room` says how much, and "
                                            "there is nothing after it");
            }
            if (!read_room(argv[++i], &room, json)) {
                free(paths);
                free(given);
                return 1;
            }
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
        int status = per_file(paths, path_count, what, mode, json, room);
        free(paths);
        free(given);
        return status;
    }

    if (strcmp(argv[1], "test") == 0) {
        // Without a file, what the project says its tests are. A project is a
        // thing to be inside rather than a thing to name at every command, the
        // same way `kest build` works on the entry, and the manifest has said
        // since D982 that `kest test` runs the programs under this line. It
        // did not: the line was read and nothing asked for it. See D1082.
        char **found = NULL;
        uint32_t found_count = 0;
        int status = 0;
        if (path_count == 0) {
            KestArena *asking = kest_arena_new();
            const char *why = NULL;
            KestProject *here =
                asking == NULL ? NULL : kest_project_read(asking, "", &why);
            if (here != NULL && here->tests[0] != '\0') {
                found_count = tests_under(here->tests, &found);
                if (found_count == 0) {
                    // A project that says where its tests are and has none
                    // there is a project saying something that is not so, and
                    // a run of no tests that answers nought is a gate that
                    // passes for having done nothing.
                    status = refused_at_the_words(
                        json, "K0649",
                        "this project says its tests are in `%s`, and there "
                        "is no program there",
                        here->tests);
                }
            }
            if (asking != NULL) {
                kest_arena_free(asking);
            }
        }
        if (status == 0) {
            status = found_count > 0
                         ? run_tests(argv[0], found, (int)found_count, json,
                                     room, fuel)
                         : run_tests(argv[0], paths, path_count, json, room,
                                     fuel);
        }
        for (uint32_t i = 0; i < found_count; i++) {
            free(found[i]);
        }
        free(found);
        free(paths);
        free(given);
        return status;
    }

    // A project made, and a project looked over. Neither takes a file: `new`
    // takes a name and `doctor` takes a directory or nothing at all.
    if (strcmp(argv[1], "new") == 0) {
        int status = make_project(argc > 2 ? argv[2] : NULL, json);
        free(paths);
        free(given);
        return status;
    }

    if (strcmp(argv[1], "doctor") == 0) {
        int status = look_over(argv[0], argc > 2 ? argv[2] : "", json);
        free(paths);
        free(given);
        return status;
    }

    if (strcmp(argv[1], "check") == 0 || strcmp(argv[1], "emit") == 0 ||
        strcmp(argv[1], "run") == 0 || strcmp(argv[1], "tick") == 0 ||
        strcmp(argv[1], "profile") == 0 || strcmp(argv[1], "build") == 0 ||
        strcmp(argv[1], "debug") == 0 ||
        strcmp(argv[1], "call") == 0) {
        // Without a file, what the project says to work on. A project is a
        // thing to be inside rather than a thing to name at every command, so
        // `kest build` in a directory with one is `kest build` on its entry.
        // See D982.
        char *from_project = NULL;
        if (path_count == 0) {
            KestArena *asking = kest_arena_new();
            const char *why = NULL;
            KestProject *here =
                asking == NULL ? NULL : kest_project_read(asking, "", &why);
            if (here != NULL && here->entry[0] != '\0') {
                size_t length = strlen(here->entry);
                from_project = malloc(length + 1);
                if (from_project != NULL) {
                    memcpy(from_project, here->entry, length + 1);
                    paths[0] = from_project;
                    path_count = 1;
                }
            }
            if (asking != NULL) {
                kest_arena_free(asking);
            }
        }
        if (path_count == 0) {
            refused_at_the_words(json, "K0649",
                                 "`%s` needs a file, and there is no project "
                                 "here saying which",
                                 argv[1]);
            free(paths);
            return usage(json);
        }
        int status =
            run(argv[1], argv[0], paths, path_count, json, count, given,
                reset, room, fuel, costing, writing_c);
        free(from_project);
        free(paths);
        free(given);
        return status;
    }

    free(paths);
    free(given);
    refused_at_the_words(json, "K0649", "unknown command `%s`", argv[1]);
    return usage(json);
}
