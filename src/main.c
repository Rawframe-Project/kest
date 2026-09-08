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
            "  tick <file> [n]   call `onEvents` once with n events, and\n"
            "                    `onEvent` n times, whichever are defined\n"
            "\n"
            "These read each file on its own and follow no imports, because\n"
            "what a file is does not depend on what it imports.\n"
            "\n"
            "  fmt <file>...     print the file in the one form it has\n"
            "  parse <file>...   print the syntax tree\n"
            "  lex <file>...     print the token stream\n"
            "\n"
            "  help              this\n"
            "\n"
            "options:\n"
            "  --json            everything this command says, as JSON:\n"
            "                    the diagnostics, and for `check` what the\n"
            "                    program holds\n"
            "  -w                fmt writes each file it is given\n"
            "  --check           fmt names the files that are not already in\n"
            "                    the form it prints, and exits non-zero\n"
            "  --reset           tick throws the heap away between events\n"
            "  --version         print the version\n"
            "\n"
            "exit status is 1 when anything was reported, and otherwise what\n"
            "`main` returned.\n"
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
        !kest_host_bind(host, "Io.write", io_write, output)) {
        kest_host_free(host);
        return NULL;
    }
    return host;
}

// The name a function lives under in the file the command named.
static const char *entry_name(KestArena *arena, const KestUnitInfo *root,
                              const char *what) {
    if (root->alias[0] == '\0') {
        return what;
    }
    size_t room = strlen(root->alias) + strlen(what) + 2;
    char *name = kest_arena_alloc(arena, room, 1);
    if (name != NULL) {
        snprintf(name, room, "%s.%s", root->alias, what);
    }
    return name;
}

#define MAX_EVENTS 65536

// The host calling into the program, in both shapes W11 measured. One call
// carrying the batch is the shape D007 makes the default; one call per event
// is kept because it has to remain expressible.
// Whether the program's entry takes what this host has to hand it. `tick`
// carries a batch of `i32`, and a program whose `onEvents` takes something
// else is told rather than handed the wrong bytes.
static bool takes_events(KestProgram *program, const char *name,
                         const char *shape, KestArena *arena) {
    KestSymbol *entry =
        kest_lookup_global(program, name, strlen(name));
    if (entry == NULL || entry->type->tag != KEST_T_FN) {
        return false;
    }
    if (entry->type->param_count != 1) {
        fprintf(stderr, "kest: `%s` takes %u arguments, and tick passes one\n",
                name, entry->type->param_count);
        return false;
    }
    const char *written = kest_type_name(arena, entry->type->params[0]);
    if (strcmp(written, shape) != 0) {
        fprintf(stderr,
                "kest: `%s` takes `%s`, and tick has `%s` to give it\n", name,
                written, shape);
        return false;
    }
    return true;
}

