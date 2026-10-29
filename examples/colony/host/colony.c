// The colony's host: a window, a camera, the player's mouse, and a Kest
// program that is the whole of the game's rules. The machine, the world it
// keeps between frames, and the reload under that world are the language's
// `kest_held_*` doors; what is left here is the game's: drawing, input, and
// the numbers in the corner.
//
// It is also an instrument. What each call into the rules cost is measured
// every frame and drawn in the corner, because the question this game exists
// to answer is what a Kest frame costs inside a real loop, not a benchmark.

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kest.h"
#include "raylib.h"

#define MOST_PEOPLE 4096
#define COUNTS 64

// Numbers the program uses for what is on a cell, what the player asked for
// and what somebody is doing. They are the program's and are written here
// once, beside what they are drawn as.
enum { GRASS, TREE, ROCK, WATER, WALL, FIELD, STOCK, TILES };
enum { TICK, STORED_WOOD, STORED_FOOD, BORN, DIED, CLOCK, MADE };

static const Color tile_colour[TILES] = {
    {92, 142, 64, 255},  {38, 92, 44, 255},  {128, 124, 118, 255},
    {52, 98, 160, 255},  {120, 84, 52, 255}, {176, 150, 72, 255},
    {150, 120, 90, 255},
};
static const Color mark_colour[5] = {
    {0, 0, 0, 0},        {230, 60, 40, 255},   {240, 200, 60, 255},
    {160, 220, 80, 255}, {250, 250, 120, 255},
};
static const char *const job_name[9] = {
    "idle", "chop", "build", "sow", "reap", "fetch", "carry", "eat", "sleep"};
static const Color job_colour[9] = {
    {220, 220, 220, 255}, {230, 60, 40, 255},   {240, 200, 60, 255},
    {160, 220, 80, 255},  {250, 250, 120, 255}, {120, 180, 255, 255},
    {80, 140, 255, 255},  {255, 140, 200, 255}, {150, 110, 200, 255},
};

typedef struct {
    KestHeld *held;
    bool native;
    int wide, high;
    const uint8_t *tiles, *marks, *items;
    float xs[MOST_PEOPLE], ys[MOST_PEOPLE];
    int32_t doing[MOST_PEOPLE];
    int32_t counts[COUNTS];
    int many;
    // What a reload said last, drawn in the corner until the next one.
    char reloaded[256];
} Game;

typedef struct {
    double step_us, look_us;
} Costs;

static double now_us(void) {
    struct timespec at;
    clock_gettime(CLOCK_MONOTONIC, &at);
    return (double)at.tv_sec * 1e6 + (double)at.tv_nsec / 1e3;
}

// What `kest emit --c` wrote for this program, bound to the held machine. It
// refuses a machine built from another program, which is what the file is
// after the first reload.
bool kest_natives_here(KestRuntime *runtime);

// A run of this host's memory lent for one call and ended after it.
static KestValue lend(Game *g, void *block, uint32_t many, const char *of,
                      size_t wide) {
    return kest_borrow(kest_held_runtime(g->held), block, many, of, wide);
}

// What to draw: the ground read where it lies in the world, and the people
// and the counts written by the program into memory this host lent it.
static bool look(Game *g) {
    uint32_t cells = 0;
    g->tiles = kest_held_run(g->held, "ground.tiles", &cells, NULL);
    g->marks = kest_held_run(g->held, "marks", NULL, NULL);
    g->items = kest_held_run(g->held, "items", NULL, NULL);
    if (g->tiles == NULL || g->marks == NULL || g->items == NULL ||
        cells != (uint32_t)(g->wide * g->high)) {
        fprintf(stderr, "the world has no ground this host can draw\n");
        return false;
    }
    KestValue who[3] = {
        lend(g, g->xs, MOST_PEOPLE, "f32", sizeof(float)),
        lend(g, g->ys, MOST_PEOPLE, "f32", sizeof(float)),
        lend(g, g->doing, MOST_PEOPLE, "i32", sizeof(int32_t)),
    };
    KestValue answer = {0};
    bool drawn = kest_held_call(g->held, "people", who, 3, &answer);
    g->many = drawn ? (int)answer.integer : 0;
    KestValue into = lend(g, g->counts, COUNTS, "i32", sizeof(int32_t));
    drawn = drawn && kest_held_call(g->held, "tally", &into, 1, NULL);
    KestRuntime *runtime = kest_held_runtime(g->held);
    for (int i = 0; i < 3; i++) {
        kest_lend_ends(runtime, who[i]);
    }
    kest_lend_ends(runtime, into);
    // The ground again, because a call may have moved it.
    g->tiles = kest_held_run(g->held, "ground.tiles", NULL, NULL);
    g->marks = kest_held_run(g->held, "marks", NULL, NULL);
    g->items = kest_held_run(g->held, "items", NULL, NULL);
    return drawn && g->tiles != NULL;
}

