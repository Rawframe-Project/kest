// The smallest host there is: compile a file, bind what the program asks for,
// size a machine from what the program says it needs, call one function and
// hand back everything. It is what a host writer copies before they know what
// else there is, and it is here rather than in the reference because a host in
// a document is a host nobody has run.
//
//   make least && ./examples/least examples/least.kest
//
// `examples/embed.c` is the other kind of host: it crosses every part of this
// boundary and counts the bytes both sides spend, because this project holds
// itself to what it says about them. Nothing here counts anything. See D624.
#include <stdio.h>
#include <string.h>

#include "kest.h"

// What the program asks the host for. A host function reads its arguments out
// of the frame it is handed and writes its answer over them, which is the one
// convention at this boundary.
static void write_it(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    // A slot of text is a pointer the machine owns and terminates, so a host
    // may read it while the call lasts and must copy what it wants to keep.
    // What is in it is the program's, newline and all.
    fputs(frame[0].text, stdout);
}

// What this host provides, beside what each of them takes and whether it gives
// anything back. A host with three of these has three rows rather than three
// comparisons, and what is in the row is what the program is asked about
// before anything is bound.
typedef struct {
    const char *name;
    KestNative function;
    uint32_t takes;
    bool gives;
} Provided;

static const Provided provided[] = {
    {"Host.write", write_it, 1, false},
    {NULL, NULL, 0, false},
};

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "examples/least.kest";

    // Compiling says what is wrong with the program where it is wrong, into
    // the file this host hands it. Nothing is a machine yet.
    KestBuild *build = kest_build(path, NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 1;
    }

    // What the program asks this host for, by name, before there is a machine
    // to refuse one. A host that binds what it is asked for rather than what
    // it remembers is a host that keeps working when the program changes.
    KestHost *host = kest_host_new();
    if (host == NULL) {
        kest_build_free(build);
        return 1;
    }
    for (uint32_t at = 0;; at++) {
        const char *wanted = kest_build_extern(build, at);
        if (wanted == NULL) {
            break;
        }
        // By name, out of a list of what this host has functions for. One name
        // is a comparison and three are a list, and a host that bound whatever
        // it was asked for would hand the machine a function that reads a
        // number as a pointer the first time it is called.
        const Provided *ours = NULL;
        for (uint32_t which = 0; provided[which].name != NULL; which++) {
            if (strcmp(wanted, provided[which].name) == 0) {
                ours = &provided[which];
            }
        }
        // And what the program expects it to take, which the program says and
        // this host wrote down: the two are declared in different files and
        // nothing but this makes them agree. A host that skips it finds out at
        // the first call, in the frame.
        if (ours == NULL || kest_extern_takes(build, at) != ours->takes ||
            (kest_extern_gives(build, at) != NULL) != ours->gives) {
            fprintf(stderr, "this host does not provide `%s`\n", wanted);
            kest_host_free(host);
            kest_build_free(build);
            return 1;
        }
        if (!kest_host_bind(host, wanted, ours->function, NULL)) {
            fprintf(stderr, "`%s` could not be bound\n", wanted);
            kest_host_free(host);
            kest_build_free(build);
            return 1;
        }
    }

    // What the program needs, rather than a number this host guessed. A
    // program that can reach itself has no answer and says so, and then the
    // machine picks for itself — which is what passing nothing gets.
    KestLimits limits = {0, 0, 0};
    KestReason why = {KEST_REACH_UNASKED, NULL};
    bool measured = kest_needs(build, &limits, &why);
    limits.heap_bytes = 1024 * 1024;

    KestRuntime *runtime = kest_start(build, host, measured ? &limits : NULL);
    kest_host_free(host);
    if (runtime == NULL) {
        // No machine, so there is nothing to ask why: what a host has then is
        // the build, and it has been told which names went unbound.
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        kest_build_free(build);
        return 1;
    }

    // A name the program does not define is -1, and asking is free. What
    // comes back from the call is written over the frame it was handed.
    int32_t entry = kest_entry(runtime, "main");
    KestValue frame[8] = {{0}};
    if (entry < 0 || !kest_call(runtime, entry, frame,
                                sizeof(frame) / sizeof(frame[0]))) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        kest_runtime_free(runtime);
        kest_build_free(build);
        return 1;
    }
    printf("`main` gave back %lld\n", (long long)frame[0].integer);

    // The machine goes first and the build after it: a build under a machine
    // is the program that machine is running, and this says so rather than
    // leaving a host to find out.
    if (!kest_runtime_free(runtime) || !kest_build_free(build)) {
        fprintf(stderr, "something was still standing on something else\n");
        return 1;
    }
    return 0;
}
