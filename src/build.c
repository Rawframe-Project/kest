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
    build->reported = 0;
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
        // Nothing to read and nothing said, which is a stage that could not
        // write down either the program or what was wrong with it. A caller
        // that is told no and given no reason is a command that stops and
        // prints nothing, and answers as though it had worked.
        if (build->diags.error_count == 0) {
            kest_diags_starve(&build->diags);
        }
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
    // A body with the end missing reads as an instruction of the wrong width
    // to the proof below, which would say the two halves of this compiler
    // disagree about what a program is. What happened is that the host had
    // nothing left, and this is where that is noticed. See D750.
    if (build->module.out_of_room) {
        kest_diags_starve(&build->diags);
    }
    // The promise was checked against the tree; this holds it against what was
    // emitted. If the two disagree the tree walk missed something, and finding
    // that out here beats finding it out in a frame (D058).
    if (build->diags.error_count == 0) {
        kest_module_prove(&build->module, build->arena, &build->diags);
    }
    build->compiled = build->diags.error_count == 0;
    // And the trees, which nothing reads once every copy has been compiled and
    // the promise has been held against what was emitted. What it cost is
    // already counted, where each file was read. See D748.
    if (build->units.trees != NULL) {
        kest_arena_returned(build->arena, kest_arena_held(build->units.trees));
        kest_arena_free(build->units.trees);
        build->units.trees = NULL;
    }
    return build->compiled;
}

