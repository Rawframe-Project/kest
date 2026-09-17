#ifndef KEST_DEBUG_H
#define KEST_DEBUG_H

#include <stdio.h>

#include "build.h"

// A source-level debugger, over two streams. Breakpoints are written into the
// code and taken out again, so a machine nobody is debugging pays nothing at
// all: there is no test in the dispatch loop, because one measured a third of
// the machine. See D991.
//
// The build and the machine are the caller's, already started with whatever
// the caller binds, so a program that prints prints. Answers what the process
// should exit with.
int kest_debug_serve(KestBuild *build, KestRuntime *runtime, const char *entry,
                     FILE *in, FILE *out);

#endif
