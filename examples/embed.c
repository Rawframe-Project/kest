// A host that is not the command line. It compiles a file, makes a machine,
// and keeps a world between frames by holding the handle the program gave it.
//
//   make embed && ./examples/embed
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "kest.h"

// The same shape `embed.kest` declares as an enum. A four byte tag at nought
// and the payload after it at its own alignment is what a C tagged union is
// and what D026 says an enum is, so this array is the array Kest walks: the
// host lends it and nothing is copied at the boundary.
enum { EVENT_IDLE, EVENT_MOVED, EVENT_HIT, EVENT_NAMED };

// The host's own type with an array in it. `float at[3]` is twelve bytes
// where it stands, and `struct Point { at: [f32; 3] }` beside it is the same
// twelve: that is what D064 is for.
typedef struct {
    float at[3];
} Point;

// The other half of the same shape: a run of the host's own structs inside a
// struct. Twenty-eight bytes, and `struct Row { cells: [Cell; 3], tag: i32 }`
// beside it is the same twenty-eight.
typedef struct {
    int32_t at;
    float weight;
} Cell;

typedef struct {
    Cell cells[3];
    int32_t tag;
} Row;

typedef struct {
    int32_t tag;
    union {
        struct {
            float x;
            float y;
        } moved;
        int32_t hit;
        const char *named;
    } as;
} Event;

// The context is whatever was given at binding, which is how a host reaches
// its own state from inside a function the program calls.
static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    fputs(frame[0].text, (FILE *)context);
}

// The engine's own policy, which asks the program. Calling in from inside a
// call the program made is what an engine does when its rules live on both
// sides, and the machine puts what this starts above what is already running.
static void engine_decide(KestValue *frame, KestRuntime *runtime,
                          void *context) {
    KestValue asked[2] = {{0}};
    asked[0] = frame[0];
    if (kest_call(runtime, *(const int32_t *)context, asked, 2)) {
        frame[0] = asked[0];
    } else {
        frame[0].integer = 1;
    }
}

// What this host calls itself, handed over as text the machine owns. Copying
// is the point: this host's own pointer would have to outlive whatever the
// program does with it.
static void engine_name(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)context;
    frame[0] = kest_text(runtime, "embed", 5);
}

