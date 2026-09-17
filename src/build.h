#ifndef KEST_BUILD_H
#define KEST_BUILD_H

#include "check.h"
#include "compile.h"
#include "contract.h"
#include "loader.h"
#include "lower.h"
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
    // Whether the checker keeps where every name was written and what it
    // named. Set between opening a build and checking it, by whoever is going
    // to ask -- which is the language server and nothing else. See D977.
    bool index_names;
    // The one walk of the whole program, worked out when somebody first asks
    // and handed to everybody who asks after, machines included. See D607.
    KestWalk walked;
};

// The stages, so the command line can stop between them and a host does not
// have to know there are any. `library` may be NULL for `lib/` beside the
// program.
//
// `room` is the most this build may ask the machine for, in bytes, and nought
// is as much as there is. A build with a ceiling is refused at the allocation
// that would cross it and says what it had taken and what it wanted, the way a
// program stopped by a heap ceiling does; a build without one asks until the
// machine has nothing left, which is what took a machine down eight times in a
// day. See D843.
KestBuild *kest_build_open(const char *library, char **paths, int count,
                           size_t room);

// The name something lives under in the file that was named. A host does not
// need this — `kest_entry` leaves the module off for it — but the command line
// asks the program's own symbol table, which is registered qualified.
//
// Asked after the program is emitted, because it reads the module's own alias,
// which is the field `kest_entry` reads when it looks for the qualified form
// of a name a host wrote plainly. One field, so the two directions of one rule
// cannot come apart.
const char *kest_build_name(KestBuild *build, const char *name);
// Asks the next check to keep the index an editor reads. Nothing else wants
// it and it is a third again of what a finished build holds, so it is off.
void kest_build_index_names(KestBuild *build, bool keep);
bool kest_build_check(KestBuild *build);
bool kest_build_emit(KestBuild *build);

#endif
