// `bench/hosts` through Kest's own embedding API, twice: by the machine, and
// by the bodies the other backend wrote for the same program, bound to a
// second machine of the same build. The frame lends the host's run where it
// stands, a crossing a body hands four numbers and the wall over and reads the
// body back, and the program asks its host through `Host.add`. The floor, the
// same frame in C, is said by this host too. See D1272.
//
//   kest [lib/]
#include <stdbool.h>

#include "common.h"
#include "kest.h"

bool kest_natives_here(KestRuntime *runtime);

static void add(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = frame[0].integer + frame[1].integer;
}

static void refused(KestRuntime *runtime, const char *what) {
    fprintf(stderr, "%s:\n", what);
    kest_report(runtime, stderr, KEST_FORM_TEXT);
    exit(1);
}

static void measured(KestRuntime *runtime, const char *engine) {
    int32_t stepping = kest_entry(runtime, "bench.hosts.bodies.step");
    int32_t once = kest_entry(runtime, "bench.hosts.bodies.one");
    int32_t asking = kest_entry(runtime, "bench.hosts.bodies.asks");
    if (stepping < 0 || once < 0 || asking < 0) {
        refused(runtime, "the program has not got `step`, `one` or `asks`");
    }
    Body *world = malloc(sizeof *world * BODIES);
    long long took[FRAMES];
    double sum = 0.0;

    fill(world, BODIES);
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        KestValue lent = kest_borrow(runtime, world, BODIES,
                                     "bench.hosts.bodies.Body", sizeof *world);
        KestValue slots[2] = {{0}};
        slots[0] = lent;
        slots[1].real = WALL;
        bool went = kest_call(runtime, stepping, slots, 2);
        kest_lend_ends(runtime, lent);
        if (!went) {
            refused(runtime, "step");
        }
        sum = slots[0].real;
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    said(engine, "frame", (double)middle_of(took, FRAMES) / BODIES, sum);

    fill(world, BODIES);
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        sum = 0.0;
        for (int i = 0; i < BODIES; i++) {
            KestValue slots[5] = {{0}};
            slots[0].real = world[i].x;
            slots[1].real = world[i].y;
            slots[2].real = world[i].dx;
            slots[3].real = world[i].dy;
            slots[4].real = WALL;
            if (!kest_call(runtime, once, slots, 5)) {
                refused(runtime, "one");
            }
            world[i].x = slots[0].real;
            world[i].y = slots[1].real;
            world[i].dx = slots[2].real;
            world[i].dy = slots[3].real;
            sum += world[i].x + world[i].y;
        }
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    said(engine, "body", (double)middle_of(took, FRAMES) / BODIES, sum);

    long long asked[9];
    double answer = 0.0;
    for (int r = 0; r < 9; r++) {
        long long before = now_ns();
        KestValue slots[1] = {{0}};
        slots[0].integer = ASKS;
        if (!kest_call(runtime, asking, slots, 1)) {
            refused(runtime, "asks");
        }
        answer = (double)slots[0].integer;
        asked[r] = now_ns() - before;
    }
    said(engine, "ask", (double)middle_of(asked, 9) / ASKS, answer);
    free(world);
}

static KestRuntime *started(KestBuild *build) {
    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Host.add", add, NULL)) {
        fprintf(stderr, "the host could not be made\n");
        exit(1);
    }
    KestRuntime *runtime = kest_start(build, host, NULL);
    kest_host_free(host);
    if (runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        exit(1);
    }
    return runtime;
}

int main(int argc, char **argv) {
    const char *library = argc > 1 ? argv[1] : "lib/";
    KestBuild *build = kest_build("bench/hosts/bodies.kest", library, stderr,
                                  KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    floor_frames("C");
    KestRuntime *machine = started(build);
    measured(machine, "Kest");
    KestRuntime *compiled = started(build);
    if (!kest_natives_here(compiled)) {
        fprintf(stderr, "the C this was linked with is another program's\n");
        return 2;
    }
    measured(compiled, "Kest, compiled");
    kest_runtime_free(compiled);
    kest_runtime_free(machine);
    kest_build_free(build);
    return 0;
}
