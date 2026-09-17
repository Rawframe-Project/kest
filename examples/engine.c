// An engine, in the shape an engine has: build a program, start a machine,
// ask it for a world, and then drive that world one frame at a time through
// memory this host owns. It ends by reloading the program from source without
// losing the world it had.
//
// `examples/embed.c` is the other host here and is a different thing: it asks
// every door this language has, one after another, and is long because the
// list is. This one asks the doors a frame loop uses, in the order a frame
// loop uses them, and is meant to be read start to finish. See D949.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kest.h"

#define BODIES 8
#define FRAMES 60

// The world is two slots -- a store handle and a number -- and this host keeps
// them between frames the way an engine keeps a scene. They are the machine's
// memory and not this host's: they are good for as long as the machine is, and
// a reload is where that stops being true.
typedef struct {
    KestBuild *build;
    KestRuntime *runtime;
    KestValue world[2];
    int32_t begin;
    int32_t step;
    int32_t place;
    int32_t save;
    int32_t restore;
    int32_t round;
    int32_t watched;
} Engine;

// What this host saved of a world, which is numbers and nothing else. Which
// body another one chases is that body's own number: a reference is a world, a
// stamp and a place in the machine that made it, and none of the three means
// anything in the machine that reads this back.
typedef struct {
    int32_t ids[BODIES];
    float xs[BODIES];
    float ys[BODIES];
    float vxs[BODIES];
    float vys[BODIES];
    int32_t chases[BODIES];
    uint64_t shaped;
    int32_t count;
} Saved;

// The program calls this once a frame and this host calls back in from inside
// it, which is a crossing out and a crossing in with the first still on the
// stack. What the reentrant call costs is the machine's to account for, and
// what this host has to get right is that the frame it hands in is its own.
static Engine *driving = NULL;

static void engine_watch(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    (void)context;
    // What this door costs the program. The machine cannot see anything a host
    // does, so a budget that is not told about it is a budget with a hole in
    // it the size of every crossing. This one walks the world it is watching,
    // so it charges for what that came to.
    kest_fuel_spend(runtime, BODIES * sizeof(float));
    frame[0].integer = 0;
    if (driving == NULL || driving->round < 0) {
        return;
    }
    KestValue inner[4] = {{0}};
    inner[0] = driving->world[0];
    inner[1] = driving->world[1];
    inner[2].integer = 0;
    if (!kest_call(runtime, driving->round, inner, 4)) {
        // A call that refuses from inside a host function is the host's to
        // notice: the program is not told, and a host that carries on is a
        // host answering with whatever was in the frame.
        kest_native_failed(runtime, "the ring could not be walked from inside "
                                    "a host function");
        return;
    }
    frame[0].integer = inner[0].integer;
}

// And the other half of that door: what a host says when it cannot do what it
// was asked. The program does not carry on with a number that means nothing --
// the call refuses at the instruction that made it, with these words under it.
static void engine_refuse(KestValue *frame, KestRuntime *runtime,
                          void *context) {
    (void)context;
    (void)frame;
    kest_native_failed(runtime, "this engine has no such thing to give");
}

static bool entries(Engine *engine) {
    const struct {
        const char *name;
        int32_t *into;
    } doors[] = {{"begin", &engine->begin},     {"step", &engine->step},
                 {"place", &engine->place},     {"save", &engine->save},
                 {"restore", &engine->restore}, {"round", &engine->round},
                 {"watched", &engine->watched}};
    for (size_t i = 0; i < sizeof(doors) / sizeof(doors[0]); i++) {
        *doors[i].into = kest_entry(engine->runtime, doors[i].name);
        if (*doors[i].into < 0) {
            fprintf(stderr, "the program has no `%s` to call\n",
                    doors[i].name);
            return false;
        }
    }
    return true;
}

