// A host that is not the command line. It compiles a file, makes a machine,
// and keeps a world between frames by holding the handle the program gave it.
//
//   make embed && ./examples/embed
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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
       EMPTIED, UNDER, NAMED, AT_ONCE, COPIED, BLANK, FIRST,
       BORN, HEALTH_OF, DROPPED,
       // What the list of names below has to be as long as. This host looked
       // each of them up into an array sized by the last name in this list,
       // so a name added after that one was a write past the end of it — this
       // host getting wrong the one thing it is here to show being got right.
       ENTRIES };

// What this host is between calls. A host that runs a program every frame
// holds exactly this: the machine, the names it looked up once because a
// lookup is a search over everything the program defines, the frame it calls
// with, and the world the program handed it. A run of locals in one function
// is what this was, and a host writer reading it would have to guess which of
// them their own engine wants.
typedef struct {
    KestRuntime *runtime;
    int32_t entry[ENTRIES];
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
    // Whether this host asks, from inside this call, for the two things it may
    // not have while a program is running, and how many of the two it was
    // refused. Asked for here because here is inside a call: a host holding
    // the machine between calls may have either of them.
    bool meddles;
    int32_t refused;
} Decider;

// The engine's own policy. Asking the program is calling in from inside a call
// the program made, which is what an engine does when its rules live on both
// sides, and the machine puts what this starts above what is already running.
// Answering by itself is the same function on a different day.
static void engine_decide(KestValue *frame, KestRuntime *runtime,
                          void *context) {
    Decider *decider = context;
    if (decider->meddles) {
        // Throwing the heap away takes what the program is holding, and
        // freeing the machine takes the stack it is standing on. Both are
        // refused here rather than found out about afterwards, and what the
        // machine says is read where it is said: the words are on the build's
        // memory rather than on the heap, so they are there to read either
        // way, and reading them here keeps them out of what the run reports.
        //
        // A refusal that did not happen is said here and now, because what
        // runs after one is a machine reading memory it has given back.
        FILE *said = tmpfile();
        if (said == NULL) {
            fprintf(stderr, "this host has nowhere to read a report back\n");
            _Exit(1);
        }
        if (kest_heap_reset(runtime)) {
            fprintf(stderr,
                    "the heap was thrown away while the program was running\n");
            _Exit(1);
        }
        kest_report(runtime, said, KEST_FORM_TEXT);
        kest_runtime_free(runtime);
        kest_report(runtime, said, KEST_FORM_TEXT);
        rewind(said);
        char line[512];
        while (fgets(line, sizeof(line), said) != NULL) {
            if (strstr(line, "K0613") != NULL) {
                decider->refused++;
            }
        }
        fclose(said);
        decider->meddles = false;
    }
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
    KestValue lent = engine->frame[0];
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

    // And what the program made out of them, kept after the lend it was made
    // from is over. A lend is the host's memory and what a program copies out
    // of one is the program's own: this ends the lend, writes something else
    // into the block, and asks what the program is holding.
    engine->frame[0] = lent;
    if (!kest_call(engine->runtime, engine->entry[COPIED], engine->frame,
                   sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    KestValue kept = engine->frame[0];
    if (!kest_lend_ends(engine->runtime, lent)) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    letters[0] = 'w';
    letters[1] = 'r';
    letters[2] = 'o';
    letters[3] = 'g';
    if (!kest_still_holds(engine->runtime, kept) ||
        strcmp(kept.text, "kest") != 0) {
        fprintf(stderr,
                "what the program copied out of a lend says `%s` after the "
                "lend was taken back\n",
                kest_still_holds(engine->runtime, kept) ? kept.text : "");
        return false;
    }
    printf("and what it copied out of them says `%s` after the lend ended\n",
           kept.text);

    // And a nought written into the lend by the program, read back out of the
    // host's own array. A lend is memory: what the program writes into it is
    // what the host has, and a nought is a byte like any other in a run of
    // them however little it can mean in a piece of text.
    letters[0] = 'k';
    letters[1] = 'e';
    letters[2] = 's';
    letters[3] = 't';
    KestValue writable = kest_borrow(engine->runtime, letters, 4, "u8",
                                     sizeof(letters[0]));
    if (writable.object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    engine->frame[0] = writable;
    engine->frame[1].integer = 1;
    if (!kest_call(engine->runtime, engine->entry[BLANK], engine->frame,
                   sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    if (letters[1] != 0 || letters[0] != 'k') {
        fprintf(stderr,
                "what the program wrote into a lend is not what this host "
                "holds: %c%d\n",
                letters[0], letters[1]);
        return false;
    }
    printf("and the program wrote a nought into the host's own bytes\n");

    // And the other way round, between calls. A lend is memory rather than a
    // copy, so what this host writes into its own block while nothing is
    // running is what the program reads the next time it looks. Whoever is
    // running is the one writing: a call holds the machine, and between calls
    // the host has it.
    letters[0] = 'w';
    letters[1] = 'h';
    letters[2] = 'a';
    letters[3] = 't';
    engine->frame[0] = writable;
    if (!kest_call(engine->runtime, engine->entry[FIRST], engine->frame,
                   sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    if (engine->frame[0].integer != 'w') {
        fprintf(stderr,
                "what this host wrote between calls is not what the program "
                "read: %lld\n",
                (long long)engine->frame[0].integer);
        return false;
    }
    printf("and read back what this host wrote into it between calls\n");
    letters[0] = 'k';
    letters[1] = 'e';

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

    // And what it was reaching for when it was stopped, which is what says
    // whether a megabyte was nearly enough. The two numbers are one number
    // said from either side: what it used stops short of what it was allowed
    // by exactly what it was refused.
    KestLimits given = {0, 0, 0};
    kest_allowed(engine->runtime, &given);
    size_t wanted = kest_heap_wanted(engine->runtime);
    if (wanted == 0 ||
        kest_heap_used(engine->runtime) + wanted <= given.heap_bytes) {
        fprintf(stderr,
                "a heap that ran out said it was reaching for %zu bytes\n",
                wanted);
        return false;
    }
    printf("and it was reaching for %zu more than it had\n", wanted);

    // And which of the two said no, because the number above is the same
    // number either way and this host does something different about each: a
    // ceiling of its own is a number it can raise, and a machine with nothing
    // left is not. Written as a list with nothing else in it, so a fourth
    // answer would stop this host compiling rather than be printed as one of
    // the three.
    switch (kest_heap_refused_by(engine->runtime)) {
    case KEST_REFUSED_CEILING:
        printf("and it was this host's own ceiling that said no\n");
        break;
    case KEST_REFUSED_MACHINE:
        fprintf(stderr, "a ceiling this host set was blamed on the machine\n");
        return false;
    case KEST_REFUSED_NOTHING:
        fprintf(stderr, "a heap that ran out was refused by nobody\n");
        return false;
    }

    // And what it was doing, read back rather than printed. What a host raises
    // a ceiling by is not what the last allocation asked for — a thing that
    // doubles asks for the double again — so the line that says what was
    // growing and how far along it was is the one an engine logs.
    FILE *said = tmpfile();
    if (said == NULL) {
        fprintf(stderr, "this host has nowhere to read a report back from\n");
        return false;
    }
    kest_report(engine->runtime, said, KEST_FORM_TEXT);
    rewind(said);
    char line[512];
    bool told = false;
    while (fgets(line, sizeof(line), said) != NULL) {
        if (strstr(line, "growing to") != NULL) {
            told = true;
        }
    }
    fclose(said);
    if (!told) {
        fprintf(stderr, "a heap that ran out did not say what was growing\n");
        return false;
    }
    printf("and said what it was growing when it ran out\n");

    // And the other way to want more than there is: a million of something in
    // one go. Nothing is growing there, so what the message has to say is what
    // was being made — a number in the program rather than a ceiling that was
    // nearly enough, and not the same thing for a host to do something about.
    engine->frame[0].integer = 1000000;
    if (kest_call(engine->runtime, engine->entry[AT_ONCE], engine->frame,
                  sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        fprintf(stderr, "a million elements fitted in a megabyte\n");
        return false;
    }
    FILE *once = tmpfile();
    if (once == NULL) {
        fprintf(stderr, "this host has nowhere to read a report back from\n");
        return false;
    }
    kest_report(engine->runtime, once, KEST_FORM_TEXT);
    rewind(once);
    told = false;
    while (fgets(line, sizeof(line), once) != NULL) {
        if (strstr(line, "making an array of") != NULL) {
            told = true;
        }
    }
    fclose(once);
    if (!told) {
        fprintf(stderr, "a heap that ran out at once did not say what it was "
                        "making\n");
        return false;
    }
    printf("and said what it was making when it ran out at once\n");

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

    // An array this host lent on that heap and handed back after it was
    // thrown away. The header of a lend is on the machine's heap even though
    // the block is the host's, so a reset takes it with everything else. The
    // memory has not been handed out again yet, so what is at that address
    // still reads as the array it was — which is exactly the case nothing but
    // the age it carries can tell apart. `heaviest` promises `no.alloc`, so
    // nothing has been put on the new heap by the time it is asked.
    //
    // Not under the sanitisers: the arena poisons what it takes back, so
    // reading the header at all is caught there, harder and one step earlier
    // than the machine can catch it.
#if !defined(__SANITIZE_ADDRESS__)
    Row rows[2];
    memset(rows, 0, sizeof(rows));
    KestValue lent =
        kest_borrow(engine->runtime, rows, 2, "Row", sizeof(Row));
    if (lent.object == NULL || !kest_heap_reset(engine->runtime)) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    engine->frame[0] = lent;
    if (kest_call(engine->runtime, engine->entry[HEAVIEST], engine->frame,
                  sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        fprintf(stderr,
                "a handle from before the heap was thrown away was taken\n");
        return false;
    }
    printf("and refused an array it lent before the heap was thrown away\n");
#endif

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
    static Decider decider = {-1, 1, true, false, 0};
    if (host == NULL || !kest_host_bind(host, "Io.write", io_write, stdout) ||
        !kest_host_bind(host, "Engine.decide", engine_decide, &decider) ||
        !kest_host_bind(host, "Engine.name", engine_name, &decider)) {
        return 1;
    }

    // A name is bound once, which is a thing a host writer finds out the
    // first time they bind one twice. Asking for it here means somebody has:
    // what comes back is false, and what stays bound is the first — the
    // program's writing goes to this host's output below, not to its errors.
    // Binding into nothing, which is what a host that did not look at what
    // `kest_host_new` answered would be doing. There is one reason it answers
    // nothing and it is the reason everything else here is refused for.
    if (kest_host_bind(NULL, "Io.write", io_write, stdout)) {
        fprintf(stderr, "a bind into nothing was taken\n");
        return 1;
    }
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
        // This host calls back into the program from inside one of its own
        // functions, and what that needs is not a number to double and hope
        // over: it is where the machine already is when it reaches a host
        // function, plus what the function called from there needs on its own.
        KestLimits inside = {0, 0, 0};
        KestLimits rule = {0, 0, 0};
        if (kest_needs_from(build, NULL, &inside, NULL) &&
            inside.call_depth > 0 &&
            kest_needs_of(build, "rule", &rule, NULL)) {
            uint32_t slots = inside.stack_slots + rule.stack_slots;
            uint32_t frames = inside.call_depth + rule.call_depth;
            printf("  it reaches this host %u slots and %u frame%s in, and "
                   "`rule` from there wants %u and %u more\n",
                   inside.stack_slots, inside.call_depth,
                   inside.call_depth == 1 ? "" : "s", rule.stack_slots,
                   rule.call_depth);
            if (slots > limits.stack_slots) {
                limits.stack_slots = slots;
            }
            if (frames > limits.call_depth) {
                limits.call_depth = frames;
            }
        }
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

    // A second machine from the same build, which is what an engine has when
    // it runs two worlds side by side. They share the program they were
    // compiled from and nothing else — each has its own heap — and what that
    // means for a handle is at the end of this file.
    KestRuntime *other = kest_start(build, host, &limits);
    // And a third, as new as the second: what the two of them are for is at
    // the end of this file, where a reference from one is handed to the other.
    KestRuntime *third = kest_start(build, host, &limits);

    // And a fourth from a second host, which shares nothing with the first.
    // Two hosts in one process are two lists of bindings, each the caller's
    // own; a machine reads the list it was started from and keeps its own
    // copy. So the same name bound in both to different contexts is two
    // answers, and neither host can reach through the other's machine to
    // change them: what this one holds stays what it held while the first
    // host's decider is swapped under its own machine below.
    static Decider apart = {-1, 2, true, false, 0};
    KestHost *elsewhere = kest_host_new();
    if (elsewhere == NULL ||
        !kest_host_bind(elsewhere, "Io.write", io_write, stdout) ||
        !kest_host_bind(elsewhere, "Engine.decide", engine_decide, &apart) ||
        !kest_host_bind(elsewhere, "Engine.name", engine_name, &apart)) {
        fprintf(stderr, "a second host could not be given what the first has\n");
        kest_host_free(elsewhere);
        kest_host_free(host);
        kest_build_free(build);
        return 1;
    }
    KestRuntime *apart_at = kest_start(build, elsewhere, &limits);
    // Freed here rather than beside the first, because a machine that has
    // started is done with the list it started from.
    kest_host_free(elsewhere);
    if (other == NULL || third == NULL || apart_at == NULL) {
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
                            "under",
                            "named",
                            "atOnce",
                            "copied",
                            "blank",
                            "first",
                            "born",
                            "healthOf",
                            "dropped"};
    _Static_assert(sizeof(wanted) / sizeof(wanted[0]) == ENTRIES,
                   "every name this host asks for has somewhere to be put");
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

    // And the same question of the machine the second host started, whose
    // decider was never swapped. What comes back is the other answer: a
    // machine reads its own host's context, and one host writing over what it
    // holds is not something the other's programs can see. Nothing enforces
    // that — there is nothing to reach through — which is why it is asked here
    // rather than refused somewhere.
    char apart_about[32];
    KestValue apart_frame[6] = {{0}};
    int32_t apart_under = kest_entry(apart_at, "under");
    if (apart_under < 0 ||
        !kest_call(apart_at, apart_under, apart_frame,
                   sizeof(apart_frame) / sizeof(apart_frame[0])) ||
        kest_gave_text(apart_at, apart_under, apart_frame, apart_about,
                       sizeof(apart_about)) < 0) {
        kest_report(apart_at, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (strcmp(apart_about, about) == 0) {
        fprintf(stderr, "two hosts answered the same: %s\n", apart_about);
        return 1;
    }
    printf("and under the other host, the same program is running under: %s\n",
           apart_about);
    kest_runtime_free(apart_at);

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
    // What this host is about to write, said before it writes it: three
    // slots, each a float read as a `double`. Saying it is the only way a
    // frame of the right width with the wrong things in it is caught, because
    // a slot holds whatever was put there and nothing carries what it is.
    const uint8_t writing[3] = {KEST_L_F32, KEST_L_F32, KEST_L_F32};
    if (!kest_frame_fills(engine.runtime, engine.entry[LENGTH_OF], writing, 3)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // And the same three said wrong, which is what this host would be doing if
    // it wrote `integer` into a slot the program reads as a number of its own.
    const uint8_t wrongly[3] = {KEST_L_F32, KEST_L_I64, KEST_L_F32};
    if (kest_frame_fills(engine.runtime, engine.entry[LENGTH_OF], wrongly, 3)) {
        fprintf(stderr, "the program agreed to a frame it does not take\n");
        return 1;
    }
    printf("a frame said to hold what it does not was refused\n");
    // And the other direction: what this host is about to read back out of the
    // frame. `lengthOf` gives one float, and this host reads
    // `engine.frame[0].real` because of it — a slot read as the wrong thing is
    // a number nobody wrote, and nothing but this says so.
    const uint8_t reading[1] = {KEST_L_F32};
    if (!kest_frame_reads(engine.runtime, engine.entry[LENGTH_OF], reading, 1)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    const uint8_t read_wrong[1] = {KEST_L_I64};
    if (kest_frame_reads(engine.runtime, engine.entry[LENGTH_OF], read_wrong, 1)) {
        fprintf(stderr, "the program agreed to a result it does not give\n");
        return 1;
    }
    // And a function that gives nothing back, read as though it gave one: the
    // width is the disagreement rather than what is in it.
    const uint8_t nothing[1] = {KEST_L_WORD};
    if (kest_frame_reads(engine.runtime, engine.entry[SILENCE], nothing, 1)) {
        fprintf(stderr, "a function that gives nothing back gave a slot\n");
        return 1;
    }
    printf("a result said to hold what it does not was refused, twice\n");

    // And the way a host has nothing to be wrong about: the arguments handed
    // over as words, written the way a program writes them, and the machine
    // laying them out. The other half of `kest_gave_text`, which says what a
    // frame holds without this host reading a slot.
    //
    // The one that takes two numbers rather than a `Point`, which is the same
    // name and the other function under it.
    int32_t by_words = -1;
    for (uint32_t at = 0; by_words < 0; at++) {
        int32_t candidate = kest_entry_of(engine.runtime, "lengthOf", at);
        if (candidate < 0) {
            break;
        }
        const KestLayout *first =
            kest_frame_layout(engine.runtime, candidate, 0);
        if (first != NULL && first->count == 1) {
            by_words = candidate;
        }
    }
    const uint32_t wide = sizeof(engine.frame) / sizeof(engine.frame[0]);
    const char *given[2] = {"3.0", "4.0"};
    if (by_words < 0 ||
        !kest_takes_text(engine.runtime, by_words, engine.frame, wide, given, 2) ||
        !kest_call(engine.runtime, by_words, engine.frame, wide)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host handed over two words and got %g back\n",
           engine.frame[0].real);

    // A word that is not what it takes, and the wrong number of them. Both are
    // what a host would otherwise find out by handing over a slot holding
    // whatever `strtod` left in it.
    const char *nonsense[2] = {"3.0", "wide"};
    if (kest_takes_text(engine.runtime, by_words, engine.frame, wide, nonsense, 2)) {
        fprintf(stderr, "a word that is not a number was read as one\n");
        return 1;
    }
    const char *too_few[1] = {"3.0"};
    if (kest_takes_text(engine.runtime, by_words, engine.frame, wide, too_few, 1)) {
        fprintf(stderr, "a frame short of an argument was filled\n");
        return 1;
    }
    printf("a word that is not a number and an argument short were refused\n");

    engine.frame[0].real = 1.0;
    engine.frame[1].real = 2.0;
    engine.frame[2].real = 2.0;
    if (!asks(&engine, LENGTH_OF)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host passed a point by value: %g\n", engine.frame[0].real);

    // And the same answer as words, read back the way a host reads a number:
    // what the machine writes is the shortest spelling that reads back as the
    // same number, and the reader that promise is about is this one. A host
    // logging what a frame answered and a host adding it up have to get the
    // same number out of the same line.
    {
        char digits[64];
        double answered = engine.frame[0].real;
        int64_t room = kest_gave_text(engine.runtime, engine.entry[LENGTH_OF],
                                      engine.frame, digits, sizeof(digits));
        char *after = NULL;
        double read_back = room < 0 ? 0.0 : strtod(digits, &after);
        if (room < 0 || after == digits || *after != '\0' ||
            (float)read_back != (float)answered) {
            fprintf(stderr,
                    "what the program answered was written `%s` and this host "
                    "read %g out of it\n",
                    room < 0 ? "" : digits, read_back);
            return 1;
        }
        printf("and read `%s` back as the number it was\n", digits);
    }

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
    KestValue rented = kest_borrow(engine.runtime, rows, 2, "Row", sizeof(Row));
    engine.frame[0] = rented;
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

    // And the end of that lend, which is this host saying the rows are not its
    // to lend any more — what a host does at the end of a frame with what it
    // lent for the length of one. The block is this host's throughout and
    // nothing is freed; what changes is that the program can no longer read
    // it, which is the whole of what a lend with no end was missing.
    if (!kest_lend_ends(engine.runtime, rented)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.frame[0] = rented;
    if (kest_call(engine.runtime, engine.entry[HEAVIEST], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a lend the host took back was read\n");
        return 1;
    }
    // And ending it again, which is a host that has lost track of what it
    // lent. There is nothing there to take back a second time, and what says
    // so is the same question a call in asks about a handle.
    if (kest_lend_ends(engine.runtime, rented)) {
        fprintf(stderr, "a lend was taken back twice\n");
        return 1;
    }
    printf("and took the lend back, which the program can no longer read\n");

    // And what a frame of lending costs, which is the question a host lending
    // a batch every frame is really asking. The block is the host's, so what a
    // lend puts on the machine's heap is a header — and a header a frame is a
    // frame budget that grows for a program doing the same thing every time.
    // Ending one gives its header back to the next lend, so a thousand frames
    // of it cost what one does.
    size_t held = kest_heap_used(engine.runtime);
    for (int frame = 0; frame < 1000; frame++) {
        KestValue each =
            kest_borrow(engine.runtime, rows, 2, "Row", sizeof(Row));
        if (each.object == NULL || !kest_lend_ends(engine.runtime, each)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    if (kest_heap_used(engine.runtime) != held) {
        fprintf(stderr, "a thousand frames of lending grew the heap by %zu\n",
                kest_heap_used(engine.runtime) - held);
        return 1;
    }
    printf("a thousand lends taken back cost the heap nothing\n");

    // The same rows lent a second time, which is a host with one block and two
    // handles — and one block is what it takes back. Ending either ends both,
    // because a handle left alive over memory the host has moved on from is
    // the thing ending a lend is for.
    KestValue one = kest_borrow(engine.runtime, rows, 2, "Row", sizeof(Row));
    KestValue two = kest_borrow(engine.runtime, rows, 2, "Row", sizeof(Row));
    if (one.object == NULL || two.object == NULL || one.object == two.object) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "lending the same block twice gave one handle\n");
        return 1;
    }
    if (!kest_lend_ends(engine.runtime, two)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.frame[0] = one;
    if (kest_call(engine.runtime, engine.entry[HEAVIEST], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "one handle of a block was taken back and the other "
                        "was read\n");
        return 1;
    }
    printf("and took a block back from both handles at once\n");

    // And the tail of a block lent on its own, which is two runs that share
    // their ends rather than two names for one. What a host takes back is
    // memory, so what goes with it is every handle over any of that memory.
    KestValue whole = kest_borrow(engine.runtime, rows, 2, "Row", sizeof(Row));
    KestValue tail = kest_borrow(engine.runtime, &rows[1], 1, "Row",
                                 sizeof(Row));
    if (whole.object == NULL || tail.object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!kest_lend_ends(engine.runtime, whole)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.frame[0] = tail;
    if (kest_call(engine.runtime, engine.entry[HEAVIEST], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a block was taken back and the tail of it was read\n");
        return 1;
    }
    printf("and the tail of it went with it\n");

    // And more of them than there are, which is the mistake this whole
    // crossing is shaped around: the count is the host's word, and a program
    // given a longer one walks off the end of somebody else's memory. Nothing
    // in a build that ships can weigh that word — the block is the host's and
    // its end is written down nowhere the library can read — so it is asked
    // where it can be asked, and this host asks it there.
#if defined(__SANITIZE_ADDRESS__)
    if (kest_borrow(engine.runtime, rows, 4, "Row", sizeof(Row)).object !=
        NULL) {
        fprintf(stderr, "a lend of four out of two was taken\n");
        return 1;
    }
    printf("a lend of %zu rows out of %zu was refused\n", (size_t)4,
           sizeof(rows) / sizeof(rows[0]));
#endif


    // Text is the other thing a host hands over, and the machine copies it:
    // what a program holds it must own. So a host that hands the same name
    // every frame keeps what it was given rather than saying it again — this
    // one asks for the same bytes twice and gets the same text back, which is
    // what makes a name a host says once cost once.
    KestValue name = kest_text(engine.runtime, "the engine", 10);
    size_t paid = kest_heap_used(engine.runtime);
    if (name.text == NULL || strcmp(name.text, "the engine") != 0) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // And bytes with a nought among them, which is a run a host may hold and
    // may not hand over as text: text ends at its first nought, so what came
    // back would be shorter than what was given and nobody would be told. The
    // program never sees it — what comes back is empty and the machine says
    // which byte it was.
    const char cut[6] = {'h', 'a', 'l', 0, 'f', 0};
    KestValue halved = kest_text(engine.runtime, cut, 5);
    if (halved.text == NULL || halved.text[0] != '\0') {
        fprintf(stderr, "bytes with a nought among them were taken as text\n");
        return 1;
    }
    printf("and refused %zu bytes with a nought among them\n", sizeof(cut) - 1);

    KestValue again = kest_text(engine.runtime, "the engine", 10);
    if (again.text == NULL || kest_heap_used(engine.runtime) == paid) {
        fprintf(stderr, "saying the same bytes twice cost nothing\n");
        return 1;
    }
    printf("host said %zu bytes of text and paying twice cost %zu more\n",
           strlen(name.text), kest_heap_used(engine.runtime) - paid);

    // And what a host must not hand over: bytes of its own, which the program
    // would hold for as long as it liked while this host got on with its life.
    // Nothing about the pointer says where it came from, so what says it is
    // the machine asking whether it gave that address out.
    engine.frame[0] = name;
    if (!kest_call(engine.runtime, engine.entry[NAMED], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.frame[0].text = "a string of this host's own";
    if (kest_call(engine.runtime, engine.entry[NAMED], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a host's own string was taken as the program's\n");
        return 1;
    }
    printf("and refused a piece of text this host never had copied\n");

    // What this host keeps of what it was handed. Text lasts as long as the
    // heap it is on, which is as long as nothing throws that away — so a host
    // holding a name between frames asks the machine rather than remembering
    // for it.
    if (!kest_still_holds(engine.runtime, name)) {
        fprintf(stderr, "the machine had lost text nothing had thrown away\n");
        return 1;
    }

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

    // And what a host may not do while the program is running, asked for from
    // inside the one function of this host's the program calls. The machine
    // refuses both and says so, and this host counts what it was told. Before
    // the heap is spent, because what is asked for here is that the machine
    // carries on afterwards.
    // One alive to step over, because what asks this host anything is the
    // program walking the world, and by here everything in it has been
    // stepped to death.
    engine.frame[0] = engine.world;
    engine.frame[1].integer = 4;
    if (!asks(&engine, SPAWN)) {
        return 1;
    }
    decider.meddles = true;
    engine.frame[0] = engine.world;
    if (!asks(&engine, STEP) || decider.meddles || decider.refused != 2) {
        fprintf(stderr,
                "a host asked for two things it may not have and was told "
                "about %d\n",
                decider.refused);
        return 1;
    }
    printf("and refused this host the heap and the machine while running\n");

    // Still running, which is the other half of a refusal: a machine that said
    // no and did it anyway would answer this from memory it had given back.
    if (!asks(&engine, UNDER)) {
        return 1;
    }

    if (!spends_the_heap(&engine)) {
        return 1;
    }

    // And after the heap that name was on was thrown away. Nothing about the
    // pointer this host is holding changed; what changed is whose memory it
    // is, which is the one thing a host cannot see for itself.
    if (kest_still_holds(engine.runtime, name)) {
        fprintf(stderr, "the machine still had text it had thrown away\n");
        return 1;
    }
    printf("and the name this host kept is gone with the heap it was on\n");

    // A reference the host keeps between calls, and what happens to it when the
    // program drops what it named. A reference is a number — a slot and how
    // many times that slot has been used — so this host holds one across three
    // calls and is told at the third that what it named is gone, without ever
    // being able to look inside the store itself.
    engine.frame[0] = engine.world;
    if (!kest_call(engine.runtime, engine.entry[BORN], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestValue kept_ref = engine.frame[0];
    engine.frame[0] = engine.world;
    engine.frame[1] = kept_ref;
    if (!kest_call(engine.runtime, engine.entry[HEALTH_OF], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0])) ||
        engine.frame[0].integer != 5) {
        fprintf(stderr, "a reference this host kept named nothing: %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    engine.frame[0] = engine.world;
    engine.frame[1] = kept_ref;
    if (!kest_call(engine.runtime, engine.entry[DROPPED], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.frame[0] = engine.world;
    engine.frame[1] = kept_ref;
    if (!kest_call(engine.runtime, engine.entry[HEALTH_OF], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0])) ||
        engine.frame[0].integer != -1) {
        fprintf(stderr,
                "a reference to something dropped still named it: %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    printf("and a reference it kept named nothing once the program dropped "
           "what it named\n");

    // And the same reference handed to another store of the same shape, which
    // is the mistake a host makes rather than a program: two references are
    // two numbers and nothing about either says which store it came from. What
    // says it is the stamp — the machine hands those out, so a place in one
    // store is never stamped like a place in another.
    if (!kest_call(engine.runtime, engine.entry[CREATE], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestValue elsewhere_store = engine.frame[0];
    engine.frame[0] = elsewhere_store;
    if (!kest_call(engine.runtime, engine.entry[BORN], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestValue elsewhere_ref = engine.frame[0];
    engine.frame[0] = engine.world;
    engine.frame[1] = elsewhere_ref;
    if (!kest_call(engine.runtime, engine.entry[HEALTH_OF], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0])) ||
        engine.frame[0].integer != -1) {
        fprintf(stderr,
                "a reference from another store named something here: %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    printf("and a reference from another store named nothing in this one\n");

    // And a reference from the other machine, which is two worlds of one
    // program: a host running both holds references from each and they are
    // numbers. What tells them apart is that the stamps are the build's — two
    // machines from one build never stamp a place the same — so a reference
    // from over there names nothing here.
    KestValue theirs_frame[6] = {{0}};
    int32_t their_create = kest_entry(other, "create");
    int32_t their_born = kest_entry(other, "born");
    if (their_create < 0 || their_born < 0 ||
        !kest_call(other, their_create, theirs_frame,
                   sizeof(theirs_frame) / sizeof(theirs_frame[0]))) {
        kest_report(other, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestValue their_store = theirs_frame[0];
    if (!kest_call(other, their_born, theirs_frame,
                   sizeof(theirs_frame) / sizeof(theirs_frame[0]))) {
        kest_report(other, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestValue their_ref = theirs_frame[0];

    // A third machine, as new as the second was: its first store's first place
    // is the first place it has ever handed out, and so is the second
    // machine's. Two machines counting on their own would stamp both of those
    // the same, and this hands one to the other to find out.
    KestValue third_frame[6] = {{0}};
    if (!kest_call(third, kest_entry(third, "create"), third_frame,
                   sizeof(third_frame) / sizeof(third_frame[0])) ||
        !kest_call(third, kest_entry(third, "born"), third_frame,
                   sizeof(third_frame) / sizeof(third_frame[0]))) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 1;
    }
    theirs_frame[0] = their_store;
    theirs_frame[1] = third_frame[0];
    if (!kest_call(other, kest_entry(other, "healthOf"), theirs_frame,
                   sizeof(theirs_frame) / sizeof(theirs_frame[0])) ||
        theirs_frame[0].integer != -1) {
        fprintf(stderr,
                "a reference from another machine named something here: "
                "%lld\n",
                (long long)theirs_frame[0].integer);
        return 1;
    }
    kest_runtime_free(third);
    printf("and one from another machine named nothing in this one\n");
    (void)their_ref;

    // And a handle that is a real handle and belongs to somebody else. The
    // other machine made this store, so everything the first machine reads to
    // know what a handle is reads right — the tag at the front is the tag it
    // looks for — and the only thing wrong with it is which heap it lives on.
    // A host running two worlds has one of these to hand every frame.
    KestValue theirs[2] = {{0}};
    int32_t make = kest_entry(other, "create");
    if (make < 0 || !kest_call(other, make, theirs, 2)) {
        kest_report(other, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.frame[0] = theirs[0];
    engine.frame[1].integer = 1;
    if (kest_call(engine.runtime, engine.entry[SPAWN], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a handle another machine made was taken\n");
        return 1;
    }
    printf("and refused a store the other machine made\n");

    kest_runtime_free(other);
    kest_runtime_free(engine.runtime);
    kest_build_free(build);
    return 0;
}
