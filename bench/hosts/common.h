// What every host in `bench/hosts` shares: the clock, the bodies the host
// owns, the arithmetic done in C as the floor, and the one way each says what
// it measured -- a line of `engine`, `measure`, nanoseconds and the answer,
// split by tabs, which `bench/hosts.sh` gathers and `bench/chart.py` draws.
// Each host is one engine's embedding API driven over the same work, so what
// differs between two lines is the engine and the way into it. See D1272.
#ifndef KEST_BENCH_HOSTS_COMMON_H
#define KEST_BENCH_HOSTS_COMMON_H

#if !defined(_WIN32) && !defined(__cplusplus)
#define _POSIX_C_SOURCE 200809L
// `MAP_ANONYMOUS`, which every system this runs on has and POSIX does not say.
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#endif

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BODIES 20000
#define FRAMES 200
#define ASKS 1000000
#define WALL 100.0
#define RELOADS 31
#define THINGS 5000
#define REPLACED 250
#define CHURNS 5000
#define SCALED_FRAMES 60
#define STOPS 11

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

// A world that makes garbage, a frame at a time, every frame timed: what is
// said is the middle frame, the ninety-ninth in a hundred and the worst, which
// is where a collector shows. `frame` runs one frame of the host's engine and
// answers its sum.
static inline void churned(const char *engine, double (*frame)(void *),
                           void *context) {
    static long long took[CHURNS];
    double sum = 0.0;
    for (int f = -20; f < CHURNS; f++) {
        long long before = now_ns();
        sum = frame(context);
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    qsort(took, CHURNS, sizeof *took, nearer);
    said(engine, "churn50", (double)took[CHURNS / 2], sum);
    said(engine, "churn99", (double)took[CHURNS * 99 / 100], sum);
    said(engine, "churnmax", (double)took[CHURNS - 1], sum);
}

// The same frame on many threads at once, a machine or a state each, which is
// how an engine runs worlds side by side: `make` makes one on the thread that
// asks and hands it to a thread of its own, `frame` runs a frame of it and
// answers the sum, and `drop` lets it go. Every thread starts at once and the
// clock is the whole of it, from the first to the last done; what is said is
// nanoseconds a body over all of them, so a line that stays level as threads
// are added is one that scales.
typedef struct {
    void *(*make)(void *context);
    double (*frame)(void *state);
    void (*drop)(void *state);
    void *context;
} Scaled;

typedef struct {
    const Scaled *how;
    void *state;
    pthread_barrier_t *gate;
    double sum;
} Lane;

static void *lane_runs(void *given) {
    Lane *lane = (Lane *)given;
    pthread_barrier_wait(lane->gate);
    for (int f = 0; f < SCALED_FRAMES; f++) {
        lane->sum = lane->how->frame(lane->state);
    }
    return NULL;
}

static inline void scaled(const char *engine, const Scaled *how) {
    // One, two, four and on up to as many as the machine has, and that many.
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    int counts[16];
    int count = 0;
    for (int n = 1; n < cores && count < 15; n *= 2) {
        counts[count++] = n;
    }
    counts[count++] = cores > 0 ? (int)cores : 1;
    for (int c = 0; c < count; c++) {
        int many = counts[c];
        Lane *lanes = (Lane *)calloc((size_t)many, sizeof *lanes);
        pthread_t *threads =
            (pthread_t *)calloc((size_t)many, sizeof *threads);
        pthread_barrier_t gate;
        pthread_barrier_init(&gate, NULL, (unsigned)many + 1);
        for (int i = 0; i < many; i++) {
            lanes[i].how = how;
            lanes[i].state = how->make(how->context);
            lanes[i].gate = &gate;
            // Warmed where it will be run: a first frame compiles what
            // compiles and faults in what faults.
            how->frame(lanes[i].state);
            pthread_create(&threads[i], NULL, lane_runs, &lanes[i]);
        }
        long long before = now_ns();
        pthread_barrier_wait(&gate);
        for (int i = 0; i < many; i++) {
            pthread_join(threads[i], NULL);
        }
        long long took = now_ns() - before;
        char measure[32];
        snprintf(measure, sizeof measure, "threads%d", many);
        said(engine, measure,
             (double)took / ((double)many * SCALED_FRAMES * BODIES),
             lanes[0].sum);
        for (int i = 0; i < many; i++) {
            how->drop(lanes[i].state);
        }
        pthread_barrier_destroy(&gate);
        free(lanes);
        free(threads);
    }
}

// How soon a program that has got away stops once its host asks: `make` makes
// a state holding `spin`, `run` calls it and comes back when it has been
// stopped, `ask` is what another thread does to stop it, and `again` makes
// the state ready to be run once more. Each is timed from the ask to the call
// coming back, the middle of eleven. It is done in a process of its own that
// is given three seconds, because an engine that cannot be stopped is a call
// that never comes back, and what is said then is nought for "did not stop".
typedef struct {
    void *(*make)(void *context);
    void (*run)(void *state);
    void (*ask)(void *state);
    void (*again)(void *state);
    void *context;
} Stoppable;

typedef struct {
    const Stoppable *how;
    void *state;
    volatile long long *asked_at;
} Asker;

static void *asker_runs(void *given) {
    Asker *asker = (Asker *)given;
    struct timespec wait = {0, 20 * 1000 * 1000};
    nanosleep(&wait, NULL);
    *asker->asked_at = now_ns();
    asker->how->ask(asker->state);
    return NULL;
}

static inline void stopped(const char *engine, const Stoppable *how) {
    size_t room = sizeof(long long) * (STOPS + 1);
    long long *shared =
        (long long *)mmap(NULL, room, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    shared[STOPS] = 0;
    fflush(stdout);
    pid_t child = fork();
    if (child == 0) {
        void *state = how->make(how->context);
        for (int s = 0; s < STOPS; s++) {
            volatile long long asked_at = 0;
            Asker asker = {how, state, &asked_at};
            pthread_t thread;
            pthread_create(&thread, NULL, asker_runs, &asker);
            how->run(state);
            long long back = now_ns();
            pthread_join(thread, NULL);
            shared[s] = back - asked_at;
            how->again(state);
        }
        shared[STOPS] = 1;
        _exit(0);
    }
    long long gave_up = now_ns() + 3000000000LL;
    int status = 0;
    while (waitpid(child, &status, WNOHANG) == 0) {
        if (now_ns() > gave_up) {
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
            break;
        }
        struct timespec wait = {0, 10 * 1000 * 1000};
        nanosleep(&wait, NULL);
    }
    if (shared[STOPS] == 1) {
        said(engine, "stop", (double)middle_of(shared, STOPS), 0.0);
    } else {
        said(engine, "stop", 0.0, 0.0);
    }
    munmap(shared, room);
}

// The floor on many threads: the same frame in C, which is what the machine
// under every engine does when nothing is interpreted, memory and all.
static inline void *floor_make(void *context) {
    (void)context;
    Body *world = (Body *)malloc(sizeof *world * BODIES);
    fill(world, BODIES);
    return world;
}

static inline double floor_frame(void *state) {
    Body *world = (Body *)state;
    double sum = 0.0;
    for (int i = 0; i < BODIES; i++) {
        moved(&world[i], WALL);
        sum += world[i].x + world[i].y;
    }
    return sum;
}

static inline void floor_drop(void *state) {
    free(state);
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
