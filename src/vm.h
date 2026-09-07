#ifndef KEST_VM_H
#define KEST_VM_H

#include "value.h"

// Runs the module's `main`. A runtime failure is reported into diags in the
// same shape a compile failure is, so a caller renders both the same way and
// `--json` covers both.
//
// `exit_code` is what `main` returned when it returns an integer, and zero
// when it returns nothing.
bool kest_vm_run(KestArena *arena, const KestModule *module,
                 const char *entry, const KestHost *host, KestDiags *diags,
                 int64_t *exit_code);

// A machine that outlives one call, so the host can call in more than once.
// `limits` may be NULL, which is a host with no opinion about how much the
// machine may use.
KestRuntime *kest_runtime_new(KestArena *arena, const KestModule *module,
                              const KestHost *host, KestDiags *diags,
                              const KestLimits *limits);
void kest_runtime_free(KestRuntime *runtime);

#endif
