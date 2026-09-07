#include "loader.h"

#ifndef KEST_LIB_DIR
#define KEST_LIB_DIR "/usr/local/lib/kest/"
#endif

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

// `game.world` under `root` is `root/game/world.kest`. Imports resolve from
// one place rather than from whoever wrote them, so a module path names one
// file however it is reached.
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

// Where the package directories start, worked out from what the file calls
// itself: `module a.b.c` in `x/y/a/b/c.kest` means the root is `x/y/`. A file
// that names no module has only its own directory to go on.
static const char *root_of(KestArena *arena, const char *path,
                           const char *dotted, size_t length) {
    if (dotted == NULL) {
        return directory_of(arena, path);
    }

    size_t room = length + 6;
    char *suffix = kest_arena_alloc(arena, room, 1);
    if (suffix == NULL) {
        return directory_of(arena, path);
    }
    for (size_t i = 0; i < length; i++) {
        suffix[i] = dotted[i] == '.' ? '/' : dotted[i];
    }
    snprintf(suffix + length, room - length, ".kest");

    size_t path_length = strlen(path);
    size_t suffix_length = strlen(suffix);
    if (path_length < suffix_length ||
        strcmp(path + path_length - suffix_length, suffix) != 0) {
        return directory_of(arena, path);
    }
    return kest_arena_strndup(arena, path, path_length - suffix_length);
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

// `std` is reserved: a module named that always comes from the library, so a
// program cannot shadow one and a reader always knows which is which.
static bool is_library(const char *dotted, size_t length) {
    return length >= 4 && memcmp(dotted, "std.", 4) == 0;
}

static bool load_one(KestArena *arena, KestDiags *diags, const char *root,
                     const char *library, const char *path, KestUnits *units,
                     KestSpan blame, const KestSource *blamed_in, bool follow,
                     const char **root_out) {
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

    // What the file calls itself comes first, because the root that its
    // imports resolve from is worked out from it.
    const KestDecl *module = NULL;
    for (uint32_t i = 0; i < units->items[self].unit.count; i++) {
        if (units->items[self].unit.items[i]->kind == KEST_DECL_MODULE) {
            module = units->items[self].unit.items[i];
            units->items[self].alias = last_segment(
                arena, units->items[self].source.text + module->name.offset,
                module->name.length);
        }
    }
    if (root_out != NULL) {
        *root_out = root_of(arena, path,
                            module == NULL
                                ? NULL
                                : units->items[self].source.text +
                                      module->name.offset,
                            module == NULL ? 0 : module->name.length);
        root = *root_out;
    }

    for (uint32_t i = 0; i < units->items[self].unit.count; i++) {
        const KestDecl *decl = units->items[self].unit.items[i];
        const char *name = units->items[self].source.text + decl->name.offset;

        if (decl->kind != KEST_DECL_IMPORT || !follow) {
            continue;
        }

        const char *from =
            is_library(name, decl->name.length) ? library : root;
        const char *next =
            path_of_import(arena, from, name, decl->name.length);
        if (next == NULL) {
            return false;
        }
        if (!load_one(arena, diags, root, library, next, units, decl->name,
                      &units->items[self].source, follow, NULL)) {
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

bool kest_load_many(KestArena *arena, KestDiags *diags, const char *library,
                    char **paths, int count, KestUnits *units) {
    if (count <= 0) {
        return false;
    }
    const char *root = directory_of(arena, paths[0]);
    KestSpan nowhere = {0, 0};
    for (int i = 0; i < count; i++) {
        // The first file settles the root; the rest are read against it.
        if (!load_one(arena, diags, root, library, paths[i], units, nowhere,
                      NULL, true, i == 0 ? &root : NULL)) {
            return false;
        }
    }
    return true;
}

// Whether the standard library is here, asked by looking for a file that is
// always in it. A path that is merely plausible is worse than no path.
static bool library_is_at(const char *directory) {
    char probe[1024];
    int written = snprintf(probe, sizeof(probe), "%sstd/io.kest", directory);
    if (written <= 0 || (size_t)written >= sizeof(probe)) {
        return false;
    }
    FILE *file = fopen(probe, "rb");
    if (file == NULL) {
        return false;
    }
    fclose(file);
    return true;
}

const char *kest_library_path(KestArena *arena, const char *program) {
    // A caller that has no arena yet gets one answer at a time, which is all
    // anybody needs of this.
    static char scratch[1024];

    const char *given = getenv("KEST_LIB");
    if (given != NULL && given[0] != '\0') {
        size_t length = strlen(given);
        snprintf(scratch, sizeof(scratch), "%s%s", given,
                 given[length - 1] == '/' ? "" : "/");
    } else {
        const char *slash = strrchr(program, '/');
        int length = slash == NULL ? 0 : (int)(slash - program) + 1;

        // Beside the program, which is where it is in a source tree, and then
        // where it is once installed, which is beside the program's own
        // directory rather than inside it.
        snprintf(scratch, sizeof(scratch), "%.*slib/", length, program);
        if (!library_is_at(scratch)) {
            snprintf(scratch, sizeof(scratch), "%.*s../lib/kest/", length,
                     program);
        }
        // And where it was put when the language was installed, which is what
        // a host that is not the command line has to fall back on.
        if (!library_is_at(scratch)) {
            snprintf(scratch, sizeof(scratch), "%s", KEST_LIB_DIR);
        }
    }

    if (arena == NULL) {
        return scratch;
    }
    return kest_arena_strndup(arena, scratch, strlen(scratch));
}

bool kest_load_alone(KestArena *arena, KestDiags *diags, const char *path,
                     KestUnits *units) {
    KestSpan nowhere = {0, 0};
    return load_one(arena, diags, "", "", path, units, nowhere, NULL, false,
                    NULL);
}

void kest_ast_dump_all(const KestUnits *units, FILE *out) {
    for (uint32_t i = 0; i < units->count; i++) {
        if (units->count > 1) {
            fprintf(out, "// %s\n", units->items[i].source.path);
        }
        kest_ast_dump(&units->items[i].unit, &units->items[i].source, out);
    }
}
