#include <math.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "kest.h"
#include "ast.h"
#include "diag.h"
#include "lexer.h"
#include "mem.h"
#include "parser.h"
#include "check.h"
#include "loader.h"
#include "compile.h"
#include "contract.h"
#include "vm.h"
#include "types.h"

static int usage(void) {
    fprintf(stderr,
            "usage: kest <command> [options]\n"
            "\n"
            "  lex <file>      print the token stream\n"
            "  parse <file>    print the syntax tree\n"
            "  check <file>    resolve declarations and report what is wrong\n"
            "  emit <file>     print the bytecode\n"
            "  run <file>      compile and run `main`\n"
            "  tick <file> [n] call `onEvents` once with n events, and\n"
            "                  `onEvent` n times, whichever are defined\n"
            "  --version       print the version\n"
            "\n"
            "options:\n"
            "  --errors=json   report diagnostics as JSON\n");
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

// What this command line offers a program as its host. It is not a standard
// library: it is three functions, here so that `extern` means something a
// program can be run against.
static void host_sqrt(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    frame[0].real = sqrt(frame[0].real);
}

static void host_write(KestValue *frame, KestRuntime *runtime) {
    (void)runtime;
    fputs(frame[0].text, stdout);
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
        !kest_host_bind(host, "Host.sample", host_sample)) {
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
                         const KestUnitInfo *root, int32_t count) {
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
        for (int32_t i = 0; i < count; i++) {
            KestValue frame[1];
            frame[0].integer = events[i];
            if (!kest_call(runtime, single, frame)) {
                return;
            }
            total += frame[0].integer;
        }
        printf("onEvent   %d crossings returned %lld\n", count,
               (long long)total);
    }
}

static int run(const char *command, const char *path, bool json,
               int32_t count) {
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        fprintf(stderr, "kest: out of memory\n");
        return 1;
    }

    KestDiags diags;
    kest_diags_init(&diags, arena);

    KestUnits units = {0};
    bool loaded = kest_load(arena, &diags, path, &units);

    bool ticking = strcmp(command, "tick") == 0;
    bool running = strcmp(command, "run") == 0 || ticking;
    bool emitting = strcmp(command, "emit") == 0;
    bool checking = strcmp(command, "check") == 0 || emitting || running;
    bool lexing = strcmp(command, "lex") == 0;

    KestProgram *program = NULL;
    KestModule module = {0};
    int64_t exit_code = 0;

    if (loaded && units.count > 0) {
        const KestSource *root = &units.items[0].source;
        if (lexing) {
            uint32_t count = 0;
            kest_diags_in(&diags, root);
            KestToken *tokens = kest_lex_all(arena, root, &diags, &count);
            if (diags.error_count == 0 && !json) {
                dump_tokens(tokens, count, root);
            }
        } else if (checking && diags.error_count == 0) {
            if (kest_check(arena, &diags, &units, &program)) {
                kest_check_bodies(program, &units);
                if (diags.error_count == 0) {
                    kest_check_contracts(program, &units);
                }
            }
            if ((emitting || running) && diags.error_count == 0) {
                kest_module_init(&module, arena);
                kest_compile(program, &units, &module);
            }
            // Running happens before the diagnostics are rendered, so a
            // failure while running joins the same set and prints the same
            // way.
            if (running && diags.error_count == 0) {
                KestHost *host = make_host();
                if (host == NULL) {
                    fprintf(stderr, "kest: out of memory\n");
                    kest_arena_free(arena);
                    return 1;
                }
                if (ticking) {
                    KestRuntime *runtime =
                        kest_runtime_new(arena, &module, host, &diags);
                    if (runtime != NULL) {
                        drive_events(runtime, arena, &units.items[0], count);
                        kest_runtime_free(runtime);
                    }
                } else {
                    // What is being run is the file the command named, which
                    // is where a message about it belongs.
                    kest_diags_in(&diags, &units.items[0].source);
                    kest_vm_run(arena, &module,
                                entry_name(arena, &units.items[0], "main"),
                                host, &diags, &exit_code);
                }
                kest_host_free(host);
            }
        }

        if (diags.error_count == 0 && !json) {
            if (emitting) {
                kest_module_disassemble(&module, stdout);
            } else if (strcmp(command, "check") == 0) {
                kest_program_dump(program, arena, stdout);
            } else if (!running && !lexing) {
                kest_ast_dump_all(&units, stdout);
            }
        }
    }

    kest_diags_sort(&diags);
    if (json) {
        kest_diags_render_json(&diags, stdout);
    } else {
        kest_diags_render(&diags, stderr);
    }

    int status = diags.error_count > 0 ? 1 : (int)(exit_code & 0xff);
    kest_arena_free(arena);
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

    bool json = false;
    const char *path = NULL;
    int32_t count = 1024;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--errors=json") == 0) {
            json = true;
        } else if (path == NULL) {
            path = argv[i];
        } else if (argv[i][0] >= '0' && argv[i][0] <= '9') {
            count = atoi(argv[i]);
            if (count < 0 || count > MAX_EVENTS) {
                fprintf(stderr, "kest: between 0 and %d events\n", MAX_EVENTS);
                return 1;
            }
        } else {
            fprintf(stderr, "kest: unexpected argument '%s'\n", argv[i]);
            return usage();
        }
    }

    if (strcmp(argv[1], "lex") == 0 || strcmp(argv[1], "parse") == 0 ||
        strcmp(argv[1], "check") == 0 || strcmp(argv[1], "emit") == 0 ||
        strcmp(argv[1], "run") == 0 || strcmp(argv[1], "tick") == 0) {
        if (path == NULL) {
            fprintf(stderr, "kest: %s needs a file\n", argv[1]);
            return usage();
        }
        return run(argv[1], path, json, count);
    }

    fprintf(stderr, "kest: unknown command '%s'\n", argv[1]);
    return usage();
}
