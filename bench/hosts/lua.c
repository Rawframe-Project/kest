// `bench/hosts` through the C API Lua 5.4 and LuaJIT share: the frame, a
// crossing a body, and the program asking its host, over `bodies.lua`. Built
// once against each (`-DLUAJIT` for the second), and run with the name the line
// is to carry. LuaJIT is run twice, with its compiler and with it switched off,
// which is its interpreter. See D1272.
//
//   lua <engine name> bench/hosts/bodies.lua [off]
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

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: lua <engine name> bodies.lua [off]\n");
        return 2;
    }
    const char *engine = argv[1];
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
#if defined(LUAJIT)
    if (argc > 3 && strcmp(argv[3], "off") == 0) {
        luaJIT_setmode(L, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    }
#endif
    lua_pushcfunction(L, add);
    lua_setglobal(L, "add");
    if (luaL_dofile(L, argv[2]) != 0) {
        refused(L, "reading the program");
    }

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
    lent = !(argc > 3 && strcmp(argv[3], "off") == 0);
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

    free(world);
    lua_close(L);
    return 0;
}
