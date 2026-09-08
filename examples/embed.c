// A host that is not the command line. It compiles a file, makes a machine,
// and keeps a world between frames by holding the handle the program gave it.
//
//   make embed && ./examples/embed
#include <stdint.h>
#include <stdio.h>

#include "kest.h"

// The same shape `embed.kest` declares as an enum. A four byte tag at nought
// and the payload after it at its own alignment is what a C tagged union is
// and what D026 says an enum is, so this array is the array Kest walks: the
// host lends it and nothing is copied at the boundary.
enum { EVENT_IDLE, EVENT_MOVED, EVENT_HIT, EVENT_NAMED };

// The host's own type with an array in it. `float at[3]` is twelve bytes
// where it stands, and `struct Point { at: [f32; 3] }` beside it is the same
// twelve: that is what D064 is for.
typedef struct {
    float at[3];
} Point;

typedef struct {
    int32_t tag;
    union {
        struct {
            float x;
            float y;
        } moved;
        int32_t hit;
        const char *named;
    } as;
} Event;

// The context is whatever was given at binding, which is how a host reaches
// its own state from inside a function the program calls.
static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    fputs(frame[0].text, (FILE *)context);
}

// The engine's own policy, which asks the program. Calling in from inside a
// call the program made is what an engine does when its rules live on both
// sides, and the machine puts what this starts above what is already running.
static void engine_decide(KestValue *frame, KestRuntime *runtime,
                          void *context) {
    KestValue asked[2] = {{0}};
    asked[0] = frame[0];
    if (kest_call(runtime, *(const int32_t *)context, asked, 2)) {
        frame[0] = asked[0];
    } else {
        frame[0].integer = 1;
    }
}

int main(int argc, char **argv) {
    // NULL for the library, which is the compiler finding its own: what
    // `KEST_LIB` says, or where it was installed.
    const char *path = argc > 1 ? argv[1] : "examples/embed.kest";
    KestBuild *build = kest_build(path, NULL, stderr);
    if (build == NULL) {
        return 1;
    }

    KestHost *host = kest_host_new();
    static int32_t rule;
    if (host == NULL || !kest_host_bind(host, "Io.write", io_write, stdout) ||
        !kest_host_bind(host, "Engine.decide", engine_decide, &rule)) {
        return 1;
    }

    // What the program needs, rather than a number this host guessed. A
    // program that can reach itself has no answer, and then a guess is all
    // there is.
    KestLimits limits = {0, 0};
    if (kest_needs(build, &limits)) {
        // What the program needs for one call in. This host calls back in
        // from inside one, so it asks for room for another on top: what the
        // program says covers the call it makes, and the one made from inside
        // it is this host's to account for.
        printf("the program needs %u slots and %u frames\n",
               limits.stack_slots, limits.call_depth);
        limits.stack_slots *= 2;
        limits.call_depth *= 2;
    } else {
        printf("the program has no deepest call; giving it room\n");
        limits.stack_slots = 4096;
        limits.call_depth = 64;
    }
    KestRuntime *runtime = kest_start(build, host, &limits);
    if (runtime == NULL) {
        return 1;
    }

    // The arguments go where the result comes back, so a frame has to be
    // wide enough for whichever is wider. The program says which, rather than
    // this host guessing and being told at the first call that is too narrow.
    KestValue frame[4] = {{0}};
    const char *wanted[] = {"create", "spawn", "step", "onEvents", "silence",
                            "spread"};
    rule = kest_entry(runtime, "rule");
    int32_t entry[sizeof(wanted) / sizeof(wanted[0])];
    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
        // Found once, at the start. What a name means is a search over
        // everything the program defines, and a frame should not do one. The
        // name is the one the file writes; that it registered them under
        // `embed` is not this host's business.
        entry[i] = kest_entry(runtime, wanted[i]);
        if (entry[i] < 0 ||
            kest_frame_slots(runtime, entry[i]) >
                sizeof(frame) / sizeof(frame[0])) {
            fprintf(stderr, "`%s` is not there or needs more than %zu slots\n",
                    wanted[i], sizeof(frame) / sizeof(frame[0]));
            return 1;
        }
    }
    enum { CREATE, SPAWN, STEP, ON_EVENTS, SILENCE, SPREAD };
    if (!kest_call(runtime, entry[CREATE], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        return 1;
    }
    KestValue world = frame[0];

    for (int i = 0; i < 5; i++) {
        frame[0] = world;
        frame[1].integer = i + 1;
        if (!kest_call(runtime, entry[SPAWN], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
            return 1;
        }
        printf("frame %d: spawned, %lld alive\n", i, (long long)frame[0].integer);
    }

    for (int i = 0; i < 5; i++) {
        frame[0] = world;
        if (!kest_call(runtime, entry[STEP], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
            return 1;
        }
        printf("frame %d: stepped, %lld alive, %zu bytes\n", i + 5,
               (long long)frame[0].integer, kest_heap_used(runtime));
    }

    // A struct of the host's with an array inside it, lent by name. A run on
    // its own has no name to lend against, which is what `Point` is for.
    Point corners[4];
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 3; k++) {
            corners[i].at[k] = (float)(i * 3 + k);
        }
    }
    frame[0] = kest_borrow(runtime, corners, 4, "Point", sizeof(Point));
    if (frame[0].object == NULL) {
        kest_report(runtime, stderr);
        return 1;
    }
    if (!kest_call(runtime, entry[SPREAD], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        kest_report(runtime, stderr);
        return 1;
    }
    printf("host lent %zu byte points: %g across\n", sizeof(Point),
           (double)frame[0].real);

    // A batch the host owns, walked in place. D007 measured the inward
    // crossing as the wider of the two, so one call carries the whole batch
    // rather than one call per event.
    Event events[4];
    events[0].tag = EVENT_HIT;
    events[0].as.hit = 4;
    events[1].tag = EVENT_MOVED;
    events[1].as.moved.x = 1.5f;
    events[1].as.moved.y = 2.5f;
    events[2].tag = EVENT_IDLE;
    events[3].tag = EVENT_NAMED;
    events[3].as.named = "trap";

    // The stride is the program's own, so what this host has to get right is
    // only that its `Event` is the program's `Event`. Saying `sizeof` is what
    // makes a disagreement a message rather than a wrong read.
    frame[0] = kest_borrow(runtime, events, 4, "Event", sizeof(Event));
    if (frame[0].object == NULL) {
        kest_report(runtime, stderr);
        return 1;
    }
    if (!kest_call(runtime, entry[ON_EVENTS], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        kest_report(runtime, stderr);
        return 1;
    }
    printf("host lent %zu byte events: %lld damage\n", sizeof(Event),
           (long long)frame[0].integer);

    // And back the other way: what the program writes is what the host reads,
    // because there is one copy of it.
    KestValue lent = kest_borrow(runtime, events, 4, "Event", sizeof(Event));
    frame[0] = lent;
    frame[1].integer = 0;
    if (!kest_call(runtime, entry[SILENCE], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        return 1;
    }
    printf("silenced the first: tag is now %d\n", events[0].tag);

    frame[0] = lent;
    if (!kest_call(runtime, entry[ON_EVENTS], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        return 1;
    }
    printf("host reads it back: %lld damage\n", (long long)frame[0].integer);

    kest_runtime_free(runtime);
    kest_host_free(host);
    kest_build_free(build);
    return 0;
}
