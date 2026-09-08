#ifndef KEST_VM_H
#define KEST_VM_H

#include "value.h"

// A machine that outlives one call, so the host can call in more than once.
// `limits` may be NULL, which is a host with no opinion about how much the
// machine may use.
// Writes a value the way a program writes one, in the manner of `snprintf`:
// the length it needed comes back whether or not it fitted, and the caller
// terminates what it asked for. This is what a hole in a string is filled
// with, so a value printed anywhere else reads the same as one printed there.
size_t kest_write_value(char *out, size_t room, const KestType *type,
                        const KestValue *slots);

// What this machine has said. A host reads it with `kest_report`; the command
// line wants the set rather than a rendering, because it sorts what running
// found together with what compiling did.
KestDiags *kest_runtime_said(KestRuntime *runtime);

KestRuntime *kest_runtime_new(KestArena *arena, const KestModule *module,
                              const KestHost *host, KestDiags *diags,
                              const KestLimits *limits);
void kest_runtime_free(KestRuntime *runtime);

#endif
