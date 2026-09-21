/* The host half of the frame workload, and the one with the clock in it.
 *
 * Kest is an embedded language, so what a frame costs is not what the machine
 * costs: it is what the host pays to hand its own memory over, what the
 * program does with it, and what the host pays to read the answer. This host
 * owns the bodies -- a contiguous run of four floats each, the shape an engine
 * actually has -- and drives them three ways over the same data:
 *
 *   lend    one crossing a frame, the host's memory read and written in place
 *   fine    one crossing a body, the four numbers passed and one answered
 *   native  the same arithmetic written here in C, as the floor
 *
 * The three do the same work and answer the same checksum, so the difference
 * between them is the boundary and nothing else. `native` is not a claim about
 * anything: it is what this machine does when nothing crosses at all, which is
 * the number the other two are read against.
 *
 * Every frame is timed on its own and every sample is kept, so what it answers
 * with is the middle, the tails and the worst -- which for a frame budget is
 * the number that matters. See D1004 for why, and D1006 for what it found. */

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

#include "kest.h"

#define WALL 100.0f

static long long in_nanoseconds(void) {
#if defined(_WIN32)
    static LARGE_INTEGER a_second;
    LARGE_INTEGER now;
    if (a_second.QuadPart == 0) {
        QueryPerformanceFrequency(&a_second);
    }
    QueryPerformanceCounter(&now);
    return (long long)((double)now.QuadPart * 1e9 / (double)a_second.QuadPart);
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
#endif
}

/* The host's own memory, in the shape the program's `Body` is laid out in.
   Nothing is marshalled: the run is lent as it stands and the program writes
   through it. */
typedef struct {
    float x;
    float y;
    float dx;
    float dy;
} Body;

