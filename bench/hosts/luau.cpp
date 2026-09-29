// `bench/hosts` through Luau's C API: the program compiled at -O2 and loaded,
// and run by its interpreter or, with `native`, by its native tier. The frame
// is over the program's own tables, and a crossing a body and the program
// asking its host are the same calls every Lua takes. Built twice, with the
// native tier (`-DNATIVE`) and without, so what each adds to an executable is
// said apart. See D1272.
//
//   luau <engine name> bench/hosts/bodies.lua [--native]
//       [--churn bench/hosts/churn.lua]
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include "common.h"
}
#include "luacode.h"
#if defined(NATIVE)
#include "luacodegen.h"
#endif
#include "lualib.h"
#include "lua.h"

static int add(lua_State *L) {
    double a = lua_tonumber(L, 1);
    double b = lua_tonumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1;
}

static void refused(lua_State *L, const char *what) {
    fprintf(stderr, "%s: %s\n", what, lua_tostring(L, -1));
    exit(1);
}

static void called(lua_State *L, int in, int out, const char *what) {
    if (lua_pcall(L, in, out, 0) != 0) {
        refused(L, what);
    }
}

static std::vector<char> read_all(const char *path) {
    std::vector<char> source;
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        exit(2);
    }
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) {
        source.insert(source.end(), chunk, chunk + got);
    }
    fclose(file);
    return source;
}

// A state of its own with `path` compiled at -O2 and run in it, and its
// functions compiled to machine code where `native` asks.
static lua_State *loaded(const char *path, bool native) {
    std::vector<char> source = read_all(path);
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
#if defined(NATIVE)
    if (native) {
        if (!luau_codegen_supported()) {
            fprintf(stderr, "this machine has no native tier\n");
            exit(2);
        }
        luau_codegen_create(L);
    }
#else
    if (native) {
        fprintf(stderr, "this host was built without the native tier\n");
        exit(2);
    }
#endif
    lua_pushcfunction(L, add, "add");
    lua_setglobal(L, "add");
    lua_CompileOptions options = {};
    options.optimizationLevel = 2;
    size_t size = 0;
    char *bytecode =
        luau_compile(source.data(), source.size(), &options, &size);
    if (luau_load(L, path, bytecode, size, 0) != 0) {
        refused(L, "reading the program");
    }
    free(bytecode);
#if defined(NATIVE)
    if (native) {
        luau_codegen_compile(L, -1);
    }
#endif
    called(L, 0, 0, "running the program");
    return L;
}

static double churn_frame(void *context) {
    lua_State *L = (lua_State *)context;
    lua_getglobal(L, "frame");
    lua_pushinteger(L, REPLACED);
    called(L, 1, 1, "frame");
    double sum = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return sum;
}

struct Scaling {
    const char *path;
    bool native;
};

static void *lane_make(void *context) {
    Scaling *scaling = (Scaling *)context;
    lua_State *L = loaded(scaling->path, scaling->native);
    lua_getglobal(L, "fill");
    lua_pushinteger(L, BODIES);
    called(L, 1, 0, "fill");
    return L;
}

static double lane_frame(void *state) {
    lua_State *L = (lua_State *)state;
    lua_getglobal(L, "step");
    lua_pushnumber(L, WALL);
    called(L, 1, 1, "step");
    double sum = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return sum;
}

static void lane_drop(void *state) {
    lua_close((lua_State *)state);
}

// What a sandbox made of Luau stops a program with: the interrupt it calls at
// every loop's back edge and every call, which raises once the host has asked.
// Installed and never asked, it is the cost of being able to.
static std::atomic<bool> asked(false);

static void interrupted(lua_State *L, int gc) {
    if (gc < 0 && asked.load(std::memory_order_relaxed)) {
        luaL_error(L, "the host asked this program to stop");
    }
}

static void *spin_make(void *context) {
    Scaling *scaling = (Scaling *)context;
    lua_State *L = loaded(scaling->path, scaling->native);
    lua_callbacks(L)->interrupt = interrupted;
    return L;
}

