/* What a program costs, separated into the four things that are usually added
   together and reported as one number: starting a process, compiling, the
   first run, and the run after that.
 *
 * `bench/run.sh` times a whole process with the shell's clock and answers the
 * best of five. That is the right shape for comparing two languages, where a
 * process is what a person runs, and the wrong shape for asking where a
 * millisecond went: it cannot tell a compiler that got slower from a machine
 * that did, it has the shell's own milliseconds as its resolution, and a best
 * of five says nothing about the fifth frame in a hundred that took twice as
 * long -- which for a game is the number that matters.
 *
 * So this is a host with a clock in it. It builds the program once and times
 * that; it starts a machine and times that; it calls the entry once and times
 * that, which is the cold run; and then it calls it again as many times as it
 * is asked, keeping every sample, and answers with the middle, the tail and
 * the worst. Nothing is thrown away: a sample that was slow because something
 * else was running is a sample of this machine, and what says how much of that
 * there was is the dispersion printed beside the middle.
 *
 * The clock is the host's monotonic one, which is what the mission asks for
 * and what `tools/inward.c` deliberately does not use -- that one is an
 * instrument the gate runs everywhere and holds itself to ISO C. This is a
 * bench tool, run by whoever is optimizing, and it says which clock it read.
 *
 * It is not part of `make check`. A duration is not a pass or a fail. */

/* A monotonic clock is not in ISO C, so this one file asks for the POSIX one
   by name, before anything is included. `include/kest.h` is held to the
   standard and nothing beyond it and this is not that file. */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define CLOCK_READ "QueryPerformanceCounter"
#elif defined(__unix__) || defined(__APPLE__)
#include <time.h>
#define CLOCK_READ "clock_gettime(CLOCK_MONOTONIC)"
#else
#include <time.h>
#define CLOCK_READ "clock()"
#endif

#include "kest.h"

#if defined(_WIN32)
static long long in_nanoseconds(void) {
    static LARGE_INTEGER a_second;
    LARGE_INTEGER now;
    if (a_second.QuadPart == 0) {
        QueryPerformanceFrequency(&a_second);
    }
    QueryPerformanceCounter(&now);
    return (long long)((double)now.QuadPart * 1e9 / (double)a_second.QuadPart);
}
#elif defined(__unix__) || defined(__APPLE__)
static long long in_nanoseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
}
#else
static long long in_nanoseconds(void) {
    return (long long)clock() * (1000000000LL / CLOCKS_PER_SEC);
}
#endif

#if defined(__clang__)
#define BUILT_BY "clang " __clang_version__
#elif defined(__GNUC__)
#define BUILT_BY "gcc " __VERSION__
#elif defined(_MSC_VER)
#define BUILT_BY "msvc"
#else
#define BUILT_BY "an unnamed compiler"
#endif

#if defined(_WIN32)
#define RAN_ON "windows"
#elif defined(__APPLE__)
#define RAN_ON "macos"
#elif defined(__linux__)
#define RAN_ON "linux"
#else
#define RAN_ON "an unnamed system"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define RAN_AS "x86-64"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define RAN_AS "arm64"
#else
#define RAN_AS "an unnamed architecture"
#endif

/* Every sample is kept. The middle, the tails and the worst are read off a
   sorted copy, and how spread out they are is said as the median of how far
   each one is from the middle -- which is the one dispersion figure a handful
   of very slow samples cannot move, and a run on a machine somebody else is
   also using has a handful of those. */
typedef struct {
    long long middle;
    long long p95;
    long long p99;
    long long worst;
    long long least;
    long long spread;
    long long count;
} Spread;

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

static Spread spread_of(long long *samples, long long count) {
    Spread out = {0, 0, 0, 0, 0, 0, count};
    if (count <= 0) {
        return out;
    }
    qsort(samples, (size_t)count, sizeof *samples, nearer);
    out.least = samples[0];
    out.worst = samples[count - 1];
    out.middle = at_share(samples, count, 0.5);
    out.p95 = at_share(samples, count, 0.95);
    out.p99 = at_share(samples, count, 0.99);
    long long *away = malloc(sizeof *away * (size_t)count);
    if (away == NULL) {
        return out;
    }
    for (long long i = 0; i < count; i++) {
        long long from = samples[i] - out.middle;
        away[i] = from < 0 ? -from : from;
    }
    qsort(away, (size_t)count, sizeof *away, nearer);
    out.spread = at_share(away, count, 0.5);
    free(away);
    return out;
}

