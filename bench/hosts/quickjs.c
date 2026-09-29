// `bench/hosts` through QuickJS's C API: the frame over the host's run of
// doubles lent as an `ArrayBuffer`, a crossing a body, and the program asking
// its host, over `bodies.js`. QuickJS has an interpreter and no compiler to
// machine code, so there is one line of each. See D1272.
//
//   quickjs bench/hosts/bodies.js [bench/hosts/churn.js]
#include <stdatomic.h>
#include <string.h>

#include "common.h"
#include "quickjs.h"

static JSValue add(JSContext *ctx, JSValueConst self, int argc,
                   JSValueConst *argv) {
    (void)self;
    (void)argc;
    double a = 0.0;
    double b = 0.0;
    JS_ToFloat64(ctx, &a, argv[0]);
    JS_ToFloat64(ctx, &b, argv[1]);
    return JS_NewFloat64(ctx, a + b);
}

static void refused(JSContext *ctx, const char *what) {
    JSValue error = JS_GetException(ctx);
    const char *said = JS_ToCString(ctx, error);
    fprintf(stderr, "%s: %s\n", what, said != NULL ? said : "?");
    exit(1);
}

static JSValue global(JSContext *ctx, const char *name) {
    JSValue all = JS_GetGlobalObject(ctx);
    JSValue found = JS_GetPropertyStr(ctx, all, name);
    JS_FreeValue(ctx, all);
    return found;
}

static double number(JSContext *ctx, JSValue value, const char *what) {
    if (JS_IsException(value)) {
        refused(ctx, what);
    }
    double answer = 0.0;
    JS_ToFloat64(ctx, &answer, value);
    JS_FreeValue(ctx, value);
    return answer;
}

typedef struct {
    JSContext *ctx;
    JSValue frame;
} Churn;

static double churn_frame(void *context) {
    Churn *churn = context;
    JSValue args[1] = {JS_NewInt32(churn->ctx, REPLACED)};
    return number(churn->ctx,
                  JS_Call(churn->ctx, churn->frame, JS_UNDEFINED, 1, args),
                  "frame");
}

// The world in `churn.js`, in a runtime of its own so that what the collector
// walks is that world and nothing else.
static void churns(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        exit(2);
    }
    static char source[1 << 16];
    size_t length = fread(source, 1, sizeof source - 1, file);
    fclose(file);
    source[length] = '\0';
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    JSValue ran = JS_Eval(ctx, source, length, path, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ran)) {
        refused(ctx, "reading the world");
    }
    JS_FreeValue(ctx, ran);
    JSValue begin = global(ctx, "begin");
    JSValue args[1] = {JS_NewInt32(ctx, THINGS)};
    JS_FreeValue(ctx, JS_Call(ctx, begin, JS_UNDEFINED, 1, args));
    Churn churn = {ctx, global(ctx, "frame")};
    churned("QuickJS", churn_frame, &churn);
    JS_FreeValue(ctx, churn.frame);
    JS_FreeValue(ctx, begin);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

typedef struct {
    JSRuntime *rt;
    JSContext *ctx;
    JSValue step;
    JSValue memory;
    Body *world;
} Lane_state;

static void *lane_make(void *context) {
    const char *source = context;
    Lane_state *lane = malloc(sizeof *lane);
    lane->rt = JS_NewRuntime();
    lane->ctx = JS_NewContext(lane->rt);
    JSValue all = JS_GetGlobalObject(lane->ctx);
    JS_SetPropertyStr(lane->ctx, all, "add",
                      JS_NewCFunction(lane->ctx, add, "add", 2));
    JS_FreeValue(lane->ctx, all);
    JSValue ran = JS_Eval(lane->ctx, source, strlen(source), "bodies.js",
                          JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ran)) {
        refused(lane->ctx, "reading the program");
    }
    JS_FreeValue(lane->ctx, ran);
    lane->step = global(lane->ctx, "step");
    lane->world = malloc(sizeof *lane->world * BODIES);
    fill(lane->world, BODIES);
    size_t bytes = sizeof *lane->world * BODIES;
    lane->memory = JS_NewArrayBuffer(lane->ctx, (uint8_t *)lane->world, bytes,
                                     0, NULL, NULL, false);
    return lane;
}

static double lane_frame(void *state) {
    Lane_state *lane = state;
    // Where the stack is, which QuickJS measures from the thread that made the
    // runtime, and a lane runs on another.
    JS_UpdateStackTop(lane->rt);
    JSValue args[3] = {lane->memory, JS_NewInt32(lane->ctx, BODIES),
                       JS_NewFloat64(lane->ctx, WALL)};
    return number(lane->ctx,
                  JS_Call(lane->ctx, lane->step, JS_UNDEFINED, 3, args),
                  "step");
}

static void lane_drop(void *state) {
    Lane_state *lane = state;
    JS_FreeValue(lane->ctx, lane->memory);
    JS_FreeValue(lane->ctx, lane->step);
    JS_FreeContext(lane->ctx);
    JS_FreeRuntime(lane->rt);
    free(lane->world);
    free(lane);
}

// What a sandbox made of QuickJS stops a program with: the handler it calls
// every so many operations, which answers non-nought once the host has asked.
static atomic_bool asked;

