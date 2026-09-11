#ifndef KEST_VM_H
#define KEST_VM_H

#include "value.h"


// What this machine has said. A host reads it with `kest_report`; the command
// line wants the set rather than a rendering, because it sorts what running
// found together with what compiling did.
KestDiags *kest_runtime_said(KestRuntime *runtime);

// A machine of its own, in an arena of its own. It takes nothing from the
// build's arena: what a machine is made of goes when the machine goes, and
// what it says goes on saying it, because a diagnostic is written where the
// build's are. See D574.
// `walked` is what a walk of the whole program said, which the build works out
// once and every machine is handed: a machine sizes itself from it and holds
// every call into the host against it, and it is the same answer for every
// machine a build starts. See D607.
KestRuntime *kest_runtime_new(KestModule *stamped, const KestHost *host,
                              KestDiags *diags, const KestLimits *limits,
                              const KestWalk *walked);
bool kest_runtime_free(KestRuntime *runtime);

#endif