static int nearer(const void *left, const void *right) {
    long long a = *(const long long *)left;
    long long b = *(const long long *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static long long at_share(const long long *sorted, long long count,
                          double share) {
    long long at = (long long)(share * (double)(count - 1) + 0.5);
    return sorted[at < 0 ? 0 : at >= count ? count - 1 : at];
}

static void say(const char *what, long long *samples, long long count,
                double answered) {
    qsort(samples, (size_t)count, sizeof *samples, nearer);
    long long middle = at_share(samples, count, 0.5);
    long long *away = malloc(sizeof *away * (size_t)count);
    long long spread = 0;
    if (away != NULL) {
        for (long long i = 0; i < count; i++) {
            long long from = samples[i] - middle;
            away[i] = from < 0 ? -from : from;
        }
        qsort(away, (size_t)count, sizeof *away, nearer);
        spread = at_share(away, count, 0.5);
        free(away);
    }
    printf("%-8s %9.3f %9.3f %9.3f %9.3f %9.3f   %.0f\n", what,
           (double)middle / 1e3, (double)at_share(samples, count, 0.95) / 1e3,
           (double)at_share(samples, count, 0.99) / 1e3,
           (double)samples[count - 1] / 1e3, (double)spread / 1e3, answered);
}

static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(frame, &length);
    if (bytes != NULL && length > 0) {
        fwrite(bytes, 1, length, (FILE *)context);
    }
}

static void fill(Body *world, uint32_t many) {
    for (uint32_t i = 0; i < many; i++) {
        float f = (float)i;
        world[i].x = fmodf(f, 100.0f);
        world[i].y = fmodf(f * 3.0f, 100.0f);
        world[i].dx = 1.0f + fmodf(f, 3.0f);
        world[i].dy = 2.0f;
    }
}

/* One frame, the run lent where it stands, timed and kept. Written once
   because it is run twice: by the machine, and by the same program with the
   bodies the other backend wrote bound to it. What a frame costs is the
   question a game asks, and it has two answers now. See D1123. */
static bool lending(KestRuntime *runtime, int32_t stepping, Body *world,
                    uint32_t many, long long frames, long long *took,
                    const char *what, double *answered) {
    fill(world, many);
    for (long long f = 0; f < frames; f++) {
        long long before = in_nanoseconds();
        KestValue lent = kest_borrow(runtime, world, many, "bench.frame.Body",
                                     sizeof *world);
        KestValue slots[3] = {{0}};
        slots[0] = lent;
        slots[1].real = (double)WALL;
        bool went = kest_call(runtime, stepping, slots, 3);
        kest_lend_ends(runtime, lent);
        took[f] = in_nanoseconds() - before;
        if (!went) {
            kest_report(runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        *answered = slots[0].real;
    }
    say(what, took, frames, *answered);
    return true;
}

/* And the same frame with a crossing a body, over the same data. */
static bool crossing_each(KestRuntime *runtime, int32_t once, Body *world,
                          uint32_t many, long long frames, long long *took,
                          const char *what, double *answered) {
    fill(world, many);
    for (long long f = 0; f < frames; f++) {
        long long before = in_nanoseconds();
        double sum = 0.0;
        for (uint32_t i = 0; i < many; i++) {
            KestValue slots[5] = {{0}};
            slots[0].real = (double)world[i].x;
            slots[1].real = (double)world[i].y;
            slots[2].real = (double)world[i].dx;
            slots[3].real = (double)world[i].dy;
            slots[4].real = (double)WALL;
            if (!kest_call(runtime, once, slots, 5)) {
                kest_report(runtime, stderr, KEST_FORM_TEXT);
                return false;
            }
            /* The body comes back over the arguments, which is what makes
               this the same work rather than a cheaper program. */
            world[i].x = (float)slots[0].real;
            world[i].y = (float)slots[1].real;
            world[i].dx = (float)slots[2].real;
            world[i].dy = (float)slots[3].real;
            sum += (double)(world[i].x + world[i].y);
        }
        took[f] = in_nanoseconds() - before;
        *answered = sum;
    }
    say(what, took, frames, *answered);
    return true;
}

/* What the other backend wrote for this program, bound to a machine of its
   own. A generated file exports one function and this is the whole of what a
   host does with it (D1111): make a machine for the same program, hand it
   over, and the bodies in the file are the ones that machine runs. */
bool kest_natives_here(KestRuntime *runtime);

int main(int argc, char **argv) {
    uint32_t many = 20000;
    long long frames = 200;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bodies") == 0 && i + 1 < argc) {
            many = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = strtoll(argv[++i], NULL, 10);
        } else {
            fprintf(stderr, "usage: frame [--bodies n] [--frames n]\n");
            return 2;
        }
    }
    if (many == 0 || frames <= 0) {
        fprintf(stderr, "frame: a frame of no bodies is not a frame\n");
        return 2;
    }

    KestBuild *build =
        kest_build("bench/frame.kest", "lib/", stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Io.write", io_write, stderr)) {
        fprintf(stderr, "frame: the host could not be made\n");
        return 2;
    }
    KestRuntime *runtime = kest_start(build, host, NULL);
    kest_host_free(host);
    if (runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 2;
    }

    int32_t stepping = kest_entry(runtime, "bench.frame.step");
    int32_t once = kest_entry(runtime, "bench.frame.one");
    if (stepping < 0 || once < 0) {
        fprintf(stderr, "frame: this program has no `step` or `one`\n");
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 2;
    }

    Body *world = calloc(many, sizeof *world);
    long long *took = malloc(sizeof *took * (size_t)frames);
    if (world == NULL || took == NULL) {
        fprintf(stderr, "frame: no room for %u bodies\n", many);
        return 2;
    }

    printf("bench/frame, %u bodies, %lld frames, %s\n", many, frames,
           kest_version());
    printf("         p50       p95       p99       max       mad   answered "
           "(microseconds)\n");

    /* One crossing a frame. The run is lent where it stands, so what crosses
       is an address and a count rather than the bodies. */
    double answered = 0.0;
    KestTelemetry before_frames = {0};
    kest_telemetry(runtime, &before_frames);
    if (!lending(runtime, stepping, world, many, frames, took, "lend",
                 &answered)) {
        return 3;
    }

    /* And the question a frame budget actually asks: did anything happen in
       there that the program did not ask for? The hot phase promises
       `no.alloc`, a walk happens only inside an allocation, and so a frame of
       this shape cannot be interrupted by one. That is a thing to read in the
       source and a thing to show: over every frame above, this is what the
       heap under it did. */
    KestTelemetry after = {0};
    kest_telemetry(runtime, &after);
    printf("over those %lld frame(s): %llu allocation(s), %llu walk(s), "
           "%llu byte(s) copied, %llu lend(s)\n",
           frames, (unsigned long long)(after.allocations - before_frames.allocations),
           (unsigned long long)(after.sweeps - before_frames.sweeps),
           (unsigned long long)(after.copied - before_frames.copied),
           (unsigned long long)(after.lends - before_frames.lends));

    /* One crossing a body, over the same data, so that what a crossing costs
       is a number rather than an argument. */
    if (!crossing_each(runtime, once, world, many, frames, took, "fine",
                       &answered)) {
        return 3;
    }

    /* And the same arithmetic here, which is the floor: what this machine
       does when nothing crosses at all. */
    fill(world, many);
    for (long long f = 0; f < frames; f++) {
        long long before = in_nanoseconds();
        float sum = 0.0f;
        for (uint32_t i = 0; i < many; i++) {
            Body one = world[i];
            one.x += one.dx;
            one.y += one.dy;
            if (one.x < 0.0f) {
                one.x = -one.x;
                one.dx = -one.dx;
            } else if (one.x > WALL) {
                one.x = WALL - (one.x - WALL);
                one.dx = -one.dx;
            }
            if (one.y < 0.0f) {
                one.y = -one.y;
                one.dy = -one.dy;
            } else if (one.y > WALL) {
                one.y = WALL - (one.y - WALL);
                one.dy = -one.dy;
            }
            world[i] = one;
            sum += one.x + one.y;
        }
        took[f] = in_nanoseconds() - before;
        answered = (double)sum;
    }
    say("native", took, frames, answered);
    /* And whether the tails above are worth reading at all. The C row is the
       control: it is the same arithmetic with no machine under it, so what it
       does above its own middle is what this computer was doing at the time
       rather than anything this project wrote. A run whose control is quiet
       is a run whose tails are the engine's; a run whose control is not says
       so here rather than leaving a reader to believe a worst frame that
       belongs to somebody else's build. See D1123. */
    long long floor_middle = took[0];
    long long floor_worst = took[0];
    for (long long f = 0; f < frames; f++) {
        if (took[f] < floor_middle) {
            floor_middle = took[f];
        }
        if (took[f] > floor_worst) {
            floor_worst = took[f];
        }
    }
    printf("the floor's worst frame is %.1f times its best, so the tails "
           "above are %s\n",
           floor_middle > 0 ? (double)floor_worst / (double)floor_middle : 0.0,
           floor_worst > floor_middle * 4 ? "this computer's and not this "
                                            "language's"
                                          : "the engines' own");

    /* What each of the two crossed, which the timings above cannot say: a
       boundary is a count of crossings and a count of bytes, and two ways of
       doing one frame's work differ in both. The host knows all of it exactly
       because the host is what writes the slots. See D1029.

       `bytes crossing` is the host's own memory the frame worked on, which is
       the same for both because the work is the same. `bytes marshalled` is
       what was copied at the boundary to make that happen: nought for the
       lend, where the run is read and written where it stands and what crosses
       is an address and a count, and nine slots a body for the fine path --
       four fields and the wall in, four fields back. */
    unsigned long long bodies = (unsigned long long)many *
                                (unsigned long long)frames;
    unsigned long long crossing = bodies * (unsigned long long)sizeof(Body);
    printf("what crossed: %s\n",
           "crossings, elements, bytes of the host's memory, bytes marshalled");
    printf("%-8s %11lld %11llu %14llu %14llu\n", "lend", frames, bodies,
           crossing, 0ULL);
    printf("%-8s %11llu %11llu %14llu %14llu\n", "fine", bodies, bodies,
           crossing, bodies * 9ULL * sizeof(KestValue));
    printf("%-8s %11d %11llu %14llu %14llu\n", "native", 0, bodies, crossing,
           0ULL);

    /* And the same two frames again, run by the other engine: a second
       machine of the same build, with the bodies the C backend wrote bound to
       it. Same world, same clock, same host -- the only difference is which
       of the two engines is answering, which is what makes these rows a
       comparison rather than two measurements. See D1123. */
    KestHost *again = kest_host_new();
    if (again == NULL || !kest_host_bind(again, "Io.write", io_write, stderr)) {
        fprintf(stderr, "frame: the host could not be made twice\n");
        return 2;
    }
    KestRuntime *compiled = kest_start(build, again, NULL);
    kest_host_free(again);
    if (compiled == NULL || !kest_natives_here(compiled)) {
        fprintf(stderr, "frame: this C was written from another program\n");
        return 2;
    }
    int32_t stepping_c = kest_entry(compiled, "bench.frame.step");
    int32_t once_c = kest_entry(compiled, "bench.frame.one");
    if (stepping_c < 0 || once_c < 0) {
        fprintf(stderr, "frame: the compiled program has no `step` or `one`\n");
        return 2;
    }
    printf("the same two frames, run by the bodies the other backend wrote "
           "(microseconds)\n");
    if (!lending(compiled, stepping_c, world, many, frames, took, "lend",
                 &answered) ||
        !crossing_each(compiled, once_c, world, many, frames, took, "fine",
                       &answered)) {
        return 3;
    }

    free(world);
    free(took);
    kest_runtime_free(compiled);
    kest_runtime_free(runtime);
    kest_build_free(build);
    return 0;
}