static bool give_order(Game *g, int what, int x0, int y0, int x1, int y1) {
    KestValue asked[5];
    memset(asked, 0, sizeof(asked));
    asked[0].integer = what;
    asked[1].integer = x0;
    asked[2].integer = y0;
    asked[3].integer = x1;
    asked[4].integer = y1;
    return kest_held_call(g->held, "order", asked, 5, NULL);
}

static void reload(Game *g, const char *from) {
    double began = now_us();
    char said[200];
    bool took = kest_held_reload(g->held, from, said, sizeof(said));
    if (took) {
        g->native = false;
    }
    snprintf(g->reloaded, sizeof(g->reloaded), "%s in %.1f ms", said,
             (now_us() - began) / 1000.0);
}

// Only the cells the camera can see: a map of a quarter of a million cells
// drawn whole is the host's frame gone, whatever the rules cost.
static void draw_world(const Game *g, int cell, Camera2D camera) {
    Vector2 from = GetScreenToWorld2D((Vector2){0, 0}, camera);
    Vector2 to = GetScreenToWorld2D(
        (Vector2){(float)GetScreenWidth(), (float)GetScreenHeight()}, camera);
    int x0 = (int)(from.x / (float)cell) - 1;
    int y0 = (int)(from.y / (float)cell) - 1;
    int x1 = (int)(to.x / (float)cell) + 1;
    int y1 = (int)(to.y / (float)cell) + 1;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > g->wide ? g->wide : x1;
    y1 = y1 > g->high ? g->high : y1;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            size_t c = (size_t)y * (size_t)g->wide + (size_t)x;
            uint8_t t = g->tiles[c] < TILES ? g->tiles[c] : GRASS;
            DrawRectangle(x * cell, y * cell, cell, cell, tile_colour[t]);
            if (g->items[c] != 0) {
                Color it = g->items[c] == 1 ? (Color){200, 150, 90, 255}
                                            : (Color){240, 220, 90, 255};
                DrawRectangle(x * cell + cell / 3, y * cell + cell / 3,
                              cell / 3, cell / 3, it);
            }
            if (g->marks[c] != 0 && g->marks[c] < 5) {
                DrawRectangleLines(x * cell + 1, y * cell + 1, cell - 2,
                                   cell - 2, mark_colour[g->marks[c]]);
            }
        }
    }
    for (int i = 0; i < g->many; i++) {
        int j = g->doing[i] >= 0 && g->doing[i] < 9 ? g->doing[i] : 0;
        DrawCircle((int)(g->xs[i] * (float)cell) + cell / 2,
                   (int)(g->ys[i] * (float)cell) + cell / 2, (float)cell * 0.4f,
                   job_colour[j]);
    }
}

