#ifndef KEST_LOADER_H
#define KEST_LOADER_H

#include "parser.h"

// One file, parsed, with the name its declarations live under. A module
// `game.world` puts its names under `world`, so a file that imports it writes
// `world.Npc` and the file itself may write `Npc`.
//
// Everything here is filled in before the unit is handed on, and the fields
// that come off a `module` line have an answer for a file that has none: the
// loader zeroes a unit before it reads anything into it, so a reader of one
// never has to ask whether a field was reached.
typedef struct {
    // The path as it will be reported and the text as it was read. Always
    // both: a file that could not be read is not a unit.
    KestSource source;
    // What parsed, which is everything the parser could make of the file
    // rather than everything the file holds: a parse error is reported and the
    // walk goes on, so this is filled in for a file with mistakes in it.
    KestUnit unit;
    // The last part of what the file calls itself, or `""` for a file that
    // names no module. Those are legal and their names live under nothing,
    // which is what a program written on the spot to be run once does.
    const char *alias;
    // Whether this file is the library's. `std` is the one name a program
    // cannot use, and which side of that a file is on decides where it is read
    // from — so it is decided here, once, rather than by reading the module
    // line again wherever the answer is wanted. False for a file that names no
    // module, which cannot be the library's.
    bool from_library;
    // The aliases this file may reach, which is what it imports and its own.
    // Filled in after the imports have been followed, so it holds what was
    // written even when one of them could not be read.
    const char **imports;
    uint32_t import_count;
    // And which of them a name in this file was reached through, marked where
    // the reach is decided. An import nothing reaches is a module read,
    // parsed, checked and compiled for a file that never writes its name, and
    // what that costs is the whole of it. See D725.
    bool *import_reached;
} KestUnitInfo;

typedef struct {
    KestUnitInfo *items;
    uint32_t count;
    uint32_t capacity;
    // Where `std` was read from, kept because the question a checker asks of
    // the library is about a module no file imported — which is a module that
    // is not here at all, and so cannot be found by looking at what is. NULL
    // for a read that follows no imports, which has no library to speak of.
    // See D734.
    const char *library;
    // Where the trees are, which is not where everything else is. A tree is
    // read by the checker and by the compiler and by nothing after them, so it
    // is given back when the last copy has been compiled. NULL for a read that
    // parses into whatever arena it was handed, which is what the commands
    // that stop at a tree want. See D748.
    KestArena *trees;
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

// Whether the library has a module of this name, asked by looking for the file
// it would be read from. What this answers is about the installation and not
// about the program: a module nothing imports is in no program, so the only
// way to know it could have been imported is to ask where it would come from.
// False for a library that is nowhere, and for a name that could not be one.
// See D734.
bool kest_library_has(const char *library, const char *name, size_t length);

// Where the standard library is: what `KEST_LIB` says, or `lib/` beside the
// program, which is where it is when nothing has been installed.
// `arena` may be NULL, in which case the answer is not owned by one and is
// good until the next call.
const char *kest_library_path(KestArena *arena, const char *program);

// The two ways to read one file, named for how far each goes. A file that
// cannot be read is refused the same way by both.
//
// The source and nothing else. `lex` is the whole of what this is for: the
// token stream is what that command answers, and parsing to reach it is work
// nobody asked for and a second reading of the same file.
bool kest_read_source(KestArena *arena, KestDiags *diags, const char *path,
                      KestSource *into);

// The source and the tree it makes, following nothing it imports. `parse` and
// `fmt` want that: printing a file back does not depend on what it imports
// being there.
bool kest_read_unit(KestArena *arena, KestDiags *diags, const char *path,
                     KestUnits *units);

#endif
