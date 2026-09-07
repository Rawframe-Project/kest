// A host that is not the command line. It compiles a file, makes a machine,
// and keeps a world between frames by holding the handle the program gave it.
//
//   make embed && ./examples/embed
#include <stdio.h>

#include "kest.h"

// The context is whatever was given at binding, which is how a host reaches
// its own state from inside a function the program calls.
static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    fputs(frame[0].text, (FILE *)context);
}

int main(int argc, char **argv) {
    // NULL for the library, which is the compiler finding its own: what
    // `KEST_LIB` says, or where it was installed.
    const char *path = argc > 1 ? argv[1] : "examples/embed.kest";
    KestBuild *build = kest_build(path, NULL, stderr);
    if (build == NULL) {
        return 1;
    }

    KestHost *host = kest_host_new();
    if (host == NULL || !kest_host_bind(host, "Io.write", io_write, stdout)) {
        return 1;
    }

    // Fifty frames of a hundred and twenty slots, rather than whatever the
    // machine would have picked.
    KestLimits limits = {4096, 64};
    KestRuntime *runtime = kest_start(build, host, &limits);
    if (runtime == NULL) {
        return 1;
    }

    // Wide enough for the most any of these calls passes or returns, because
    // the arguments go where the result comes back.
    KestValue frame[4] = {{0}};
    if (!kest_call(runtime, kest_build_name(build, "create"), frame)) {
        return 1;
    }
    KestValue world = frame[0];

    for (int i = 0; i < 5; i++) {
        frame[0] = world;
        frame[1].integer = i + 1;
        if (!kest_call(runtime, kest_build_name(build, "spawn"), frame)) {
            return 1;
        }
        printf("frame %d: spawned, %lld alive\n", i, (long long)frame[0].integer);
    }

    for (int i = 0; i < 5; i++) {
        frame[0] = world;
        if (!kest_call(runtime, kest_build_name(build, "step"), frame)) {
            return 1;
        }
        printf("frame %d: stepped, %lld alive, %zu bytes\n", i + 5,
               (long long)frame[0].integer, kest_heap_used(runtime));
    }

    kest_runtime_free(runtime);
    kest_host_free(host);
    kest_build_free(build);
    return 0;
}
