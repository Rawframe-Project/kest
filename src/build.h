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
};

// The stages, so the command line can stop between them and a host does not
// have to know there are any. `library` may be NULL for `lib/` beside the
// program.
KestBuild *kest_build_open(const char *library, char **paths, int count);
bool kest_build_check(KestBuild *build);
bool kest_build_emit(KestBuild *build);

#endif
