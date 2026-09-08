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
// Which of the names this host looks up is which. Every part of the run
// below asks for one of these, so they are named here rather than inside
// the one function that used to be all of it.
enum { CREATE, SPAWN, STEP, ON_EVENTS, SILENCE, HEAVIEST, LENGTH_OF,
       BETWEEN, SPREAD, HOARD, PILE, CHURN, READY, FILLING, GLUED,
       JOINED, REPEATED, JOINED_PIECES, READABLE, GREW, POPPED, TOOK,
       EMPTIED, UNDER };

// What this host is between calls. A host that runs a program every frame
// holds exactly this: the machine, the names it looked up once because a
// lookup is a search over everything the program defines, the frame it calls
// with, and the world the program handed it. A run of locals in one function
// is what this was, and a host writer reading it would have to guess which of
// them their own engine wants.
typedef struct {
    KestRuntime *runtime;
    int32_t entry[UNDER + 1];
    // Wide enough for whichever is wider, what is passed or what comes back,
    // because they are the same slots. The program says how many.
    KestValue frame[6];
    KestValue world;
} Engine;

static void io_write(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    fputs(frame[0].text, (FILE *)context);
}

// What this host decides with, and the reason it is a thing rather than a
// number: a name is bound once, so a host that wants to answer differently
// later binds one function that decides and changes what it decides with.
typedef struct {
    // Where the program's own opinion is, which this host asks for while it
    // is asking.
    int32_t rule;
    // And what it answers when it has stopped asking.
    int32_t itself;
    bool asks_the_program;
} Decider;

// The engine's own policy. Asking the program is calling in from inside a call
// the program made, which is what an engine does when its rules live on both
// sides, and the machine puts what this starts above what is already running.
// Answering by itself is the same function on a different day.
static void engine_decide(KestValue *frame, KestRuntime *runtime,
                          void *context) {
    Decider *decider = context;
    if (!decider->asks_the_program) {
        frame[0].integer = decider->itself;
        return;
    }
    KestValue asked[2] = {{0}};
    asked[0] = frame[0];
    if (kest_call(runtime, decider->rule, asked, 2)) {
        frame[0] = asked[0];
    } else {
        frame[0].integer = 1;
    }
}

// What this host calls itself, handed over as text the machine owns. Copying
// is the point: this host's own pointer would have to outlive whatever the
// program does with it.
//
// It says what it is doing as well as what it is called, because a host that
// changes its mind is two things to a program that asks.
static void engine_name(KestValue *frame, KestRuntime *runtime, void *context) {
    const Decider *decider = context;
    const char *said = decider == NULL || decider->asks_the_program
                           ? "embed, asking"
                           : "embed, deciding";
    frame[0] = kest_text(runtime, said, (uint32_t)strlen(said));
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

// Where this host's own three floats are, written out from its own type with
// `offsetof`. Two places want the answer — what is lent and what a frame
// takes — and one of them asking the other would be checking a belief against
// itself.
static void point_pieces(KestPiece point[3]) {
    for (size_t k = 0; k < 3; k++) {
        point[k].offset = (uint16_t)(offsetof(Point, at) + k * sizeof(float));
        point[k].kind = KEST_L_F32;
    }
}

// What this host believes about the types it lends, asked once and before
// anything runs. A host lending in a loop has nothing else to check its own
// declarations against, and finding out at the first lend is finding out
// late.
static bool lays_them_out_the_same(KestBuild *build) {
    // What the program thinks these are, asked once. A host lending in a loop
    // has nothing else to check its own declarations against, and finding out
    // at the first lend is finding out late.
    // Where this host's own fields are, written out from its own types with
    // `offsetof`. This is the host saying what it believes, which is the
    // point: reading it out of the layout instead would be checking the
    // layout against itself, and the thing worth catching is the two sides
    // disagreeing. One piece a slot, each a byte offset and what is there.
    KestPiece point[3];
    point_pieces(point);
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
            return false;
        }
        if (layout->size != lending[i].size) {
            fprintf(stderr, "`%s` is %u bytes there and %zu here\n",
                    lending[i].name, layout->size, lending[i].size);
            return false;
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
            return false;
        }
        // The size is what the lend itself compares, because the size is all
        // it is given. Two types of the same size with their fields in a
        // different order are the same size, so a host that cares compares
        // where the fields are, which is what the layout says piece by piece.
        if (!same_pieces(layout, lending[i].pieces, lending[i].count,
                         lending[i].tagged)) {
            fprintf(stderr, "`%s` is laid out differently here\n",
                    lending[i].name);
            return false;
        }
        printf("`%s` is %u bytes in %u slots, aligned to %u\n",
               lending[i].name, layout->size, layout->count, layout->align);
    }
    return true;
}

