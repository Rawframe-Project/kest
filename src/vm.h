#ifndef KEST_VM_H
#define KEST_VM_H

#include "value.h"

// A machine that outlives one call, so the host can call in more than once.
// `limits` may be NULL, which is a host with no opinion about how much the
// machine may use.
// What this machine has said. A host reads it with `kest_report`; the command
// line wants the set rather than a rendering, because it sorts what running
// found together with what compiling did.
KestDiags *kest_runtime_said(KestRuntime *runtime);

KestRuntime *kest_runtime_new(KestArena *arena, const KestModule *module,
                              const KestHost *host, KestDiags *diags,
                              const KestLimits *limits);
void kest_runtime_free(KestRuntime *runtime);

#endif
