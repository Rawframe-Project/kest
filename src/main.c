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
            "Every command takes more than one file. The first settles where\n"
            "imports resolve from, and for `run` and `tick` it is the one\n"
            "whose `main` is called.\n"
            "\n"
            "commands:\n"
            "  check <file>...   resolve everything and report what is wrong\n"
            "  run <file>...     compile and run `main`\n"
            "  fmt <file>...     print the file in the one form it has\n"
            "  emit <file>...    print the bytecode\n"
            "  parse <file>...   print the syntax tree\n"
            "  lex <file>        print the token stream\n"
            "  tick <file> [n]   call `onEvents` once with n events, and\n"
            "                    `onEvent` n times, whichever are defined\n"
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

static void host_sqrt(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].real = sqrt(frame[0].real);
}

static void host_write(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    fputs(frame[0].text, stdout);
}

// What `std.io` declares. This command line writes to its output; an engine
// would write to its console.
static void io_write(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    fputs(frame[0].text, stdout);
}

// What the standard library declares and every host has to provide. A program
// that never reaches one of these never asks for it.
static void math_sqrt(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].real = sqrt(frame[0].real);
}

static void math_floor(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].real = floor(frame[0].real);
}

static void math_ceil(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].real = ceil(frame[0].real);
}

static void math_sin(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].real = sin(frame[0].real);
}

static void math_cos(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].real = cos(frame[0].real);
}

static void math_pow(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].real = pow(frame[0].real, frame[1].real);
}

static void host_clock(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].integer = (int64_t)clock() * 1000000 / CLOCKS_PER_SEC;
}

// Memory this program owns, handed to Kest without copying it. A real engine
// would hand over its particle positions the same way.
#define HOST_SAMPLE_COUNT 1024
static float host_samples[HOST_SAMPLE_COUNT];

// Reads the host's own array, so a program writing through the view it was
// lent can be shown to have written here.
static void host_sample(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    int64_t index = frame[0].integer;
    frame[0].real = index >= 0 && index < HOST_SAMPLE_COUNT
                        ? host_samples[index]
                        : -1.0f;
}

static void host_samples_view(KestValue *frame, KestRuntime *runtime) {
    for (uint32_t i = 0; i < HOST_SAMPLE_COUNT; i++) {
        host_samples[i] = (float)i * 0.5f;
    }
    frame[0] = kest_borrow(runtime, host_samples, HOST_SAMPLE_COUNT,
                           sizeof(float));
}

static KestHost *make_host(void) {
    KestHost *host = kest_host_new();
    if (host == NULL) {
        return NULL;
    }
    if (!kest_host_bind(host, "Host.sqrt", host_sqrt) ||
        !kest_host_bind(host, "Host.write", host_write) ||
        !kest_host_bind(host, "Host.clock", host_clock) ||
        !kest_host_bind(host, "Host.samples", host_samples_view) ||
        !kest_host_bind(host, "Host.sample", host_sample) ||
        !kest_host_bind(host, "Math.sqrt", math_sqrt) ||
        !kest_host_bind(host, "Math.floor", math_floor) ||
        !kest_host_bind(host, "Math.ceil", math_ceil) ||
        !kest_host_bind(host, "Math.sin", math_sin) ||
        !kest_host_bind(host, "Math.cos", math_cos) ||
        !kest_host_bind(host, "Math.pow", math_pow) ||
        !kest_host_bind(host, "Io.write", io_write)) {
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
static void drive_events(KestRuntime *runtime, KestArena *arena,
                         const KestUnitInfo *root, int32_t count,
                         bool reset) {
    static int32_t events[MAX_EVENTS];
    for (int32_t i = 0; i < count; i++) {
        events[i] = i;
    }

    const char *bulk = entry_name(arena, root, "onEvents");
    const char *single = entry_name(arena, root, "onEvent");

    if (kest_defines(runtime, bulk)) {
        KestValue frame[1];
        frame[0] = kest_borrow(runtime, events, (uint32_t)count,
                               sizeof(int32_t));
        if (kest_call(runtime, bulk, frame)) {
            printf("onEvents  1 crossing   returned %lld\n",
                   (long long)frame[0].integer);
        }
    }

    if (kest_defines(runtime, single)) {
        int64_t total = 0;
        size_t peak = 0;
        for (int32_t i = 0; i < count; i++) {
            KestValue frame[1];
            frame[0].integer = events[i];
            if (!kest_call(runtime, single, frame)) {
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

// Formatting reads one file and follows nothing, so each is its own answer and
// one that cannot be parsed does not stop the rest.
static int format_files(char **paths, int count, FormatMode mode) {
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

        size_t length = 0;
        const char *text = NULL;
        if (kest_load_alone(arena, &diags, paths[i], &units) &&
            units.count > 0 && diags.error_count == 0) {
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

static int run(const char *command, const char *executable, char **paths,
               int path_count, bool json, int32_t count, bool reset) {
    KestBuild *build = kest_build_open(kest_library_path(NULL, executable),
                                       paths, path_count);
    if (build == NULL) {
        fprintf(stderr, "kest: out of memory\n");
        return 1;
    }

    bool ticking = strcmp(command, "tick") == 0;
    bool running = strcmp(command, "run") == 0 || ticking;
    bool emitting = strcmp(command, "emit") == 0;
    bool checking = strcmp(command, "check") == 0;
    bool lexing = strcmp(command, "lex") == 0;
    int64_t exit_code = 0;

    if (build->units.count > 0 && build->diags.error_count == 0) {
        const KestSource *root = &build->units.items[0].source;
        if (lexing) {
            uint32_t tokens_found = 0;
            kest_diags_in(&build->diags, root);
            KestToken *tokens =
                kest_lex_all(build->arena, root, &build->diags, &tokens_found);
            if (build->diags.error_count == 0 && !json) {
                dump_tokens(tokens, tokens_found, root);
            }
        } else if (checking) {
            if (kest_build_check(build) && !json) {
                kest_program_dump(build->program, build->arena, stdout);
            }
        } else if (emitting) {
            if (kest_build_emit(build) && !json) {
                kest_module_disassemble(&build->module, stdout);
            }
        } else if (running && kest_build_emit(build)) {
            KestHost *host = make_host();
            if (host == NULL) {
                fprintf(stderr, "kest: out of memory\n");
                kest_build_free(build);
                return 1;
            }
            KestRuntime *runtime = kest_start(build, host, NULL);
            if (runtime != NULL) {
                if (ticking) {
                    drive_events(runtime, build->arena, &build->units.items[0],
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
                    } else if (kest_call(runtime, entry, frame)) {
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

    int status =
        build->diags.error_count > 0 ? 1 : (int)(exit_code & 0xff);
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

    if (strcmp(argv[1], "fmt") == 0) {
        if (path_count == 0) {
            fprintf(stderr, "kest: fmt needs a file\n");
            free(paths);
            return usage();
        }
        int status = format_files(paths, path_count, mode);
        free(paths);
        return status;
    }

    if (strcmp(argv[1], "lex") == 0 || strcmp(argv[1], "parse") == 0 ||
        strcmp(argv[1], "check") == 0 || strcmp(argv[1], "emit") == 0 ||
        strcmp(argv[1], "run") == 0 || strcmp(argv[1], "tick") == 0) {
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
