// What the task is judged by, which whoever does it does not see. This one is
// a host rather than a program: the task is about the doors an engine gives,
// so the thing that holds it has to be what is on the other side of them.
//
// Every door writes down what it was called with, so what the log holds
// afterwards is the order the calls were made in. Each check answers with its
// own number; nought is every check passing.
#include <stdio.h>
#include <string.h>

#include "kest.h"

#define MOST 32
#define LOGGED 256

// The engine this host is pretending to be: ids handed out from nought up,
// and a ceiling past which it will not make another.
typedef struct {
    bool alive[MOST];
    int32_t handed;
    int32_t ceiling;
    // A positive number is a spawn of that kind, a negative is a despawn of
    // the id one below its size, and nought is a count being asked for.
    int32_t log[LOGGED];
    int32_t said;
} Engine;

static int32_t living(const Engine *engine) {
    int32_t many = 0;
    for (int32_t i = 0; i < MOST; i++) {
        many += engine->alive[i] ? 1 : 0;
    }
    return many;
}

static void wrote(Engine *engine, int32_t what) {
    if (engine->said < LOGGED) {
        engine->log[engine->said++] = what;
    }
}

static void engine_spawn(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    Engine *engine = context;
    (void)runtime;
    int32_t kind = (int32_t)frame[0].integer;
    wrote(engine, kind > 0 ? kind : 1);
    if (living(engine) >= engine->ceiling || engine->handed >= MOST) {
        frame[0].integer = -1;
        return;
    }
    int32_t id = engine->handed++;
    engine->alive[id] = true;
    frame[0].integer = id;
}

static void engine_despawn(KestValue *frame, KestRuntime *runtime,
                           void *context) {
    Engine *engine = context;
    (void)runtime;
    int32_t id = (int32_t)frame[0].integer;
    wrote(engine, -(id + 1));
    bool was = id >= 0 && id < MOST && engine->alive[id];
    if (id >= 0 && id < MOST) {
        engine->alive[id] = false;
    }
    // A `bool` crosses as `integer`, nought or one: a member narrower than a
    // slot would leave the other bytes holding whatever was in them.
    frame[0].integer = was ? 1 : 0;
}

static void engine_alive(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    Engine *engine = context;
    (void)runtime;
    wrote(engine, 0);
    frame[0].integer = living(engine);
}

typedef struct {
    KestRuntime *runtime;
    int32_t settle;
    Engine engine;
    int32_t mine[MOST];
    int32_t answered;
} Asking;

