/* The other direction, and the one the language cannot measure about itself:
   how long a call *in* from a host takes, against a call the program makes on
   its own. D007 says the two crossings are separate specifications — the frame
   a machine writes for a host is not the frame a host writes for it — and
   `tools/crossing.kest` measured only the way out.
 *
 * A host is what does the calling here, so this is C rather than Kest. What it
 * runs is written below and put in a file of its own at run time rather than
 * kept beside this one, because a `.kest` under `tools` is an instrument the
 * gate runs and expects a measurement from, and this program is a thing to be
 * called rather than to be run.
 *
 * The two numbers are not the same measurement and are not a difference. A
 * call in from a host is one `kest_call`: a frame written by the host, the
 * arguments weighed on the way in and the answer weighed on the way back. A
 * call the program makes itself is one hop of a loop inside one `kest_call`,
 * so it carries the loop with it — which is what a call in a program is and
 * what the frame instrument measures a helper as. Subtracting them says a
 * crossing costs less than nothing, which is true of the arithmetic and of
 * nothing else, so both are printed and neither is taken away from the other.
 *
 * That the first is the smaller of the two is the thing worth reading here: a
 * host writing the frame itself and calling straight in is cheaper than the
 * machine reaching an instruction that starts a call. The way out, which
 * `tools/crossing.kest` measures, is the dearer direction.
 *
 * It is not a benchmark suite and `make check` reads only whether it ran and
 * what shape its line is. `make time` runs it beside the other two. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kest.h"

#define CALLS 1000000
#define ROUNDS 7

/* The same shape the other instrument weighs: one argument, one answer, and a
   body that is the least a body can be and still be one. `many` is that call
   made in a loop, so what it costs a hop is a call the program makes itself. */
static const char *const PROGRAM =
    "module tools.inward\n"
    "\n"
    "fn inside(value: f64) -> f64 no.alloc no.host {\n"
    "    return value\n"
    "}\n"
    "\n"
    "fn many(count: i32) -> f64 no.alloc no.host {\n"
    "    let sum: f64 = 0.0\n"
    "    for i in 0..count {\n"
    "        sum += inside(f64(i % 64))\n"
    "    }\n"
    "    return sum\n"
    "}\n"
    "\n"
    "fn main() -> i32 {\n"
    "    return i32(many(1))\n"
    "}\n";

/* The clock the command line hands a program, read the same way: what the
   standard has rather than what this machine has, so this host compiles where
   the rest of the tree compiles. It counts the processor's time in
   microseconds, which is fine for a round of thirty thousand of them and no
   use at all for one call — which is why a round is what is timed. */
static long long in_microseconds(void) {
    return (long long)clock() * 1000000 / CLOCKS_PER_SEC;
}