// A machine over a build, with this engine's own doors bound. Two of these run
// at once further down, which is what says a build is read-only once it is
// built: what a machine changes is its own.
static KestRuntime *started(KestBuild *build) {
    KestHost *host = kest_host_new();
    if (host == NULL) {
        return NULL;
    }
    if (!kest_host_bind(host, "Engine.watch", engine_watch, NULL) ||
        !kest_host_bind(host, "Engine.refuse", engine_refuse, NULL)) {
        kest_host_free(host);
        return NULL;
    }
    KestRuntime *runtime = kest_start(build, host, NULL);
    kest_host_free(host);
    return runtime;
}

// The frame step promises all three things this language can prove about a
// body: it reaches no heap, it calls nothing of this host's, and it answers
// the same on every machine that keeps the simulation profile. An engine reads
// those before it installs a step, because a step that may allocate belongs
// somewhere other than a frame and one that may call back in cannot run while
// this host holds its own lock.
static bool worth_installing(Engine *engine) {
    const struct {
        KestPromise which;
        const char *called;
    } wanted[] = {{KEST_PROMISE_NO_ALLOC, "no.alloc"},
                  {KEST_PROMISE_NO_HOST, "no.host"},
                  {KEST_PROMISE_DETERMINISTIC, "deterministic"}};
    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
        if (!kest_entry_promises(engine->runtime, engine->step,
                                 wanted[i].which)) {
            fprintf(stderr, "`step` does not promise `%s`, so this engine will "
                            "not put it in a frame\n", wanted[i].called);
            return false;
        }
    }
    return true;
}

// One frame: the step, and every eighth frame this host's own memory filled in
// place. Nothing is copied across the boundary either way -- the lend is the
// engine's block with a header over it, and it ends when the frame does.
static bool one_frame(Engine *engine, int frame, float xs[BODIES],
                      float ys[BODIES]) {
    KestValue call[8] = {{0}};
    call[0] = engine->world[0];
    call[1] = engine->world[1];
    call[2].real = (double)(float)(1.0 / 60.0);
    if (!kest_call(engine->runtime, engine->step, call, 8)) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    if (call[0].integer != BODIES) {
        fprintf(stderr, "frame %d moved %lld of %d bodies\n", frame,
                (long long)call[0].integer, BODIES);
        return false;
    }
    if (frame % 8 != 0) {
        return true;
    }

    KestValue here = kest_borrow(engine->runtime, xs, BODIES, "f32",
                                 sizeof(float));
    KestValue there = kest_borrow(engine->runtime, ys, BODIES, "f32",
                                  sizeof(float));
    bool lent = here.object != NULL && there.object != NULL;
    if (lent) {
        KestValue asking[8] = {{0}};
        asking[0] = engine->world[0];
        asking[1] = engine->world[1];
        asking[2] = here;
        asking[3] = there;
        lent = kest_call(engine->runtime, engine->place, asking, 8) &&
               asking[0].integer == BODIES;
    }
    // Ended whatever happened, because a lend this host does not end is a
    // handle the program holds over a block this host is about to move on
    // from.
    if (here.object != NULL && !kest_lend_ends(engine->runtime, here)) {
        lent = false;
    }
    if (there.object != NULL && !kest_lend_ends(engine->runtime, there)) {
        lent = false;
    }
    if (!lent) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "frame %d could not read the world into this "
                        "engine's own memory\n", frame);
        return false;
    }
    return true;
}

