// `bench/hosts` through Luau's C API: the program compiled at -O2 and loaded,
// and run by its interpreter or, with `native`, by its native tier. The frame
// is over the program's own tables, and a crossing a body and the program
// asking its host are the same calls every Lua takes. See D1272.
//
//   luau <engine name> bench/hosts/bodies.lua [native]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include "common.h"
}
#include "luacode.h"
#include "luacodegen.h"
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

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: luau <engine name> bodies.lua [native]\n");
        return 2;
    }
    const char *engine = argv[1];
    bool native = argc > 3 && strcmp(argv[3], "native") == 0;
    FILE *file = fopen(argv[2], "rb");
    if (file == NULL) {
        perror(argv[2]);
        return 2;
    }
    std::vector<char> source;
    char chunk[4096];
    size_t got;
    while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) {
        source.insert(source.end(), chunk, chunk + got);
    }
    fclose(file);

    lua_State *L = luaL_newstate();
    luaL_openlibs(L);
    if (native) {
        if (!luau_codegen_supported()) {
            fprintf(stderr, "this machine has no native tier\n");
            return 2;
        }
        luau_codegen_create(L);
    }
    lua_pushcfunction(L, add, "add");
    lua_setglobal(L, "add");

    lua_CompileOptions options = {};
    options.optimizationLevel = 2;
    size_t size = 0;
    char *bytecode = luau_compile(source.data(), source.size(), &options, &size);
    if (luau_load(L, "bodies", bytecode, size, 0) != 0) {
        refused(L, "reading the program");
    }
    free(bytecode);
    if (native) {
        luau_codegen_compile(L, -1);
    }
    called(L, 0, 0, "running the program");

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
    return 0;
}
