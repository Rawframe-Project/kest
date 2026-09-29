// `bench/hosts` through the C API Lua 5.4 and LuaJIT share: the frame, a
// crossing a body, and the program asking its host, over `bodies.lua`, and a
// reload of the Tetris clone as it was written in Lua, and a world that makes
// garbage every frame. Built once against each (`-DLUAJIT` for the second),
// and run with the name the line is to carry. LuaJIT is run twice, with its
// compiler and with it switched off (`--off`), which is its interpreter; Lua
// 5.4 twice, with its collector as it comes and with it generational. See
// D1272 and D1273.
//
//   lua <engine name> bench/hosts/bodies.lua [--off] [--generational]
//       [--game bench/hosts/tetromino.lua] [--churn bench/hosts/churn.lua]
#include <stdbool.h>
#include <string.h>

#include "common.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#if defined(LUAJIT)
#include "luajit.h"
#endif

static int add(lua_State *L) {
    lua_Number a = lua_tonumber(L, 1);
    lua_Number b = lua_tonumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1;
}

static void refused(lua_State *L, const char *what) {
    fprintf(stderr, "%s: %s\n", what, lua_tostring(L, -1));
    exit(1);
}

// One call of a global function the program defined, with `in` numbers on the
// stack already, answering `out`.
static void called(lua_State *L, int in, int out, const char *what) {
    if (lua_pcall(L, in, out, 0) != 0) {
        refused(L, what);
    }
}

// One frame of the world in `churn.lua`, which is a state of its own so that
// what the collector walks is that world and nothing else.
static double churn_frame(void *context) {
    lua_State *L = context;
    lua_getglobal(L, "frame");
    lua_pushinteger(L, REPLACED);
    called(L, 1, 1, "frame");
    double sum = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return sum;
}

static void churns(const char *engine, const char *path, bool off,
                   bool generational) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
#if defined(LUAJIT)
    if (off) {
        luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    }
#else
    (void)off;
#endif
    if (generational &&
        luaL_dostring(L, "collectgarbage('generational')") != 0) {
        refused(L, "a generational collector");
    }
    if (luaL_dofile(L, path) != 0) {
        refused(L, "reading the world");
    }
    lua_getglobal(L, "begin");
    lua_pushinteger(L, THINGS);
    called(L, 1, 0, "begin");
    churned(engine, churn_frame, L);
    lua_close(L);
}

// A state of its own on a thread of its own, holding the program and its
// bodies where the frame above keeps them.
typedef struct {
    const char *path;
    bool off;
} Scaling;

typedef struct {
    lua_State *L;
    Body *world;
} Lane_state;

static void *lane_make(void *context) {
    Scaling *scaling = context;
    Lane_state *lane = malloc(sizeof *lane);
    lane->L = luaL_newstate();
    luaL_openlibs(lane->L);
    lane->world = NULL;
#if defined(LUAJIT)
    if (scaling->off) {
        luaJIT_setmode(lane->L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    } else {
        lane->world = malloc(sizeof *lane->world * BODIES);
        fill(lane->world, BODIES);
    }
#endif
    lua_pushcfunction(lane->L, add);
    lua_setglobal(lane->L, "add");
    if (luaL_dofile(lane->L, scaling->path) != 0) {
        refused(lane->L, "reading the program");
    }
    if (lane->world == NULL) {
        lua_getglobal(lane->L, "fill");
        lua_pushinteger(lane->L, BODIES);
        called(lane->L, 1, 0, "fill");
    }
    return lane;
}

static double lane_frame(void *state) {
    Lane_state *lane = state;
    if (lane->world != NULL) {
        lua_getglobal(lane->L, "steplent");
        lua_pushlightuserdata(lane->L, lane->world);
        lua_pushinteger(lane->L, BODIES);
        lua_pushnumber(lane->L, WALL);
        called(lane->L, 3, 1, "steplent");
    } else {
        lua_getglobal(lane->L, "step");
        lua_pushnumber(lane->L, WALL);
        called(lane->L, 1, 1, "step");
    }
    double sum = lua_tonumber(lane->L, -1);
    lua_pop(lane->L, 1);
    return sum;
}

static void lane_drop(void *state) {
    Lane_state *lane = state;
    lua_close(lane->L);
    free(lane->world);
    free(lane);
}

// What a sandbox made of Lua has to stop a program with: a hook on a count of
// instructions. The one that does nothing is its cost when nobody is asking;
// the one that raises is what a host sets, from any thread, when it is.
static void counted(lua_State *L, lua_Debug *seen) {
    (void)L;
    (void)seen;
}

static void stopping(lua_State *L, lua_Debug *seen) {
    (void)seen;
    luaL_error(L, "the host asked this program to stop");
}

static void *spin_make(void *context) {
    Scaling *scaling = context;
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
#if defined(LUAJIT)
    if (scaling->off) {
        luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    }
#endif
    if (luaL_dofile(L, scaling->path) != 0) {
        refused(L, "reading the program");
    }
    return L;
}

static void spin_run(void *state) {
    lua_State *L = state;
    lua_getglobal(L, "spin");
    lua_pushinteger(L, 0);
    if (lua_pcall(L, 1, 1, 0) == 0) {
        fprintf(stderr, "spin came back without being stopped\n");
        exit(1);
    }
    lua_pop(L, 1);
}

static void spin_ask(void *state) {
    lua_sethook((lua_State *)state, stopping, LUA_MASKCOUNT, 1);
}

static void spin_again(void *state) {
    lua_sethook((lua_State *)state, NULL, 0, 0);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: lua <engine name> bodies.lua [--off] "
                        "[--generational] [--game file] [--churn file]\n");
        return 2;
    }
    const char *engine = argv[1];
    bool off = false;
    bool generational = false;
    const char *game = NULL;
    const char *churn = NULL;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--off") == 0) {
            off = true;
        } else if (strcmp(argv[i], "--generational") == 0) {
            generational = true;
        } else if (strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            game = argv[++i];
        } else if (strcmp(argv[i], "--churn") == 0 && i + 1 < argc) {
            churn = argv[++i];
        }
    }
    if (generational) {
        // Only the world that makes garbage is about the collector.
        if (churn != NULL) {
            churns(engine, churn, off, true);
        }
        return 0;
    }
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
#if defined(LUAJIT)
    if (off) {
        luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    }
