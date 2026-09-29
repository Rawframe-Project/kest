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

    // The same frame with a budget on it, which is what a machine running
    // code nobody trusts has: a count of what it may do, spent as it goes.
    kest_fuel_set(runtime, (uint64_t)1 << 62);
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
    kest_fuel_set(runtime, 0);
    said(engine, "budget", (double)middle_of(took, FRAMES) / BODIES, sum);

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

// What a machine holding the program costs before it has done anything: the
// program the build keeps, the machine, and its heap. Sized from the program
// itself (`kest_needs`) it is the same to within a hundred bytes, so the
// machine a host starts without saying is the one weighed.
static void weighed(KestBuild *build, KestRuntime *machine) {
    said("Kest", "memory",
         (double)(kest_build_held(build) + kest_runtime_cost(machine) +
                  kest_heap_used(machine)),
         0.0);
}

// The world in `churn.kest`, held between frames by `kest_held`, and a frame
// of it. With `between`, the host walks the heap after every frame and the
// walk is part of the frame, which is what moving a collection to between
// frames costs; without, the machine walks when it decides to.
typedef struct {
    KestHeld *held;
    bool between;
} Churn;

static double churn_frame(void *context) {
    Churn *churn = context;
    KestValue args[1] = {{.integer = REPLACED}};
    KestValue answer = {0};
    if (!kest_held_call(churn->held, "frame", args, 1, &answer)) {
        refused(kest_held_runtime(churn->held), "frame");
    }
    if (churn->between) {
        kest_collect(kest_held_runtime(churn->held));
    }
    return answer.real;
}

static void churns(const char *library, const char *engine, bool between) {
    KestHost *host = kest_host_new();
    if (host == NULL) {
        exit(1);
    }
    KestHeld *held =
        kest_held_new("bench/hosts/churn.kest", library, host, stderr);
    KestValue many = {.integer = THINGS};
    if (held == NULL || !kest_held_begin(held, "begin", &many, 1)) {
        fprintf(stderr, "the world did not start\n");
        exit(1);
    }
    Churn churn = {held, between};
    churned(engine, churn_frame, &churn);
    kest_held_free(held);
    kest_host_free(host);
}

// A machine of the one build on a thread of its own, with the host's bodies
// it lends each frame.
// Which build: the bodies the other backend wrote are bound to a build rather
// than to a machine, so every machine a build starts after they are bound runs
// them, and the machine and the compiled bodies are two builds of one program.
typedef struct {
    KestBuild *build;
} Scaling;

typedef struct {
    KestRuntime *runtime;
    int32_t stepping;
    Body *world;
} Lane_state;

static KestRuntime *started(KestBuild *build);

static void *lane_make(void *context) {
    Scaling *scaling = context;
    Lane_state *lane = malloc(sizeof *lane);
    lane->runtime = started(scaling->build);
    lane->stepping = kest_entry(lane->runtime, "bench.hosts.bodies.step");
    lane->world = malloc(sizeof *lane->world * BODIES);
    fill(lane->world, BODIES);
    return lane;
}

static double lane_frame(void *state) {
    Lane_state *lane = state;
    KestValue lent = kest_borrow(lane->runtime, lane->world, BODIES,
                                 "bench.hosts.bodies.Body",
                                 sizeof *lane->world);
    KestValue slots[2] = {{0}};
    slots[0] = lent;
    slots[1].real = WALL;
    bool went = kest_call(lane->runtime, lane->stepping, slots, 2);
    kest_lend_ends(lane->runtime, lent);
    if (!went) {
        refused(lane->runtime, "step");
    }
    return slots[0].real;
}

static void lane_drop(void *state) {
    Lane_state *lane = state;
    kest_runtime_free(lane->runtime);
    free(lane->world);
    free(lane);
}

// A machine running `spin`, stopped from another thread with `kest_cancel`,
// and given its budget back with `kest_fuel_set` to be run again.
typedef struct {
    KestRuntime *runtime;
    int32_t spinning;
} Spinning;

static void *spin_make(void *context) {
    Scaling *scaling = context;
    Spinning *spinning = malloc(sizeof *spinning);
    spinning->runtime = started(scaling->build);
    spinning->spinning =
        kest_entry(spinning->runtime, "bench.hosts.bodies.spin");
    return spinning;
}

static void spin_run(void *state) {
    Spinning *spinning = state;
    KestValue slots[1] = {{0}};
    if (kest_call(spinning->runtime, spinning->spinning, slots, 1)) {
        fprintf(stderr, "spin came back without being stopped\n");
        exit(1);
    }
}

static void spin_ask(void *state) {
    kest_cancel(((Spinning *)state)->runtime);
}

static void spin_again(void *state) {
    kest_fuel_set(((Spinning *)state)->runtime, 0);
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

static void quiet(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)frame;
    (void)runtime;
    (void)context;
}

// The same game in each language made ready to run from its source again,
// which is what a reload is before anything is carried over: here the Tetris
// clone built with the library it imports and a machine started on it.
static void reloads(const char *library) {
    long long took[RELOADS];
    for (int r = -3; r < RELOADS; r++) {
        long long before = now_ns();
        KestBuild *build = kest_build("examples/tetromino.kest", library,
                                      stderr, KEST_FORM_TEXT, 0);
        KestHost *host = kest_host_new();
        if (build == NULL || host == NULL ||
            !kest_host_bind(host, "Io.write", quiet, NULL)) {
            fprintf(stderr, "the game did not build\n");
            exit(1);
        }
        KestRuntime *runtime = kest_start(build, host, NULL);
        kest_host_free(host);
        if (runtime == NULL) {
            kest_build_report(build, stderr, KEST_FORM_TEXT);
            exit(1);
        }
        kest_runtime_free(runtime);
        kest_build_free(build);
        if (r >= 0) {
            took[r] = now_ns() - before;
        }
    }
    said("Kest", "reload", (double)middle_of(took, RELOADS), 0.0);
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
    weighed(build, machine);
    measured(machine, "Kest");
    KestBuild *native = kest_build("bench/hosts/bodies.kest", library, stderr,
                                   KEST_FORM_TEXT, 0);
    if (native == NULL) {
        return 2;
    }
    KestRuntime *compiled = started(native);
    if (!kest_natives_here(compiled)) {
        fprintf(stderr, "the C this was linked with is another program's\n");
        return 2;
    }
    measured(compiled, "Kest, compiled");
    Scaled floor = {floor_make, floor_frame, floor_drop, NULL};
    scaled("C", &floor);
    Scaling machines = {build};
    Scaled by_machine = {lane_make, lane_frame, lane_drop, &machines};
    scaled("Kest", &by_machine);
    Scaling natives = {native};
    Scaled by_natives = {lane_make, lane_frame, lane_drop, &natives};
    scaled("Kest, compiled", &by_natives);
    Stoppable by_machine_stop = {spin_make, spin_run, spin_ask, spin_again,
                                 &machines};
    stopped("Kest", &by_machine_stop);
    Stoppable by_natives_stop = {spin_make, spin_run, spin_ask, spin_again,
                                 &natives};
    stopped("Kest, compiled", &by_natives_stop);
    reloads(library);
    churns(library, "Kest", false);
    churns(library, "Kest, walked between frames", true);
    kest_runtime_free(compiled);
    kest_runtime_free(machine);
    kest_build_free(native);
    kest_build_free(build);
    return 0;
}
