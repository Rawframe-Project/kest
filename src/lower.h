#ifndef KEST_LOWER_H
#define KEST_LOWER_H

#include "ir.h"

// Writes the stack machine's bytecode for every body. It is one of two readers
// of the same bodies and decides nothing about what a program means: what it
// decides is which instruction, how wide a jump is, and which pairs of
// instructions are worth writing as one.
bool kest_lower(KestProgram *program, KestModule *module,
                const KestIrProgram *ir);

#endif
