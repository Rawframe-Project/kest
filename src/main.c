#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kest.h"
#include "ast.h"
#include "diag.h"
#include "lexer.h"
#include "mem.h"
#include "parser.h"

static int usage(void) {
    fprintf(stderr,
            "usage: kest <command> [options]\n"
            "\n"
            "  lex <file>      print the token stream\n"
            "  parse <file>    print the syntax tree\n"
            "  --version       print the version\n"
            "\n"
            "options:\n"
            "  --errors=json   report diagnostics as JSON\n");
    return 1;
}

// Reads the whole file into arena memory, terminated so the lexer can look one
// byte past the end without a bounds check on every character.
static char *read_file(KestArena *arena, const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "kest: cannot open '%s'\n", path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size < 0) {
        fprintf(stderr, "kest: cannot read '%s'\n", path);
        fclose(file);
        return NULL;
    }

    char *text = kest_arena_alloc(arena, (size_t)size + 1, 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }

    size_t read = fread(text, 1, (size_t)size, file);
    fclose(file);
    text[read] = '\0';
    *length = read;
    return text;
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

static int run(const char *command, const char *path, bool json) {
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        fprintf(stderr, "kest: out of memory\n");
        return 1;
    }

    size_t length = 0;
    char *text = read_file(arena, path, &length);
    if (text == NULL) {
        kest_arena_free(arena);
        return 1;
    }

    KestSource source;
    KestDiags diags;
    kest_source_init(&source, arena, path, text, length);
    kest_diags_init(&diags, arena);

    bool lexing = strcmp(command, "lex") == 0;
    KestUnit unit = {0};
    uint32_t token_count = 0;
    KestToken *tokens = NULL;

    if (lexing) {
        tokens = kest_lex_all(arena, &source, &diags, &token_count);
    } else {
        kest_parse(arena, &source, &diags, &unit);
    }

    if (json) {
        kest_diags_render_json(&diags, &source, stdout);
    } else {
        if (diags.error_count == 0) {
            if (lexing) {
                dump_tokens(tokens, token_count, &source);
            } else {
                kest_ast_dump(&unit, &source, stdout);
            }
        }
        kest_diags_render(&diags, &source, stderr);
    }

    int status = diags.error_count > 0 ? 1 : 0;
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

    if (strcmp(argv[1], "lex") == 0 || strcmp(argv[1], "parse") == 0) {
        if (path == NULL) {
            fprintf(stderr, "kest: %s needs a file\n", argv[1]);
            return usage();
        }
        return run(argv[1], path, json);
    }

    fprintf(stderr, "kest: unknown command '%s'\n", argv[1]);
    return usage();
}