// A budget, and what a machine that ran out of one is: not broken and not
// finished. Everything the program built is where it was, and a host that
// gives it fuel again carries on from the next frame.
//
// Four is a small number of steps and a frame over eight bodies is more than
// four, which is all a host needs to know to write this: what a step is is the
// machine's business and what a budget buys is written down.
static bool a_budget_and_a_stop(Engine *engine) {
    kest_fuel_set(engine->runtime, 4);
    KestValue call[8] = {{0}};
    call[0] = engine->world[0];
    call[1] = engine->world[1];
    call[2].real = (double)(float)(1.0 / 60.0);
    if (kest_call(engine->runtime, engine->step, call, 8)) {
        fprintf(stderr, "a frame ran to the end on four steps of budget\n");
        return false;
    }
    kest_fuel_set(engine->runtime, KEST_FUEL_UNLIMITED);

    // And the other half of the same mechanism: a host asking a running
    // program to stop. A machine stays cancelled until it is given fuel.
    kest_cancel(engine->runtime);
    if (!kest_cancelled(engine->runtime)) {
        fprintf(stderr, "a cancelled machine says it was not asked\n");
        return false;
    }
    KestValue again[8] = {{0}};
    again[0] = engine->world[0];
    again[1] = engine->world[1];
    again[2].real = (double)(float)(1.0 / 60.0);
    if (kest_call(engine->runtime, engine->step, again, 8)) {
        fprintf(stderr, "a cancelled machine ran a frame\n");
        return false;
    }
    kest_fuel_set(engine->runtime, KEST_FUEL_UNLIMITED);
    if (kest_cancelled(engine->runtime)) {
        fprintf(stderr, "a machine given fuel is still cancelled\n");
        return false;
    }
    // And the frame that comes after all that, which is the point of both:
    // the world is where it was, so an engine that was interrupted carries on
    // rather than starting again.
    KestValue after[8] = {{0}};
    after[0] = engine->world[0];
    after[1] = engine->world[1];
    after[2].real = (double)(float)(1.0 / 60.0);
    if (!kest_call(engine->runtime, engine->step, after, 8) ||
        after[0].integer != BODIES) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "the frame after a stop moved %lld of %d bodies\n",
                (long long)after[0].integer, BODIES);
        return false;
    }
    return true;
}

// What this host saves, and the shape it saved it as. A number for the shape
// rather than a walk of its pieces, because every host doing this would write
// that walk and two of them would write it differently.
static bool save_the_world(Engine *engine, Saved *saved) {
    const KestLayout *shape = NULL;
    if (kest_build_layout(engine->build, "Body", &shape) != 1) {
        fprintf(stderr, "the program has no one `Body` to save\n");
        return false;
    }
    saved->shaped = kest_layout_mark(shape);

    KestValue lent[6];
    void *blocks[6] = {saved->ids, saved->xs,  saved->ys,
                       saved->vxs, saved->vys, saved->chases};
    const char *of[6] = {"i32", "f32", "f32", "f32", "f32", "i32"};
    size_t wide[6] = {sizeof(int32_t), sizeof(float), sizeof(float),
                      sizeof(float), sizeof(float), sizeof(int32_t)};
    for (size_t i = 0; i < 6; i++) {
        lent[i] = kest_borrow(engine->runtime, blocks[i], BODIES, of[i],
                              wide[i]);
    }
    KestValue asking[16] = {{0}};
    asking[0] = engine->world[0];
    asking[1] = engine->world[1];
    bool wrote = true;
    for (size_t i = 0; i < 6; i++) {
        if (lent[i].object == NULL) {
            wrote = false;
        }
        asking[2 + i] = lent[i];
    }
    wrote = wrote && kest_call(engine->runtime, engine->save, asking, 16);
    saved->count = wrote ? (int32_t)asking[0].integer : 0;
    for (size_t i = 0; i < 6; i++) {
        if (lent[i].object != NULL && !kest_lend_ends(engine->runtime,
                                                      lent[i])) {
            wrote = false;
        }
    }
    if (!wrote || saved->count != BODIES) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "this engine saved %d of %d bodies\n", saved->count,
                BODIES);
        return false;
    }
    return true;
}

