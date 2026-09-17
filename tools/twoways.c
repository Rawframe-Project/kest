/* The backend experiment section 5 of the continuation asks for, run as one
   number against another: the same work, the same bodies, and the two machines
   that read them.
 *
 * It is C rather than Kest because the work has to be entered directly. The
 * frame instrument's `main` prints and reads a clock, and neither of those is
 * something the second machine has been given an instruction for — what it
 * covers is what a frame is made of, which is arithmetic over a run of value
 * structs, and a program is run by it only when the entry and everything it
 * calls were written for it. So the host lends the world and calls `step`.
 *
 * Which machine is in the environment rather than in an argument, because it
 * is the build that decides: `KEST_SLOTS=1` makes the build write for both and
 * the machine take the second where it can. Run it twice, once each way, in
 * the same sitting, and read the two numbers against each other. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kest.h"
#include "slots.h"

#define ENTITIES 10000
#define STEPS 100
#define ROUNDS 7

/* The same work `tools/frame.kest` measures, without the parts that are about
   saying what happened: one array of value structs, walked in order, handed
   one at a time to two helpers that each take one and give one back. */
static const char *const PROGRAM =
    "module tools.twoways\n"
    "\n"
    "const ENTITIES: i32 = 10000\n"
    "\n"
    "struct Npc {\n"
    "    x: f32\n"
    "    y: f32\n"
    "    dx: f32\n"
    "    dy: f32\n"
    "    health: i32\n"
    "}\n"
    "\n"
    "fn moved(one: Npc, dt: f32) -> Npc no.alloc {\n"
    "    return Npc(\n"
    "        one.x + one.dx * dt,\n"
    "        one.y + one.dy * dt,\n"
    "        one.dx,\n"
    "        one.dy,\n"
    "        one.health\n"
    "    )\n"
    "}\n"
    "\n"
    "fn turned(one: Npc) -> Npc no.alloc {\n"
    "    let dx = if one.x < 0.0 || one.x > 100.0 -> 0.0 - one.dx"
    " else -> one.dx\n"
    "    let dy = if one.y < 0.0 || one.y > 100.0 -> 0.0 - one.dy"
    " else -> one.dy\n"
    "    let health = if one.health > 0 -> one.health - 1 else -> ENTITIES\n"
    "    return Npc(one.x, one.y, dx, dy, health)\n"
    "}\n"
    "\n"
    "fn step(world: [Npc], dt: f32) -> i32 no.alloc {\n"
    "    let alive = 0\n"
    "    for i in 0..len(world) {\n"
    "        let one = turned(moved(world[i], dt))\n"
    "        world[i] = one\n"
    "        if one.health > 0 {\n"
    "            alive += 1\n"
    "        }\n"
    "    }\n"
    "    return alive\n"
    "}\n"
    "\n"
    "fn alone(world: [Npc], dt: f32) -> i32 no.alloc {\n"
    "    let alive = 0\n"
    "    for i in 0..len(world) {\n"
    "        let was = world[i]\n"
    "        let far = Npc(\n"
    "            was.x + was.dx * dt,\n"
    "            was.y + was.dy * dt,\n"
    "            was.dx,\n"
    "            was.dy,\n"
    "            was.health\n"
    "        )\n"
    "        let dx = if far.x < 0.0 || far.x > 100.0 -> 0.0 - far.dx"
    " else -> far.dx\n"
    "        let dy = if far.y < 0.0 || far.y > 100.0 -> 0.0 - far.dy"
    " else -> far.dy\n"
    "        let health = if far.health > 0 -> far.health - 1"
    " else -> ENTITIES\n"
    "        let one = Npc(far.x, far.y, dx, dy, health)\n"
    "        world[i] = one\n"
    "        if one.health > 0 {\n"
    "            alive += 1\n"
    "        }\n"
    "    }\n"
    "    return alive\n"
    "}\n"
    "\n"
    "fn counted(world: [Npc]) -> i32 no.alloc {\n"
    "    let sum = 0\n"
    "    for i in 0..len(world) {\n"
    "        let one = world[i]\n"
    "        if one.health > 50 {\n"
    "            sum += one.health\n"
    "        } else {\n"
    "            sum -= 1\n"
    "        }\n"
    "    }\n"
    "    return sum\n"
    "}\n"
    "\n"
    "fn main() -> i32 {\n"
    "    return 0\n"
    "}\n";

