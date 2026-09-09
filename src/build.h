#ifndef KEST_BUILD_H
#define KEST_BUILD_H

#include "check.h"
#include "compile.h"
#include "contract.h"
#include "loader.h"
#include "vm.h"

// A compiled program and everything it was compiled from. One arena holds all
// of it, so freeing the build frees the lot.
struct KestBuild {
    KestArena *arena;
    KestDiags diags;
    KestUnits units;
    KestProgram *program;
    KestModule module;
    // How much of what the build has said has been written out, so a host
    // asking twice is not told the same thing twice. The command line renders
    // the whole set itself and does not touch this.
    uint32_t reported;
    // And whether the one thing a run with no memory can say has been said.
    // It is not in the list — making a list entry is what there was no room
    // for — so what keeps it from being said twice is a bit of its own.
    bool starve_said;
    bool compiled;
};

// The stages, so the command line can stop between them and a host does not
// have to know there are any. `library` may be NULL for `lib/` beside the
// program.
KestBuild *kest_build_open(const char *library, char **paths, int count);

// The name something lives under in the file that was named. A host does not
// need this — `kest_entry` leaves the module off for it — but the command line
// asks the program's own symbol table, which is registered qualified.
//
// Asked after the program is emitted, because it reads the module's own alias,
// which is the field `kest_entry` reads when it looks for the qualified form
// of a name a host wrote plainly. One field, so the two directions of one rule
// cannot come apart.
const char *kest_build_name(KestBuild *build, const char *name);
bool kest_build_check(KestBuild *build);
bool kest_build_emit(KestBuild *build);

#endif
