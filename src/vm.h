#ifndef KEST_VM_H
#define KEST_VM_H

#include "value.h"

// Runs the module's `main`. A runtime failure is reported into diags in the
// same shape a compile failure is, so a caller renders both the same way and
// `--errors=json` covers both.
//
// `exit_code` is what `main` returned when it returns an integer, and zero
// when it returns nothing.
bool kest_vm_run(KestArena *arena, const KestModule *module,
                 const char *entry, KestDiags *diags, int64_t *exit_code);

#endif
