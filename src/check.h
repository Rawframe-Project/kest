#ifndef KEST_CHECK_H
#define KEST_CHECK_H

#include "types.h"

// Checks every function body against the declarations `types` resolved:
// names, operators, calls, field access, assignment and returns. Errors are
// reported and recovered from, so one run reports the whole file.
bool kest_check_bodies(KestProgram *program, const KestUnit *unit);

#endif