static void drive_events(KestRuntime *runtime, KestProgram *program,
                         KestArena *arena, const KestUnitInfo *root,
                         int32_t count, bool reset) {
    static int32_t events[MAX_EVENTS];
    for (int32_t i = 0; i < count; i++) {
        events[i] = i;
    }

    const char *bulk = entry_name(arena, root, "onEvents");
    const char *single = entry_name(arena, root, "onEvent");

    if (kest_defines(runtime, bulk) &&
        takes_events(program, bulk, "[i32]", arena)) {
        KestValue frame[1];
        frame[0] = kest_borrow(runtime, events, (uint32_t)count, "i32",
                               sizeof(int32_t));
        if (kest_call(runtime, bulk, frame, 1)) {
            printf("onEvents  1 crossing   returned %lld\n",
                   (long long)frame[0].integer);
        }
    }

    if (kest_defines(runtime, single) &&
        takes_events(program, single, "i32", arena)) {
        int64_t total = 0;
        size_t peak = 0;
        for (int32_t i = 0; i < count; i++) {
            KestValue frame[1];
            frame[0].integer = events[i];
            if (!kest_call(runtime, single, frame, 1)) {
                return;
            }
            total += frame[0].integer;
            if (kest_heap_used(runtime) > peak) {
                peak = kest_heap_used(runtime);
            }
            // Nothing of the program's survives a call, so between two of
            // them there is nothing left pointing at the heap.
            if (reset && !kest_heap_reset(runtime)) {
                return;
            }
        }
        printf("onEvent   %d crossings returned %lld, peak %zu bytes\n", count,
               (long long)total, peak);
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
        bool read = kest_load_alone(arena, &diags, paths[i], &units) &&
                    units.count > 0 && diags.error_count == 0;

        if (what != FILE_FORMAT) {
            if (read && !json) {
                if (count > 1) {
                    printf("// %s\n", paths[i]);
                }
                if (what == FILE_LEX) {
                    uint32_t found = 0;
                    KestToken *tokens = kest_lex_all(
                        arena, &units.items[0].source, &diags, &found);
                    if (diags.error_count == 0) {
                        dump_tokens(tokens, found, &units.items[0].source);
                    }
                } else {
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

        if (text == NULL) {
            kest_diags_sort(&diags);
            kest_diags_render(&diags, stderr);
            status = 1;
            kest_arena_free(arena);
            continue;
        }

        const KestSource *source = &units.items[0].source;
        bool same = length == source->length &&
                    memcmp(text, source->text, length) == 0;

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
static void write_result(const KestValue *frame, const KestType *type,
                         KestArena *arena, FILE *out) {
    char buffer[64];
    switch (type->tag) {
    case KEST_T_VOID:
        return;
    case KEST_T_BOOL:
        fprintf(out, "%s\n", frame[0].integer ? "true" : "false");
        return;
    case KEST_T_INT:
        if (type->is_signed) {
            fprintf(out, "%lld\n", (long long)frame[0].integer);
        } else {
            fprintf(out, "%llu\n", (unsigned long long)frame[0].integer);
        }
        return;
    case KEST_T_FLOAT:
        kest_write_real(buffer, sizeof(buffer), frame[0].real,
                        type->width == 32);
        fprintf(out, "%s\n", buffer);
        return;
    case KEST_T_TEXT:
        fprintf(out, "%s\n", frame[0].text);
        return;
    case KEST_T_OPTIONAL:
        // The tag is the last slot, which is where the value stops.
        if (frame[type->element->slots].integer == 0) {
            fprintf(out, "none\n");
        } else {
            write_result(frame, type->element, arena, out);
        }
        return;
    default:
        fprintf(out, "<%s>\n", kest_type_name(arena, type));
        return;
    }
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

    if (build->units.count > 0 && build->diags.error_count == 0) {
        const KestSource *root = &build->units.items[0].source;
        if (checking) {
            if (kest_build_check(build) && !json) {
                kest_program_dump(build->program, build->arena, stdout);
            }
        } else if (emitting) {
            if (kest_build_emit(build) && !json) {
                kest_module_disassemble(&build->module, stdout);
            }
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
                KestRuntime *runtime =
                    host == NULL ? NULL : kest_start(build, host, NULL);
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
                    if (kest_call(runtime, chosen->type->symbol, frame,
                                  width + 1)) {
                        write_result(frame, chosen->type->result, build->arena,
                                     json ? stderr : stdout);
                    }
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
            KestRuntime *runtime = kest_start(build, host, NULL);
            if (runtime != NULL) {
                if (ticking) {
                    drive_events(runtime, build->program, build->arena,
                                 &build->units.items[0],
                                 count, reset);
                    // What the program allocated and nothing freed, which is
                    // D012's cost with a number on it.
                    printf("heap      %zu bytes, none of it freed\n",
                           kest_heap_used(runtime));
                } else {
                    KestValue frame[1] = {{0}};
                    const char *entry = kest_build_name(build, "main");
                    kest_diags_in(&build->diags, root);
                    if (!kest_defines(runtime, entry)) {
                        KestSpan nowhere = {0, 0};
                        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR,
                                       "K0603", nowhere,
                                       "this file has no `main` to run");
                        kest_diags_suggest(&build->diags, "add `fn main() { }`");
                    } else if (kest_call(runtime, entry, frame, 1)) {
                        exit_code = frame[0].integer;
                    }
                }
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