static void draw_panel(const Game *g, const Costs *cost, int tool) {
    static const char *const tools[5] = {"cancel", "chop", "build wall",
                                         "sow field", "-"};
    int busy[9] = {0};
    for (int i = 0; i < g->many; i++) {
        if (g->doing[i] >= 0 && g->doing[i] < 9) {
            busy[g->doing[i]]++;
        }
    }
    DrawRectangle(8, 8, 330, 194, (Color){0, 0, 0, 170});
    DrawText(TextFormat("people %d   born %d   died %d", g->many,
                        g->counts[BORN], g->counts[DIED]),
             16, 16, 18, RAYWHITE);
    DrawText(TextFormat("wood %d   food %d", g->counts[STORED_WOOD],
                        g->counts[STORED_FOOD]),
             16, 40, 18, RAYWHITE);
    for (int j = 0; j < 9; j++) {
        DrawText(TextFormat("%s %d", job_name[j], busy[j]), 16 + (j % 3) * 104,
                 66 + (j / 3) * 20, 16, job_colour[j]);
    }
    DrawText(TextFormat("kest step   %7.1f us", cost->step_us), 16, 134, 16,
             YELLOW);
    DrawText(TextFormat("kest look   %7.1f us", cost->look_us), 16, 152, 16,
             YELLOW);
    DrawText(TextFormat("tool: %s  (0 1 2 3)   fps %d   %s", tools[tool],
                        GetFPS(), g->native ? "rules as C" : "machine"),
             16, 176, 16, RAYWHITE);
    if (g->reloaded[0] != '\0') {
        DrawRectangle(8, 206, 900, 26, (Color){0, 0, 0, 170});
        DrawText(g->reloaded, 16, 211, 16,
                 strncmp(g->reloaded, "reloaded", 8) == 0 ? GREEN : ORANGE);
    }
}

