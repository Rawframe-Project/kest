// A game engine hosting Kest: raylib opens the window, reads the keyboard and
// draws, and the game is `examples/raylib/game.kest`, which is the Tetris
// clone in `examples/tetromino.kest` with two doors to draw through. The world
// is kept between frames by `kest_held`, so a frame is a call with the world in
// front of it, and the program's drawing is two functions this host binds.
// It is the shape a host has in an engine rather than a list of doors: the
// loop is the engine's, and the program is asked for one frame at a time.
//
//   make raylib RAYLIB=path/to/raylib && examples/raylib/host
//
// `--frames N --shot out.png` plays N frames with the program's own player at
// the keys, and writes what the window showed at the end, which is how this is
// run where there is no screen to look at, under a display nobody sees. The
// shot is a name and not a path: raylib writes it where the host was started.
// See D1269.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kest.h"
#include "raylib.h"

#define BOX 24
#define LEFT 40
#define TOP 60

static const Color COLORS[] = {
    {0, 0, 155, 255}, {0, 155, 0, 255}, {155, 0, 0, 255}, {155, 155, 0, 255}};
static const Color LIGHT[] = {
    {20, 20, 175, 255}, {20, 175, 20, 255}, {175, 20, 20, 255},
    {175, 175, 20, 255}};

// `Screen.box(x, y, color)`: a box on the board, the way the Lua drew one --
// a dark square with a lighter one inside it.
static void screen_box(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    int x = (int)frame[0].integer;
    int y = (int)frame[1].integer;
    int color = (int)frame[2].integer - 1;
    if (color < 0 || color > 3) {
        return;
    }
    DrawRectangle(LEFT + x * BOX + 1, TOP + y * BOX + 1, BOX - 1, BOX - 1,
                  COLORS[color]);
    DrawRectangle(LEFT + x * BOX + 1, TOP + y * BOX + 1, BOX - 4, BOX - 4,
                  LIGHT[color]);
}

// `Screen.say(line, x, y)`: a line of text. The bytes are the program's and
// are copied, because raylib wants them to end in a nought.
static void screen_say(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(&frame[0], &length);
    char line[256];
    if (bytes == NULL) {
        return;
    }
    if (length >= sizeof(line)) {
        length = sizeof(line) - 1;
    }
    memcpy(line, bytes, length);
    line[length] = '\0';
    DrawText(line, LEFT + (int)frame[2].integer,
             TOP - 40 + (int)frame[3].integer, 24, RAYWHITE);
}

// `Io.write`, which the game's own module asks for because its `main` says
// how a game went: the host's standard output.
static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(&frame[0], &length);
    if (bytes != NULL) {
        fwrite(bytes, 1, length, stdout);
    }
}

// The keys this game has, one bit each in the order the program's `Key`
// declares them.
static int keys_that(bool (*asked)(int)) {
    const int named[] = {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_Q,
                         KEY_DOWN, KEY_SPACE, KEY_P};
    int bits = 0;
    for (int i = 0; i < 7; i++) {
        if (asked(named[i])) {
            bits |= 1 << i;
        }
    }
    return bits;
}

int main(int argc, char **argv) {
    long frames = 0;
    const char *shot = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--frames") == 0) {
            frames = strtol(argv[i + 1], NULL, 10);
        } else if (strcmp(argv[i], "--shot") == 0) {
            shot = argv[i + 1];
        }
    }

    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Screen.box", screen_box, NULL) ||
        !kest_host_bind(host, "Screen.say", screen_say, NULL) ||
        !kest_host_bind(host, "Io.write", io_write, NULL)) {
        fprintf(stderr, "no host to run the game with\n");
        return 1;
    }
    KestHeld *held =
        kest_held_new("examples/raylib/game.kest", NULL, host, stderr);
    KestValue seed = {.integer = 2023};
    if (held == NULL || !kest_held_begin(held, "begin", &seed, 1)) {
        fprintf(stderr, "the game did not start\n");
        kest_host_free(host);
        return 1;
    }

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(LEFT * 2 + BOX * 10, TOP + BOX * 20 + 20, "Kest -- Tetromino");
    SetTargetFPS(60);
    long played = 0;
    KestValue score = {0};
    bool ok = true;
    while (!WindowShouldClose() && (frames == 0 || played < frames)) {
        int down = keys_that(IsKeyPressed);
        int up = keys_that(IsKeyReleased);
        BeginDrawing();
        ClearBackground(BLACK);
        DrawRectangleLines(LEFT - 2, TOP - 2, BOX * 10 + 4, BOX * 20 + 4, BLUE);
        KestValue args[3] = {{.real = 1.0 / 60.0}, {.integer = down},
                             {.integer = up}};
        ok = frames == 0 ? kest_held_call(held, "frame", args, 3, &score)
                         : kest_held_call(held, "played", args, 1, &score);
        EndDrawing();
        if (!ok) {
            break;
        }
        played++;
    }
    if (shot != NULL) {
        TakeScreenshot(shot);
    }
    CloseWindow();
    printf("tetromino under raylib: %ld frame(s), score %lld%s\n", played,
           (long long)score.integer, ok ? "" : ", and the game was refused");
    kest_held_free(held);
    kest_host_free(host);
    return ok ? 0 : 1;
}
