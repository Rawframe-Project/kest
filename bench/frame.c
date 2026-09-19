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

    int32_t stepping = kest_entry(runtime, "frame.step");
    int32_t once = kest_entry(runtime, "frame.one");
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
    fill(world, many);
    KestTelemetry before_frames = {0};
    kest_telemetry(runtime, &before_frames);
    for (long long f = 0; f < frames; f++) {
        long long before = in_nanoseconds();
        KestValue lent =
            kest_borrow(runtime, world, many, "frame.Body", sizeof *world);
        KestValue slots[3] = {{0}};
        slots[0] = lent;
        slots[1].real = (double)WALL;
        bool went = kest_call(runtime, stepping, slots, 3);
        kest_lend_ends(runtime, lent);
        took[f] = in_nanoseconds() - before;
        if (!went) {
            kest_report(runtime, stderr, KEST_FORM_TEXT);
            return 3;
        }
        answered = slots[0].real;
    }
    say("lend", took, frames, answered);

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
                return 3;
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
        answered = sum;
    }
    say("fine", took, frames, answered);

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

    free(world);
    free(took);
    kest_runtime_free(runtime);
    kest_build_free(build);
    return 0;
}
