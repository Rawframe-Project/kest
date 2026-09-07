#ifndef KEST_FMT_H
#define KEST_FMT_H

#include "parser.h"

// Writes the file back in the one form the language has. Printing from the
// tree rather than from the tokens is what lets `ref<Npc>` and `a < b` be
// told apart, which no amount of looking at the characters can do.
//
// Comments are kept, at the indent of what they precede. What is inside a
// string, including the expressions in its holes, is left exactly as written.
// Returns the file as it should be written, in arena memory. Returning it
// rather than writing it is what lets a caller compare it with what is there
// and leave a file alone that is already right.
const char *kest_format(const KestUnit *unit, const KestSource *source,
                        KestArena *arena, size_t *length);

#endif
