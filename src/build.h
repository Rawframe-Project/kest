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
    bool compiled;
    // How many of them have already been written out, so a host that asks
    // twice is told each thing once.
    uint32_t reported;
};

// The stages, so the command line can stop between them and a host does not
// have to know there are any. `library` may be NULL for `lib/` beside the
// program.
KestBuild *kest_build_open(const char *library, char **paths, int count);

// The name something lives under in the file that was named. A host does not
// need this — `kest_entry` leaves the module off for it — but the command line
// asks the program's own symbol table, which is registered qualified.
const char *kest_build_name(KestBuild *build, const char *name);
bool kest_build_check(KestBuild *build);
bool kest_build_emit(KestBuild *build);

#endif
