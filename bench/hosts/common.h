// What every host in `bench/hosts` shares: the clock, the bodies the host
// owns, the arithmetic done in C as the floor, and the one way each says what
// it measured -- a line of `engine`, `measure`, nanoseconds and the answer,
// split by tabs, which `bench/hosts.sh` gathers and `bench/chart.py` draws.
// Each host is one engine's embedding API driven over the same work, so what
// differs between two lines is the engine and the way into it. See D1272.
#ifndef KEST_BENCH_HOSTS_COMMON_H
#define KEST_BENCH_HOSTS_COMMON_H

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define BODIES 20000
#define FRAMES 200
#define ASKS 1000000
#define WALL 100.0

typedef struct {
    double x;
    double y;
    double dx;
    double dy;
} Body;

static inline long long now_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
}

static inline int nearer(const void *left, const void *right) {
    long long a = *(const long long *)left;
    long long b = *(const long long *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

// The middle of what was taken: a frame budget is read at its middle and at
// its tails, and the tails are `bench/hosts.sh`'s to ask for separately.
static inline long long middle_of(long long *took, int count) {
    qsort(took, (size_t)count, sizeof *took, nearer);
    return took[count / 2];
}

// The bodies as every host fills them, which is what `fill` in `bodies.lua`
// and `bodies.js` makes too.
static inline void fill(Body *world, int many) {
    for (int i = 0; i < many; i++) {
        world[i].x = (double)(i % 100);
        world[i].y = (double)((i * 3) % 100);
        world[i].dx = (double)(1 + i % 3);
        world[i].dy = 2.0;
    }
}

static inline void moved(Body *b, double wall) {
    b->x += b->dx;
    b->y += b->dy;
    if (b->x < 0.0) {
        b->x = -b->x;
        b->dx = -b->dx;
    } else if (b->x > wall) {
        b->x = wall - (b->x - wall);
        b->dx = -b->dx;
    }
    if (b->y < 0.0) {
        b->y = -b->y;
        b->dy = -b->dy;
    } else if (b->y > wall) {
        b->y = wall - (b->y - wall);
        b->dy = -b->dy;
    }
}

// A line of what was measured. `per` is nanoseconds for one of whatever the
// measure counts -- a body, or a call -- and `answer` is what the work summed
// to, which every engine has to agree on for the line to mean anything.
static inline void said(const char *engine, const char *measure, double per,
                        double answer) {
    printf("%s\t%s\t%.2f\t%.0f\n", engine, measure, per, answer);
    fflush(stdout);
}

// The floor: the same frame in C, nothing crossing.
static inline void floor_frames(const char *engine) {
    Body *world = (Body *)malloc(sizeof *world * BODIES);
    long long took[FRAMES];
    double sum = 0.0;
    fill(world, BODIES);
    // Twenty frames first, as every host runs before it starts timing, so the
    // bodies are where everybody else's are when the last frame is summed.
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        sum = 0.0;
        for (int i = 0; i < BODIES; i++) {
            moved(&world[i], WALL);
            sum += world[i].x + world[i].y;
        }
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    said(engine, "frame", (double)middle_of(took, FRAMES) / BODIES, sum);
    free(world);
}

#endif