typedef struct {
    float x;
    float y;
    float dx;
    float dy;
    int32_t health;
} Npc;

static long long in_microseconds(void) {
    return (long long)clock() * 1000000 / CLOCKS_PER_SEC;
}

static void fill(Npc *world) {
    for (int i = 0; i < ENTITIES; i++) {
        world[i].x = (float)(i % 100);
        world[i].y = (float)((i * 7) % 100);
        world[i].dx = (float)((i % 5) - 2);
        world[i].dy = (float)((i % 3) - 1);
        world[i].health = 100 + (i % 50);
    }
}

int main(void) {
    const char *room = getenv("TMPDIR");
    char in[512];
    snprintf(in, sizeof in, "%s/kest-twoways-program.kest",
             room == NULL ? "." : room);
    FILE *writing = fopen(in, "w");
    if (writing == NULL || fputs(PROGRAM, writing) < 0 ||
        fclose(writing) != 0) {
        fprintf(stderr, "the program this measures could not be written\n");
        return 2;
    }
    KestBuild *build = kest_build(in, "lib/", stderr, KEST_FORM_TEXT, 0);
    remove(in);
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
    static const char *const WORK[] = {"step", "alone", "counted"};
    int32_t entries[3];
    for (int w = 0; w < 3; w++) {
        entries[w] = kest_entry(runtime, WORK[w]);
        if (entries[w] < 0) {
            fprintf(stderr, "the program this measures has no `%s`\n",
                    WORK[w]);
            return 2;
        }
    }

    Npc *world = malloc(sizeof(Npc) * ENTITIES);
    if (world == NULL) {
        return 2;
    }
    fill(world);
    KestValue lent = kest_borrow(runtime, world, ENTITIES, "Npc", sizeof(Npc));
    if (lent.object == NULL) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 2;
    }

    for (int w = 0; w < 3; w++) {
        int32_t stepping = entries[w];
        /* A round before the clock, so what is measured is the steady state.
           And the answer kept, because two machines that run the same work and
           answer differently have not run the same work. */
        KestValue frame[2];
        for (int s = 0; s < STEPS; s++) {
            frame[0] = lent;
            frame[1].real = (float)0.016;
            if (!kest_call(runtime, stepping, frame, 2)) {
                kest_report(runtime, stderr, KEST_FORM_TEXT);
                return 3;
            }
        }
        uint64_t was_stack = 0;
        uint64_t was_slots = 0;
        kest_slots_counted(&was_stack, &was_slots);
        long long best = 0;
        long long worst = 0;
        long long kept = 0;
        for (int round = 0; round < ROUNDS; round++) {
            fill(world);
            long long before = in_microseconds();
            long long sum = 0;
            for (int s = 0; s < STEPS; s++) {
                frame[0] = lent;
                frame[1].real = (float)0.016;
                if (!kest_call(runtime, stepping, frame, 2)) {
                    kest_report(runtime, stderr, KEST_FORM_TEXT);
                    return 3;
                }
                sum += frame[0].integer;
            }
            long long took = in_microseconds() - before;
            if (round == 0 || took < best) {
                best = took;
            }
            if (took > worst) {
                worst = took;
            }
            if (round == 0) {
                kept = sum;
            } else if (sum != kept) {
                fprintf(stderr, "two rounds of the same work answered %lld "
                                "and %lld\n",
                        kept, sum);
                return 3;
            }
        }
        uint64_t by_the_stack = 0;
        uint64_t by_the_slots = 0;
        kest_slots_counted(&by_the_stack, &by_the_slots);
        double each = (double)best * 1000.0 / (double)(STEPS * ENTITIES);
        int spread = best == 0 ? 0 : (int)((worst - best) * 100 / best);
        printf("%-8s %7.1f ns an entity a step on the %s machine, "
               "%llu instruction(s), answering %lld, best of %d, spread %d%%\n",
               WORK[w], each,
               getenv("KEST_SLOTS") != NULL ? "slot " : "stack",
               (unsigned long long)((by_the_stack - was_stack) +
                                    (by_the_slots - was_slots)),
               kept, ROUNDS, spread);
    }
    free(world);
    return 0;
}
