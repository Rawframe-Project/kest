#include "loader.h"

#ifndef KEST_LIB_DIR
#define KEST_LIB_DIR "/usr/local/lib/kest/"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Reads the whole file into arena memory, terminated so the lexer can look one
// byte past the end without a bounds check on every character.
// A stream that cannot be measured is read until it ends. A pipe is one, which
// is what a shell hands over for `kest check <(...)`, and it used to come back
// as a file this could not read.
static char *read_stream(KestArena *arena, FILE *file, size_t *length) {
    size_t room = 4096;
    size_t used = 0;
    char *text = kest_arena_alloc(arena, room, 1);
    while (text != NULL) {
        size_t read = fread(text + used, 1, room - used - 1, file);
        used += read;
        if (read == 0 || feof(file)) {
            break;
        }
        if (used + 1 < room) {
            continue;
        }
        char *grown = kest_arena_alloc(arena, room * 2, 1);
        if (grown == NULL) {
            return NULL;
        }
        memcpy(grown, text, used);
        text = grown;
        room *= 2;
    }
    if (text == NULL || ferror(file)) {
        return NULL;
    }
    text[used] = '\0';
    *length = used;
    return text;
}

static char *read_file(KestArena *arena, const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);
    if (size < 0) {
        // Not a mistake: a stream that cannot say how long it is is read to
        // the end instead of being refused for not knowing.
        char *text = read_stream(arena, file, length);
        fclose(file);
        return text;
    }

    char *text = kest_arena_alloc(arena, (size_t)size + 1, 1);
    if (text == NULL) {
        fclose(file);
        return NULL;
    }
    size_t read = fread(text, 1, (size_t)size, file);
    // A read that failed is not a file this read. A directory opens, measures
    // nought, and refuses to be read — and reading nought bytes of it fails at
    // nothing, so it is asked for one. Without that, a path that is a
    // directory was a file with nothing in it, and `kest check` said it
    // declared nothing.
    if (size == 0) {
        fgetc(file);
    }
    bool broke = ferror(file) != 0;
    fclose(file);
    if (broke) {
        return NULL;
    }
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
    const char *kept =
        kest_arena_strndup(arena, path, (size_t)(slash - path) + 1);
    // No room for the directory is the same answer as there being none: what
    // is written round it is a path, and a path built onto nothing is a path
    // that will not be read. See D510.
    return kept == NULL ? "" : kept;
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
    const char *kept =
        kest_arena_strndup(arena, path, path_length - suffix_length);
    return kept == NULL ? directory_of(arena, path) : kept;
}

static const char *last_segment(KestArena *arena, const char *dotted,
                                size_t length) {
    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        if (dotted[i] == '.') {
            start = i + 1;
        }
    }
    const char *kept = kest_arena_strndup(arena, dotted + start, length - start);
    // An alias nobody could write is an alias nothing matches, which is what a
    // file that named no module already has. See D510.
    return kept == NULL ? "" : kept;
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

// The same file spelled two ways is the same file. A command line names
// `lib/std/random.kest` and an import of it resolves to `./lib/std/random.kest`
// from the library root, and reading both would declare everything in it
// twice — which is what happened, and what it said was that the file disagreed
// with itself.
//
// Only the spellings that arise from putting paths together: a leading `./`, a
// doubled slash, and a step into a directory and back out of it. Two paths
// that reach one file by different routes through the file system are two
// files as far as this is concerned, which is the same answer a compiler that
// reads what it is given has to give.
static const char *tidied(KestArena *arena, const char *path) {
    size_t length = strlen(path);
    char *out = kest_arena_alloc(arena, length + 1, 1);
    if (out == NULL) {
        return path;
    }
    size_t used = 0;
    for (size_t i = 0; i < length;) {
        if (path[i] == '/' && used > 0 && out[used - 1] == '/') {
            i++;
            continue;
        }
        if (path[i] == '.' && path[i + 1] == '/' &&
            (used == 0 || out[used - 1] == '/')) {
            i += 2;
            continue;
        }
        // `a/b/../c` is `a/c`, and `../c` at the front is left as it is
        // because there is nothing above it to take away.
        if (path[i] == '.' && path[i + 1] == '.' && path[i + 2] == '/' &&
            used > 1) {
            size_t back = used - 1;
            while (back > 0 && out[back - 1] != '/') {
                back--;
            }
            bool upwards = used - back == 3 && out[back] == '.' &&
                           out[back + 1] == '.';
            if (!upwards) {
                used = back;
                i += 3;
                continue;
            }
        }
        out[used++] = path[i++];
    }
    out[used] = '\0';
    return out;
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

// The one place a file that could not be read is refused, whether a command
// named it or an import asked for it. What a reader is pointed at is the
// import when there is one and nothing when the path came from a command line,
// which has nowhere in a file to point.
static void refuse_to_read(KestDiags *diags, const char *path, KestSpan blame,
                           const KestSource *blamed_in) {
    KestSpan nowhere = {0, 0};
    kest_diags_in(diags, blamed_in);
    kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0701",
                   blamed_in == NULL ? nowhere : blame, "cannot read `%s`",
                   path);
}