int main(int argc, char **argv) {
    static Game g;
    // Run from the root of the Kest tree: the rules where they are in it, and
    // the standard library where `KEST_LIB` says or in `lib/`.
    const char *path = "examples/colony/game/colony.kest";
    const char *library = getenv("KEST_LIB");
    if (library == NULL || library[0] == '\0') {
        library = "lib/";
    }
    g.wide = 96;
    g.high = 64;
    int many = 8;
    int64_t seed = 7;
    int frames = 0;
    const char *shot = NULL;
    int reload_at = -1;
    const char *reload_from = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--size") == 0) {
            sscanf(argv[i + 1], "%dx%d", &g.wide, &g.high);
        } else if (strcmp(argv[i], "--people") == 0) {
            many = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--seed") == 0) {
            seed = atoll(argv[i + 1]);
        } else if (strcmp(argv[i], "--frames") == 0) {
            frames = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--shot") == 0) {
            shot = argv[i + 1];
        } else if (strcmp(argv[i], "--native") == 0) {
            g.native = atoi(argv[i + 1]) != 0;
        } else if (strcmp(argv[i], "--reload-at") == 0) {
            reload_at = atoi(argv[i + 1]);
        } else if (strcmp(argv[i], "--reload-from") == 0) {
            reload_from = argv[i + 1];
        } else if (strcmp(argv[i], "--game") == 0) {
            path = argv[i + 1];
        }
    }

    KestHost *host = kest_host_new();
    g.held = host == NULL ? NULL : kest_held_new(path, library, host, stderr);
    if (g.held == NULL) {
        return 1;
    }
    if (g.native && !kest_natives_here(kest_held_runtime(g.held))) {
        fprintf(stderr, "the compiled bodies were written from another "
                        "program; `cmake --build` writes them again\n");
        return 1;
    }
    KestValue begun[4];
    memset(begun, 0, sizeof(begun));
    begun[0].integer = seed;
    begun[1].integer = g.wide;
    begun[2].integer = g.high;
    begun[3].integer = many;
    if (!kest_held_begin(g.held, "begin", begun, 4) || !look(&g)) {
        return 1;
    }

    int cell = 12;
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 800, "colony -- a game written in Kest");
    SetTargetFPS(frames > 0 ? 0 : 60);
    Camera2D camera = {0};
    camera.zoom = 1.0f;
    camera.target =
        (Vector2){(float)(g.wide * cell) / 2.0f, (float)(g.high * cell) / 2.0f};
    int tool = 1;
    Vector2 dragged = {0};
    bool dragging = false;
    Costs cost = {0};
    double stepped_total = 0.0;
    double looked_total = 0.0;

    for (int frame = 0; !WindowShouldClose(); frame++) {
        if (frames > 0 && frame >= frames) {
            break;
        }
        float dt = frames > 0 ? 1.0f / 60.0f : GetFrameTime();
        if (dt > 0.1f) {
            dt = 0.1f;
        }
        for (int k = KEY_ZERO; k <= KEY_THREE; k++) {
            if (IsKeyPressed(k)) {
                tool = k - KEY_ZERO;
            }
        }
        float pan = 600.0f * GetFrameTime() / camera.zoom;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) {
            camera.target.x -= pan;
        }
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) {
            camera.target.x += pan;
        }
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) {
            camera.target.y -= pan;
        }
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) {
            camera.target.y += pan;
        }
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            camera.zoom *= wheel > 0.0f ? 1.1f : 1.0f / 1.1f;
        }
        camera.offset = (Vector2){(float)GetScreenWidth() / 2.0f,
                                  (float)GetScreenHeight() / 2.0f};
        Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), camera);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            dragged = mouse;
            dragging = true;
        }
        if (dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            dragging = false;
            if (!give_order(&g, tool, (int)(dragged.x / (float)cell),
                            (int)(dragged.y / (float)cell),
                            (int)(mouse.x / (float)cell),
                            (int)(mouse.y / (float)cell))) {
                return 1;
            }
        }
        if (frames > 0 && frame == 1) {
            // What a player would do first, asked on the player's behalf so
            // that a run with no hands on it is a colony at work.
            int cx = g.wide / 2;
            int cy = g.high / 2;
            if (!give_order(&g, 1, cx - 24, cy - 16, cx + 24, cy + 16) ||
                !give_order(&g, 3, cx + 3, cy - 2, cx + 6, cy + 2) ||
                !give_order(&g, 2, cx - 6, cy - 4, cx - 6, cy + 4)) {
                return 1;
            }
        }
        // An edit to any file the rules were read from, looked for twice a
        // second, or F5, or the frame a run with no hands on it was told to.
        if (IsKeyPressed(KEY_F5) ||
            (frames == 0 && frame % 30 == 0 && kest_held_changed(g.held)) ||
            frame == reload_at) {
            reload(&g, frame == reload_at ? reload_from : NULL);
            if (frames > 0) {
                printf("frame %d: %s\n", frame, g.reloaded);
            }
        }

        double began = now_us();
        KestValue passed = {.real = (double)dt};
        if (!kest_held_call(g.held, "step", &passed, 1, NULL)) {
            return 1;
        }
        double stepped = now_us();
        if (!look(&g)) {
            return 1;
        }
        cost.step_us = stepped - began;
        cost.look_us = now_us() - stepped;
        stepped_total += cost.step_us;
        looked_total += cost.look_us;

        BeginDrawing();
        ClearBackground((Color){20, 24, 20, 255});
        BeginMode2D(camera);
        draw_world(&g, cell, camera);
        if (dragging) {
            float lx = dragged.x < mouse.x ? dragged.x : mouse.x;
            float ly = dragged.y < mouse.y ? dragged.y : mouse.y;
            DrawRectangleLinesEx((Rectangle){lx, ly, fabsf(mouse.x - dragged.x),
                                             fabsf(mouse.y - dragged.y)},
                                 2.0f, WHITE);
        }
        EndMode2D();
        draw_panel(&g, &cost, tool);
        EndDrawing();
        if (shot != NULL && frames > 0 && frame == frames - 1) {
            TakeScreenshot(shot);
        }
    }
    if (frames > 0) {
        printf("%d frames: people %d, wood %d, food %d, step %.1f us and "
               "looking %.1f us a frame on average\n",
               frames, g.many, g.counts[STORED_WOOD], g.counts[STORED_FOOD],
               stepped_total / (double)frames, looked_total / (double)frames);
    }
    CloseWindow();
    kest_held_free(g.held);
    kest_host_free(host);
    return 0;
}