static void say_spread(const char *what, Spread it) {
    printf("%-14s %9.3f %9.3f %9.3f %9.3f %9.3f  %lld sample(s)\n", what,
           (double)it.middle / 1e6, (double)it.p95 / 1e6,
           (double)it.p99 / 1e6, (double)it.worst / 1e6,
           (double)it.spread / 1e6, it.count);
}

static void say_spread_json(const char *what, Spread it, bool comma) {
    printf("    \"%s\": {\"p50\": %lld, \"p95\": %lld, \"p99\": %lld, "
           "\"max\": %lld, \"min\": %lld, \"mad\": %lld, \"samples\": %lld}%s\n",
           what, it.middle, it.p95, it.p99, it.worst, it.least, it.spread,
           it.count, comma ? "," : "");
}

/* What the program wrote, which a measurement does not want to see: a
   workload that prints its checksum every call would be measuring the
   terminal. It goes where the caller sent it, and the caller sends it to
   nowhere by pointing the stream at a file it does not read. Bound under both
   names the library and a host may declare. */
static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(frame, &length);
    if (bytes != NULL && length > 0) {
        fwrite(bytes, 1, length, (FILE *)context);
    }
}

/* The rest of what `std.os` declares, because a machine refuses a program
   whose externs are not all bound and a workload that reads its own scale
   knobs imports it. What a host provides is what a program may do, so this one
   provides a clock and nothing else: no arguments, no files. A workload run
   here gets its own defaults, which is what a measurement wants anyway. */
static void os_arg_count(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = 0;
}

static void os_arg(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    kest_text(runtime, "", 0, frame);
    frame[2].integer = 0;
}

static void os_clock(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = in_nanoseconds() / 1000;
}

static void os_file_read(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    (void)context;
    kest_text(runtime, "", 0, frame);
    frame[2].integer = 0;
}

static void os_says_no(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    frame[0].integer = 0;
}

static uint64_t a_clock(void *context) {
    (void)context;
    return (uint64_t)in_nanoseconds();
}

static void how_to_run(void) {
    fprintf(stderr,
            "usage: measure <file.kest> [options]\n"
            "  --entry <name>     which function to call, `main` by default\n"
            "  --arg <number>     an i32 argument, repeatable\n"
            "  --samples <n>      steady-state calls to time, 200 by default\n"
            "  --warmup <n>       calls before the clock, 20 by default\n"
            "  --builds <n>       times to compile, 5 by default\n"
            "  --library <dir>    where the standard library is, `lib/`\n"
            "  --room <bytes>     a heap ceiling, none by default\n"
            "  --json             say it again as JSON\n");
}

