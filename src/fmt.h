#ifndef KEST_FMT_H
#define KEST_FMT_H

#include "parser.h"

// Writes the file back in the one form the language has. Printing from the
// tree rather than from the tokens is what lets `ref<Npc>` and `a < b` be
// told apart, which no amount of looking at the characters can do.
//
// Comments are kept, at the indent of what they precede. What is inside a
// string is left exactly as written, except for the expressions in its holes,
// which are code and are written back in the one form like any other code:
// `"{ a  +  1 }"` comes back as `"{a + 1}"`. That is why a comment may not be
// written inside a hole -- there is nothing to write it back into -- and the
// lexer refuses one.
// Returns the file as it should be written, in arena memory. Returning it
// rather than writing it is what lets a caller compare it with what is there
// and leave a file alone that is already right.
const char *kest_format(const KestUnit *unit, const KestSource *source,
                        KestArena *arena, size_t *length);

typedef enum {
    KEST_WORDS_SAME,
    KEST_WORDS_CHANGED,
    KEST_WORDS_NO_ROOM,
} KestWords;

// Whether what the formatter wrote holds the words the file held: every token
// in the same order with the same text, and every comment. What the formatter
// may change is left out -- where lines break, the brackets it takes away
// because the tree does not need them, and the spaces inside a string's holes
// -- and nothing else. Asked before a file is written over, because a slip in
// what a file is read as, which a read-back of the formatted text cannot see,
// is otherwise a person's code deleted without a word: a nought byte ended a
// file where it stood, and `fmt -w` wrote it back without the rest.
KestWords kest_format_same_words(KestArena *arena, const KestSource *before,
                                 const KestSource *after);

#endif