// One run of `settle` against a world set up on purpose.
static bool settled(Asking *asking, int32_t kind, const int32_t *held,
                    int32_t many, int32_t room, int32_t wanted) {
    memset(asking->mine, 0, sizeof(asking->mine));
    for (int32_t i = 0; i < many; i++) {
        asking->mine[i] = held[i];
    }
    KestValue lent = kest_borrow(asking->runtime, asking->mine,
                                 (uint32_t)room, "i32",
                                 sizeof(asking->mine[0]));
    if (lent.object == NULL) {
        kest_report(asking->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    KestValue frame[8] = {{0}};
    frame[0].integer = kind;
    frame[1] = lent;
    frame[2].integer = many;
    frame[3].integer = wanted;
    if (!kest_call(asking->runtime, asking->settle, frame, 8)) {
        kest_report(asking->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    asking->answered = (int32_t)frame[0].integer;
    return kest_lend_ends(asking->runtime, lent);
}

static bool logged(const Engine *engine, const int32_t *wanted, int32_t many) {
    if (engine->said != many) {
        return false;
    }
    for (int32_t i = 0; i < many; i++) {
        if (engine->log[i] != wanted[i]) {
            return false;
        }
    }
    return true;
}

static void world(Asking *asking, int32_t alive, int32_t ceiling) {
    memset(&asking->engine, 0, sizeof(asking->engine));
    asking->engine.ceiling = ceiling;
    for (int32_t i = 0; i < alive; i++) {
        asking->engine.alive[i] = true;
        asking->engine.handed = i + 1;
    }
}

static int checks(Asking *asking) {
    // Nothing alive, three wanted, and room for them: three spawns after the
    // one question, the ids written down in order, and three alive.
    world(asking, 0, MOST);
    if (!settled(asking, 7, NULL, 0, 8, 3)) {
        return 1;
    }
    {
        const int32_t wanted[] = {0, 7, 7, 7};
        if (!logged(&asking->engine, wanted, 4)) {
            return 2;
        }
    }
    if (asking->answered != 3 || living(&asking->engine) != 3) {
        return 3;
    }
    if (asking->mine[0] != 0 || asking->mine[1] != 1 || asking->mine[2] != 2) {
        return 4;
    }

    // Already there: nothing but the one question.
    world(asking, 2, MOST);
    {
        const int32_t held[] = {0, 1};
        if (!settled(asking, 7, held, 2, 8, 2)) {
            return 5;
        }
    }
    {
        const int32_t wanted[] = {0};
        if (!logged(&asking->engine, wanted, 1)) {
            return 6;
        }
    }
    if (asking->answered != 2 || living(&asking->engine) != 2) {
        return 7;
    }

    // Too many: the last one this program is holding goes first.
    world(asking, 3, MOST);
    {
        const int32_t held[] = {0, 1, 2};
        if (!settled(asking, 7, held, 3, 8, 1)) {
            return 8;
        }
    }
    {
        const int32_t wanted[] = {0, -3, -2};
        if (!logged(&asking->engine, wanted, 3)) {
            return 9;
        }
    }
    if (asking->answered != 1 || living(&asking->engine) != 1) {
        return 10;
    }

    // An engine that will not make another: it is asked once and answers
    // that it made nothing, and asking again would answer the same.
    world(asking, 2, 2);
    {
        const int32_t held[] = {0, 1};
        if (!settled(asking, 7, held, 2, 8, 5)) {
            return 11;
        }
    }
    {
        const int32_t wanted[] = {0, 7};
        if (!logged(&asking->engine, wanted, 2)) {
            return 12;
        }
    }
    if (asking->answered != 2 || living(&asking->engine) != 2) {
        return 13;
    }

    // An id this program is holding that the engine has already lost: the
    // answer says so, and it is not one alive fewer.
    world(asking, 3, MOST);
    asking->engine.alive[2] = false;
    {
        const int32_t held[] = {0, 1, 2};
        if (!settled(asking, 7, held, 3, 8, 1)) {
            return 14;
        }
    }
    {
        const int32_t wanted[] = {0, -3, -2};
        if (!logged(&asking->engine, wanted, 3)) {
            return 15;
        }
    }
    if (living(&asking->engine) != 1 || asking->answered != 1) {
        return 16;
    }

    // And a run with no room left in it: what it is holding is all it can.
    world(asking, 2, MOST);
    {
        const int32_t held[] = {0, 1};
        if (!settled(asking, 7, held, 2, 2, 5)) {
            return 17;
        }
    }
    {
        const int32_t wanted[] = {0};
        if (!logged(&asking->engine, wanted, 1)) {
            return 18;
        }
    }
    if (asking->answered != 2 || living(&asking->engine) != 2) {
        return 19;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: host <the answer>\n");
        return 2;
    }
    KestBuild *build = kest_build(argv[1], NULL, stderr, KEST_FORM_TEXT, 0);
    if (build == NULL) {
        return 2;
    }
    Asking asking;
    memset(&asking, 0, sizeof(asking));
    KestHost *host = kest_host_new();
    if (host == NULL ||
        !kest_host_bind(host, "Engine.spawn", engine_spawn, &asking.engine) ||
        !kest_host_bind(host, "Engine.despawn", engine_despawn,
                        &asking.engine) ||
        !kest_host_bind(host, "Engine.alive", engine_alive, &asking.engine)) {
        fprintf(stderr, "this host could not bind its own doors\n");
        return 2;
    }
    asking.runtime = kest_start(build, host, NULL);
    kest_host_free(host);
    if (asking.runtime == NULL) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 2;
    }
    asking.settle = kest_entry(asking.runtime, "settle");
    if (asking.settle < 0) {
        fprintf(stderr, "the program has no `settle` to call\n");
        return 2;
    }
    printf("checks %d\n", checks(&asking));
    kest_runtime_free(asking.runtime);
    kest_build_free(build);
    return 0;
}
