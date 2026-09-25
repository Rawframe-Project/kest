// The same boundary `tools/fuzz.c`'s `source` puts random bytes through, fed
// by a fuzzer that watches which branches of this compiler each input reached
// and keeps the ones that reached something new. Random bytes and broken
// examples get past the lexer and not much further; a fuzzer that learns what
// the parser wants gets to the checker, the compiler, the verifier and the
// machine, which is where source nobody trusts goes on somebody else's
// machine. See D1253.
//
// Built with clang's libFuzzer and the sanitisers (`make tools/fuzz-cover`),
// and run as `tools/fuzz-cover corpus/ -max_total_time=...`: what a finding is
// is the input it writes, which is a file a reader runs `kest` on.
//
// Every input is compiled with a ceiling on its bytes and its work and run with
// a ceiling on its heap and its fuel, which is what a host compiling what it
// was sent gives: an input that takes all of either is refused, and that is an
// answer. What is looked for is anything else -- a report from the sanitisers,
// a hang, a crash.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kest.h"

// Where each input is written for the build to read. One file a process, so
// fuzzers run side by side do not read each other's.
static char where[256];

// Where the standard library is: `lib/` beside the fuzzer where it was put
// there, which is how `tools/oss-fuzz-build.sh` lays one out, and `lib/` where
// it is run from otherwise, which is this tree.
static char library[1024] = "lib/";

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc;
    const char *self = (*argv)[0];
    const char *slash = strrchr(self, '/');
    if (slash != NULL) {
        char probe[1024];
        snprintf(probe, sizeof probe, "%.*s/lib/std/io.kest",
                 (int)(slash - self), self);
        FILE *there = fopen(probe, "rb");
        if (there != NULL) {
            fclose(there);
            snprintf(library, sizeof library, "%.*s/lib/", (int)(slash - self),
                     self);
        }
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (where[0] == '\0') {
        const char *under = getenv("TMPDIR");
        snprintf(where, sizeof where, "%s/kest-cover-%ld.kest",
                 under != NULL ? under : "/tmp", (long)getpid());
    }
    FILE *file = fopen(where, "wb");
    if (file == NULL) {
        return 0;
    }
    fwrite(data, 1, size, file);
    fclose(file);

    KestBuild *build = kest_build_within(where, library, NULL, KEST_FORM_TEXT,
                                         (size_t)64 << 20, 20000000);
    if (build == NULL) {
        return 0;
    }
    KestLimits bounded = {4096, 64, (size_t)8 << 20, 200000};
    KestRuntime *runtime = kest_start(build, NULL, &bounded);
    if (runtime != NULL) {
        int32_t at = kest_entry(runtime, "main");
        if (at >= 0) {
            KestValue frame[16];
            memset(frame, 0, sizeof frame);
            kest_call(runtime, at, frame, sizeof frame / sizeof frame[0]);
        }
        kest_runtime_free(runtime);
    }
    kest_build_free(build);
    return 0;
}
