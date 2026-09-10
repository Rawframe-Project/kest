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
KestRuntime *kest_runtime_new(KestModule *stamped, const KestHost *host,
                              KestDiags *diags, const KestLimits *limits);
bool kest_runtime_free(KestRuntime *runtime);

#endif
