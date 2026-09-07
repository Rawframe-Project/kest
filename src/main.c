#include <stdio.h>
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

// The name `main` lives under in the file the command named.
static const char *entry_name(KestArena *arena, const KestUnitInfo *root) {
    if (root->alias[0] == '\0') {
        return "main";
    }
    size_t room = strlen(root->alias) + 6;
    char *name = kest_arena_alloc(arena, room, 1);
    if (name != NULL) {
        snprintf(name, room, "%s.main", root->alias);
    }
    return name;
}

static int run(const char *command, const char *path, bool json) {
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        fprintf(stderr, "kest: out of memory\n");
        return 1;
    }

    KestDiags diags;
    kest_diags_init(&diags, arena);

    KestUnits units = {0};
    bool loaded = kest_load(arena, &diags, path, &units);

    bool running = strcmp(command, "run") == 0;
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
                kest_vm_run(arena, &module, entry_name(arena, &units.items[0]),
                            &diags, &exit_code);
            }
        }

        if (diags.error_count == 0 && !json) {
            if (emitting) {
                kest_module_disassemble(&module, stdout);
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
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--errors=json") == 0) {
            json = true;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "kest: unexpected argument '%s'\n", argv[i]);
            return usage();
        }
    }

    if (strcmp(argv[1], "lex") == 0 || strcmp(argv[1], "parse") == 0 ||
        strcmp(argv[1], "check") == 0 || strcmp(argv[1], "emit") == 0 ||
        strcmp(argv[1], "run") == 0) {
        if (path == NULL) {
            fprintf(stderr, "kest: %s needs a file\n", argv[1]);
            return usage();
        }
        return run(argv[1], path, json);
    }

    fprintf(stderr, "kest: unknown command '%s'\n", argv[1]);
    return usage();
}