int main(int argc, char **argv) {
    const char *path = NULL;
    const char *entry_name = "main";
    const char *library = "lib/";
    long long samples = 200;
    long long warmup = 20;
    long long builds = 5;
    size_t room = 0;
    bool as_json = false;
    int32_t args[8];
    uint32_t arg_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *one = argv[i];
        bool has_next = i + 1 < argc;
        if (strcmp(one, "--entry") == 0 && has_next) {
            entry_name = argv[++i];
        } else if (strcmp(one, "--arg") == 0 && has_next) {
            if (arg_count == sizeof args / sizeof *args) {
                fprintf(stderr, "measure: more arguments than a frame here "
                                "holds\n");
                return 2;
            }
            args[arg_count++] = (int32_t)strtol(argv[++i], NULL, 10);
        } else if (strcmp(one, "--samples") == 0 && has_next) {
            samples = strtoll(argv[++i], NULL, 10);
        } else if (strcmp(one, "--warmup") == 0 && has_next) {
            warmup = strtoll(argv[++i], NULL, 10);
        } else if (strcmp(one, "--builds") == 0 && has_next) {
            builds = strtoll(argv[++i], NULL, 10);
        } else if (strcmp(one, "--library") == 0 && has_next) {
            library = argv[++i];
        } else if (strcmp(one, "--room") == 0 && has_next) {
            room = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(one, "--json") == 0) {
            as_json = true;
        } else if (one[0] == '-') {
            fprintf(stderr, "measure: `%s` is not an option here\n", one);
            how_to_run();
            return 2;
        } else if (path == NULL) {
            path = one;
        } else {
            fprintf(stderr, "measure: one program at a time\n");
            return 2;
        }
    }
    if (path == NULL || samples <= 0 || builds <= 0 || warmup < 0) {
        how_to_run();
        return 2;
    }

    /* Compiling, timed on its own, because a compiler that got slower and a
       machine that got slower look the same from outside the process. The
       build that is kept is the last one. */
    long long *build_took = malloc(sizeof *build_took * (size_t)builds);
    if (build_took == NULL) {
        fprintf(stderr, "measure: no room to keep the samples\n");
        return 2;
    }
    KestBuild *build = NULL;
    for (long long i = 0; i < builds; i++) {
        if (build != NULL) {
            kest_build_free(build);
        }
        long long before = in_nanoseconds();
        build = kest_build(path, library, stderr, KEST_FORM_TEXT, 0);
        build_took[i] = in_nanoseconds() - before;
        if (build == NULL) {
            free(build_took);
            return 2;
        }
    }

    long long before_start = in_nanoseconds();
    KestHost *host = kest_host_new();
    if (host != NULL &&
        (!kest_host_bind(host, "Io.write", io_write, stderr) ||
         !kest_host_bind(host, "Host.write", io_write, stderr) ||
         !kest_host_bind(host, "Host.argCount", os_arg_count, NULL) ||
         !kest_host_bind(host, "Host.arg", os_arg, NULL) ||
         !kest_host_bind(host, "Host.clock", os_clock, NULL) ||
         !kest_host_bind(host, "Host.fileRead", os_file_read, NULL) ||
         !kest_host_bind(host, "Host.fileWrite", os_says_no, NULL) ||
         !kest_host_bind(host, "Host.fileExists", os_says_no, NULL))) {
        fprintf(stderr, "measure: the host could not bind what it provides\n");
        free(build_took);
        return 2;
    }
    KestRuntime *runtime = host == NULL ? NULL : kest_start(build, host, NULL);
    long long start_took = in_nanoseconds() - before_start;
    kest_host_free(host);
    if (runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        free(build_took);
        return 2;
    }
    if (room != 0) {
        kest_heap_allow(runtime, room);
    }
    /* The clock the machine times its own walks with, which is this host's
       because the library is ISO C and has no monotonic one. */
    kest_clock(runtime, a_clock, NULL);

    int32_t entry = kest_entry(runtime, entry_name);
    if (entry < 0) {
        fprintf(stderr, "measure: this program has no `%s`\n", entry_name);
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        free(build_took);
        return 2;
    }
    uint32_t wide = kest_frame_slots(runtime, entry);
    if (wide < arg_count) {
        wide = arg_count;
    }
    KestValue *frame = wide == 0 ? NULL : calloc(wide, sizeof *frame);
    if (wide != 0 && frame == NULL) {
        fprintf(stderr, "measure: no room for a frame of %u\n", wide);
        free(build_took);
        return 2;
    }

    /* One call with the clock on it before anything else, which is the only
       cold one there is: everything after it runs code that has been run. */
    for (uint32_t a = 0; a < arg_count; a++) {
        frame[a].integer = args[a];
    }
    long long before_cold = in_nanoseconds();
    bool ran = kest_call(runtime, entry, frame, wide);
    long long cold_took = in_nanoseconds() - before_cold;
    if (!ran) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        free(build_took);
        free(frame);
        return 3;
    }

    for (long long i = 0; i < warmup; i++) {
        for (uint32_t a = 0; a < arg_count; a++) {
            frame[a].integer = args[a];
        }
        if (!kest_call(runtime, entry, frame, wide)) {
            kest_report(runtime, stderr, KEST_FORM_TEXT);
            free(build_took);
            free(frame);
            return 3;
        }
    }

    long long *call_took = malloc(sizeof *call_took * (size_t)samples);
    /* What the walks took inside each timed call, which is the number a frame
       budget is spent by: a collection that takes twelve milliseconds is not
       a slow program, it is a frame that was missed. */
    long long *walk_took = malloc(sizeof *walk_took * (size_t)samples);
    if (call_took == NULL || walk_took == NULL) {
        fprintf(stderr, "measure: no room to keep the samples\n");
        free(build_took);
        free(frame);
        return 2;
    }
    kest_count(runtime, true);
    for (long long i = 0; i < samples; i++) {
        for (uint32_t a = 0; a < arg_count; a++) {
            frame[a].integer = args[a];
        }
        KestTelemetry was = {0};
        kest_telemetry(runtime, &was);
        long long before = in_nanoseconds();
        bool went = kest_call(runtime, entry, frame, wide);
        call_took[i] = in_nanoseconds() - before;
        KestTelemetry now = {0};
        kest_telemetry(runtime, &now);
        walk_took[i] = (long long)(now.walked - was.walked);
        if (!went) {
            kest_report(runtime, stderr, KEST_FORM_TEXT);
            free(build_took);
            free(call_took);
            free(frame);
            return 3;
        }
    }
    KestCounted counted = {0};
    kest_counted(runtime, &counted);
    KestTelemetry heap = {0};
    kest_telemetry(runtime, &heap);

    Spread building = spread_of(build_took, builds);
    Spread calling = spread_of(call_took, samples);
    Spread walking = spread_of(walk_took, samples);
    size_t held = kest_heap_used(runtime);
    size_t taken = kest_heap_taken(runtime);
    size_t most = kest_heap_most(runtime);

    printf("%s %s, entry %s\n", path, kest_version(), entry_name);
    printf("%s %s, built by %s, clock %s\n", RAN_ON, RAN_AS, BUILT_BY,
           CLOCK_READ);
    printf("                      p50       p95       p99       max       "
           "mad  (milliseconds)\n");
    say_spread("compiling", building);
    say_spread("calling", calling);
    printf("%-14s %9.3f  once, the machine and the world it starts with\n",
           "starting", (double)start_took / 1e6);
    printf("%-14s %9.3f  once, the first call of all\n", "cold",
           (double)cold_took / 1e6);
    printf("a call ran %llu step(s), %llu call(s) and %llu crossing(s), "
           "which is %llu step(s) over the %lld timed\n",
           (unsigned long long)(counted.steps / (unsigned long long)samples),
           (unsigned long long)(counted.calls / (unsigned long long)samples),
           (unsigned long long)(counted.crossings /
                                (unsigned long long)samples),
           (unsigned long long)counted.steps, samples);
    printf("the heap holds %llu byte(s), was handed %llu, and held %llu at "
           "most\n",
           (unsigned long long)held, (unsigned long long)taken,
           (unsigned long long)most);
    printf("%llu allocation(s) asking %llu byte(s) and given %llu, %llu grown "
           "where they stood, %llu byte(s) copied\n",
           (unsigned long long)heap.allocations, (unsigned long long)heap.asked,
           (unsigned long long)heap.given, (unsigned long long)heap.grown,
           (unsigned long long)heap.copied);
    printf("%llu walk(s) gave back %llu byte(s) over %llu root slot(s); "
           "%llu plot(s) made and %llu handed back; %llu block(s)\n",
           (unsigned long long)heap.sweeps, (unsigned long long)heap.reclaimed,
           (unsigned long long)heap.roots,
           (unsigned long long)heap.plots_made,
           (unsigned long long)heap.plots_freed,
           (unsigned long long)heap.blocks);
    if (heap.walked > 0) {
        printf("the walks took %.3f ms in all -- %.3f marking, %.3f sweeping "
               "-- and the longest %.3f ms\n",
               (double)heap.walked / 1e6, (double)heap.marking / 1e6,
               (double)heap.sweeping / 1e6, (double)heap.worst_walk / 1e6);
        say_spread("walking", walking);
        printf("%-14s %9.1f%% of the middle call\n", "which is",
               calling.middle == 0
                   ? 0.0
                   : 100.0 * (double)walking.middle / (double)calling.middle);
    }

    if (as_json) {
        printf("{\n");
        printf("  \"program\": \"%s\",\n", path);
        printf("  \"entry\": \"%s\",\n", entry_name);
        printf("  \"kest\": \"%s\",\n", kest_version());
        printf("  \"abi\": %u,\n", kest_abi_version());
        printf("  \"os\": \"%s\",\n", RAN_ON);
        printf("  \"arch\": \"%s\",\n", RAN_AS);
        printf("  \"compiler\": \"%s\",\n", BUILT_BY);
        printf("  \"clock\": \"%s\",\n", CLOCK_READ);
        printf("  \"warmup\": %lld,\n", warmup);
        printf("  \"guestOnly\": true,\n");
        printf("  \"startupIncluded\": false,\n");
        printf("  \"compileIncluded\": false,\n");
        printf("  \"nanoseconds\": {\n");
        say_spread_json("compiling", building, true);
        say_spread_json("calling", calling, true);
        printf("    \"starting\": %lld,\n", start_took);
        printf("    \"cold\": %lld\n", cold_took);
        printf("  },\n");
        printf("  \"perCall\": {\"steps\": %llu, \"calls\": %llu, "
               "\"crossings\": %llu},\n",
               (unsigned long long)(counted.steps /
                                    (unsigned long long)samples),
               (unsigned long long)(counted.calls /
                                    (unsigned long long)samples),
               (unsigned long long)(counted.crossings /
                                    (unsigned long long)samples));
        printf("  \"heap\": {\"held\": %llu, \"taken\": %llu, \"most\": %llu}\n",
               (unsigned long long)held, (unsigned long long)taken,
               (unsigned long long)most);
        printf("}\n");
    }

    free(build_took);
    free(call_took);
    free(walk_took);
    free(frame);
    kest_runtime_free(runtime);
    kest_build_free(build);
    return 0;
}