// And a world made again in a machine that has never seen the one it came
// from. The candidate is built and filled beside the old one, which is still
// running and still answering: nothing is published until this comes back
// true, and a host that cannot make the new world keeps the one it has.
static bool restore_into(KestRuntime *into, int32_t restore, int32_t round,
                         const Saved *saved, KestValue world[2]) {
    KestValue lent[6];
    const void *blocks[6] = {saved->ids, saved->xs,  saved->ys,
                             saved->vxs, saved->vys, saved->chases};
    const char *of[6] = {"i32", "f32", "f32", "f32", "f32", "i32"};
    size_t wide[6] = {sizeof(int32_t), sizeof(float), sizeof(float),
                      sizeof(float), sizeof(float), sizeof(int32_t)};
    KestValue asking[16] = {{0}};
    bool made = true;
    for (size_t i = 0; i < 6; i++) {
        // The block is this host's and the program only reads it, so the cast
        // is this host saying so: a lend is an address and a count, and it is
        // the host that knows which way the memory goes.
        lent[i] = kest_borrow(into, (void *)(uintptr_t)blocks[i], BODIES,
                              of[i], wide[i]);
        if (lent[i].object == NULL) {
            made = false;
        }
        asking[i] = lent[i];
    }
    made = made && kest_call(into, restore, asking, 16);
    if (made) {
        world[0] = asking[0];
        world[1] = asking[1];
    }
    for (size_t i = 0; i < 6; i++) {
        if (lent[i].object != NULL && !kest_lend_ends(into, lent[i])) {
            made = false;
        }
    }
    if (!made) {
        kest_report(into, stderr, KEST_FORM_TEXT);
        return false;
    }
    // The one question a save either answers or loses: the bodies name each
    // other in a ring, and a walk of it in the new machine has to come back
    // round in as many steps as there are bodies.
    KestValue walk[8] = {{0}};
    walk[0] = world[0];
    walk[1] = world[1];
    walk[2].integer = 0;
    if (!kest_call(into, round, walk, 8) || walk[0].integer != BODIES) {
        kest_report(into, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "the ring came back round in %lld steps of %d\n",
                (long long)walk[0].integer, BODIES);
        return false;
    }
    return true;
}

// The whole of a reload, in the order a reload happens: read the file again,
// build it beside the one that is running, refuse if the shape the world was
// saved as is not the shape the new program has, make the world again in the
// new machine, and only then let go of the old one. Nothing here touches the
// running world until the candidate has answered.
static bool reload(Engine *engine, const char *path, const Saved *saved) {
    KestBuild *candidate = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
    if (candidate == NULL) {
        fprintf(stderr, "a reload would not build; keeping the world\n");
        return false;
    }
    const KestLayout *shape = NULL;
    if (kest_build_layout(candidate, "Body", &shape) != 1 ||
        kest_layout_mark(shape) != saved->shaped) {
        fprintf(stderr, "`Body` is a different shape after a reload, and this "
                        "engine has no migration for it\n");
        kest_build_free(candidate);
        return false;
    }
    KestRuntime *fresh = started(candidate);
    if (fresh == NULL) {
        kest_build_report(candidate, stderr, KEST_FORM_TEXT);
        kest_build_free(candidate);
        return false;
    }
    int32_t restore = kest_entry(fresh, "restore");
    int32_t round = kest_entry(fresh, "round");
    KestValue world[2] = {{0}, {0}};
    if (restore < 0 || round < 0 ||
        !restore_into(fresh, restore, round, saved, world)) {
        // The candidate is discarded whole and the old machine has not been
        // touched: this host is exactly where it was before it tried.
        kest_runtime_free(fresh);
        kest_build_free(candidate);
        return false;
    }

    // Published. The old machine and the old build go now and not before, and
    // every handle this host held into the old machine goes with them.
    kest_runtime_free(engine->runtime);
    kest_build_free(engine->build);
    engine->build = candidate;
    engine->runtime = fresh;
    engine->world[0] = world[0];
    engine->world[1] = world[1];
    return entries(engine) && worth_installing(engine);
}