KestBuild *kest_build(const char *path, const char *library, FILE *errors,
                      KestForm form) {
    char *paths[1] = {(char *)path};
    KestBuild *build = kest_build_open(library, paths, 1);
    if (build == NULL) {
        // Nothing was made, so there is nothing to ask what went wrong: a
        // host that got NULL here and called `kest_build_report` would be
        // handing it the nothing it was given. There is one reason to be here
        // and it is said in the form the caller asked for, through the writer
        // every other diagnostic goes through, because a shape written twice
        // is a shape that comes apart.
        kest_diags_say_one(errors, form == KEST_FORM_JSON, "K0705",
                           "there is not enough memory to read a program");
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

size_t kest_build_cost(const KestBuild *build) {
    // Nought for no build, which is the same answer as a build that has read
    // nothing: a host that was handed NULL asked about a thing that is not
    // there, and there is nothing for it to have cost.
    return build == NULL ? 0 : kest_arena_used(build->arena);
}

size_t kest_build_held(const KestBuild *build) {
    return build == NULL ? 0 : kest_arena_held(build->arena);
}

const char *kest_build_read(const KestBuild *build, uint32_t at) {
    // Past the last one is NULL rather than a refusal, because walking to the
    // end is how a host learns how many there are: a walk that has to ask the
    // count first is two questions for one answer, which is what
    // `kest_build_extern` above it settled.
    if (build == NULL || at >= build->units.count) {
        return NULL;
    }
    return build->units.items[at].source.path;
}

size_t kest_build_read_bytes(const KestBuild *build, uint32_t at) {
    if (build == NULL || at >= build->units.count) {
        return 0;
    }
    return build->units.items[at].source.length;
}

uint64_t kest_build_read_mark(const KestBuild *build, uint32_t at) {
    if (build == NULL || at >= build->units.count) {
        return 0;
    }
    return build->units.items[at].source.mark;
}

uint64_t kest_build_mark(const KestBuild *build) {
    if (build == NULL || build->units.count == 0) {
        return 0;
    }
    // The files' own marks, folded in the order they were read. Folded here
    // rather than left to a host: two hosts that combined them their own way
    // would have two numbers for one program, and the point of the number is
    // that it is the same everywhere. See D658.
    uint64_t mark = KEST_MARK_START;
    for (uint32_t at = 0; at < build->units.count; at++) {
        mark = kest_mark_number(mark, build->units.items[at].source.mark, 8);
    }
    return mark;
}

uint64_t kest_build_code_mark(const KestBuild *build) {
    if (build == NULL || !build->compiled) {
        return 0;
    }
    return kest_module_mark(&build->module);
}

size_t kest_build_source(const KestBuild *build) {
    if (build == NULL) {
        return 0;
    }
    size_t bytes = 0;
    for (uint32_t at = 0; at < build->units.count; at++) {
        bytes += build->units.items[at].source.length;
    }
    return bytes;
}

void kest_build_report(KestBuild *build, FILE *out, KestForm form) {
    if (build == NULL || out == NULL) {
        return;
    }
    // A run that ran out has one thing to say and nowhere it was written down,
    // so it is said here rather than found in the list. It is said once, like
    // everything else in the list is.
    bool starving = build->diags.starved && !build->starve_said;
    if (build->reported >= build->diags.count && !starving) {
        return;
    }
    // The tail rather than anything taken out, the way a machine reports what
    // it has said: what came before was written when it was said.
    KestDiags tail = build->diags;
    tail.items = build->diags.items + build->reported;
    tail.count = build->diags.count - build->reported;
    tail.error_count = 0;
    for (uint32_t i = 0; i < tail.count; i++) {
        if (tail.items[i].severity == KEST_SEVERITY_ERROR) {
            tail.error_count++;
        }
    }
    tail.starved = starving;
    tail.error_count += starving ? 1 : 0;
    if (form == KEST_FORM_JSON) {
        kest_diags_render_json(&tail, out);
    } else {
        kest_diags_render(&tail, out);
    }
    build->reported = build->diags.count;
    build->starve_said = build->starve_said || starving;
}

// The same rule the frame questions follow, one crossing over. What a host is
// asked for is walked with `kest_build_extern` until it answers nothing, and
// every other question about the one at a place answers a host past the end
// the way it answers a host about a real one that takes nothing or gives
// nothing back. So the end of the walk is silent and asking past it is not.
// See D436.
static bool no_extern_at(const KestBuild *build, uint32_t at) {
    if (build == NULL) {
        return true;
    }
    if (at < build->module.extern_count) {
        return false;
    }
    KestSpan nowhere = {0, 0};
    KestBuild *said = (KestBuild *)build;
    kest_diags_in(&said->diags, NULL);
    kest_diags_add(&said->diags, KEST_SEVERITY_ERROR, "K0648", nowhere,
                   "this program asks the host for %u function%s and there is "
                   "nothing at %u",
                   build->module.extern_count,
                   build->module.extern_count == 1 ? "" : "s", at);
    kest_diags_suggest(&said->diags,
                       "`kest_build_extern` gives the name of the one at a "
                       "place and nothing past the last, which is where a walk "
                       "of them ends");
    return true;
}

const char *kest_build_extern(const KestBuild *build, uint32_t at) {
    if (build == NULL || at >= build->module.extern_count) {
        return NULL;
    }
    return build->module.externs[at].name;
}

uint32_t kest_extern_takes(const KestBuild *build, uint32_t at) {
    if (no_extern_at(build, at)) {
        return 0;
    }
    return build->module.externs[at].takes_count;
}

const KestLayout *kest_extern_layout(const KestBuild *build, uint32_t at,
                                     uint32_t which) {
    if (no_extern_at(build, at)) {
        return NULL;
    }
    const KestExtern *one = &build->module.externs[at];
    // Past the last argument is the walk ending, which is not the same news.
    if (which >= one->takes_count) {
        return NULL;
    }
    return &build->module.layouts[one->takes[which]];
}

const KestLayout *kest_extern_gives(const KestBuild *build, uint32_t at) {
    if (no_extern_at(build, at)) {
        return NULL;
    }
    const KestExtern *one = &build->module.externs[at];
    return one->gives_value ? &build->module.layouts[one->gives] : NULL;
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

bool kest_build_free(KestBuild *build) {
    if (build == NULL) {
        // Nothing to free is not a refusal, the same as freeing no machine.
        return true;
    }
    if (build->module.machines > 0) {
        // The program is in here and the machines are standing on it: what
        // they run, what their layouts say, and every piece of text a
        // diagnostic points at are all on this arena. Freeing it under them is
        // not something they survive, so it is refused where it is asked for.
        KestSpan nowhere = {0, 0};
        kest_diags_in(&build->diags, NULL);
        kest_diags_add(&build->diags, KEST_SEVERITY_ERROR, "K0640", nowhere,
                       "this build cannot be freed while %u machine%s standing "
                       "on it",
                       build->module.machines,
                       build->module.machines == 1 ? " is" : "s are");
        kest_diags_suggest(&build->diags,
                           "free every machine this build made, and then the "
                           "build");
        return false;
    }
    // A build that was never compiled still has its trees, because what gives
    // them back is the last stage that reads them. See D748.
    if (build->units.trees != NULL) {
        kest_arena_free(build->units.trees);
        build->units.trees = NULL;
    }
    kest_arena_free(build->arena);
    return true;
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
    // Already under a module, which is how `check` prints it and therefore how
    // somebody types it: `math.factorial` is not `math.math.factorial`. Any
    // dot at all, and not this module's own name, because a program is the
    // file that was named and everything it imports — `shapes.doubled` in a
    // file that imports `shapes` is a function of this program, and putting
    // the root module in front of it made a name nobody could have typed and
    // then said that name back. See D341.
    if (strchr(name, '.') != NULL) {
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

// The whole program's walk, worked out the first time anybody asks for it and
// answered out of the build after. What it costs is scratch — six arrays a
// function wide — and for `examples/embed.kest` that is 1596 bytes against the
// 600 a machine is made of, so a host that makes a machine a frame was paying
// for the same walk every frame. The module it walks does not change after it
// is compiled, so neither does the answer. See D607.
static const KestWalk *walk_it(KestBuild *build) {
    if (!build->walked.taken) {
        build->walked.taken = true;
        build->walked.measured = kest_module_needs(
            &build->module, build->arena, -1, &build->walked.slots,
            &build->walked.frames, &build->walked.host_slots,
            &build->walked.host_frames, NULL, &build->walked.why);
        // And what a program with no least is bounded by, worked out only
        // when there is no least to have: the widest body, which one walk
        // answers along with which functions go round and how wide the ones
        // that do not are altogether. See D815 and D816.
        if (!build->walked.measured) {
            kest_module_cycles(&build->module, build->arena, -1,
                               &build->walked.widest, &build->walked.in_a_turn,
                               &build->walked.off_the_turns);
        }
    }
    return &build->walked;
}

bool kest_needs(KestBuild *build, KestLimits *least, KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    // A build that did not compile is not one a host can be holding —
    // `kest_build` frees it and answers NULL — so this is the same nothing as
    // a NULL: what could be said about it is a program's worth of diagnostics
    // and none of them is a reason there is no least. See D566.
    if (build == NULL || least == NULL || !build->compiled) {
        return false;
    }
    const KestWalk *walked = walk_it(build);
    if (!walked->measured) {
        *why = walked->why;
        return false;
    }
    // The name the walk found is where the program calls into the host, which
    // is what `kest_needs_from` was asked and this was not.
    why->reach = KEST_REACH_KNOWN;
    least->stack_slots = walked->slots;
    least->call_depth = walked->frames;
    // And nought for the heap, which is not a number a program has: what one
    // allocates is what it is given to work on, and a loop over four events
    // and a loop over four thousand are the same program. Written rather than
    // left alone, because a field an answer does not touch is one a caller
    // cannot tell from one it did — and these three doors answer into the same
    // shape, so all three say the same nothing about the one thing none of
    // them knows. A host's own cap goes on after asking. See D724.
    least->heap_bytes = 0;
    return true;
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
        why->reach = KEST_REACH_NO_NAME;
        return false;
    }
    if (!kest_module_needs(&build->module, build->arena, found,
                           &least->stack_slots, &least->call_depth, NULL, NULL,
                           NULL, why)) {
        return false;
    }
    least->heap_bytes = 0;
    return true;
}

bool kest_bound_of(KestBuild *build, const char *name, uint32_t frames,
                   KestLimits *most, KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || name == NULL || most == NULL || !build->compiled) {
        return false;
    }
    // The same lookup the two above do, which is the one `kest_entry` does:
    // a host cannot be bounded for a function it cannot call.
    int32_t about = kest_module_entry(&build->module, name);
    if (about < 0) {
        why->reach = KEST_REACH_NO_NAME;
        return false;
    }
    most->heap_bytes = 0;
    // A least is better than a bound, so a name that has one is answered with
    // it and the frames are not looked at.
    if (kest_module_needs(&build->module, build->arena, about,
                          &most->stack_slots, &most->call_depth, NULL, NULL,
                          NULL, why)) {
        why->reach = KEST_REACH_KNOWN;
        return true;
    }
    // And a name that has none is bounded by the two readings of a chain of
    // frames, over what this one reaches rather than over the whole program.
    // The reason the walk gave is kept: a host is told a bound and why there
    // was nothing better. See D817.
    if (why->reach == KEST_REACH_NO_ROOM) {
        return false;
    }
    uint32_t widest = 0;
    uint32_t in_a_turn = 0;
    uint32_t off_the_turns = 0;
    kest_module_cycles(&build->module, build->arena, about, &widest,
                       &in_a_turn, &off_the_turns);
    if (widest == 0) {
        why->reach = KEST_REACH_NO_ROOM;
        return false;
    }
    uint32_t allowed = frames == 0 ? KEST_CALL_DEPTH : frames;
    uint64_t a_frame_each = (uint64_t)widest * allowed;
    uint64_t a_turn_each =
        (uint64_t)off_the_turns + (uint64_t)in_a_turn * allowed;
    if (in_a_turn > 0 && a_turn_each < a_frame_each) {
        a_frame_each = a_turn_each;
    }
    most->stack_slots = a_frame_each < (uint64_t)KEST_STACK_SLOTS
                            ? (uint32_t)a_frame_each
                            : KEST_STACK_SLOTS;
    most->call_depth = allowed;
    return true;
}

bool kest_needs_from(KestBuild *build, const char *name, KestLimits *inside,
                     KestReason *why) {
    KestReason ignored;
    if (why == NULL) {
        why = &ignored;
    }
    why->reach = KEST_REACH_UNASKED;
    why->where = NULL;
    if (build == NULL || inside == NULL || !build->compiled) {
        return false;
    }
    int32_t found = -1;
    if (name != NULL) {
        found = kest_module_entry(&build->module, name);
        if (found < 0) {
            why->reach = KEST_REACH_NO_NAME;
            return false;
        }
    }
    // The two a host is being told about are where a call into the host
    // happens; what that call itself costs the machine is nothing, because a
    // host function runs on the host's own stack.
    if (found < 0) {
        const KestWalk *walked = walk_it(build);
        *why = walked->why;
        if (!walked->measured) {
            return false;
        }
        inside->stack_slots = walked->host_slots;
        inside->call_depth = walked->host_frames;
        inside->heap_bytes = 0;
        return true;
    }
    uint32_t reached = 0;
    uint32_t deep = 0;
    if (!kest_module_needs(&build->module, build->arena, found, &reached, &deep,
                           &inside->stack_slots, &inside->call_depth, NULL,
                           why)) {
        return false;
    }
    inside->heap_bytes = 0;
    return true;
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
        // Nowhere to put what this machine would have said, which is the one
        // refusal that cannot be written down. The build is told the one thing
        // that can be recorded without room to record it.
        kest_diags_starve(&build->diags);
        return NULL;
    }
    kest_diags_init(said, build->arena);
    KestRuntime *runtime =
        kest_runtime_new(&build->module, host, said, limits, walk_it(build));
    if (runtime == NULL) {
        // A machine that never started has nothing to be asked, so what it
        // said on the way out is given to the build: that is what a host has
        // when there is no machine, and a refusal nobody can read is a refusal
        // that did not happen.
        kest_diags_absorb(&build->diags, said);
    }
    return runtime;
}
