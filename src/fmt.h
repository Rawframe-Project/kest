#ifndef KEST_FMT_H
#define KEST_FMT_H

#include "parser.h"

// Writes the file back in the one form the language has. Printing from the
// tree rather than from the tokens is what lets `ref<Npc>` and `a < b` be
// told apart, which no amount of looking at the characters can do.
//
// Comments are kept, at the indent of what they precede. What is inside a
// string, including the expressions in its holes, is left exactly as written.
void kest_format(const KestUnit *unit, const KestSource *source, FILE *out);

#endif
