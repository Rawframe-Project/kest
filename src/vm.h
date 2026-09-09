#ifndef KEST_VM_H
#define KEST_VM_H

#include "value.h"


// What this machine has said. A host reads it with `kest_report`; the command
// line wants the set rather than a rendering, because it sorts what running
// found together with what compiling did.
KestDiags *kest_runtime_said(KestRuntime *runtime);

KestRuntime *kest_runtime_new(KestArena *arena, KestModule *stamped,
                              const KestHost *host, KestDiags *diags,
                              const KestLimits *limits);
void kest_runtime_free(KestRuntime *runtime);

#endif