static bool load_one(KestArena *arena, KestDiags *diags, const char *root,
                     const char *library, const char *given, KestUnits *units,
                     KestSpan blame, const KestSource *blamed_in, bool follow,
                     bool from_library, const char **root_out) {
    // One spelling per file, whether it was named on a command line or worked
    // out from an import.
    const char *path = tidied(arena, given);
    // A file that imports itself. Its own names are already its own, so the
    // line asks for nothing and reads as though it did: two files importing
    // each other is a program, and one importing itself is a mistake nobody
    // means to write.
    if (blamed_in != NULL && strcmp(blamed_in->path, path) == 0) {
        kest_diags_in(diags, blamed_in);
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0704", blame,
                       "this file imports itself");
        kest_diags_suggest(diags,
                           "its own names are its own already, written "
                           "without a module in front of them");
        return true;
    }
    if (already_loaded(units, path)) {
        return true;
    }

    size_t length = 0;
    char *text = read_file(arena, path, &length);
    if (text == NULL) {
        // A missing import is reported where it was written, unless this is
        // the file the command named, which has nowhere to point at.
        refuse_to_read(diags, path, blame, blamed_in);
        // Which directory that path came from, for the reader who is looking
        // at the import and not at the loader. A program handed over as a
        // stream is the case this is really for: it is nowhere, so an import
        // of its own resolves under `/dev` and there is nothing there.
        if (blamed_in != NULL) {
            const char *slash = strrchr(path, '/');
            if (from_library) {
                kest_diags_suggest(diags,
                                   "a `std` import resolves from the library, "
                                   "which is `%.*s`",
                                   slash == NULL ? 1 : (int)(slash - path),
                                   slash == NULL ? "." : path);
            } else if (slash == NULL) {
                kest_diags_suggest(diags,
                                   "an import resolves from where the file "
                                   "that wrote it is, which is here");
            } else {
                kest_diags_suggest(diags,
                                   "an import resolves from where the file "
                                   "that wrote it is, which is `%.*s`",
                                   (int)(slash - path), path);
            }
        }
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
            units->items[self].from_library = is_library(
                units->items[self].source.text + module->name.offset,
                module->name.length);
        }
    }
    // A file that names no module puts its names under nothing, which is what
    // a program written to be run once wants and is no use to anybody
    // importing it: its names would land in the importing file's own, and
    // where a name came from is written at every use of it in this language.
    if (blamed_in != NULL && units->items[self].alias[0] == '\0') {
        kest_diags_in(diags, blamed_in);
        kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0702", blame,
                       "`%s` names no module, so its names have nowhere to "
                       "live",
                       path);
        // The name this import asked for is the name it should have: an
        // import is a path, so the two are the same thing written twice.
        kest_diags_suggest(diags,
                           "a file that is imported says what it is called: "
                           "`module %.*s`",
                           (int)blame.length, blamed_in->text + blame.offset);
        return true;
    }

    // A file is imported by its path and says under what name its own names
    // live. Two spellings of one file is a file nothing can import: the names
    // land where nobody wrote them, and the only message was `unknown name`
    // at every use of one, in the file that did nothing wrong.
    if (blamed_in != NULL && module != NULL) {
        const char *called = units->items[self].source.text +
                             module->name.offset;
        const char *asked = blamed_in->text + blame.offset;
        if (module->name.length != blame.length ||
            memcmp(called, asked, blame.length) != 0) {
            kest_diags_in(diags, blamed_in);
            kest_diags_add(diags, KEST_SEVERITY_ERROR, "K0703", blame,
                           "`%s` calls itself `%.*s`", path,
                           (int)module->name.length, called);
            kest_diags_suggest(diags,
                               "an import is a path, so a file read by this "
                               "one says `module %.*s`",
                               (int)blame.length, asked);
            // The line somebody has to change is in the other file, and the
            // reader of this message is looking at their own.
            kest_diags_note(diags, &units->items[self].source, module->name,
                            "this is the name it says");
            return true;
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

        bool library_import = is_library(name, decl->name.length);
        const char *from = library_import ? library : root;
        const char *next =
            path_of_import(arena, from, name, decl->name.length);
        if (next == NULL) {
            return false;
        }
        if (!load_one(arena, diags, root, library, next, units, decl->name,
                      &units->items[self].source, follow, library_import,
                      NULL)) {
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
    loaded->import_reached = KEST_ARENA_ARRAY(arena, bool, imports + 1);
    if (loaded->imports == NULL || loaded->import_reached == NULL) {
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
                      NULL, true, false, i == 0 ? &root : NULL)) {
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
    // The same answer the caller with no arena gets, and the same promise: one
    // at a time. A copy is what an arena is for and not what this needs to be
    // right. See D510.
    const char *kept = kest_arena_strndup(arena, scratch, strlen(scratch));
    return kept == NULL ? scratch : kept;
}

bool kest_read_source(KestArena *arena, KestDiags *diags, const char *path,
                      KestSource *into) {
    const char *tidy = tidied(arena, path);
    size_t length = 0;
    char *text = read_file(arena, tidy, &length);
    if (text == NULL) {
        KestSpan nowhere = {0, 0};
        refuse_to_read(diags, tidy, nowhere, NULL);
        return false;
    }
    const char *owned = kest_arena_strndup(arena, tidy, strlen(tidy));
    return owned != NULL &&
           kest_source_init(into, arena, owned, text, length);
}

bool kest_read_unit(KestArena *arena, KestDiags *diags, const char *path,
                     KestUnits *units) {
    KestSpan nowhere = {0, 0};
    return load_one(arena, diags, "", "", path, units, nowhere, NULL, false,
                    false, NULL);
}