#endif
    lua_pushcfunction(L, add);
    lua_setglobal(L, "add");
    if (luaL_dofile(L, argv[2]) != 0) {
        refused(L, "reading the program");
    }

    // What the state holding the program costs, with the libraries a host
    // opens, before it has done anything.
    said(engine, "memory",
         (double)lua_gc(L, LUA_GCCOUNT, 0) * 1024.0 +
             (double)lua_gc(L, LUA_GCCOUNTB, 0),
         0.0);

    Body *world = malloc(sizeof *world * BODIES);
    long long took[FRAMES];
    double sum = 0.0;

    // A frame with one crossing. LuaJIT reaches the host's run through its
    // FFI; Lua 5.4 cannot, so the bodies are its own tables, filled once, and
    // so are LuaJIT's with its compiler off, because the FFI is a thing its
    // compiler does and its interpreter reads a field of one fifty times
    // slower than a table's.
    bool lent = false;
#if defined(LUAJIT)
    lent = !off;
#endif
    fill(world, BODIES);
    if (!lent) {
        lua_getglobal(L, "fill");
        lua_pushinteger(L, BODIES);
        called(L, 1, 0, "fill");
    }
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        if (lent) {
            lua_getglobal(L, "steplent");
            lua_pushlightuserdata(L, world);
            lua_pushinteger(L, BODIES);
            lua_pushnumber(L, WALL);
            called(L, 3, 1, "steplent");
        } else {
            lua_getglobal(L, "step");
            lua_pushnumber(L, WALL);
            called(L, 1, 1, "step");
        }
        sum = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    said(engine, "frame", (double)middle_of(took, FRAMES) / BODIES, sum);

    // The same frame with the hook a sandbox needs set, every thousand
    // instructions, doing nothing.
    lua_sethook(L, counted, LUA_MASKCOUNT, 1000);
    if (!lent) {
        lua_getglobal(L, "fill");
        lua_pushinteger(L, BODIES);
        called(L, 1, 0, "fill");
    } else {
        fill(world, BODIES);
    }
    for (int f = -20; f < FRAMES; f++) {
        long long before = now_ns();
        if (lent) {
            lua_getglobal(L, "steplent");
            lua_pushlightuserdata(L, world);
            lua_pushinteger(L, BODIES);
            lua_pushnumber(L, WALL);
            called(L, 3, 1, "steplent");
        } else {
            lua_getglobal(L, "step");
            lua_pushnumber(L, WALL);
            called(L, 1, 1, "step");
        }
        sum = lua_tonumber(L, -1);
        lua_pop(L, 1);
        if (f >= 0) {
            took[f] = now_ns() - before;
        }
    }
    lua_sethook(L, NULL, 0, 0);
    said(engine, "budget", (double)middle_of(took, FRAMES) / BODIES, sum);

    // A crossing a body: four numbers and the wall in, four back.
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

    // And the other way: the program asking its host, a million times a run.
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

    // A reload: the Tetris clone this language's version of the game was
    // written in, loaded into the running Lua and run, the way a LÖVE
    // hot-reloader does it. What its top level asks of LÖVE is a font and the
    // time.
    if (game != NULL) {
        FILE *file = fopen(game, "rb");
        if (file == NULL) {
            perror(game);
            return 2;
        }
        static char source[1 << 16];
        size_t length = fread(source, 1, sizeof source, file);
        fclose(file);
        if (luaL_dostring(L, "love = { graphics = { newFont = function() "
                             "return {} end }, timer = { getTime = "
                             "function() return 0 end } }") != 0) {
            refused(L, "the stand-in for LÖVE");
        }
        long long reloaded[RELOADS];
        for (int r = -3; r < RELOADS; r++) {
            long long before = now_ns();
            if (luaL_loadbuffer(L, source, length, "tetromino") != 0) {
                refused(L, "reading the game");
            }
            called(L, 0, 0, "the game");
            if (r >= 0) {
                reloaded[r] = now_ns() - before;
            }
        }
        said(engine, "reload", (double)middle_of(reloaded, RELOADS), 0.0);
    }

    free(world);
    lua_close(L);
    if (churn != NULL) {
        churns(engine, churn, off, false);
    }
    Scaling scaling = {argv[2], off};
    Stoppable stops = {spin_make, spin_run, spin_ask, spin_again, &scaling};
    stopped(engine, &stops);
    Scaled lanes = {lane_make, lane_frame, lane_drop, &scaling};
    scaled(engine, &lanes);
    return 0;
}
