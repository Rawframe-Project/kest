#include <stdio.h>
#include <string.h>

#include "kest.h"

static int usage(void) {
    fprintf(stderr,
            "usage: kest <command> [options]\n"
            "\n"
            "  run <file>    compile and run a program\n"
            "  --version     print the version\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return usage();
    }

    if (strcmp(argv[1], "--version") == 0) {
        printf("kest %s\n", kest_version());
        return 0;
    }

    fprintf(stderr, "kest: unknown command '%s'\n", argv[1]);
    return usage();
}