int main(void) {
    /* In whatever room the caller works in, and at the root of the tree when
       there is none. `tmpnam` is the only name the standard hands out and it
       is the one thing in it nobody should use; `TMPDIR` is what the gate
       hands every check so that what a check leaves behind is a thing it can
       look at. Not under `tools` and not under `examples`, because a `.kest`
       written there is one a sweep running beside this would find and try to
       read. */
    const char *room = getenv("TMPDIR");
    char in[512];
    snprintf(in, sizeof in, "%s/kest-inward-program.kest",
             room == NULL ? "." : room);
    const char *path = in;
    FILE *writing = fopen(path, "w");
    if (writing == NULL || fputs(PROGRAM, writing) < 0 ||
        fclose(writing) != 0) {
        fprintf(stderr, "the program this measures could not be written\n");
        return 2;
    }

    KestBuild *build = kest_build(path, "lib/", stderr, KEST_FORM_TEXT, 0);
    remove(path);
    if (build == NULL) {
        return 2;
    }
    KestHost *host = kest_host_new();
    KestRuntime *runtime = host == NULL ? NULL : kest_start(build, host, NULL);
    kest_host_free(host);
    if (runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 2;
    }

    int32_t one = kest_entry(runtime, "inside");
    int32_t all = kest_entry(runtime, "many");
    if (one < 0 || all < 0) {
        fprintf(stderr, "the program this measures has no `inside` or `many`\n");
        return 2;
    }

    /* A round of each before the clock, so what is measured is the steady
       state rather than the first call into anything. */
    KestValue frame[2] = {{0}};
    double sum = 0.0;
    for (int32_t i = 0; i < CALLS; i++) {
        frame[0].real = (double)(i % 64);
        if (!kest_call(runtime, one, frame, 2)) {
            kest_report(runtime, stderr, KEST_FORM_TEXT);
            return 3;
        }
        sum += frame[0].real;
    }
    frame[0].integer = CALLS;
    if (!kest_call(runtime, all, frame, 2)) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 3;
    }
    sum += frame[0].real;

    /* The best of several rounds rather than the average of them, for the
       reason the other two give: anything else sharing the machine only ever
       adds time, so the smallest is the closest this gets to the work. */
    long long from_out = 0;
    long long worst_out = 0;
    for (int round = 0; round < ROUNDS; round++) {
        long long before = in_microseconds();
        for (int32_t i = 0; i < CALLS; i++) {
            frame[0].real = (double)(i % 64);
            if (!kest_call(runtime, one, frame, 2)) {
                kest_report(runtime, stderr, KEST_FORM_TEXT);
                return 3;
            }
            sum += frame[0].real;
        }
        long long took = in_microseconds() - before;
        if (round == 0 || took < from_out) {
            from_out = took;
        }
        if (took > worst_out) {
            worst_out = took;
        }
    }

    long long within = 0;
    long long worst_in = 0;
    for (int round = 0; round < ROUNDS; round++) {
        long long before = in_microseconds();
        frame[0].integer = CALLS;
        if (!kest_call(runtime, all, frame, 2)) {
            kest_report(runtime, stderr, KEST_FORM_TEXT);
            return 3;
        }
        long long took = in_microseconds() - before;
        sum += frame[0].real;
        if (round == 0 || took < within) {
            within = took;
        }
        if (took > worst_in) {
            worst_in = took;
        }
    }

    /* The clock counts microseconds and what is wanted is nanoseconds for one
       call. */
    long long here = within * 1000 / CALLS;
    long long there = from_out * 1000 / CALLS;
    /* Each against its own worst, because the two are two measurements: a
       round of calls in is slower than a round of calls inside by exactly the
       thing being weighed. */
    long long spread_in = (worst_in - within) * 100 / (within > 0 ? within : 1);
    long long spread_out =
        (worst_out - from_out) * 100 / (from_out > 0 ? from_out : 1);
    long long spread = spread_in > spread_out ? spread_in : spread_out;
    /* Both numbers and no difference between them, because they are not a
       difference. One is a crossing and a body; the other is a hop of a loop,
       and a loop is what a call in a program is written inside. Subtracting
       them would say a crossing costs less than nothing, which is the shape a
       reader would have to be told not to read. */
    printf("%lld ns for a call in from a host and %lld ns for one the program "
           "makes in a loop, best of %d over %d calls, spread %lld%%%s\n",
           there, here, ROUNDS, CALLS, spread,
           spread <= 25 ? "" : " — the machine was somebody else's");

    /* And what the work adds up to, which is a number and not a duration: an
       instrument that stops doing its work still prints a duration. Every call
       gives back what it was handed, so what the loops put in is what comes
       out. */
    double each = 0.0;
    for (int32_t i = 0; i < CALLS; i++) {
        each += (double)(i % 64);
    }
    double wanted = each * (double)(ROUNDS * 2 + 2);
    kest_runtime_free(runtime);
    kest_build_free(build);
    if (sum != wanted) {
        fprintf(stderr, "the calls added up to %f and not %f\n", sum, wanted);
        return 1;
    }
    return 0;
}
