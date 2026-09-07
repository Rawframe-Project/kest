#include "loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reads the whole file into arena memory, terminated so the lexer can look one
// byte past the end without a bounds check on every character.
static char *read_file(KestArena *arena, const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size < 0) {
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

// The directory a path is in, with its separator, or an empty string.
static const char *directory_of(KestArena *arena, const char *path) {
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return "";
    }
    return kest_arena_strndup(arena, path, (size_t)(slash - path) + 1);
}

// `game.world` beside `dir` is `dir/game/world.kest`.
static const char *path_of_import(KestArena *arena, const char *directory,
                                  const char *dotted, size_t length) {
    size_t room = strlen(directory) + length + 6;
    char *path = kest_arena_alloc(arena, room, 1);
    if (path == NULL) {
        return NULL;
    }
    size_t used = (size_t)snprintf(path, room, "%s", directory);
    for (size_t i = 0; i < length; i++) {
        path[used++] = dotted[i] == '.' ? '/' : dotted[i];
    }
    snprintf(path + used, room - used, ".kest");
    return path;
}

static const char *last_segment(KestArena *arena, const char *dotted,
                                size_t length) {
    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        if (dotted[i] == '.') {
            start = i + 1;
        }
    }
    return kest_arena_strndup(arena, dotted + start, length - start);
}

static KestUnitInfo *reserve(KestArena *arena, KestUnits *units) {
    if (units->count == units->capacity) {
        uint32_t grown = units->capacity == 0 ? 8 : units->capacity * 2;
        KestUnitInfo *moved = KEST_ARENA_ARRAY(arena, KestUnitInfo, grown);
        if (moved == NULL) {
            return NULL;
        }
        if (units->count > 0) {
            memcpy(moved, units->items, sizeof(KestUnitInfo) * units->count);
        }
        units->items = moved;
        units->capacity = grown;
    }
    return &units->items[units->count++];
}

static bool already_loaded(const KestUnits *units, const char *path) {
    for (uint32_t i = 0; i < units->count; i++) {
        if (strcmp(units->items[i].source.path, path) == 0) {
            return true;
        }
    }
    return false;
}

static bool load_one(KestArena *arena, KestDiags *diags, const char *path,
                     KestUnits *units, KestSpan blame,
                     const KestSource *blamed_in) {
    if (already_loaded(units, path)) {
        return true;
    }

    size_t length = 0;
    char *text = read_file(arena, path, &length);
    if (text == NULL) {
        // A missing import is reported where it was written, unless this is
        // the file the command named, which has nowhere to point at.
        kest_diags_in(diags, blamed_in);
        KestSpan nowhere = {0, 0};
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0701",
                       blamed_in == NULL ? nowhere : blame,
                       "cannot read `%s`", path);
        return blamed_in != NULL;
    }

    KestUnitInfo *info = reserve(arena, units);
    if (info == NULL) {
        return false;
    }
    memset(info, 0, sizeof(*info));
    info->alias = "";

    const char *owned = kest_arena_strndup(arena, path, strlen(path));
    if (owned == NULL ||
        !kest_source_init(&info->source, arena, owned, text, length)) {
        return false;
    }

    kest_diags_in(diags, &info->source);
    if (!kest_parse(arena, &info->source, diags, &info->unit)) {
        return false;
    }

    // The array may move as more files are read, so nothing below holds the
    // pointer across a load.
    uint32_t self = units->count - 1;
    const char *directory = directory_of(arena, path);

    for (uint32_t i = 0; i < units->items[self].unit.count; i++) {
        const KestDecl *decl = units->items[self].unit.items[i];
        const char *name = units->items[self].source.text + decl->name.offset;

        if (decl->kind == KEST_DECL_MODULE) {
            units->items[self].alias =
                last_segment(arena, name, decl->name.length);
            continue;
        }
        if (decl->kind != KEST_DECL_IMPORT) {
            continue;
        }

        const char *next =
            path_of_import(arena, directory, name, decl->name.length);
        if (next == NULL) {
            return false;
        }
        if (!load_one(arena, diags, next, units, decl->name,
                      &units->items[self].source)) {
            return false;
        }
    }

    // Collected after the walk, because the source pointer is stable but the
    // unit array is not.
    KestUnitInfo *loaded = &units->items[self];
    uint32_t imports = 0;
    for (uint32_t i = 0; i < loaded->unit.count; i++) {
        if (loaded->unit.items[i]->kind == KEST_DECL_IMPORT) {
            imports++;
        }
    }
    loaded->imports = KEST_ARENA_ARRAY(arena, const char *, imports + 1);
    if (loaded->imports == NULL) {
        return false;
    }
    for (uint32_t i = 0; i < loaded->unit.count; i++) {
        const KestDecl *decl = loaded->unit.items[i];
        if (decl->kind == KEST_DECL_IMPORT) {
            loaded->imports[loaded->import_count++] = last_segment(
                arena, loaded->source.text + decl->name.offset,
                decl->name.length);
        }
    }
    return true;
}

bool kest_load(KestArena *arena, KestDiags *diags, const char *path,
               KestUnits *units) {
    KestSpan nowhere = {0, 0};
    return load_one(arena, diags, path, units, nowhere, NULL);
}

void kest_ast_dump_all(const KestUnits *units, FILE *out) {
    for (uint32_t i = 0; i < units->count; i++) {
        if (units->count > 1) {
            fprintf(out, "// %s\n", units->items[i].source.path);
        }
        kest_ast_dump(&units->items[i].unit, &units->items[i].source, out);
    }
}
