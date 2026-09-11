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

// What a machine said, in this host's own words rather than on this host's
// terminal. `kest_report` writes to a `FILE *`, so a host that wants the words
// renders into a file of its own and reads them back: `tmpfile` is what C
// gives every host, and a host that has somewhere better puts them there.
//
// What it costs is one file a report and a copy through the C library. A host
// in a frame loop that only wants to know whether anything went wrong does not
// need this at all — every call answers false when it was refused — and this is
// for the times it wants to say why. See D632.
static void say_what_happened(KestRuntime *runtime, const char *about) {
    FILE *words = tmpfile();
    if (words == NULL) {
        return;
    }
    kest_report(runtime, words, KEST_FORM_TEXT);
    rewind(words);
    char line[256];
    while (fgets(line, sizeof(line), words) != NULL) {
        printf("[%s] %s", about, line);
    }
    fclose(words);
}

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
// comparisons, and what is in the row is what the program is asked about before
// anything is bound.
//
// What each argument is made of is in the row as well, a kind a slot. Two
// numbers are not enough: `Host.write(value: i32)` takes one and gives nothing,
// the same as the one this host wrote, and the function under it would read a
// number as a pointer. A host whose arguments are its own structs has a run of
// kinds a row rather than one, and `examples/embed.c` is that host.
typedef struct {
    const char *name;
    KestNative function;
    const uint8_t *kinds;
    uint32_t slots;
    bool gives;
} Provided;

static const uint8_t one_piece_of_text[] = {KEST_L_WORD};

static const Provided provided[] = {
    {"Host.write", write_it, one_piece_of_text, 1, false},
    {NULL, NULL, NULL, 0, false},
};

// Whether what the program says crosses at this name is what the host wrote
// down: as many arguments, as many slots in them, each slot the kind the row
// says, and an answer where the row expects one. Asked of the build, because a
// host binds before there is a machine. See D626.
static bool crosses_as_written(const KestBuild *build, uint32_t at,
                               const Provided *ours) {
    if ((kest_extern_gives(build, at) != NULL) != ours->gives) {
        return false;
    }
    uint32_t slots = 0;
    for (uint32_t which = 0; which < kest_extern_takes(build, at); which++) {
        const KestLayout *layout = kest_extern_layout(build, at, which);
        if (layout == NULL) {
            return false;
        }
        for (uint16_t piece = 0; piece < layout->count; piece++) {
            if (slots >= ours->slots ||
                layout->pieces[piece].kind != ours->kinds[slots]) {
                return false;
            }
            slots++;
        }
    }
    return slots == ours->slots;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "examples/least.kest";

    // Compiling says what is wrong with the program where it is wrong, into
    // the file this host hands it. Nothing is a machine yet.
    KestBuild *build = kest_build(path, NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 1;
    }

    // And what compiling had to say about a program it compiled: a shape
    // nothing names, a declaration nothing calls. `kest_build` writes what
    // stopped it and keeps the rest, so a host that never asks is a host that
    // drops every warning its programs have. See D631.
    kest_build_report(build, stderr, KEST_FORM_TEXT);

    // What the program asks this host for, by name, before there is a machine
    // to refuse one. A host that binds what it is asked for rather than what
    // it remembers is a host that keeps working when the program changes.
    //
    // A program that asks for nothing needs none of this: `kest_start` takes
    // NULL where there is no host, and everything below the loop is then a
    // host this program would never have called. That is the smallest a host
    // gets — a build, a call and what came back. See D627.
    KestHost *host = kest_build_extern(build, 0) == NULL ? NULL
                                                         : kest_host_new();
    if (host == NULL && kest_build_extern(build, 0) != NULL) {
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
        if (ours == NULL || !crosses_as_written(build, at, ours)) {
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
    // Freeing nothing is not a refusal, the same as freeing no machine, so a
    // host that has none says nothing special here.
    kest_host_free(host);
    if (runtime == NULL) {
        // No machine, so there is nothing to ask why: what a host has then is
        // the build, and it has been told which names went unbound. A line of
        // this host's own under it, because a report that says nothing is a
        // program nobody can tell from a host that stopped for its own
        // reasons.
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "no machine for `%s`\n", path);
        kest_build_free(build);
        return 1;
    }

    // A name the program does not define is -1, and asking is free. What
    // comes back from the call is written over the frame it was handed.
    const char *called = argc > 2 ? argv[2] : "main";
    int32_t entry = kest_entry(runtime, called);
    KestValue frame[8] = {{0}};
    // And what to call it with, where somebody said so. Words are what a
    // command line has and what a host reading a line of configuration has;
    // the machine lays each one out as the type the declaration says, so there
    // are no slots to be wrong about. A host holding values of its own writes
    // them into the frame itself and says what it wrote with
    // `kest_frame_fills`, which is `examples/embed.c`. See D629.
    uint32_t given = argc > 3 ? (uint32_t)(argc - 3) : 0;
    if (given > 0 &&
        !kest_takes_text(runtime, entry, frame,
                         sizeof(frame) / sizeof(frame[0]),
                         (const char *const *)&argv[3], given)) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        kest_runtime_free(runtime);
        kest_build_free(build);
        return 1;
    }
    if (entry < 0 || !kest_call(runtime, entry, frame,
                                sizeof(frame) / sizeof(frame[0]))) {
        say_what_happened(runtime, called);
        kest_runtime_free(runtime);
        kest_build_free(build);
        return 1;
    }

    // And what came back, written the way the language writes it: a number, a
    // `bool`, a case of an enum, or text as itself. One call for all of them,
    // so a host reading an answer that is not a number writes no more than one
    // reading a number. The length is what it needed rather than what fitted,
    // the way `snprintf` answers.
    //
    // Nought less than nothing is a struct, a run, a store or a reference:
    // what a program means by one of those is the host's to decide, and
    // `examples/embed.c` is the host that decides it. See D628.
    char said[128];
    int64_t room = kest_gave_text(runtime, entry, frame, said, sizeof(said));
    if (room < 0) {
        kest_report(runtime, stdout, KEST_FORM_TEXT);
        printf("`%s` gave back something this host does not write\n", called);
    } else if ((size_t)room >= sizeof(said)) {
        printf("`%s` gave back %lld bytes and this host has room for %zu\n",
               called, (long long)room, sizeof(said));
    } else {
        printf("`%s` gave back %s\n", called, said);
    }

    // The machine goes first and the build after it: a build under a machine
    // is the program that machine is running, and this says so rather than
    // leaving a host to find out.
    if (!kest_runtime_free(runtime) || !kest_build_free(build)) {
        fprintf(stderr, "something was still standing on something else\n");
        return 1;
    }
    return 0;
}
