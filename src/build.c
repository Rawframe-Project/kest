#include "build.h"

#include <stdlib.h>
#include <string.h>

KestBuild *kest_build_open(const char *library, char **paths, int count) {
    KestArena *arena = kest_arena_new();
    if (arena == NULL) {
        return NULL;
    }
    KestBuild *build = KEST_ARENA_NEW(arena, KestBuild);
    if (build == NULL) {
        kest_arena_free(arena);
        return NULL;
    }

    build->arena = arena;
    kest_diags_init(&build->diags, arena);
    kest_module_init(&build->module, arena);
    kest_load_many(arena, &build->diags,
                   library == NULL ? kest_library_path(arena, "kest") : library,
                   paths, count, &build->units);
    return build;
}

bool kest_build_check(KestBuild *build) {
    if (build->diags.error_count > 0 || build->units.count == 0) {
        return false;
    }
    if (!kest_check(build->arena, &build->diags, &build->units,
                    &build->program)) {
        return false;
    }
    kest_check_bodies(build->program, &build->units);
    if (build->diags.error_count > 0) {
        return false;
    }
    kest_check_contracts(build->program, &build->units);
    return build->diags.error_count == 0;
}

bool kest_build_emit(KestBuild *build) {
    if (build->compiled) {
        return true;
    }
    if (!kest_build_check(build)) {
        return false;
    }
    kest_compile(build->program, &build->units, &build->module);
    build->compiled = build->diags.error_count == 0;
    return build->compiled;
}

KestBuild *kest_build(const char *path, const char *library, FILE *errors) {
    char *paths[1] = {(char *)path};
    KestBuild *build = kest_build_open(library, paths, 1);
    if (build == NULL) {
        return NULL;
    }
    if (!kest_build_emit(build)) {
        if (errors != NULL) {
            kest_diags_sort(&build->diags);
            kest_diags_render(&build->diags, errors);
        }
        kest_build_free(build);
        return NULL;
    }
    return build;
}

void kest_build_free(KestBuild *build) {
    if (build != NULL) {
        kest_arena_free(build->arena);
    }
}

// The name something lives under in the file that was named, which is what a
// host has to ask for and does not otherwise know.
const char *kest_build_name(KestBuild *build, const char *name) {
    if (build->units.count == 0 || build->units.items[0].alias[0] == '\0') {
        return name;
    }
    size_t room = strlen(build->units.items[0].alias) + strlen(name) + 2;
    char *qualified = kest_arena_alloc(build->arena, room, 1);
    if (qualified == NULL) {
        return name;
    }
    snprintf(qualified, room, "%s.%s", build->units.items[0].alias, name);
    return qualified;
}

KestRuntime *kest_start(KestBuild *build, const KestHost *host,
                        const KestLimits *limits) {
    if (!build->compiled) {
        return NULL;
    }
    return kest_runtime_new(build->arena, &build->module, host, &build->diags,
                            limits);
}