// A run of the host's own bytes, which is the smallest thing there is to
// lend: one byte a slot. What a program may do with one is read it, write
// it, and make text of it — and what it may not do is change how many
// there are, because the length is the host's.

// One call, made the way every call here is made: the whole frame, because the
// program says how many slots it needs and this host gave it room for the
// widest of them. What comes back is in the same slots.
static bool asks(Engine *engine, int32_t which) {
    return kest_call(engine->runtime, engine->entry[which], engine->frame,
                     sizeof(engine->frame) / sizeof(engine->frame[0]));
}

static bool lends_bytes(Engine *engine) {
    // The host's own bytes, lent as bytes rather than as text. A lend says a
    // name, a size, an address and a count, and never what is in the memory —
    // which is right, because a run of bytes may hold anything. Where that
    // stops being true is `text`, which the program asks for here: text ends
    // at its first nought and these do not have one.
    unsigned char letters[] = {'k', 'e', 's', 't'};
    engine->frame[0] = kest_borrow(engine->runtime, letters, 4, "u8", sizeof(letters[0]));
    if (engine->frame[0].object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    size_t before_text = kest_heap_used(engine->runtime);
    if (!kest_call(engine->runtime, engine->entry[READABLE], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    // A lend copies nothing; making text of one copies everything. This is
    // the one place that promise ends, and the number says so: the bytes are
    // the host's and the text is the program's.
    size_t copied = kest_heap_used(engine->runtime) - before_text;
    printf("host lent %zu bytes and the program read %lld of them, "
           "at %zu bytes of heap\n",
           sizeof(letters), (long long)engine->frame[0].integer, copied);
    if (copied < sizeof(letters)) {
        fprintf(stderr, "text of a lent run cost less than the run\n");
        return false;
    }

    // And the same bytes with a nought among them, which is a run of bytes a
    // program may hold and may not make text of. Nothing refuses the lend,
    // because nothing about it is wrong; what refuses is the asking.
    letters[2] = 0;
    engine->frame[0] = kest_borrow(engine->runtime, letters, 4, "u8", sizeof(letters[0]));
    if (engine->frame[0].object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    if (kest_call(engine->runtime, engine->entry[READABLE], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        fprintf(stderr, "text was made of bytes with a nought among them\n");
        return false;
    }
    printf("and refused to read them with a nought among them\n");

    // And the four things that would change how many there are. The length of
    // a lent run is the host's, so a program may read and write what is there
    // and may not make it longer or shorter. Nothing here had ever asked, so
    // the refusals were four sentences nobody had heard.
    const int32_t changes[4] = {GREW, POPPED, TOOK, EMPTIED};
    const char *changed_it[4] = {"push", "pop", "remove", "clear"};
    for (int which = 0; which < 4; which++) {
        engine->frame[0] = kest_borrow(engine->runtime, letters, 4, "u8", sizeof(letters[0]));
        if (engine->frame[0].object == NULL) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        if (kest_call(engine->runtime, engine->entry[changes[which]], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
            fprintf(stderr, "`%s` changed how many the host lent\n",
                    changed_it[which]);
            return false;
        }
    }
    printf("and refused every way of changing how many there are\n");
    return true;
}

// What a budget is for, from both sides: a program that spends the heap it
// was given and is told so, and one that stays inside it because a store
// hands back the room of what it drops. The first two say the message and
// the third says there is nothing to say.
static bool spends_the_heap(Engine *engine) {
    // And the other half of a budget, which is what happens when a program
    // spends it. Everything above stays inside a megabyte without trying;
    // this one asks for more, so that the message a host gets is one this
    // host has seen rather than one it is promised. Last, because a heap
    // thrown away takes the world with it.
    if (kest_call(engine->runtime, engine->entry[HOARD], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        fprintf(stderr, "a program that asks for everything was let finish\n");
        return false;
    }
    printf("the program spent the heap it was given, at %zu bytes\n",
           kest_heap_used(engine->runtime));

    // What a host does about it is its own business, and this one starts the
    // heap again rather than stopping. Nothing the program made survives it,
    // which is why nothing here is asked for afterwards.
    if (!kest_heap_reset(engine->runtime)) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    printf("and the heap it has now holds %zu bytes\n",
           kest_heap_used(engine->runtime));

    // And the same ceiling the other way, because an array grows by taking one
    // block and a store by taking four: the same message from different code,
    // and a host that has seen one has not seen the other.
    if (kest_call(engine->runtime, engine->entry[PILE], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        fprintf(stderr, "an array with no end to it was let finish\n");
        return false;
    }
    printf("and again filling an array, at %zu bytes\n",
           kest_heap_used(engine->runtime));
    if (!kest_heap_reset(engine->runtime)) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }

    // And the other side of a budget, which is a program that stays inside one
    // it could not stay inside by luck. A store hands out the room of what was
    // dropped, so emptying and filling one is work rather than growth; a store
    // that kept the room would want more than a megabyte here and be told so.
    // The two above prove the message, and this proves there is nothing to say.
    engine->frame[0].integer = 100000;
    if (!kest_call(engine->runtime, engine->entry[CHURN], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    printf("emptied and filled 100000 times, holding %lld, in %zu bytes\n",
           (long long)engine->frame[0].integer, kest_heap_used(engine->runtime));
    return true;
}

// What a thing costs, asked from outside in bytes: room asked for against
// room grown into, text gathered against text joined a piece at a time,
// and the library held to the way the reference says to write it. A host
// cannot read `std.text`, and it can ask what two sizes cost.
static bool weighs_what_it_costs(Engine *engine) {
    // What asking for room is worth, in bytes, from outside. An array grows by
    // doubling and copying, so a thousand pushed without asking pays for every
    // step on the way up; a thousand pushed after asking pays once. The
    // language has no word for a reservation because it does not need one:
    // `array(n, v)` and `clear` are it.
    size_t costs[2];
    const int32_t asked[2] = {READY, FILLING};
    for (int which = 0; which < 2; which++) {
        engine->frame[0].integer = 1000;
        size_t spent = kest_heap_used(engine->runtime);
        if (!kest_call(engine->runtime, engine->entry[asked[which]], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        costs[which] = kest_heap_used(engine->runtime) - spent;
        printf("a thousand pushed %s room: %lld held, %zu bytes\n",
               which == 0 ? "after asking for" : "without asking for",
               (long long)engine->frame[0].integer, costs[which]);
    }
    if (costs[0] >= costs[1]) {
        fprintf(stderr, "asking for room cost as much as not asking\n");
        return false;
    }

    // And what the reference says about building text a piece at a time,
    // which is that gathering bytes and paying once beats making a new piece
    // out of both every time round. Six hundred of them is a small enough
    // number to say it inside the megabyte this host allows, and a big enough
    // one to say it clearly.
    size_t text_costs[2];
    const int32_t ways[2] = {GLUED, JOINED};
    for (int which = 0; which < 2; which++) {
        engine->frame[0].integer = 600;
        size_t spent = kest_heap_used(engine->runtime);
        if (!kest_call(engine->runtime, engine->entry[ways[which]], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        text_costs[which] = kest_heap_used(engine->runtime) - spent;
        printf("six hundred bytes of text, %s: %lld long, %zu bytes\n",
               which == 0 ? "a piece at a time" : "gathered and paid for once",
               (long long)engine->frame[0].integer, text_costs[which]);
    }
    if (text_costs[1] >= text_costs[0]) {
        fprintf(stderr, "gathering cost as much as copying every time\n");
        return false;
    }

    // The library, held to being written the way the reference says to write
    // it. A host cannot read `std.text` and would not be told if somebody
    // rewrote `join` out of `slice` tomorrow — but it can ask what two sizes
    // cost. Twice the work costs about twice as much when the bytes are
    // gathered, and about four times as much when everything is copied every
    // time round, so anything under three says which of the two it is.
    const int32_t linear[2] = {REPEATED, JOINED_PIECES};
    const char *called[2] = {"text.repeat", "text.join"};
    for (int which = 0; which < 2; which++) {
        size_t cost[2];
        for (int size = 0; size < 2; size++) {
            engine->frame[0].integer = size == 0 ? 200 : 400;
            size_t spent = kest_heap_used(engine->runtime);
            if (!kest_call(engine->runtime, engine->entry[linear[which]], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
                kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
                return false;
            }
            cost[size] = kest_heap_used(engine->runtime) - spent;
        }
        printf("`%s` over 200 and 400: %zu bytes and %zu\n", called[which],
               cost[0], cost[1]);
        if (cost[0] == 0 || cost[1] > cost[0] * 3) {
            fprintf(stderr, "`%s` costs %zu for twice the work, which is not "
                            "the gathering way\n",
                    called[which], cost[1]);
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
    static Decider decider = {-1, 1, true};
    if (host == NULL || !kest_host_bind(host, "Io.write", io_write, stdout) ||
        !kest_host_bind(host, "Engine.decide", engine_decide, &decider) ||
        !kest_host_bind(host, "Engine.name", engine_name, &decider)) {
        return 1;
    }

    // A name is bound once, which is a thing a host writer finds out the
    // first time they bind one twice. Asking for it here means somebody has:
    // what comes back is false, and what stays bound is the first — the
    // program's writing goes to this host's output below, not to its errors.
    if (kest_host_bind(host, "Io.write", io_write, stderr)) {
        fprintf(stderr, "`Io.write` was bound twice\n");
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
    if (!lays_them_out_the_same(build)) {
        return 1;
    }

    Engine engine = {0};
    engine.runtime = kest_start(build, host, &limits);
    if (engine.runtime == NULL) {
        // Nothing started, so there is nothing to ask what went wrong: what a
        // host has then is the build, and it has been told.
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        kest_host_free(host);
        kest_build_free(build);
        return 1;
    }

    // Starting reads what the host bound and keeps its own copy, so the list
    // of names is done with here. Freeing it now rather than at the end is
    // this host saying so out loud: what has to outlive the machine is the
    // build, and nothing else.
    kest_host_free(host);

    // The arguments go where the result comes back, so a frame has to be
    // wide enough for whichever is wider. The program says which, rather than
    // this host guessing and being told at the first call that is too narrow.
    KestPiece point[3];
    point_pieces(point);


    const char *wanted[] = {"create", "spawn", "step", "onEvents", "silence",
                            "heaviest",
                            "lengthOf",
                            "between",
                            "spread",
                            "hoard",
                            "pile",
                            "churn",
                            "ready",
                            "filling",
                            "glued",
                            "joined",
                            "repeated",
                            "joinedPieces",
                            "readable",
                            "grew",
                            "popped",
                            "took",
                            "emptied",
                            "under"};
    decider.rule = kest_entry(engine.runtime, "rule");

    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); i++) {
        // Found once, at the start. What a name means is a search over
        // everything the program defines, and a frame should not do one. The
        // name is the one the file writes; that it registered them under
        // `embed` is not this host's business.
        // A name that is several functions has no one index, and asking for
        // the second one says whether this is such a name without asking for
        // an index that is not there. This host means the one that takes a
        // `Point`, so it walks them and asks each what it takes.
        if (kest_entry_of(engine.runtime, wanted[i], 1) >= 0) {
            engine.entry[i] = -1;
            for (uint32_t at = 0; engine.entry[i] < 0; at++) {
                int32_t candidate = kest_entry_of(engine.runtime, wanted[i], at);
                if (candidate < 0) {
                    break;
                }
                const KestLayout *first =
                    kest_frame_layout(engine.runtime, candidate, 0);
                if (first != NULL && first->size == sizeof(Point) &&
                    first->align == _Alignof(Point) &&
                    same_pieces(first, point, 3, false)) {
                    engine.entry[i] = candidate;
                }
            }
        } else {
            engine.entry[i] = kest_entry(engine.runtime, wanted[i]);
        }
        if (engine.entry[i] < 0 ||
            kest_frame_slots(engine.runtime, engine.entry[i]) >
                sizeof(engine.frame) / sizeof(engine.frame[0])) {
            fprintf(stderr, "`%s` is not there or needs more than %zu slots\n",
                    wanted[i], sizeof(engine.frame) / sizeof(engine.frame[0]));
            return 1;
        }
    }
    if (!asks(&engine, CREATE)) {
        return 1;
    }
    engine.world = engine.frame[0];

    // What a frame costs, which is the heap on either side of it. The running
    // total is a number without a scale — every host that watches a frame
    // budget wants the difference, and the difference is a subtraction this
    // host does rather than a thing it is given.
    for (int i = 0; i < 5; i++) {
        engine.frame[0] = engine.world;
        engine.frame[1].integer = i + 1;
        size_t spent = kest_heap_used(engine.runtime);
        if (!asks(&engine, SPAWN)) {
            return 1;
        }
        printf("frame %d: spawned, %lld alive, %zu bytes this frame\n", i,
               (long long)engine.frame[0].integer, kest_heap_used(engine.runtime) - spent);
    }

    // And the same subtraction over a frame that promised nothing, which is
    // the promise read from outside: `step` is `no.alloc`, so what these cost
    // is nought and a host can watch that rather than take it on faith.
    for (int i = 0; i < 5; i++) {
        engine.frame[0] = engine.world;
        // And the swap the reference describes: a name is bound once, so the
        // one function that decides is asked to decide differently. From the
        // third frame on this host stops asking the program and answers for
        // itself, which the count says without anything being rebound.
        if (i == 2) {
            decider.asks_the_program = false;
        }
        size_t spent = kest_heap_used(engine.runtime);
        if (!asks(&engine, STEP)) {
            return 1;
        }
        printf("frame %d: stepped, %lld alive, %zu bytes this frame, %s\n",
               i + 5, (long long)engine.frame[0].integer,
               kest_heap_used(engine.runtime) - spent,
               decider.asks_the_program ? "asking the program"
                                        : "deciding for itself");
    }

    // The same answer twice: what this host makes of a slot, and what the
    // program writes for the type it declared it as. A host that does not want
    // to know how a number is spelt asks for the words, and the number it gets
    // back is the room they need.
    char said[32];
    char mine[32];
    int64_t room = kest_gave_text(engine.runtime, engine.entry[STEP], engine.frame, said,
                                  sizeof(said));
    snprintf(mine, sizeof(mine), "%lld", (long long)engine.frame[0].integer);
    if (room < 0 || (size_t)room >= sizeof(said) || strcmp(said, mine) != 0) {
        fprintf(stderr, "what came back reads as `%s` and is %s\n", said,
                mine);
        return 1;
    }
    printf("what `step` gave, in the program's own words: %s\n", said);

    // And what the host is, asked for by the program. A host tells a program
    // about itself the way it tells it anything — an `extern` like any other
    // — so this asks twice, either side of the swap above, and the answer is
    // the host's own words about what it is doing.
    char about[32];
    if (!asks(&engine, UNDER) ||
        kest_gave_text(engine.runtime, engine.entry[UNDER], engine.frame, about,
                       sizeof(about)) < 0) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("the program asked what it is running under: %s\n", about);

    // And a store is a thing the language has no text for, which it says
    // rather than inventing one. What the host wants of a store, only the host
    // knows.
    if (kest_gave_text(engine.runtime, engine.entry[CREATE], engine.frame, said, sizeof(said))
        >= 0) {
        fprintf(stderr, "a store has no text and something wrote one\n");
        return 1;
    }

    // A struct passed by value rather than lent: one slot a scalar, in the
    // order the fields are declared, and a float is a double in a slot even
    // where it is an `f32` in memory. Lending shares the host's bytes; this
    // copies three numbers into the engine.frame, which is the crossing D007 says to
    // reach for one item at a time and not for a batch.
    engine.frame[0].real = 1.0;
    engine.frame[1].real = 2.0;
    engine.frame[2].real = 2.0;
    if (!asks(&engine, LENGTH_OF)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host passed a point by value: %g\n", engine.frame[0].real);

    // Two of them, where the host would otherwise have to count the first
    // one's scalars to know where the second begins. The program knows, so it
    // is asked.
    // And what the argument is, checked the way anything lent is: the same
    // layout, the same pieces, the same `offsetof` on this side. A frame of
    // the right width with the wrong things in it is the mistake this catches.
    const KestLayout *takes = kest_frame_layout(engine.runtime, engine.entry[BETWEEN], 1);
    if (takes == NULL || takes->size != sizeof(Point) ||
        takes->align != _Alignof(Point) ||
        !same_pieces(takes, point, 3, false)) {
        fprintf(stderr, "`between` does not take a `Point` this host knows\n");
        return 1;
    }
    // And what comes back, which is read as a `double` in a slot and is an
    // `f32` in memory: what the layout says is which of the two the program
    // means, and this host reads `engine.frame[0].real` because of it.
    const KestLayout *gives = kest_frame_gives(engine.runtime, engine.entry[BETWEEN]);
    if (gives == NULL || gives->count != 1 ||
        gives->pieces[0].kind != KEST_L_F32) {
        fprintf(stderr, "`between` does not give back one `f32`\n");
        return 1;
    }
    uint32_t second = kest_frame_at(engine.runtime, engine.entry[BETWEEN], 1);
    for (uint32_t k = 0; k < 3; k++) {
        engine.frame[k].real = (double)k;
        engine.frame[second + k].real = (double)k + 1.0;
    }
    if (!asks(&engine, BETWEEN)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("%u arguments, the second at slot %u: %g between them\n",
           kest_frame_takes(engine.runtime, engine.entry[BETWEEN]), second, engine.frame[0].real);

    // A struct of the host's with an array inside it, lent by name. A run on
    // its own has no name to lend against, which is what `Point` is for.
    Point corners[4];
    for (int i = 0; i < 4; i++) {
        for (int k = 0; k < 3; k++) {
            corners[i].at[k] = (float)(i * 3 + k);
        }
    }
    engine.frame[0] = kest_borrow(engine.runtime, corners, 4, "Point", sizeof(Point));
    if (engine.frame[0].object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!asks(&engine, SPREAD)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host lent %zu byte points: %g across\n", sizeof(Point),
           (double)engine.frame[0].real);

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
    engine.frame[0] = kest_borrow(engine.runtime, rows, 2, "Row", sizeof(Row));
    if (engine.frame[0].object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!asks(&engine, HEAVIEST)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host lent %zu byte rows: heaviest is %lld\n", sizeof(Row),
           (long long)engine.frame[0].integer);

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
    engine.frame[0] = kest_borrow(engine.runtime, events, 4, "Event", sizeof(Event));
    if (engine.frame[0].object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!asks(&engine, ON_EVENTS)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host lent %zu byte events: %lld damage\n", sizeof(Event),
           (long long)engine.frame[0].integer);

    // And a lend this host is not allowed to make. Where the array sits is
    // the one thing about a lend that nothing in the program decides, so
    // asking for the refusal on purpose is the only way anybody sees it: half
    // an alignment into a properly aligned array is an address an `Event` may
    // not sit at, and the program would be reading its payload across a word
    // boundary the C standard has no answer for.
    Event aligned[2];
    void *crooked = (unsigned char *)(void *)aligned + _Alignof(Event) / 2;
    if (kest_borrow(engine.runtime, crooked, 1, "Event", sizeof(Event)).object !=
        NULL) {
        fprintf(stderr, "a lend at a crooked address was allowed\n");
        return 1;
    }
    printf("a lend %zu bytes into an `Event` was refused\n",
           _Alignof(Event) / 2);

    // And one this host could not be told it was wrong about any other way.
    // How many there are is this host's word, and the one thing the library
    // knows about the number is what the program can count to. Nothing is
    // read here either: the refusal comes before the length is believed.
    if (kest_borrow(engine.runtime, aligned, 2147483648u, "Event", sizeof(Event))
            .object != NULL) {
        fprintf(stderr, "a lend longer than a count was allowed\n");
        return 1;
    }
    printf("a lend of more `Event` than an `i32` counts was refused\n");

    // And back the other way: what the program writes is what the host reads,
    // because there is one copy of it.
    KestValue lent = kest_borrow(engine.runtime, events, 4, "Event", sizeof(Event));
    engine.frame[0] = lent;
    engine.frame[1].integer = 0;
    if (!asks(&engine, SILENCE)) {
        return 1;
    }
    printf("silenced the first: tag is now %d\n", events[0].tag);

    engine.frame[0] = lent;
    if (!asks(&engine, ON_EVENTS)) {
        return 1;
    }
    printf("host reads it back: %lld damage\n", (long long)engine.frame[0].integer);

    // A frame is not one call, it is the same call sixty times a second, and
    // a promise that holds once and leaks a little each time is a promise
    // that runs out overnight. `onEvents` says `no.alloc`, so a thousand of
    // them have to leave the heap exactly where they found it — not nearly,
    // since what this is looking for is the byte a frame keeps.
    size_t before = kest_heap_used(engine.runtime);
    for (int i = 0; i < 1000; i++) {
        engine.frame[0] = lent;
        if (!asks(&engine, ON_EVENTS)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    size_t after = kest_heap_used(engine.runtime);
    if (after != before) {
        fprintf(stderr, "a thousand frames that promise nothing left %zu "
                        "bytes behind\n",
                after - before);
        return 1;
    }
    printf("a thousand frames left the heap where they found it, at %zu "
           "bytes\n",
           after);

    if (!weighs_what_it_costs(&engine)) {
        return 1;
    }


    if (!lends_bytes(&engine)) {
        return 1;
    }

    // What the machine is running with, asked of the machine rather than kept
    // beside it: a number allocated is a number without a scale on its own.
    KestLimits allowed = {0, 0, 0};
    kest_allowed(engine.runtime, &allowed);
    printf("used %zu of %zu bytes, in %u slots and %u frames\n",
           kest_heap_used(engine.runtime), allowed.heap_bytes, allowed.stack_slots,
           allowed.call_depth);

    if (!spends_the_heap(&engine)) {
        return 1;
    }

    kest_runtime_free(engine.runtime);
    kest_build_free(build);
    return 0;
}
