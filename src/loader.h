#ifndef KEST_LOADER_H
#define KEST_LOADER_H

#include "parser.h"

// One file, parsed, with the name its declarations live under. A module
// `game.world` puts its names under `world`, so a file that imports it writes
// `world.Npc` and the file itself may write `Npc`.
typedef struct {
    KestSource source;
    KestUnit unit;
    const char *alias;
    // The aliases this file may reach, which is what it imports and its own.
    const char **imports;
    uint32_t import_count;
} KestUnitInfo;

typedef struct {
    KestUnitInfo *items;
    uint32_t count;
    uint32_t capacity;
} KestUnits;

// Reads a file, follows its imports, and parses everything reachable. An
// import names a path relative to the file that wrote it: `import game.world`
// is `game/world.kest` beside it. Returns false when a file cannot be read or
// the host is out of memory; a parse error is reported and does not stop the
// walk.
// Reads every file named and everything they import. The first one sets the
// root that imports resolve from, so a project is checked as a project rather
// than as whatever its entry point happens to reach.
// `library` is where `std` lives, which is the one name a project cannot use
// for itself. Everything else resolves from the root the first file settles.
bool kest_load_many(KestArena *arena, KestDiags *diags, const char *library,
                    char **paths, int count, KestUnits *units);

// Where the standard library is: what `KEST_LIB` says, or `lib/` beside the
// program, which is where it is when nothing has been installed.
// `arena` may be NULL, in which case the answer is not owned by one and is
// good until the next call.
const char *kest_library_path(KestArena *arena, const char *program);

// Reads and parses one file and follows nothing. Printing a file back does not
// depend on what it imports being there.
bool kest_load_alone(KestArena *arena, KestDiags *diags, const char *path,
                     KestUnits *units);

#endif
