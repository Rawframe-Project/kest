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

// What a machine or a build said, in this host's own words rather than on this
// host's terminal. One place for both, because they are one thing: a report
// goes where the host says, and this host says the same thing about either.
//
// `kest_report` writes to a `FILE *`, so a host that wants the words renders
// into a file of its own and reads them back; `tmpfile` is what C gives every
// host, and a host with somewhere better puts them there. What it costs is one
// file — this one, for the life of the host — and a copy through the C library.
// A host that only wants to know whether something went wrong needs none of it:
// every call answers false when it was refused. See D632.
static void say_what_happened(KestRuntime *runtime, KestBuild *build,
                              const char *about) {
    // One file, wound back and written over. A host that made a new one every
    // time it asked would make one a frame, and what is after this report is
    // the last one — so it is read to where this one ended and no further.
    // See D633.
    static FILE *words = NULL;
    if (words == NULL) {
        words = tmpfile();
        if (words == NULL) {
            return;
        }
    }
    rewind(words);
    if (runtime != NULL) {
        kest_report(runtime, words, KEST_FORM_TEXT);
    } else {
        kest_build_report(build, words, KEST_FORM_TEXT);
    }
    long end = ftell(words);
    rewind(words);
    // A line at a time, which is how a host reads a report longer than
    // anything it wants to hold: the file has the whole of it and this holds
    // one line of it. See D635.
    char line[256];
    while (end > 0 && fgets(line, sizeof(line), words) != NULL) {
        printf("[%s] %s", about, line);
        end -= (long)strlen(line);
    }
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

static const uint8_t one_piece_of_text[] = {KEST_L_TEXT};

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
    KestBuild *build = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 1;
    }

    // And what compiling had to say about a program it compiled: a shape
    // nothing names, a declaration nothing calls. `kest_build` writes what
    // stopped it and keeps the rest, so a host that never asks is a host that
    // drops every warning its programs have. See D631.
    say_what_happened(NULL, build, path);

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
    // The name this host is about to call, read before the machine is made
    // because what it is about to call is what the machine is sized for.
    const char *called = argc > 2 ? argv[2] : "main";
    KestLimits limits = {0, 0, 0};
    KestReason why = {KEST_REACH_UNASKED, NULL};
    bool measured = kest_needs(build, &limits, &why);
    limits.heap_bytes = 1024 * 1024;

    // And what a program with no answer costs a host that names a depth. There
    // is no worst chain to add up, but a frame is at most the widest body the
    // program has and a host says how many frames there are — so the slots are
    // that many a frame, and twice the frames is twice the slots. Asked here
    // because this is the host with nothing else in it: two machines, no
    // program run in them, and the numbers read back out. See D815.
    if (!measured) {
        KestLimits few = {0, 16, 0};
        KestLimits many = {0, 64, 0};
        KestRuntime *shallow = kest_start(build, host, &few);
        KestRuntime *deeper = kest_start(build, host, &many);
        KestLimits had_few = {0, 0, 0};
        KestLimits had_many = {0, 0, 0};
        kest_allowed(shallow, &had_few);
        kest_allowed(deeper, &had_many);
        // A frame costs the same wherever it is, so the two answers differ by
        // the frames between them and what is left over is the same in each:
        // that fixed part is the bodies the loop does not go round, which
        // stand in a chain once and do not repeat. Held as the relationship
        // and not as either number. See D816.
        uint32_t more = had_many.stack_slots - had_few.stack_slots;
        uint32_t a_turn = more / (64 - 16);
        if (shallow == NULL || deeper == NULL || had_few.stack_slots == 0 ||
            had_many.stack_slots <= had_few.stack_slots || a_turn == 0 ||
            more != a_turn * (64 - 16) ||
            had_few.stack_slots - a_turn * 16 !=
                had_many.stack_slots - a_turn * 64 ||
            had_few.stack_slots <= a_turn * 16 ||
            had_many.stack_slots >= KEST_STACK_SLOTS) {
            fprintf(stderr,
                    "`%s` has no least, and 16 frames of it is %u slots "
                    "against %u for 64\n",
                    path, had_few.stack_slots, had_many.stack_slots);
            kest_runtime_free(shallow);
            kest_runtime_free(deeper);
            kest_host_free(host);
            kest_build_free(build);
            return 1;
        }
        printf("`%s` has no least, and is %u slots for 16 frames and %u for "
               "64 — %u a frame and %u whatever the frames\n",
               path, had_few.stack_slots, had_many.stack_slots, a_turn,
               had_few.stack_slots - a_turn * 16);
        // The number a host can read before it makes anything, held against
        // the machine it would get: `kest_bound` is what a machine given
        // nothing is sized by, said early. A number a host reads that is not
        // the machine it is handed is a number it cannot budget with. See
        // D823.
        KestLimits told_first = {0, 0, 0};
        KestReason told_why = {KEST_REACH_UNASKED, NULL};
        if (!kest_bound(build, 16, &told_first, &told_why) ||
            told_why.reach == KEST_REACH_KNOWN ||
            told_first.stack_slots != had_few.stack_slots ||
            told_first.call_depth != had_few.call_depth) {
            fprintf(stderr,
                    "`%s` is bounded at %u slots and %u frames and a machine "
                    "of 16 frames got %u and %u\n",
                    path, told_first.stack_slots, told_first.call_depth,
                    had_few.stack_slots, had_few.call_depth);
            kest_runtime_free(shallow);
            kest_runtime_free(deeper);
            kest_host_free(host);
            kest_build_free(build);
            return 1;
        }

        // And the same question about one name rather than the whole program.
        // A host that calls one function is not calling the whole of what the
        // file defines, and what that one reaches is what it can want. This
        // program has two functions `main` never calls, so its own bound is
        // under the one the whole file gets — held as less and not as no
        // more, because a bound that counted everything would be equal and
        // read as right. See D817.
        KestLimits about_main = {0, 0, 0};
        KestReason bounded = {KEST_REACH_UNASKED, NULL};
        if (!kest_bound_of(build, "main", 16, &about_main, &bounded) ||
            about_main.call_depth != 16 ||
            about_main.stack_slots == 0 ||
            about_main.stack_slots >= had_few.stack_slots ||
            bounded.reach == KEST_REACH_KNOWN) {
            fprintf(stderr,
                    "`main` of `%s` is bounded at %u slots and %u frames "
                    "where the whole program is %u\n",
                    path, about_main.stack_slots, about_main.call_depth,
                    had_few.stack_slots);
            kest_runtime_free(shallow);
            kest_runtime_free(deeper);
            kest_host_free(host);
            kest_build_free(build);
            return 1;
        }
        // And where this host could be called back in from, which is the
        // third of the three doors and the one a re-entrant host reads. Only
        // the bodies that reach a host function count towards it, and this
        // program has bodies that do not, so it is under what `main` wants
        // altogether. See D818.
        KestLimits back_in = {0, 0, 0};
        KestReason from_where = {KEST_REACH_UNASKED, NULL};
        if (!kest_bound_from(build, "main", 16, &back_in, &from_where) ||
            back_in.stack_slots == 0 ||
            back_in.stack_slots >= about_main.stack_slots ||
            back_in.call_depth != 16 ||
            from_where.reach == KEST_REACH_KNOWN) {
            fprintf(stderr,
                    "a call back into `%s` starts at %u slots where `main` "
                    "wants %u\n",
                    path, back_in.stack_slots, about_main.stack_slots);
            kest_runtime_free(shallow);
            kest_runtime_free(deeper);
            kest_host_free(host);
            kest_build_free(build);
            return 1;
        }
        printf("and `main` on its own is %u of them, %u of which is where "
               "this host could be called back in from\n",
               about_main.stack_slots, back_in.stack_slots);
        kest_runtime_free(shallow);
        kest_runtime_free(deeper);
    }

    // And a machine made from the bound rather than from the usual numbers. A
    // bound is a number a host can read; this is a host reading one, writing
    // it down and running the program in what it asked for. Sixteen frames is
    // this host's own choice and the slots follow from it — a program that
    // goes deeper than that is refused at the call that would, and told what
    // it wanted, which is the whole of what a bound promises. See D819.
    KestLimits asked_for = {0, 0, 0};
    if (!measured && kest_bound_of(build, called, 16, &asked_for, NULL)) {
        asked_for.heap_bytes = 1024 * 1024;
        // What came back may be the least for that one name rather than a
        // bound — `motto` reaches nothing that comes back round — and then
        // the frames are its own and not the sixteen this host allowed.
        printf("and this host allows 16 frames, so it takes %u slots and %u "
               "frames\n",
               asked_for.stack_slots, asked_for.call_depth);
    }
    KestRuntime *runtime = kest_start(
        build, host,
        measured ? &limits : (asked_for.stack_slots > 0 ? &asked_for : NULL));
    // Freeing nothing is not a refusal, the same as freeing no machine, so a
    // host that has none says nothing special here.
    kest_host_free(host);
    if (runtime == NULL) {
        // No machine, so there is nothing to ask why: what a host has then is
        // the build, and it has been told which names went unbound. A line of
        // this host's own under it, because a report that says nothing is a
        // program nobody can tell from a host that stopped for its own
        // reasons.
        say_what_happened(NULL, build, path);
        fprintf(stderr, "no machine for `%s`\n", path);
        kest_build_free(build);
        return 1;
    }

    // A name the program does not define is -1, and asking is free. What
    // comes back from the call is written over the frame it was handed.
    // And that the machine is the one that was asked for. A bound a host reads
    // and writes down is only worth reading if the machine it makes is the
    // one it wrote: this is the same two numbers, read back off the machine
    // the program is about to run in. See D819.
    if (asked_for.stack_slots > 0) {
        KestLimits was_given = {0, 0, 0};
        kest_allowed(runtime, &was_given);
        // The heap beside them, which is the one of the three a machine has
        // no number of its own for: what comes back is the ceiling this host
        // wrote or nought where it wrote none, and nought is what no ceiling
        // is. Read here because this host wrote one. See D830.
        if (was_given.stack_slots != asked_for.stack_slots ||
            was_given.call_depth != asked_for.call_depth ||
            was_given.heap_bytes != asked_for.heap_bytes ||
            was_given.stack_slots >= KEST_STACK_SLOTS) {
            fprintf(stderr,
                    "a machine asked for %u slots, %u frames and %zu bytes "
                    "was given %u, %u and %zu\n",
                    asked_for.stack_slots, asked_for.call_depth,
                    asked_for.heap_bytes, was_given.stack_slots,
                    was_given.call_depth, was_given.heap_bytes);
            kest_runtime_free(runtime);
            kest_build_free(build);
            return 1;
        }
    }

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
        say_what_happened(runtime, NULL, called);
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