// A world in one machine is not a world in another: a reference carries which
// machine made it, and a machine that did not make it will not follow it. This
// is that, asked on purpose, because a host that finds out by accident finds
// out by reading the wrong object.
static bool worlds_stay_apart(Engine *engine, const Saved *saved) {
    KestRuntime *other = started(engine->build);
    if (other == NULL) {
        kest_build_report(engine->build, stderr, KEST_FORM_TEXT);
        return false;
    }
    int32_t restore = kest_entry(other, "restore");
    int32_t round = kest_entry(other, "round");
    KestValue world[2] = {{0}, {0}};
    bool made = restore >= 0 && round >= 0 &&
                restore_into(other, restore, round, saved, world);

    // And the handle from the first machine, asked of the second. What comes
    // back is a refusal rather than an object of somebody else's.
    if (made) {
        KestValue walk[8] = {{0}};
        walk[0] = engine->world[0];
        walk[1] = engine->world[1];
        walk[2].integer = 0;
        if (kest_call(other, round, walk, 8) && walk[0].integer == BODIES) {
            fprintf(stderr, "one machine walked another machine's world\n");
            made = false;
        }
    }
    kest_runtime_free(other);
    return made;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "examples/engine.kest";
    Engine engine = {NULL, NULL, {{0}, {0}}, -1, -1, -1, -1, -1, -1, -1};
    engine.build = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
    if (engine.build == NULL) {
        return 1;
    }
    engine.runtime = started(engine.build);
    if (engine.runtime == NULL) {
        kest_build_report(engine.build, stderr, KEST_FORM_TEXT);
        kest_build_free(engine.build);
        return 1;
    }
    driving = &engine;
    if (!entries(&engine) || !worth_installing(&engine)) {
        return 1;
    }

    KestValue asking[8] = {{0}};
    asking[0].integer = BODIES;
    if (!kest_call(engine.runtime, engine.begin, asking, 8)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.world[0] = asking[0];
    engine.world[1] = asking[1];

    float xs[BODIES];
    float ys[BODIES];
    memset(xs, 0, sizeof(xs));
    memset(ys, 0, sizeof(ys));
    for (int frame = 0; frame < FRAMES; frame++) {
        if (!one_frame(&engine, frame, xs, ys)) {
            return 1;
        }
    }
    printf("%d frames of %d bodies, and the first is at %.3f %.3f\n", FRAMES,
           BODIES, (double)xs[0], (double)ys[0]);

    // A crossing out and a crossing back in from inside it, which is the one
    // shape a host has to get right before it calls anything from a bound
    // function.
    KestValue crossing[8] = {{0}};
    crossing[0] = engine.world[0];
    crossing[1] = engine.world[1];
    crossing[2].integer = FRAMES;
    if (!kest_call(engine.runtime, engine.watched, crossing, 8) ||
        crossing[0].integer != BODIES + BODIES) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "a crossing back in from a host function answered "
                        "%lld\n", (long long)crossing[0].integer);
        return 1;
    }

    // And a door this host refuses at, which the program does not carry on
    // from.
    int32_t refused = kest_entry(engine.runtime, "refused");
    KestValue no[8] = {{0}};
    no[0].integer = 7;
    if (refused < 0 || kest_call(engine.runtime, refused, no, 8)) {
        fprintf(stderr, "a host that refused was read as an answer\n");
        return 1;
    }

    if (!a_budget_and_a_stop(&engine)) {
        return 1;
    }

    Saved saved;
    memset(&saved, 0, sizeof(saved));
    if (!save_the_world(&engine, &saved)) {
        return 1;
    }
    if (!worlds_stay_apart(&engine, &saved)) {
        return 1;
    }
    if (!reload(&engine, path, &saved)) {
        return 1;
    }
    for (int frame = 0; frame < FRAMES; frame++) {
        if (!one_frame(&engine, frame, xs, ys)) {
            return 1;
        }
    }
    printf("a reload kept the ring, and %d frames later the first body is at "
           "%.3f %.3f\n", FRAMES, (double)xs[0], (double)ys[0]);

    driving = NULL;
    if (!kest_runtime_free(engine.runtime) || !kest_build_free(engine.build)) {
        fprintf(stderr, "this engine could not give back what it had\n");
        return 1;
    }
    return 0;
}