static int interrupted(JSRuntime *rt, void *opaque) {
    (void)rt;
    (void)opaque;
    return atomic_load_explicit(&asked, memory_order_relaxed) ? 1 : 0;
}

static void *spin_make(void *context) {
    Lane_state *lane = lane_make(context);
    JS_SetInterruptHandler(lane->rt, interrupted, NULL);
    return lane;
}

static void spin_run(void *state) {
    Lane_state *lane = state;
    JSValue spin = global(lane->ctx, "spin");
    JSValue args[1] = {JS_NewInt32(lane->ctx, 0)};
    JSValue back = JS_Call(lane->ctx, spin, JS_UNDEFINED, 1, args);
    if (!JS_IsException(back)) {
        fprintf(stderr, "spin came back without being stopped\n");
        exit(1);
    }
    JS_FreeValue(lane->ctx, JS_GetException(lane->ctx));
    JS_FreeValue(lane->ctx, spin);
}

static void spin_ask(void *state) {
    (void)state;
    atomic_store_explicit(&asked, true, memory_order_relaxed);
}

static void spin_again(void *state) {
    (void)state;
    atomic_store_explicit(&asked, false, memory_order_relaxed);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: quickjs bodies.js\n");
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror(argv[1]);
        return 2;
    }
    static char source[1 << 16];
    size_t length = fread(source, 1, sizeof source - 1, file);
    fclose(file);
    source[length] = '\0';

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    JSValue all = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, all, "add", JS_NewCFunction(ctx, add, "add", 2));
    JS_FreeValue(ctx, all);
    JSValue ran = JS_Eval(ctx, source, length, argv[1], JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ran)) {
        refused(ctx, "reading the program");
    }
    JS_FreeValue(ctx, ran);

    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(rt, &usage);
    said("QuickJS", "memory", (double)usage.malloc_size, 0.0);

    JSValue step = global(ctx, "step");
    JSValue one = global(ctx, "one");
    JSValue asks = global(ctx, "asks");
    Body *world = malloc(sizeof *world * BODIES);
    long long took[FRAMES];
    double sum = 0.0;

    fill(world, BODIES);
    size_t bytes = sizeof *world * BODIES;
    JSValue memory = JS_NewArrayBuffer(ctx, (uint8_t *)world, bytes, 0,
                                       NULL, NULL, false);
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        JSValue args[3] = {memory, JS_NewInt32(ctx, BODIES),
                           JS_NewFloat64(ctx, WALL)};
        sum = number(ctx, JS_Call(ctx, step, JS_UNDEFINED, 3, args), "step");
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    said("QuickJS", "frame", (double)middle_of(took, FRAMES) / BODIES, sum);

    // The same frame with the interrupt handler a sandbox needs installed.
    JS_SetInterruptHandler(rt, interrupted, NULL);
    fill(world, BODIES);
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        JSValue args[3] = {memory, JS_NewInt32(ctx, BODIES),
                           JS_NewFloat64(ctx, WALL)};
        sum = number(ctx, JS_Call(ctx, step, JS_UNDEFINED, 3, args), "step");
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    JS_SetInterruptHandler(rt, NULL, NULL);
    said("QuickJS", "budget", (double)middle_of(took, FRAMES) / BODIES, sum);

    fill(world, BODIES);
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        sum = 0.0;
        for (int i = 0; i < BODIES; i++) {
            JSValue args[5] = {
                JS_NewFloat64(ctx, world[i].x), JS_NewFloat64(ctx, world[i].y),
                JS_NewFloat64(ctx, world[i].dx),
                JS_NewFloat64(ctx, world[i].dy), JS_NewFloat64(ctx, WALL)};
            JSValue back = JS_Call(ctx, one, JS_UNDEFINED, 5, args);
            if (JS_IsException(back)) {
                refused(ctx, "one");
            }
            double *field[4] = {&world[i].x, &world[i].y, &world[i].dx,
                                &world[i].dy};
            for (int k = 0; k < 4; k++) {
                JSValue got = JS_GetPropertyUint32(ctx, back, (uint32_t)k);
                JS_ToFloat64(ctx, field[k], got);
                JS_FreeValue(ctx, got);
            }
            JS_FreeValue(ctx, back);
            sum += world[i].x + world[i].y;
        }
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    said("QuickJS", "body", (double)middle_of(took, FRAMES) / BODIES, sum);

    long long asked[9];
    double answer = 0.0;
    for (int r = 0; r < 9; r++) {
        long long before = now_ns();
        JSValue args[1] = {JS_NewInt32(ctx, ASKS)};
        answer = number(ctx, JS_Call(ctx, asks, JS_UNDEFINED, 1, args),
                        "asks");
        asked[r] = now_ns() - before;
    }
    said("QuickJS", "ask", (double)middle_of(asked, 9) / ASKS, answer);

    JS_FreeValue(ctx, memory);
    JS_FreeValue(ctx, step);
    JS_FreeValue(ctx, one);
    JS_FreeValue(ctx, asks);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    free(world);
    if (argc > 2) {
        churns(argv[2]);
    }
    Stoppable stops = {spin_make, spin_run, spin_ask, spin_again, source};
    stopped("QuickJS", &stops);
    Scaled lanes = {lane_make, lane_frame, lane_drop, source};
    scaled("QuickJS", &lanes);
    return 0;
}
