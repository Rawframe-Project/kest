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
                   library == NULL ? kest_library_path(arena, "") : library,
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
    // The promise was checked against the tree; this holds it against what was
    // emitted. If the two disagree the tree walk missed something, and finding
    // that out here beats finding it out in a frame (D058).
    if (build->diags.error_count == 0) {
        kest_module_prove(&build->module, build->arena, &build->diags);
    }
    build->compiled = build->diags.error_count == 0;
    return build->compiled;
}

KestBuild *kest_build(const char *path, const char *library, FILE *errors,
                      KestForm form) {
    char *paths[1] = {(char *)path};
    KestBuild *build = kest_build_open(library, paths, 1);
    if (build == NULL) {
        return NULL;
    }
    if (!kest_build_emit(build)) {
        if (errors != NULL) {
            kest_diags_sort(&build->diags);
            if (form == KEST_FORM_JSON) {
                kest_diags_render_json(&build->diags, errors);
            } else {
                kest_diags_render(&build->diags, errors);
            }
        }
        kest_build_free(build);
        return NULL;
    }
    return build;
}

const char *kest_build_extern(const KestBuild *build, uint32_t at) {
    if (build == NULL || at >= build->module.extern_count) {
        return NULL;
    }
    return build->module.externs[at].name;
}

uint32_t kest_build_layout(const KestBuild *build, const char *name,
                           const KestLayout **layout) {
    if (build == NULL) {
        if (layout != NULL) {
            *layout = NULL;
        }
        return 0;
    }
    const KestLayout *only = NULL;
    uint32_t named = kest_module_layout_of(&build->module, name, &only, 1);
    if (layout != NULL) {
        // One is what a lend can be held to. Two of them is a name and not a
        // type, and none is nothing to hand over.
        *layout = named == 1 ? only : NULL;
    }
    return named;
}

void kest_build_free(KestBuild *build) {
    if (build != NULL) {
        kest_arena_free(build->arena);
    }
}

// The name something lives under in the file that was named, which is what a
// host has to ask for and does not otherwise know.
const char *kest_build_name(KestBuild *build, const char *name) {
    // The module's own, which is the field `kest_entry` reads when it looks
    // for the qualified form of what a host asked for. One field, so the two
    // directions of one rule cannot come apart.
    const char *alias = build->module.alias;
    if (alias == NULL || alias[0] == '\0') {
        return name;
    }
    size_t room = strlen(alias) + strlen(name) + 2;
    char *qualified = kest_arena_alloc(build->arena, room, 1);
    if (qualified == NULL) {
        return name;
    }
    snprintf(qualified, room, "%s.%s", alias, name);
    return qualified;
}

bool kest_needs(KestBuild *build, KestLimits *least, KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || least == NULL || !build->compiled) {
        return false;
    }
    return kest_module_needs(&build->module, build->arena, -1,
                             &least->stack_slots, &least->call_depth, why);
}

bool kest_needs_of(KestBuild *build, const char *name, KestLimits *least,
                   KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || name == NULL || least == NULL || !build->compiled) {
        return false;
    }
    // The name a host writes, which is the one the file wrote: the same lookup
    // `kest_entry` does, so a host cannot ask about a function it cannot call.
    int32_t found = kest_module_entry(&build->module, name);
    if (found < 0) {
        return false;
    }
    return kest_module_needs(&build->module, build->arena, found,
                             &least->stack_slots, &least->call_depth, why);
}

KestRuntime *kest_start(KestBuild *build, const KestHost *host,
                        const KestLimits *limits) {
    if (!build->compiled) {
        return NULL;
    }
    // A machine says what it said. Two machines from one build share the arena
    // the strings live in and nothing else, because one reporting the other's
    // failure as its own is worse than either of them saying nothing.
    KestDiags *said = KEST_ARENA_NEW(build->arena, KestDiags);
    if (said == NULL) {
        return NULL;
    }
    kest_diags_init(said, build->arena);
    return kest_runtime_new(build->arena, &build->module, host, said, limits);
}