// Whether the program lays a type out where this host has it. The lend
// compares the size, because the size is what it is given; this compares
// where each piece is, which is the thing two types of the same size can
// disagree about. A tagged one is walked the same way: `tagged` says some of
// the pieces are payloads whose type the tag decides, not that there is
// nothing to walk, so where they sit is still a thing the two sides can
// disagree about and this host still says where it has them.
static bool same_pieces(const KestLayout *layout, const KestPiece *mine,
                        uint16_t count, bool tagged) {
    if (layout->tagged != tagged || layout->count != count) {
        return false;
    }
    for (uint16_t i = 0; i < count; i++) {
        if (layout->pieces[i].offset != mine[i].offset ||
            layout->pieces[i].kind != mine[i].kind) {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    // NULL for the library, which is the compiler finding its own: what
    // `KEST_LIB` says, or where it was installed.
    const char *path = argc > 1 ? argv[1] : "examples/embed.kest";
    KestBuild *build = kest_build(path, NULL, stderr, KEST_FORM_TEXT);
    if (build == NULL) {
        return 1;
    }

    KestHost *host = kest_host_new();
    static int32_t rule;
    if (host == NULL || !kest_host_bind(host, "Io.write", io_write, stdout) ||
        !kest_host_bind(host, "Engine.decide", engine_decide, &rule) ||
        !kest_host_bind(host, "Engine.name", engine_name, NULL)) {
        return 1;
    }

    // What each of the functions this host binds reads out of a frame and
    // writes back into it. This is the host saying what it believes, which is
    // the point: reading it out of the program instead would be checking the
    // program against itself.
    struct {
        const char *name;
        uint32_t takes;
        bool gives;
        // What the first argument is, where this host has an opinion: the
        // number of bytes it will read out of the frame. Nought for one it
        // does not read as bytes at all, which is what a piece of text is.
        uint16_t first;
    } bound[] = {
        {"Io.write", 1, false, 0},
        {"Engine.decide", 1, true, sizeof(int32_t)},
        {"Engine.name", 0, true, 0},
    };

    // What the program asks this host for, read rather than guessed: starting
    // refuses a name that is not bound, and finding that out from the refusal
    // is finding it out one failed start at a time. Every one of them is
    // named, not the first, because a host writer wants the list.
    bool missing = false;
    for (uint32_t i = 0; kest_build_extern(build, i) != NULL; i++) {
        const char *wanted = kest_build_extern(build, i);
        void *context = NULL;
        if (kest_host_find(host, wanted, &context) == NULL) {
            fprintf(stderr, "the program asks for `%s` and nothing is bound\n",
                    wanted);
            missing = true;
            continue;
        }
        // And what it expects to cross. A function bound to a name that takes
        // one thing and written to read two reads whatever is beside it, and
        // nothing else in this crossing would say so.
        for (uint32_t b = 0; b < sizeof(bound) / sizeof(bound[0]); b++) {
            if (strcmp(bound[b].name, wanted) != 0) {
                continue;
            }
            uint32_t takes = kest_extern_takes(build, i);
            bool gives = kest_extern_gives(build, i) != NULL;
            if (takes != bound[b].takes || gives != bound[b].gives) {
                fprintf(stderr,
                        "`%s` takes %u and gives %s, and this host wrote one "
                        "that takes %u and gives %s\n",
                        wanted, takes, gives ? "something" : "nothing",
                        bound[b].takes, bound[b].gives ? "something" : "nothing");
                missing = true;
            }
            const KestLayout *first = kest_extern_layout(build, i, 0);
            if (bound[b].first > 0 && first != NULL &&
                first->size != bound[b].first) {
                fprintf(stderr,
                        "`%s` is handed %u bytes and this host reads %u\n",
                        wanted, first->size, bound[b].first);
                missing = true;
            }
        }
        printf("the program asks for `%s`, which this host provides\n", wanted);
    }
    if (missing) {
        return 1;
    }

    // What the program needs, rather than a number this host guessed. A
    // program that can reach itself has no answer, and then a guess is all
    // there is.
    KestLimits limits = {0, 0, 0};
    KestReason why = {KEST_REACH_UNASKED, NULL};
    if (kest_needs(build, &limits, &why)) {
        // What the program needs for one call in. This host calls back in
        // from inside one, so it asks for room for another on top: what the
        // program says covers the call it makes, and the one made from inside
        // it is this host's to account for.
        printf("the program needs %u slots and %u frames\n",
               limits.stack_slots, limits.call_depth);
        // And what the one this host actually drives needs on its own. A host
        // that knows which functions it calls is not made to pay for the
        // deepest of the ones it never will; this one asks, prints the
        // difference and then takes the whole program's number anyway,
        // because it calls more than one.
        KestLimits stepping = {0, 0, 0};
        if (kest_needs_of(build, "step", &stepping, NULL) &&
            stepping.stack_slots < limits.stack_slots) {
            printf("  `step` alone needs %u slots and %u frame%s\n",
                   stepping.stack_slots, stepping.call_depth,
                   stepping.call_depth == 1 ? "" : "s");
        }
        limits.stack_slots *= 2;
        limits.call_depth *= 2;
        // A frame budget is a ceiling as well as a floor. The heap is the one
        // that grows while the program runs, so this host says how much of it
        // the program may have rather than finding out afterwards.
        limits.heap_bytes = 1024 * 1024;
    } else {
        printf("the program has no deepest call: `%s` %s; giving it room\n",
               why.where,
               why.reach == KEST_REACH_ITSELF ? "can reach itself"
                                              : "calls through a value");
        limits.stack_slots = 4096;
        limits.call_depth = 64;
    }
    // What the program thinks these are, asked once. A host lending in a loop
    // has nothing else to check its own declarations against, and finding out
    // at the first lend is finding out late.
    // Where this host's own fields are, written out from its own types with
    // `offsetof`. This is the host saying what it believes, which is the
    // point: reading it out of the layout instead would be checking the
    // layout against itself, and the thing worth catching is the two sides
    // disagreeing. One piece a slot, each a byte offset and what is there.
    KestPiece point[3];
    for (size_t k = 0; k < 3; k++) {
        point[k].offset = (uint16_t)(offsetof(Point, at) + k * sizeof(float));
        point[k].kind = KEST_L_F32;
    }
    KestPiece row[7];
    for (size_t k = 0; k < 3; k++) {
        size_t cell = offsetof(Row, cells) + k * sizeof(Cell);
        row[k * 2].offset = (uint16_t)(cell + offsetof(Cell, at));
        row[k * 2].kind = KEST_L_I32;
        row[k * 2 + 1].offset = (uint16_t)(cell + offsetof(Cell, weight));
        row[k * 2 + 1].kind = KEST_L_F32;
    }
    row[6].offset = (uint16_t)offsetof(Row, tag);
    row[6].kind = KEST_L_I32;

    // The tag, and then a slot per thing the widest case carries, which is
    // the case that decided how big this is. Which type each of those holds
    // depends on the tag, so this host says `payload` for them as the program
    // does — but where they sit is not a matter of opinion, and `Moved` two
    // floats into the union is where the program has to have put them.
    KestPiece event[3];
    event[0].offset = (uint16_t)offsetof(Event, tag);
    event[0].kind = KEST_L_I32;
    event[1].offset = (uint16_t)offsetof(Event, as.moved.x);
    event[1].kind = KEST_L_PAYLOAD;
    event[2].offset = (uint16_t)offsetof(Event, as.moved.y);
    event[2].kind = KEST_L_PAYLOAD;

    const struct {
        const char *name;
        size_t size;
        const KestPiece *pieces;
        uint16_t count;
        bool tagged;
        uint16_t align;
    } lending[] = {{"Point", sizeof(Point), point, 3, false, _Alignof(Point)},
                   {"Row", sizeof(Row), row, 7, false, _Alignof(Row)},
                   {"Event", sizeof(Event), event, 3, true, _Alignof(Event)}};
    for (size_t i = 0; i < sizeof(lending) / sizeof(lending[0]); i++) {
        const KestLayout *layout = NULL;
        if (kest_build_layout(build, lending[i].name, &layout) != 1) {
            fprintf(stderr, "the program has no one `%s` to lend to\n",
                    lending[i].name);
            return 1;
        }
        if (layout->size != lending[i].size) {
            fprintf(stderr, "`%s` is %u bytes there and %zu here\n",
                    lending[i].name, layout->size, lending[i].size);
            return 1;
        }
        // Where the pieces are is where they are inside one of these. What
        // the whole of it is aligned to is where a host may put one, and a
        // host lending an array of something the program reads eight bytes at
        // a time has to have put them where eight byte reads are allowed.
        // The size does not say it: two types of one size can be aligned
        // differently, and the pieces do not say it either when one of them
        // is a payload whose type the tag decides.
        if (layout->align != lending[i].align) {
            fprintf(stderr, "`%s` is aligned to %u there and %u here\n",
                    lending[i].name, layout->align, lending[i].align);
            return 1;
        }
        // The size is what the lend itself compares, because the size is all
        // it is given. Two types of the same size with their fields in a
        // different order are the same size, so a host that cares compares
        // where the fields are, which is what the layout says piece by piece.
        if (!same_pieces(layout, lending[i].pieces, lending[i].count,
                         lending[i].tagged)) {
            fprintf(stderr, "`%s` is laid out differently here\n",
                    lending[i].name);
            return 1;
        }
        printf("`%s` is %u bytes in %u slots, aligned to %u\n",
               lending[i].name, layout->size, layout->count, layout->align);
    }

    KestRuntime *runtime = kest_start(build, host, &limits);
    if (runtime == NULL) {
        // Nothing started, so there is nothing to ask what went wrong: what a
        // host has then is the build, and it has been told.
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 1;
    }

    // The arguments go where the result comes back, so a frame has to be
    // wide enough for whichever is wider. The program says which, rather than
    // this host guessing and being told at the first call that is too narrow.
    KestValue frame[6] = {{0}};
    const char *wanted[] = {"create", "spawn", "step", "onEvents", "silence",
                            "heaviest",
                            "lengthOf",
                            "between",
                            "spread"};
    rule = kest_entry(runtime, "rule");
    int32_t entry[sizeof(wanted) / sizeof(wanted[0])];
    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
        // Found once, at the start. What a name means is a search over
        // everything the program defines, and a frame should not do one. The
        // name is the one the file writes; that it registered them under
        // `embed` is not this host's business.
        // A name that is several functions has no one index, and asking for
        // the second one says whether this is such a name without asking for
        // an index that is not there. This host means the one that takes a
        // `Point`, so it walks them and asks each what it takes.
        if (kest_entry_of(runtime, wanted[i], 1) >= 0) {
            entry[i] = -1;
            for (uint32_t at = 0; entry[i] < 0; at++) {
                int32_t candidate = kest_entry_of(runtime, wanted[i], at);
                if (candidate < 0) {
                    break;
                }
                const KestLayout *first =
                    kest_frame_layout(runtime, candidate, 0);
                if (first != NULL && first->size == sizeof(Point) &&
                    first->align == _Alignof(Point) &&
                    same_pieces(first, point, 3, false)) {
                    entry[i] = candidate;
                }
            }
        } else {
            entry[i] = kest_entry(runtime, wanted[i]);
        }
        if (entry[i] < 0 ||
            kest_frame_slots(runtime, entry[i]) >
                sizeof(frame) / sizeof(frame[0])) {
            fprintf(stderr, "`%s` is not there or needs more than %zu slots\n",
                    wanted[i], sizeof(frame) / sizeof(frame[0]));
            return 1;
        }
    }
    enum { CREATE, SPAWN, STEP, ON_EVENTS, SILENCE, HEAVIEST, LENGTH_OF,
           BETWEEN, SPREAD };
    if (!kest_call(runtime, entry[CREATE], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        return 1;
    }
    KestValue world = frame[0];

    for (int i = 0; i < 5; i++) {
        frame[0] = world;
        frame[1].integer = i + 1;
        if (!kest_call(runtime, entry[SPAWN], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
            return 1;
        }
        printf("frame %d: spawned, %lld alive\n", i, (long long)frame[0].integer);
    }

    for (int i = 0; i < 5; i++) {
        frame[0] = world;
        if (!kest_call(runtime, entry[STEP], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
            return 1;
        }
        printf("frame %d: stepped, %lld alive, %zu bytes\n", i + 5,
               (long long)frame[0].integer, kest_heap_used(runtime));
    }

    // The same answer twice: what this host makes of a slot, and what the
    // program writes for the type it declared it as. A host that does not want
    // to know how a number is spelt asks for the words, and the number it gets
    // back is the room they need.
    char said[32];
    char mine[32];
    int64_t room = kest_gave_text(runtime, entry[STEP], frame, said,
                                  sizeof(said));
    snprintf(mine, sizeof(mine), "%lld", (long long)frame[0].integer);
    if (room < 0 || (size_t)room >= sizeof(said) || strcmp(said, mine) != 0) {
        fprintf(stderr, "what came back reads as `%s` and is %s\n", said,
                mine);
        return 1;
    }
    printf("what `step` gave, in the program's own words: %s\n", said);

    // And a store is a thing the language has no text for, which it says
    // rather than inventing one. What the host wants of a store, only the host
    // knows.
    if (kest_gave_text(runtime, entry[CREATE], frame, said, sizeof(said))
        >= 0) {
        fprintf(stderr, "a store has no text and something wrote one\n");
        return 1;
    }

    // A struct passed by value rather than lent: one slot a scalar, in the
    // order the fields are declared, and a float is a double in a slot even
    // where it is an `f32` in memory. Lending shares the host's bytes; this
    // copies three numbers into the frame, which is the crossing D007 says to
    // reach for one item at a time and not for a batch.
    frame[0].real = 1.0;
    frame[1].real = 2.0;
    frame[2].real = 2.0;
    if (!kest_call(runtime, entry[LENGTH_OF], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host passed a point by value: %g\n", frame[0].real);

    // Two of them, where the host would otherwise have to count the first
    // one's scalars to know where the second begins. The program knows, so it
    // is asked.
    // And what the argument is, checked the way anything lent is: the same
    // layout, the same pieces, the same `offsetof` on this side. A frame of
    // the right width with the wrong things in it is the mistake this catches.
    const KestLayout *takes = kest_frame_layout(runtime, entry[BETWEEN], 1);
    if (takes == NULL || takes->size != sizeof(Point) ||
        takes->align != _Alignof(Point) ||
        !same_pieces(takes, point, 3, false)) {
        fprintf(stderr, "`between` does not take a `Point` this host knows\n");
        return 1;
    }
    // And what comes back, which is read as a `double` in a slot and is an
    // `f32` in memory: what the layout says is which of the two the program
    // means, and this host reads `frame[0].real` because of it.
    const KestLayout *gives = kest_frame_gives(runtime, entry[BETWEEN]);
    if (gives == NULL || gives->count != 1 ||
        gives->pieces[0].kind != KEST_L_F32) {
        fprintf(stderr, "`between` does not give back one `f32`\n");
        return 1;
    }
    uint32_t second = kest_frame_at(runtime, entry[BETWEEN], 1);
    for (uint32_t k = 0; k < 3; k++) {
        frame[k].real = (double)k;
        frame[second + k].real = (double)k + 1.0;
    }
    if (!kest_call(runtime, entry[BETWEEN], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("%u arguments, the second at slot %u: %g between them\n",
           kest_frame_takes(runtime, entry[BETWEEN]), second, frame[0].real);

    // A struct of the host's with an array inside it, lent by name. A run on
    // its own has no name to lend against, which is what `Point` is for.
    Point corners[4];
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 3; k++) {
            corners[i].at[k] = (float)(i * 3 + k);
        }
    }
    frame[0] = kest_borrow(runtime, corners, 4, "Point", sizeof(Point));
    if (frame[0].object == NULL) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!kest_call(runtime, entry[SPREAD], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host lent %zu byte points: %g across\n", sizeof(Point),
           (double)frame[0].real);

    // A run of the host's structs inside a struct of the host's, walked in
    // place. Every offset in it is one both sides worked out on their own.
    Row rows[2];
    for (int i = 0; i < 2; i++) {
        rows[i].tag = i + 1;
        for (int k = 0; k < 3; k++) {
            rows[i].cells[k].at = k + 1;
            rows[i].cells[k].weight = (float)(i * 3 + k) * 0.5f;
        }
    }
    frame[0] = kest_borrow(runtime, rows, 2, "Row", sizeof(Row));
    if (frame[0].object == NULL) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!kest_call(runtime, entry[HEAVIEST], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host lent %zu byte rows: heaviest is %lld\n", sizeof(Row),
           (long long)frame[0].integer);

    // A batch the host owns, walked in place. D007 measured the inward
    // crossing as the wider of the two, so one call carries the whole batch
    // rather than one call per event.
    Event events[4];
    events[0].tag = EVENT_HIT;
    events[0].as.hit = 4;
    events[1].tag = EVENT_MOVED;
    events[1].as.moved.x = 1.5f;
    events[1].as.moved.y = 2.5f;
    events[2].tag = EVENT_IDLE;
    events[3].tag = EVENT_NAMED;
    events[3].as.named = "trap";

    // The stride is the program's own, so what this host has to get right is
    // only that its `Event` is the program's `Event`. Saying `sizeof` is what
    // makes a disagreement a message rather than a wrong read.
    frame[0] = kest_borrow(runtime, events, 4, "Event", sizeof(Event));
    if (frame[0].object == NULL) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!kest_call(runtime, entry[ON_EVENTS], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        kest_report(runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host lent %zu byte events: %lld damage\n", sizeof(Event),
           (long long)frame[0].integer);

    // And a lend this host is not allowed to make. Where the array sits is
    // the one thing about a lend that nothing in the program decides, so
    // asking for the refusal on purpose is the only way anybody sees it: half
    // an alignment into a properly aligned array is an address an `Event` may
    // not sit at, and the program would be reading its payload across a word
    // boundary the C standard has no answer for.
    Event aligned[2];
    void *crooked = (unsigned char *)(void *)aligned + _Alignof(Event) / 2;
    if (kest_borrow(runtime, crooked, 1, "Event", sizeof(Event)).object !=
        NULL) {
        fprintf(stderr, "a lend at a crooked address was allowed\n");
        return 1;
    }
    printf("a lend %zu bytes into an `Event` was refused\n",
           _Alignof(Event) / 2);

    // And back the other way: what the program writes is what the host reads,
    // because there is one copy of it.
    KestValue lent = kest_borrow(runtime, events, 4, "Event", sizeof(Event));
    frame[0] = lent;
    frame[1].integer = 0;
    if (!kest_call(runtime, entry[SILENCE], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        return 1;
    }
    printf("silenced the first: tag is now %d\n", events[0].tag);

    frame[0] = lent;
    if (!kest_call(runtime, entry[ON_EVENTS], frame,
                   sizeof(frame) / sizeof(frame[0]))) {
        return 1;
    }
    printf("host reads it back: %lld damage\n", (long long)frame[0].integer);

    // What the machine is running with, asked of the machine rather than kept
    // beside it: a number allocated is a number without a scale on its own.
    KestLimits allowed = {0, 0, 0};
    kest_allowed(runtime, &allowed);
    printf("used %zu of %zu bytes, in %u slots and %u frames\n",
           kest_heap_used(runtime), allowed.heap_bytes, allowed.stack_slots,
           allowed.call_depth);

    kest_runtime_free(runtime);
    kest_host_free(host);
    kest_build_free(build);
    return 0;
}
