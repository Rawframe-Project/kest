#ifndef KEST_COMPILE_H
#define KEST_COMPILE_H

#include "value.h"

// Emits bytecode for every function with a body. Reports what it cannot emit
// rather than emitting something that does not mean the same thing.
bool kest_compile(KestProgram *program, const KestUnit *unit,
                  KestModule *module);

#endif