static void spin_run(void *state) {
    lua_State *L = (lua_State *)state;
    lua_getglobal(L, "spin");
    lua_pushinteger(L, 0);
    if (lua_pcall(L, 1, 1, 0) == 0) {
        fprintf(stderr, "spin came back without being stopped\n");
        exit(1);
    }
    lua_pop(L, 1);
}

static void spin_ask(void *state) {
    (void)state;
    asked.store(true, std::memory_order_relaxed);
}

static void spin_again(void *state) {
    (void)state;
    asked.store(false, std::memory_order_relaxed);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: luau <engine name> bodies.lua [--native] "
                        "[--churn file]\n");
        return 2;
    }
    const char *engine = argv[1];
    bool native = false;
    const char *churn = NULL;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--native") == 0) {
            native = true;
        } else if (strcmp(argv[i], "--churn") == 0 && i + 1 < argc) {
            churn = argv[++i];
        }
    }
    lua_State *L = loaded(argv[2], native);

    said(engine, "memory",
         (double)lua_gc(L, LUA_GCCOUNT, 0) * 1024.0 +
             (double)lua_gc(L, LUA_GCCOUNTB, 0),
         0.0);

    long long took[FRAMES];
    double sum = 0.0;

    // The frame, over the program's own tables: see `bodies.lua` for why
    // not a `buffer`.
    lua_getglobal(L, "fill");
    lua_pushinteger(L, BODIES);
    called(L, 1, 0, "fill");
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        lua_getglobal(L, "step");
        lua_pushnumber(L, WALL);
        called(L, 1, 1, "step");
        sum = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    said(engine, "frame", (double)middle_of(took, FRAMES) / BODIES, sum);

    // The same frame with the interrupt a sandbox needs installed.
    lua_callbacks(L)->interrupt = interrupted;
    lua_getglobal(L, "fill");
    lua_pushinteger(L, BODIES);
    called(L, 1, 0, "fill");
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        lua_getglobal(L, "step");
        lua_pushnumber(L, WALL);
        called(L, 1, 1, "step");
        sum = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    lua_callbacks(L)->interrupt = NULL;
    said(engine, "budget", (double)middle_of(took, FRAMES) / BODIES, sum);

    Body *world = (Body *)malloc(sizeof(Body) * BODIES);
    fill(world, BODIES);
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        sum = 0.0;
        for (int i = 0; i < BODIES; i++) {
            lua_getglobal(L, "one");
            lua_pushnumber(L, world[i].x);
            lua_pushnumber(L, world[i].y);
            lua_pushnumber(L, world[i].dx);
            lua_pushnumber(L, world[i].dy);
            lua_pushnumber(L, WALL);
            called(L, 5, 4, "one");
            world[i].x = lua_tonumber(L, -4);
            world[i].y = lua_tonumber(L, -3);
            world[i].dx = lua_tonumber(L, -2);
            world[i].dy = lua_tonumber(L, -1);
            lua_pop(L, 4);
            sum += world[i].x + world[i].y;
        }
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    said(engine, "body", (double)middle_of(took, FRAMES) / BODIES, sum);

    long long asked[9];
    double answer = 0.0;
    for (int r = 0; r < 9; r++) {
        long long before = now_ns();
        lua_getglobal(L, "asks");
        lua_pushinteger(L, ASKS);
        called(L, 1, 1, "asks");
        answer = lua_tonumber(L, -1);
        lua_pop(L, 1);
        asked[r] = now_ns() - before;
    }
    said(engine, "ask", (double)middle_of(asked, 9) / ASKS, answer);

    free(world);
    lua_close(L);
    if (churn != NULL) {
        lua_State *world_state = loaded(churn, native);
        lua_getglobal(world_state, "begin");
        lua_pushinteger(world_state, THINGS);
        called(world_state, 1, 0, "begin");
        churned(engine, churn_frame, world_state);
        lua_close(world_state);
    }
    Scaling scaling = {argv[2], native};
    Stoppable stops = {spin_make, spin_run, spin_ask, spin_again, &scaling};
    stopped(engine, &stops);
    Scaled lanes = {lane_make, lane_frame, lane_drop, &scaling};
    scaled(engine, &lanes);
    return 0;
}
