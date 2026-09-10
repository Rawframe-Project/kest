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

// The widths a C header is full of, and the padding between them. `struct Tile`
// in `embed.kest` beside it is the same twelve bytes with the same four
// offsets, and neither side was told them: the compiler put the four byte
// field on a four byte boundary and both worked out the same three bytes of
// nothing at the end.
typedef struct {
    uint16_t kind;
    int16_t height;
    uint32_t flags;
    int8_t wear;
} Tile;

// A `u16` and a `bool`, which is the shape D016 works out by hand: four bytes
// aligned to two. `struct Flagged` beside it in `embed.kest` is the same four,
// and neither side was told — this one has `_Bool` where the program has
// `bool`, and both put one byte of nothing at the end. See D552.
typedef struct {
    uint16_t kind;
    _Bool on;
} Flagged;

// The shape the program keeps in a store, declared here only to be refused: a
// host cannot lend one, because the name in it is the machine's.
typedef struct {
    const char *name;
    int32_t health;
} Npc;

typedef struct {
    int32_t tag;
    union {
        struct {
            float x;
            float y;
        } moved;
        int32_t hit;
        int32_t named;
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
       BORN, HEALTH_OF, DROPPED, TOTAL_OF, ANSWER_INTO, SAY_INTO, WORN,
       MOVED, PUT_RECORD, OWN_ARRAY, HOW_MANY_ON, REACH,
       HEAVIEST_CELL, AS_WRITTEN,
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
    // not have while a program is running. Asked for here because here is
    // inside a call: a host holding the machine between calls may have either
    // of them. It is put back to false where it is answered, so a host that
    // finds it still true was never asked.
    bool meddles;
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
        // The answer as well as the words, read in that order: what the
        // machine said is what a report tells anybody, and what it answered is
        // what a host in a frame loop reads instead of one.
        bool freed = kest_runtime_free(runtime);
        kest_report(runtime, said, KEST_FORM_TEXT);
        rewind(said);
        char line[512];
        int32_t refused = 0;
        while (fgets(line, sizeof(line), said) != NULL) {
            if (strstr(line, "K0613") != NULL) {
                refused++;
            }
        }
        fclose(said);
        if (refused != 2) {
            fprintf(stderr,
                    "a host asked for two things it may not have and was told "
                    "about %d\n",
                    refused);
            _Exit(1);
        }
        // What this host does about the false is come back when this call
        // returns and ask again, which is the only thing that makes the
        // refusal stop. It does that at the end of this file.
        if (freed) {
            fprintf(stderr,
                    "a machine said it was freed while the program was "
                    "running\n");
            _Exit(1);
        }
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
// What the machine said about the last thing it refused, read back by the
// host that asked for the refusal. A host in a frame loop reads the answer
// rather than the words, and that is what most of the asking here does — but a
// refusal is a code and a sentence as well as a `false`, and a code nothing
// ever asks for is a message nobody has seen. See D419.
// The same question of a build rather than of a machine. What a build has said
// and nobody has been told yet is read the same way, out of a file nobody
// keeps: a host that made a refusal happen on purpose reads the code back and
// leaves nothing behind for the next thing that reports.
static bool build_said_that(KestBuild *build, const char *code,
                            const char *words) {
    FILE *why = tmpfile();
    if (why == NULL) {
        return false;
    }
    kest_build_report(build, why, KEST_FORM_TEXT);
    rewind(why);
    char line[512];
    bool named = false;
    while (fgets(line, sizeof(line), why) != NULL) {
        if (strstr(line, code) != NULL && strstr(line, words) != NULL) {
            named = true;
        }
    }
    fclose(why);
    if (!named) {
        fprintf(stderr, "the build refused without saying `%s` and `%s`\n",
                code, words);
    }
    return named;
}

static bool said_that(KestRuntime *runtime, const char *code,
                      const char *words) {
    FILE *why = tmpfile();
    if (why == NULL) {
        return false;
    }
    kest_report(runtime, why, KEST_FORM_TEXT);
    rewind(why);
    char line[512];
    bool named = false;
    while (fgets(line, sizeof(line), why) != NULL) {
        if (strstr(line, code) != NULL && strstr(line, words) != NULL) {
            named = true;
        }
    }
    fclose(why);
    if (!named) {
        fprintf(stderr, "the machine refused without saying `%s` and `%s`\n",
                code, words);
    }
    return named;
}

// And the same reading over what a diagnostic carries under it. A suggestion is
// written on its own line beneath the message, so a code and a suggestion are
// never on one line and a host looking for the two together reads the report
// rather than a line of it — once, because a report is what was said since it
// was last asked and asking twice finds the second half of it empty. See D571.
static bool said_under(KestRuntime *runtime, const char *code,
                       const char *words) {
    FILE *why = tmpfile();
    if (why == NULL) {
        return false;
    }
    kest_report(runtime, why, KEST_FORM_TEXT);
    rewind(why);
    char line[512];
    bool named = false;
    bool suggested = false;
    while (fgets(line, sizeof(line), why) != NULL) {
        if (strstr(line, code) != NULL) {
            named = true;
        }
        if (named && strstr(line, words) != NULL) {
            suggested = true;
        }
    }
    fclose(why);
    if (!suggested) {
        fprintf(stderr, "the machine refused without saying `%s` and, under "
                        "it, `%s`\n", code, words);
    }
    return suggested;
}

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

// What a frame is, held to itself, for every name this host looked up. A host
// makes a frame `kest_frame_slots` wide and fills it a piece at a time out of
// what `kest_frame_layout` says each argument is made of, so those are one
// number counted two ways — and they are kept apart in the program, a width
// worked out when the function was compiled and a run of layouts registered
// beside it. Nothing had ever put them next to each other. Two of the names
// this host asks for had their arguments looked at at all, and neither was
// asked how wide they came to.
static bool frame_adds_up(KestRuntime *runtime, int32_t entry,
                          const char *name) {
    uint32_t takes = kest_frame_takes(runtime, entry);
    uint32_t at = 0;
    for (uint32_t which = 0; which < takes; which++) {
        const KestLayout *layout = kest_frame_layout(runtime, entry, which);
        if (layout == NULL) {
            fprintf(stderr, "`%s` takes %u and says nothing about the one "
                            "at %u\n", name, takes, which);
            return false;
        }
        // Where an argument starts is what the ones before it come to. One of
        // those is a number the program hands over and the other is a walk
        // this host makes, which is the point of asking rather than counting.
        if (kest_frame_at(runtime, entry, which) != at) {
            fprintf(stderr, "`%s` puts argument %u at %u and the ones before "
                            "it come to %u\n", name, which,
                    kest_frame_at(runtime, entry, which), at);
            return false;
        }
        // A piece is where something is inside one of these, so it cannot be
        // at or past the end of it. A host laying its own struct over these
        // bytes reads whatever is after them if it is.
        for (uint16_t p = 0; p < layout->count; p++) {
            if (layout->pieces[p].offset >= layout->size) {
                fprintf(stderr, "`%s` has a piece of argument %u at byte %u "
                                "of %u\n", name, which,
                        layout->pieces[p].offset, layout->size);
                return false;
            }
        }
        at += layout->count;
    }
    // Past the last one there is nothing, and where a result written over the
    // arguments would start is what they come to.
    if (kest_frame_layout(runtime, entry, takes) != NULL) {
        fprintf(stderr, "`%s` takes %u and has one after the last of them\n",
                name, takes);
        return false;
    }
    if (kest_frame_at(runtime, entry, takes) != at) {
        fprintf(stderr, "`%s` says a result starts at %u and its arguments "
                        "come to %u\n", name,
                kest_frame_at(runtime, entry, takes), at);
        return false;
    }
    const KestLayout *gives = kest_frame_gives(runtime, entry);
    uint32_t back = gives == NULL ? 0 : gives->count;
    uint32_t wider = at > back ? at : back;
    if (kest_frame_slots(runtime, entry) != wider) {
        fprintf(stderr, "`%s` needs a frame %u wide and what it takes (%u) "
                        "and what it gives back (%u) come to %u\n", name,
                kest_frame_slots(runtime, entry), at, back, wider);
        return false;
    }
    return true;
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
    // Four widths in one shape, which is where padding is decided rather than
    // read off. Nothing here says 2, 4 or 8: `offsetof` does, on this side,
    // and the program does on the other.
    KestPiece tile[4];
    tile[0].offset = (uint16_t)offsetof(Tile, kind);
    tile[0].kind = KEST_L_U16;
    tile[1].offset = (uint16_t)offsetof(Tile, height);
    tile[1].kind = KEST_L_I16;
    tile[2].offset = (uint16_t)offsetof(Tile, flags);
    tile[2].kind = KEST_L_U32;
    tile[3].offset = (uint16_t)offsetof(Tile, wear);
    tile[3].kind = KEST_L_I8;

    // A `bool` is one byte and the machine reads it as one: `KEST_L_U8` is
    // what a layout calls a byte, whatever the program calls the field.
    KestPiece flagged[2];
    flagged[0].offset = (uint16_t)offsetof(Flagged, kind);
    flagged[0].kind = KEST_L_U16;
    flagged[1].offset = (uint16_t)offsetof(Flagged, on);
    flagged[1].kind = KEST_L_U8;

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
                   {"Tile", sizeof(Tile), tile, 4, false, _Alignof(Tile)},
                   {"Flagged", sizeof(Flagged), flagged, 2, false,
                    _Alignof(Flagged)},
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

// A slot filled through what the program says is in it rather than through
// what this host remembers. `kest_slot_of` is the other reading of a layout's
// kinds: the kinds are the type's own widths, where memory is shared, and a
// slot is eight bytes whatever the width is. A host that reads `KEST_L_U8` and
// writes a byte writes one of the eight and the machine reads all of them.
// See D559.
static bool put_number(KestValue *slot, uint8_t kind, double number) {
    switch (kest_slot_of(kind)) {
    case KEST_S_REAL:
        slot->real = number;
        return true;
    case KEST_S_INTEGER:
        slot->integer = (int64_t)number;
        return true;
    case KEST_S_WORD:
    case KEST_S_TAGGED:
        // Neither is a number this host has one of: a word is text or a
        // handle, and a payload is whatever the tag beside it says. A host
        // with one of those lends or reads a tag rather than writing a number,
        // and saying so here is what keeps this from writing a slot it does
        // not understand.
        return false;
    }
    return false;
}

// And the same question at the other end of the frame: which member a slot
// that came back is read through. A host that writes `frame[0].real` because
// the last result it read was a float is remembering rather than asking, and a
// result of more than one slot is where remembering stops working — the slots
// of a `Cell` are not the same member as each other. See D560.
static bool got_number(const KestValue *slot, uint8_t kind, double *number) {
    switch (kest_slot_of(kind)) {
    case KEST_S_REAL:
        *number = slot->real;
        return true;
    case KEST_S_INTEGER:
        *number = (double)slot->integer;
        return true;
    case KEST_S_WORD:
    case KEST_S_TAGGED:
        // The same two this host has no number for going the other way.
        return false;
    }
    return false;
}

// Eight blocks lent and given back a frame, for as many frames as asked, and
// what the first of them cost. A lend costs a header and a place in the
// machine's list of what is lent, and only the header comes back to a spare
// list — so what a host pays for is its widest frame, once, and every frame
// after it is free however many blocks it lends. One element each, so these
// are eight runs of memory rather than eight handles over one: ending a lend
// ends every handle over the block it names, and eight of one block would be
// one lend taken back eight times. See D561.
static bool frames_of_lending(Engine *engine, Row *batch, int frames,
                              size_t *first) {
    // Read here rather than at the top of this host, because everything
    // before it has lent and given back and what is on the spare list is
    // whatever it left there. What this weighs is the frames, not the run.
    size_t before = kest_heap_used(engine->runtime);
    size_t widest = 0;
    for (int frame = 0; frame < frames; frame++) {
        KestValue lent[8];
        for (int i = 0; i < 8; i++) {
            lent[i] = kest_borrow(engine->runtime, &batch[i], 1, "Row",
                                  sizeof(Row));
            if (lent[i].object == NULL) {
                kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
                return false;
            }
        }
        for (int i = 0; i < 8; i++) {
            if (!kest_lend_ends(engine->runtime, lent[i])) {
                kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
                return false;
            }
        }
        // What the first frame bought, against what every frame after it does.
        // Read after the first rather than before it, because the first is the
        // one that is allowed to cost something.
        if (frame == 0) {
            widest = kest_heap_used(engine->runtime);
        } else if (kest_heap_used(engine->runtime) != widest) {
            fprintf(stderr, "eight lends a frame grew the heap by %zu after "
                            "frame %d\n",
                    kest_heap_used(engine->runtime) - widest, frame);
            return false;
        }
    }
    *first = widest - before;
    return true;
}

// What this host does about each answer to where a value is kept, decided in
// one place. A switch with nothing else in it is the net the library keeps over
// its own lists, and it is the whole of what a host has to write to be given
// the same one: a fifth answer stops this host compiling rather than falling
// through to whatever the last reader assumed. The two hosts here read every
// answer this header gives that way. See D565.
static const char *keeping(KestKept where) {
    switch (where) {
    case KEST_KEPT_NOWHERE:
        return "nothing of the machine's";
    case KEST_KEPT_HEAP:
        return "the machine's until the heap goes";
    case KEST_KEPT_LENT:
        return "this host's own block behind a header of the machine's";
    case KEST_KEPT_PROGRAM:
        return "the build's, for as long as the build stands";
    }
    // Not reached while those are the answers there are, and the switch above
    // is what says so rather than this line.
    return "an answer this host has never been given";
}

// And why there is no deepest call, which is four answers a host used to read
// as two: a run of calls that comes back round is a shape to change and a call
// through a value is a number to pick, and the other two are not this at all.
static const char *no_deepest(KestReach reach) {
    switch (reach) {
    case KEST_REACH_KNOWN:
        return "has one";
    case KEST_REACH_ITSELF:
        return "can reach itself";
    case KEST_REACH_VALUE:
        return "calls through a value";
    case KEST_REACH_NO_NAME:
        return "is not a name this program has";
    case KEST_REACH_NO_ROOM:
        return "was not worked out for want of room";
    case KEST_REACH_UNASKED:
        return "was never asked";
    }
    return "is somewhere this host has not been told about";
}

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
    // What a lend costs is a header, and a header is one size whatever it
    // stands in front of: four bytes and forty thousand cost the same, which
    // is the whole reason a host lends rather than hands over a copy. It is
    // also why a function that hands one back cannot promise `no.alloc` — the
    // header is an allocation, even though the block is the host's own.
    {
        static unsigned char plenty[40000];
        size_t was_small = kest_heap_used(engine->runtime);
        KestValue small = kest_borrow(engine->runtime, letters, 4, "u8", 1);
        size_t small_cost = kest_heap_used(engine->runtime) - was_small;
        size_t was_big = kest_heap_used(engine->runtime);
        KestValue big =
            kest_borrow(engine->runtime, plenty, sizeof(plenty), "u8", 1);
        size_t big_cost = kest_heap_used(engine->runtime) - was_big;
        if (small.object == NULL || big.object == NULL) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        if (big_cost > small_cost) {
            fprintf(stderr,
                    "lending %zu bytes cost %zu and lending four cost %zu\n",
                    sizeof(plenty), big_cost, small_cost);
            return false;
        }
        if (!kest_lend_ends(engine->runtime, small) ||
            !kest_lend_ends(engine->runtime, big)) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        // And the one after those, which costs nothing at all: a header a
        // lend gave back is the header the next lend gets, so a host lending
        // every frame is a host that pays for one of these.
        size_t was_again = kest_heap_used(engine->runtime);
        KestValue again =
            kest_borrow(engine->runtime, plenty, sizeof(plenty), "u8", 1);
        size_t again_cost = kest_heap_used(engine->runtime) - was_again;
        if (again.object == NULL || again_cost != 0 ||
            !kest_lend_ends(engine->runtime, again)) {
            fprintf(stderr,
                    "a lend after one that ended cost %zu bytes\n", again_cost);
            return false;
        }
        printf("a lend costs a header: %zu bytes for four and %zu for %zu, "
               "and nothing for the one after\n",
               small_cost, big_cost, sizeof(plenty));
    }
    engine->frame[0] = kest_borrow(engine->runtime, letters, 4, "u8", sizeof(letters[0]));
    if (engine->frame[0].object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    KestValue lent = engine->frame[0];

    // And the address a host has when it has nothing: a lend is an address
    // and a count, and the one address the machine can tell is bad is no
    // address at all. A count of nought is how a host says it has nothing to
    // lend — that is a lend, and the program reads an empty run — and nought
    // of them at no address is the same thing said twice.
    if (kest_borrow(engine->runtime, NULL, 4, "u8", 1).object != NULL) {
        fprintf(stderr, "a lend of four bytes at no address was given\n");
        return false;
    }
    if (kest_borrow(engine->runtime, NULL, 0, "u8", 1).object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    if (!said_that(engine->runtime, "K0644", "no address")) {
        return false;
    }
    printf("and refused four bytes at no address, and lent nought of them\n");

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
        // Grew or shrank, which is the same refusal in two sentences: what a
        // host lent is as long as the host said and no longer.
        if (!said_that(engine->runtime, "K0608", "the host's, so it cannot")) {
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

    // A handle over a lend that ended, and a new lend that got its header.
    // The block is the host's and the machine's record of it is a pointer and
    // a count, so a header handed out again is the same bytes at the same
    // address saying it is alive — and the old handle over it says so too.
    // This is what a store solved with a stamp; a lend has nowhere to put one
    // yet, so what a host has instead is this: end it, and the machine says
    // the old one is gone.
    {
        Row first[2];
        Row second[2];
        memset(first, 0, sizeof(first));
        memset(second, 0, sizeof(second));
        // Something to recognise it by: `heaviest` answers with the tag of
        // the row holding the heaviest cell, times ten, and the cell's index.
        second[0].tag = 7;
        second[0].cells[1].weight = 9.0f;
        second[0].cells[1].at = 3;
        KestValue was_lent =
            kest_borrow(engine->runtime, first, 2, "Row", sizeof(Row));
        if (was_lent.object == NULL ||
            !kest_lend_ends(engine->runtime, was_lent)) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        KestValue now_lent =
            kest_borrow(engine->runtime, second, 2, "Row", sizeof(Row));
        if (now_lent.object == NULL) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        // The header the first lend had is the header the second one got,
        // and nothing about the old handle says which lend it is a handle to.
        // A store solved this with a stamp — a reference is a number that
        // carries one — and a lend handle is a pointer with nowhere to put
        // one, so this is the machine saying what it can: the old handle
        // names the new block, and a host that kept it is reading somebody
        // else's memory through a name it believes.
        if (was_lent.object != now_lent.object) {
            fprintf(stderr,
                    "a lend after one that ended did not take its header, so "
                    "this host has nothing to say about the old handle\n");
            return false;
        }
        engine->frame[0] = was_lent;
        if (!kest_call(engine->runtime, engine->entry[HEAVIEST],
                       engine->frame,
                       sizeof(engine->frame) / sizeof(engine->frame[0]))) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        if (engine->frame[0].integer !=
            second[0].tag * 10 + second[0].cells[1].at) {
            fprintf(stderr,
                    "a handle to a lend that ended read %lld, and the block "
                    "lent after it says %d\n",
                    (long long)engine->frame[0].integer,
                    second[0].tag * 10 + second[0].cells[1].at);
            return false;
        }
        printf("and a handle to a lend that ended names whatever was lent "
               "next: %lld\n",
               (long long)engine->frame[0].integer);
        if (!kest_lend_ends(engine->runtime, now_lent)) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
    }

    // And the headers that were waiting to be used again. Ending a lend puts
    // its header on a list of spares so the next lend costs nothing, and that
    // list is on the heap: a reset takes the headers with everything else, so
    // the list has to go with them. One left behind would hand the next lend a
    // header out of memory the machine has given back, and the only thing that
    // says it happened is that the lend cost nothing.
    unsigned char four[] = {'k', 'e', 's', 't'};
    KestValue spared = kest_borrow(engine->runtime, four, 4, "u8", 1);
    if (spared.object == NULL || !kest_lend_ends(engine->runtime, spared) ||
        !kest_heap_reset(engine->runtime)) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    size_t after_reset = kest_heap_used(engine->runtime);
    KestValue fresh = kest_borrow(engine->runtime, four, 4, "u8", 1);
    size_t fresh_cost = kest_heap_used(engine->runtime) - after_reset;
    if (fresh.object == NULL || fresh_cost == 0) {
        fprintf(stderr,
                "a lend after the heap went cost %zu bytes, so its header is "
                "one the heap took back\n",
                fresh_cost);
        return false;
    }
    if (!kest_lend_ends(engine->runtime, fresh) ||
        !kest_heap_reset(engine->runtime)) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    printf("and a lend after the heap went paid for its own header, at %zu "
           "bytes\n",
           fresh_cost);

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

    // What that cost the compiler, and what the same file costs a second time.
    // A host that compiles at startup pays this once; one that reloads a file
    // whenever it changes pays it every frame it changes, which is a number to
    // know before writing the loop that does it. Nothing is carried from one
    // build to the next — a build is its own arena and its own everything — so
    // the second is the first, to the byte. See D573.
    size_t first_build = kest_build_cost(build);
    KestBuild *read_again = kest_build(path, NULL, stderr, KEST_FORM_TEXT);
    if (read_again == NULL) {
        fprintf(stderr, "the same program would not build a second time\n");
        return 1;
    }
    if (first_build == 0 || kest_build_cost(read_again) != first_build) {
        fprintf(stderr, "building it cost %zu and building it a second time "
                        "cost %zu\n",
                first_build, kest_build_cost(read_again));
        return 1;
    }
    if (!kest_build_free(read_again)) {
        fprintf(stderr, "a second build nothing stands on was not freed\n");
        return 1;
    }
    printf("the compiler spent %zu bytes on this program, and the same on "
           "reading it a second time\n", first_build);

    // Before there is a host at all, which is what a host writer hands over
    // the first time: nothing. Every extern the program declares is unbound
    // then, and what comes back says which of them rather than nothing. A
    // machine that never started is not one standing on this build either —
    // freeing it at the end says so, because a failed start that counted
    // itself would be a build nobody could ever free.
    FILE *without = tmpfile();
    if (without == NULL) {
        fprintf(stderr, "this host has nowhere to read a report back from\n");
        return 1;
    }
    if (kest_start(build, NULL, NULL) != NULL) {
        fprintf(stderr, "a machine started with no host at all\n");
        return 1;
    }
    kest_build_report(build, without, KEST_FORM_TEXT);
    rewind(without);
    char unbound[512];
    bool named = false;
    while (fgets(unbound, sizeof(unbound), without) != NULL) {
        if (strstr(unbound, "K0606") != NULL &&
            strstr(unbound, "`Engine.decide`") != NULL) {
            named = true;
        }
    }
    fclose(without);
    if (!named) {
        fprintf(stderr, "a machine with no host did not say what it wanted\n");
        return 1;
    }
    printf("with no host at all, the program asks for `Engine.decide`\n");

    KestHost *host = kest_host_new();
    static Decider decider = {-1, 1, true, false};
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

    // And asking about one that is not there, which is the walk above run one
    // step too far. Every one of these answers a host with a number or a
    // pointer, and every one of those answers is one a real extern can give:
    // nought arguments, nothing given back. So the end of the walk and a
    // question past it read alike, and only the report tells them apart.
    // See D436.
    {
        uint32_t past = 0;
        while (kest_build_extern(build, past) != NULL) {
            past++;
        }
        if (kest_extern_takes(build, past) != 0 ||
            kest_extern_layout(build, past, 0) != NULL ||
            kest_extern_gives(build, past) != NULL) {
            fprintf(stderr, "there is a %uth function the host provides\n",
                    past);
            return 1;
        }
        if (!build_said_that(build, "K0648", "there is nothing at")) {
            return 1;
        }
        printf("the program asks for %u of them and asking for a %uth "
               "was refused\n", past, past + 1);
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
               no_deepest(why.reach));
        limits.stack_slots = 4096;
        limits.call_depth = 64;
    }

    // And the two answers that used to be one. A name the program has not got
    // and a build that did not compile are different things for a host to be
    // told: the first is a string of this host's own to fix, and the second is
    // a program's worth of diagnostics sitting in the report. A host told
    // `nothing was asked` for both has to guess which it is looking at, and
    // guessing is what this boundary is written not to make anybody do.
    // See D566.
    KestLimits nowhere = {0, 0, 0};
    KestReason no_name = {KEST_REACH_UNASKED, NULL};
    if (kest_needs_of(build, "noSuchFunction", &nowhere, &no_name) ||
        no_name.reach != KEST_REACH_NO_NAME) {
        fprintf(stderr, "a name the program has not got %s\n",
                no_deepest(no_name.reach));
        return 1;
    }
    printf("a name this program has not got is its own answer, not `nothing "
           "was asked`\n");
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
    static Decider apart = {-1, 2, true, false};
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
                            "dropped",
                            "totalOf",
                            "answerInto",
                            "sayInto",
                            "worn",
                            "moved",
                            "putRecord",
                            "ownArray",
                            "howManyOn",
                            "reach",
                            "heaviestCell",
                            "asWritten"};
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
        // And what asking for the name itself says, which is the thing this
        // walk exists to avoid: a name that is several functions has no one
        // index, and a host that asks anyway is told so rather than given the
        // first of them. Asked here because this is where a host meets it.
        // See D421.
        if (kest_entry_of(engine.runtime, wanted[i], 1) >= 0) {
            if (kest_entry(engine.runtime, wanted[i]) >= 0) {
                fprintf(stderr, "`%s` is several functions and one index came "
                                "back for it\n",
                        wanted[i]);
                return 1;
            }
            if (!said_that(engine.runtime, "K0615", "more than one function")) {
                return 1;
            }
        }
        if (engine.entry[i] < 0 ||
            kest_frame_slots(engine.runtime, engine.entry[i]) >
                sizeof(engine.frame) / sizeof(engine.frame[0])) {
            fprintf(stderr, "`%s` is not there or needs more than %zu slots\n",
                    wanted[i], sizeof(engine.frame) / sizeof(engine.frame[0]));
            return 1;
        }
        // And what the frame is, asked here for the same reason the name is:
        // once, before anything runs. A host that finds out at the first call
        // that a width and a run of layouts disagree finds out inside a frame.
        if (!frame_adds_up(engine.runtime, engine.entry[i], wanted[i])) {
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
    if (!said_that(engine.runtime, "K0634", "slot")) {
        return 1;
    }
    printf("a frame said to hold what it does not was refused\n");
    // And the width of a function that is not there. Nought is the honest
    // width of one that takes and gives nothing, so the number cannot say
    // which of the two this is and the report does.
    // And a name the program asks the host for, which is not a name the host
    // can ask the program for. `Io.write` is this host's own binding read the
    // other way round, and a host that asks for it has the two directions
    // confused — which is the mistake this whole boundary is shaped to say.
    if (kest_entry(engine.runtime, "Io.write") >= 0) {
        fprintf(stderr, "a name the program asks the host for came back as "
                        "something to call\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0614", "asks the host for")) {
        return 1;
    }
    if (kest_frame_slots(engine.runtime, -1) != 0) {
        fprintf(stderr, "nothing has a width\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0616", "to ask the width of")) {
        return 1;
    }
    // And calling it, which is the same mistake one step further on: `-1` is
    // what `kest_entry` gives for a name the program does not define, so a
    // host that does not look at the answer hands it straight back.
    if (kest_call(engine.runtime, -1, engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "nothing was called\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0607", "to call")) {
        return 1;
    }
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
    // The words as well as the code, because this code says four things and
    // which of them it was is what a host reads. See D442.
    if (!said_that(engine.runtime, "K0635", "is not a number, and")) {
        return 1;
    }
    const char *too_few[1] = {"3.0"};
    if (kest_takes_text(engine.runtime, by_words, engine.frame, wide, too_few, 1)) {
        fprintf(stderr, "a frame short of an argument was filled\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0635", "handed over")) {
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

    // And text handed over with no bytes to copy, which comes back as an
    // empty piece of text — the same thing a host handing over an empty one
    // on purpose gets. See D436.
    if (kest_text(engine.runtime, NULL, 4).text == NULL ||
        kest_text(engine.runtime, NULL, 4).text[0] != '\0') {
        fprintf(stderr, "text made of nothing was not empty\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0611", "no address to find them at")) {
        return 1;
    }

    // The same four questions about a frame, asked about an index that is no
    // function. Each answers with a number or a pointer, and each of those
    // answers is one a real function can give: nought arguments, nought slots
    // in, nothing past the last, nothing given back. `kest_frame_slots` says
    // so out loud because nought is also honest for it; these said it in
    // silence, so a host that had `kest_entry` answer -1 and went on asking
    // was told about a function that takes and gives nothing. See D436.
    {
        const int32_t nobody = -1;
        if (kest_frame_takes(engine.runtime, nobody) != 0 ||
            kest_frame_at(engine.runtime, nobody, 0) != 0 ||
            kest_frame_layout(engine.runtime, nobody, 0) != NULL ||
            kest_frame_gives(engine.runtime, nobody) != NULL) {
            fprintf(stderr, "there is a frame at %d\n", nobody);
            return 1;
        }
        if (!said_that(engine.runtime, "K0634", "there is nothing at")) {
            return 1;
        }
        printf("four questions about a frame that is not there were "
               "refused\n");
    }

    // A function written once and compiled twice, which is the one kind of
    // name a host cannot hand over plainly. There is no `pick` to call: there
    // is a copy of it per set of types anything asked for, each compiled under
    // a name with those types written into it, and a host walks them and asks
    // each what it takes — the same walk that tells two functions of one name
    // apart, over two that were written once. See D435.
    {
        if (kest_entry(engine.runtime, "pick") >= 0) {
            fprintf(stderr, "a function of two copies had one index\n");
            return 1;
        }
        // And what that says, which is what makes the walk findable: the
        // refusal names the copies, spelled the way a host would have to
        // spell one if it wanted it by name rather than by shape.
        if (!said_that(engine.runtime, "K0615", "more than one function")) {
            return 1;
        }
        int32_t whole = -1;
        uint32_t copies = 0;
        for (uint32_t at = 0;; at++) {
            int32_t one = kest_entry_of(engine.runtime, "pick", at);
            if (one < 0) {
                break;
            }
            copies++;
            const KestLayout *takes =
                kest_frame_layout(engine.runtime, one, 0);
            if (takes == NULL || takes->count != 1) {
                fprintf(stderr, "a copy of `pick` takes something else\n");
                return 1;
            }
            if (takes->pieces[0].kind == KEST_L_I32) {
                whole = one;
            }
        }
        if (copies != 2 || whole < 0) {
            fprintf(stderr, "`pick` is %u copies and none takes an `i32`\n",
                    copies);
            return 1;
        }
        // And the same one by the name it was compiled under, which is what
        // the refusal above spelled out. A host that keeps the name does not
        // have to walk again.
        if (kest_entry(engine.runtime, "pick#T,T,bool$i32") != whole) {
            fprintf(stderr, "the name a copy is compiled under found "
                            "another one\n");
            return 1;
        }
        const char *given[3] = {"3", "4", "true"};
        uint32_t room = sizeof(engine.frame) / sizeof(engine.frame[0]);
        char said[32];
        if (!kest_takes_text(engine.runtime, whole, engine.frame, room, given,
                             3) ||
            !kest_call(engine.runtime, whole, engine.frame, room) ||
            kest_gave_text(engine.runtime, whole, engine.frame, said,
                           sizeof(said)) < 0) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        printf("host walked %u copies of one body and the `i32` one "
               "answered %s\n", copies, said);
    }

    // A result of more than one slot, which is the half of a frame nothing in
    // this tree had ever read. Everything a host here calls gives back a
    // number, a handle or a piece of text — one slot — so the rule that the
    // result is written over the arguments had never had anything to write
    // over: `moved` takes a `Point` and an `f32`, four slots, and gives back
    // a `Point`, three, so the arguments are gone when it answers. See D433.
    {
        const KestLayout *back =
            kest_frame_gives(engine.runtime, engine.entry[MOVED]);
        if (back == NULL || back->size != sizeof(Point) ||
            back->align != _Alignof(Point) || !same_pieces(back, point, 3,
                                                           false)) {
            fprintf(stderr, "`moved` does not give back a `Point` this host "
                            "knows\n");
            return 1;
        }
        // What this host is about to write and what it means to read back,
        // both said before either happens: four slots in and three out, and
        // the three are not the four.
        const uint8_t writing[4] = {KEST_L_F32, KEST_L_F32, KEST_L_F32,
                                    KEST_L_F32};
        const uint8_t reading[3] = {KEST_L_F32, KEST_L_F32, KEST_L_F32};
        if (!kest_frame_fills(engine.runtime, engine.entry[MOVED], writing,
                              4) ||
            !kest_frame_reads(engine.runtime, engine.entry[MOVED], reading,
                              3)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        // And the width of the result said wrongly, which is the mistake a
        // host reading back over its own arguments makes: it read four slots
        // in and reads four out.
        const uint8_t all_four[4] = {KEST_L_F32, KEST_L_F32, KEST_L_F32,
                                     KEST_L_F32};
        if (kest_frame_reads(engine.runtime, engine.entry[MOVED], all_four,
                             4)) {
            fprintf(stderr, "a result three slots wide was read as four\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0634", "gives back 3 slots")) {
            return 1;
        }
        Point before = {{1.5f, 2.5f, 3.5f}};
        const float by = 0.25f;
        for (uint32_t k = 0; k < 3; k++) {
            engine.frame[k].real = (double)before.at[k];
        }
        engine.frame[3].real = (double)by;
        if (!asks(&engine, MOVED)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        // Read back out of the slots the arguments were in, through what the
        // program says each of them is rather than through what this host
        // wrote into them, and worked out again here so that the two are two
        // answers rather than one.
        for (uint32_t k = 0; k < 3; k++) {
            double answered = 0.0;
            if (!got_number(&engine.frame[k], back->pieces[k].kind,
                            &answered)) {
                fprintf(stderr, "a slot of a `Point` is not a number\n");
                return 1;
            }
            if ((float)answered != before.at[k] + by) {
                fprintf(stderr, "`moved` answered %g at %u and this host "
                                "worked out %g\n",
                        answered, k, (double)(before.at[k] + by));
                return 1;
            }
        }
        // And the same result asked for as words, which this language has
        // none of: a shape is written by whoever holds it, because what a
        // `Point` means is the host's to decide. Asking used to be minus one
        // and silence — a host could not tell it apart from an index that is
        // no function — and now it says which type it was. See D433.
        char said[64];
        if (kest_gave_text(engine.runtime, engine.entry[MOVED], engine.frame,
                           said, sizeof(said)) >= 0) {
            fprintf(stderr, "a shape was written as though it had words\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0646", "no text of its own")) {
            return 1;
        }
        // And the other half of the same silence: a function that gives
        // nothing back has nothing to say, which is not the same as having
        // nothing to say it with.
        if (kest_gave_text(engine.runtime, engine.entry[PUT_RECORD],
                           engine.frame, said, sizeof(said)) >= 0) {
            fprintf(stderr, "a function that gives nothing wrote something\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0646", "gives nothing back")) {
            return 1;
        }
        printf("host read a %u slot result back over its arguments, and was "
               "told why it has no words\n", back->count);
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

    // And the same question asked the other way in: one point at a time, by
    // value, with this host keeping the running answer between crossings.
    // Both inward shapes were here and nothing asked them the same thing, so
    // neither said anything about the other. D007 measured what they cost —
    // a batch crosses once and is walked in place, a value crosses per value —
    // and what a host writer is choosing between is two ways of getting one
    // number. Eight crossings against one, over the corners lent above.
    float across = (float)engine.frame[0].real;
    uint32_t whether = kest_frame_at(engine.runtime, engine.entry[REACH], 1);
    // What the program says the arguments are, said before anything is
    // written: three `f32` and a `bool`, which a layout calls a byte because
    // that is what it is where memory is shared.
    const uint8_t reaching[4] = {KEST_L_F32, KEST_L_F32, KEST_L_F32,
                                 KEST_L_U8};
    if (!kest_frame_fills(engine.runtime, engine.entry[REACH], reaching, 4)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // And the width of the slot said in place of the width of the type, which
    // is what a host reading a layout as though it described a frame would
    // say. Both readings are true of a `bool` argument — a byte in memory,
    // eight of them in the frame it is written into — and the kinds are the
    // first of them.
    const uint8_t as_slots[4] = {KEST_L_F32, KEST_L_F32, KEST_L_F32,
                                 KEST_L_I64};
    if (kest_frame_fills(engine.runtime, engine.entry[REACH], as_slots, 4)) {
        fprintf(stderr, "a `bool` argument agreed to being a slot wide\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0634", "slot")) {
        return 1;
    }
    // And which member of a value each of those slots is written through,
    // asked rather than remembered. This host has the kinds above and could
    // write `real` and `integer` by hand; a host whose program changes a
    // declaration has the kinds and nothing else.
    const KestLayout *takes_point =
        kest_frame_layout(engine.runtime, engine.entry[REACH], 0);
    const KestLayout *takes_which =
        kest_frame_layout(engine.runtime, engine.entry[REACH], 1);
    const KestLayout *gives_edge =
        kest_frame_gives(engine.runtime, engine.entry[REACH]);
    if (takes_point == NULL || takes_point->count != 3 ||
        takes_which == NULL || takes_which->count != 1 ||
        gives_edge == NULL || gives_edge->count != 1) {
        fprintf(stderr, "`reach` does not take a point and a word about it\n");
        return 1;
    }
    const uint8_t one_edge[1] = {KEST_L_F32};
    if (!kest_frame_reads(engine.runtime, engine.entry[REACH], one_edge, 1)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    float lowest = 0.0f;
    float highest = 0.0f;
    for (int i = 0; i < 4; i++) {
        // Filled twice, because the result is written over the arguments: the
        // frame that answered the first question no longer holds the point to
        // ask the second one about.
        for (int ask = 0; ask < 2; ask++) {
            for (int k = 0; k < 3; k++) {
                if (!put_number(&engine.frame[k],
                                takes_point->pieces[k].kind,
                                (double)corners[i].at[k])) {
                    fprintf(stderr, "a point slot is not a number\n");
                    return 1;
                }
            }
            // The whole slot, because a slot is eight bytes and a `bool` is
            // one: a host that writes the byte hands the machine whatever the
            // other seven were, and `false` arrives as true. See D557.
            if (!put_number(&engine.frame[whether],
                            takes_which->pieces[0].kind, ask)) {
                fprintf(stderr, "the word about a point is not a number\n");
                return 1;
            }
            if (!asks(&engine, REACH)) {
                kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
                return 1;
            }
            double read_back = 0.0;
            if (!got_number(&engine.frame[0], gives_edge->pieces[0].kind,
                            &read_back)) {
                fprintf(stderr, "what `reach` gives back is not a number\n");
                return 1;
            }
            float edge = (float)read_back;
            if (ask == 0 && (i == 0 || edge < lowest)) {
                lowest = edge;
            }
            if (ask != 0 && (i == 0 || edge > highest)) {
                highest = edge;
            }
        }
    }
    if (highest - lowest != across) {
        fprintf(stderr, "8 crossings of one point say %g across and one "
                        "crossing of a batch says %g\n",
                (double)(highest - lowest), (double)across);
        return 1;
    }
    printf("host asked the same of the same 4 points one at a time: %g "
           "across, in 8 crossings against 1\n", (double)(highest - lowest));

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
    if (!said_that(engine.runtime, "K0637", "taken this lend back")) {
        return 1;
    }
    printf("and took the lend back, which the program can no longer read\n");

    // A result of two slots that are not the same member: `heaviestCell` gives
    // back a `Cell`, an `i32` and an `f32`, so a host reading it writes
    // `integer` for one and `real` for the next. The arguments of a frame are
    // asked about now; this is the other end of it, where every host in this
    // tree read `frame[0].real` because it remembered what it had asked for.
    // A result of one slot lets a host be right by remembering. This one does
    // not. See D560.
    {
        KestValue again = kest_borrow(engine.runtime, rows, 2, "Row",
                                      sizeof(Row));
        if (again.object == NULL) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        const KestLayout *cell =
            kest_frame_gives(engine.runtime, engine.entry[HEAVIEST_CELL]);
        if (cell == NULL || cell->count != 2) {
            fprintf(stderr, "`heaviestCell` does not give back two slots\n");
            return 1;
        }
        // Said before it is read, the way this host says what it is about to
        // write: two slots, and they are not the one kind.
        const uint8_t reading[2] = {KEST_L_I32, KEST_L_F32};
        if (!kest_frame_reads(engine.runtime, engine.entry[HEAVIEST_CELL],
                              reading, 2)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        // And both of them said to be the one kind, which is what reading a
        // result by remembering the last one comes to.
        const uint8_t both_floats[2] = {KEST_L_F32, KEST_L_F32};
        if (kest_frame_reads(engine.runtime, engine.entry[HEAVIEST_CELL],
                             both_floats, 2)) {
            fprintf(stderr, "two slots of one result were read as one kind\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0634", "gives back")) {
            return 1;
        }
        uint32_t row_at = kest_frame_at(engine.runtime,
                                        engine.entry[HEAVIEST_CELL], 1);
        engine.frame[0] = again;
        engine.frame[row_at].integer = 1;
        if (!asks(&engine, HEAVIEST_CELL)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        double answered[2] = {0.0, 0.0};
        for (uint32_t k = 0; k < 2; k++) {
            if (!got_number(&engine.frame[k], cell->pieces[k].kind,
                            &answered[k])) {
                fprintf(stderr, "a slot of a `Cell` is not a number\n");
                return 1;
            }
        }
        // Against this host's own walk of its own memory, because a result
        // nothing else worked out is a number nobody can be wrong about.
        const Cell *here = &rows[1].cells[0];
        for (int k = 1; k < 3; k++) {
            if (rows[1].cells[k].weight > here->weight) {
                here = &rows[1].cells[k];
            }
        }
        if ((int32_t)answered[0] != here->at ||
            (float)answered[1] != here->weight) {
            fprintf(stderr, "`heaviestCell` answered %g weighing %g and this "
                            "host walked to %d weighing %g\n",
                    answered[0], answered[1], here->at,
                    (double)here->weight);
            return 1;
        }
        if (!kest_lend_ends(engine.runtime, again)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        printf("host read a %u slot result of two kinds through what the "
               "program says they are: %d weighing %g\n", cell->count,
               (int)answered[0], answered[1]);
    }

    // And the same crossing over the widths a C header is full of. Nothing
    // above this lends a two byte field or a four byte unsigned one, so what
    // the program does with them was never held to what this compiler does:
    // a `u16` of forty thousand read as an `i16` is a negative number, a
    // `i16` of minus three hundred read as a `u16` is sixty-five thousand,
    // and a `i8` of minus nine read as a `u8` is two hundred and forty-seven.
    // The answer says all three at once.
    Tile tiles[3] = {{7, 50, 0, -2},
                     {40000, -300, 1, -9},
                     {9, 1, 1, 3}};
    KestValue laid = kest_borrow(engine.runtime, tiles, 3, "Tile",
                                 sizeof(Tile));
    engine.frame[0] = laid;
    if (engine.frame[0].object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!asks(&engine, WORN)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (engine.frame[0].integer != 39999611) {
        fprintf(stderr, "the widths read back as %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    if (!kest_lend_ends(engine.runtime, laid)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("host lent %zu byte tiles: four widths read as %lld\n",
           sizeof(Tile), (long long)engine.frame[0].integer);

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
    if (!said_that(engine.runtime, "K0611", "is zero")) {
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
    if (!said_that(engine.runtime, "K0636", "did not come")) {
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
    events[3].as.named = 4;

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
    if (!said_that(engine.runtime, "K0610", "past a multiple")) {
        return 1;
    }
    printf("a lend %zu bytes into an `Event` was refused\n",
           _Alignof(Event) / 2);

    // And a lend of a shape holding something the machine owns, which is the
    // one thing about a lend that no number and no address can say. An `Npc`
    // holds a name, and a name is a pointer into the machine's own memory: the
    // host would be handing over one the machine did not put there and cannot
    // take back when the lend ends, so a program that kept it would be reading
    // the host's memory after the host had moved on. See D434.
    Npc crowd[2];
    memset(crowd, 0, sizeof(crowd));
    if (kest_borrow(engine.runtime, crowd, 2, "Npc", sizeof(Npc)).object !=
        NULL) {
        fprintf(stderr, "a shape holding a name was lent\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0647", "which is the machine's own")) {
        return 1;
    }
    printf("a lend of a shape holding text was refused\n");

    // And the two other things a host can be wrong about at a lend, which is
    // where a host is wrong on purpose: a type the program has no array of,
    // and one it has and lays out in another number of bytes. Both are the
    // host's own declaration coming apart from the program's, and neither had
    // ever been asked for. See D441.
    if (kest_borrow(engine.runtime, tiles, 3, "Tyle", sizeof(Tile)).object !=
        NULL) {
        fprintf(stderr, "a lend of a type the program has not got was made\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0610", "no array of `Tyle` to lend to")) {
        return 1;
    }
    if (kest_borrow(engine.runtime, tiles, 3, "Tile",
                    sizeof(Tile) + 4).object != NULL) {
        fprintf(stderr, "a lend of the wrong size was made\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0610", "and this host has")) {
        return 1;
    }
    // A handle that is not what the program takes. Four bytes at the front of
    // one say what it is, and this is the one thing about a handle the machine
    // checks, because the boundary cannot: `kest_call` knows how wide a frame
    // must be and not what is in it. Handing a store where an array was wanted
    // is a host mistake with a message, and nothing had ever made it — the
    // message was reached only by breaking the tree. See D527.
    {
        if (!asks(&engine, CREATE)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        // The frame now holds a store, and `worn` walks an array.
        if (kest_call(engine.runtime, engine.entry[WORN], engine.frame,
                      sizeof(engine.frame) / sizeof(engine.frame[0]))) {
            fprintf(stderr, "a store was walked as an array\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0612", "this is not an array")) {
            return 1;
        }
        printf("a store handed where an array was wanted was refused\n");
    }

    // And the shape D016 works out by hand, lent and read where it sits. The
    // `bool` says whether the `u16` beside it counts, so a wrong offset for
    // either is a wrong answer rather than a wrong size — and the size was
    // agreed on above, by both sides working it out and neither being told.
    // See D552.
    {
        Flagged some[3] = {{7, 1}, {9, 0}, {11, 1}};
        KestValue run = kest_borrow(engine.runtime, some, 3, "Flagged",
                                    sizeof(Flagged));
        if (run.object == NULL) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        engine.frame[0] = run;
        if (!asks(&engine, HOW_MANY_ON)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        if (engine.frame[0].integer != 18) {
            fprintf(stderr, "a `u16` beside a `bool` read back as %lld\n",
                    (long long)engine.frame[0].integer);
            return 1;
        }
        if (!kest_lend_ends(engine.runtime, run)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        printf("host lent %zu byte rows of a `u16` and a `bool`: 7 and 11 are "
               "on\n", sizeof(Flagged));
    }

    // And a name two modules wrote, which is the one thing a lend can be wrong
    // about that is not about the type at all. `Twin` is declared here and in
    // `examples/twins/twin.kest`, both held in arrays, so both have a layout
    // and `Twin` names one of two. The whole name is what tells them apart and
    // is what the machine says to write. See D526.
    {
        int32_t twins[2] = {3, 4};
        if (kest_borrow(engine.runtime, twins, 2, "Twin",
                        sizeof(twins[0])).object != NULL) {
            fprintf(stderr, "a lend of a name two modules wrote was made\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0610", "more than one `Twin`")) {
            return 1;
        }
        KestValue whole = kest_borrow(engine.runtime, twins, 2, "embed.Twin",
                                      sizeof(twins[0]));
        if (whole.object == NULL) {
            fprintf(stderr, "a lend under the whole name was refused\n");
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        // Given back, because what is lent and never ended is a header and a
        // place in the list of what is lent, and the next lend after it pays
        // for the list growing. The cost of a lend is measured further down.
        if (!kest_lend_ends(engine.runtime, whole)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        printf("a lend of a name two modules wrote was refused, and the whole "
               "name was taken\n");
    }

    printf("a lend of a type there is none of, and one of the wrong size, "
           "were refused\n");

    // And an array the program made, handed back and then handed in as though
    // this host had lent it. It is the machine's, so ending it would take
    // something back that was never given.
    engine.frame[0].integer = 0;
    if (!asks(&engine, OWN_ARRAY)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (kest_lend_ends(engine.runtime, engine.frame[0])) {
        fprintf(stderr, "the program's own array was taken back\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0637", "the program's own and not a "
                                            "lend")) {
        return 1;
    }
    printf("an array the program made was not this host's to take back\n");

    // A frame narrower than what a function takes, and one narrower than what
    // it gives back. Both are refused before anything is written, which is the
    // point of them: a frame too narrow is read past on the way in and written
    // past on the way out.
    if (kest_call(engine.runtime, engine.entry[HEAVIEST], engine.frame, 0)) {
        fprintf(stderr, "a call was made with a frame of nothing\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0611", "and this frame holds 0")) {
        return 1;
    }
    if (kest_call(engine.runtime, engine.entry[CREATE], engine.frame, 0)) {
        fprintf(stderr, "a result was written into a frame of nothing\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0611", "gives 1 slot back")) {
        return 1;
    }
    // And the same width said the other way round, where a host hands over
    // words rather than slots.
    {
        const char *one[1] = {"3.0"};
        if (kest_takes_text(engine.runtime, engine.entry[LENGTH_OF],
                            engine.frame, 1, one, 1)) {
            fprintf(stderr, "words were written into a frame of one slot\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0635", "and this frame holds 1")) {
            return 1;
        }
    }
    // And the frame a host could not make at all, which is the same news as
    // one nothing has been written into. Two of the three calls that take a
    // frame refused it already; the third read slot zero and this host had no
    // way of being told. See D511.
    {
        char nothing_said[8];
        if (kest_gave_text(engine.runtime, engine.entry[STEP], NULL,
                           nothing_said, sizeof(nothing_said)) >= 0) {
            fprintf(stderr, "a frame that was never made had something in "
                            "it\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0632", "nothing was called with it")) {
            return 1;
        }
    }
    printf("three frames too narrow to be written into were refused, and one "
           "that was never made\n");

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

    // And what a host does when what it has is bytes. A packet arrives as a
    // run of them at whatever address the reading put it, and the type the
    // program wants is read wider than a byte at a time — so the lend above
    // is refused, and refusing it is the whole of what the machine can do.
    // What a host does about it is this: copy into an array of the type
    // itself, which its own compiler aligns, and lend that. The copy is the
    // price of the bytes having arrived as bytes, and it is paid once for the
    // batch rather than once for each thing in it.
    unsigned char packet[sizeof(Event) * 2 + 1];
    memset(packet, 0, sizeof(packet));
    Event arriving[2];
    memset(arriving, 0, sizeof(arriving));
    arriving[0].tag = EVENT_HIT;
    arriving[0].as.hit = 4;
    arriving[1].tag = EVENT_HIT;
    arriving[1].as.hit = 5;
    // Written into the packet where a reader would have put them: one byte in,
    // so that nothing about the buffer is aligned for an `Event`.
    memcpy(packet + 1, arriving, sizeof(arriving));
    if (kest_borrow(engine.runtime, packet + 1, 2, "Event", sizeof(Event))
            .object != NULL) {
        fprintf(stderr, "a lend of a byte buffer as `Event` was allowed\n");
        return 1;
    }
    Event unpacked[2];
    memcpy(unpacked, packet + 1, sizeof(unpacked));
    engine.frame[0] =
        kest_borrow(engine.runtime, unpacked, 2, "Event", sizeof(Event));
    if (engine.frame[0].object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!asks(&engine, ON_EVENTS) || engine.frame[0].integer != 9) {
        fprintf(stderr, "a batch copied out of a byte buffer read as %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    printf("and read a batch copied out of a byte buffer: %d damage\n", 9);

    // And the other way to write the same crossing: hand the bytes over as
    // bytes and let the program read what it wants out of them. Nothing is
    // copied on either side — the buffer is the host's and stays where it is —
    // and what a wire form costs a program is a shift and an or per byte.
    // Four bytes a record, least significant first, which is what the program
    // says it reads and not what this machine happens to do.
    unsigned char wire[8];
    for (int record = 0; record < 2; record++) {
        // One with a byte above 127 in it, because that is where widening a
        // `u8` as though it were signed would show: every record above it
        // would be wrong and the program would still answer with a number.
        int32_t value = record == 0 ? 200 : 300;
        for (int byte = 0; byte < 4; byte++) {
            wire[record * 4 + byte] =
                (unsigned char)((value >> (byte * 8)) & 0xff);
        }
    }
    engine.frame[0] =
        kest_borrow(engine.runtime, wire, sizeof(wire), "u8", 1);
    if (engine.frame[0].object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (!asks(&engine, TOTAL_OF) || engine.frame[0].integer != 500) {
        fprintf(stderr, "a batch read out of bytes came to %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    printf("and the same batch read out of the bytes themselves: %d\n", 500);

    // And the answer written back into the same buffer, which is the crossing
    // the other way: what a program writes into a lend is the host's own
    // memory, so a program answering in a wire form writes the bytes where the
    // host will read them. Nothing is copied in either direction — the same
    // eight bytes carried the question and carry the answer.
    engine.frame[0] =
        kest_borrow(engine.runtime, wire, sizeof(wire), "u8", 1);
    engine.frame[1].integer = 7;
    engine.frame[2].integer = 258;
    if (engine.frame[0].object == NULL || !asks(&engine, ANSWER_INTO) ||
        engine.frame[0].integer != 265) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "a program answering into bytes said %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    // Read back the way this host would read anything off a wire: the bytes
    // are its own, and what is in them now is what the program put there.
    int32_t wrote_first = wire[0] | wire[1] << 8 | wire[2] << 16 | wire[3] << 24;
    int32_t wrote_second = wire[4] | wire[5] << 8 | wire[6] << 16 | wire[7] << 24;
    if (wrote_first != 7 || wrote_second != 258) {
        fprintf(stderr,
                "the program answered %d and %d into this host's bytes\n",
                wrote_first, wrote_second);
        return 1;
    }
    printf("and wrote its answer back into the same bytes: %d and %d\n",
           wrote_first, wrote_second);


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

    // And the same question with more than one alive at a time, which is the
    // frame a game actually has: entities, tiles and events are three blocks
    // rather than one. Eight at a time here, ended in the order they were
    // made, which is the order a host with a run of them has.
    Row batch[8];
    for (int i = 0; i < 8; i++) {
        batch[i].tag = i;
        for (int k = 0; k < 3; k++) {
            batch[i].cells[k].at = k;
            batch[i].cells[k].weight = (float)i;
        }
    }
    size_t widest_frame = 0;
    if (!frames_of_lending(&engine, batch, 100, &widest_frame)) {
        return 1;
    }
    printf("eight lends a frame for a hundred frames cost the heap what the "
           "first frame did: %zu bytes\n", widest_frame);

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
    if (!asks(&engine, STEP) || decider.meddles) {
        // What the machine said about either of them is said where it was
        // asked; what is left for here is whether it was asked at all, which
        // is a world with nothing in it to walk.
        fprintf(stderr, "nothing asked this host anything while running\n");
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

    // Asked before anything else is made, because what that answers about is
    // the memory: the first thing the machine makes goes where the text was,
    // and then the same pointer is the machine's again. Which is what happens
    // next — a world of its own, because the one made before is on a heap that
    // was thrown away in there. This host follows the rule it is here to show,
    // rather than reading a store it happens to still point at.
    if (!asks(&engine, CREATE)) {
        return 1;
    }
    engine.world = engine.frame[0];
    // And somebody in it, because what the probes below are about is a
    // reference into a store that has places: a store with none refuses every
    // reference by its index alone, which would make them pass without ever
    // reading a stamp.
    for (int who = 0; who < 2; who++) {
        engine.frame[0] = engine.world;
        engine.frame[1].integer = 9;
        if (!asks(&engine, SPAWN)) {
            return 1;
        }
    }

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

    // And words, which is the one answer a program cannot hand back as a value
    // without this host keeping a pointer into the machine's heap — and what
    // is kept there is gone when the heap goes, which is the rule this host is
    // shown breaking further up. Written into a lend it is this host's own
    // memory: the bytes are here afterwards whatever the machine does next.
    unsigned char words[16];
    memset(words, 0, sizeof(words));
    engine.frame[0] = kest_borrow(engine.runtime, words, sizeof(words), "u8", 1);
    engine.frame[1].integer = 0;
    engine.frame[2] = kest_text(engine.runtime, "kest", 4);
    if (engine.frame[0].object == NULL || engine.frame[2].text == NULL ||
        !asks(&engine, SAY_INTO) || engine.frame[0].integer != 4) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "a program writing words into a lend wrote %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    if (!kest_heap_reset(engine.runtime)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // Read after the heap the text was on has gone, because that is the whole
    // of why a host asks for words this way rather than keeping the value.
    if (memcmp(words, "kest", 4) != 0 || words[4] != 0) {
        fprintf(stderr, "the words a program wrote into this host's bytes "
                        "read `%.*s`\n",
                (int)sizeof(words), (const char *)words);
        return 1;
    }
    printf("and words a program wrote into this host's own bytes, still there "
           "after the heap went: `%s`\n",
           (const char *)words);

    // And how long gone lasts, for text. What a host keeps a piece of text by
    // is a pointer into the machine's heap, and a heap thrown away takes it —
    // which this host was told above. What it is not told is that the next
    // thing the machine makes goes where that was: the same pointer, the same
    // answer from `kest_still_holds`, and something else written there. A lend
    // has the same shape and the same reason (D352): a pointer carries no
    // stamp, so this is a rule rather than a refusal, and here is what it
    // looks like when a host keeps one anyway.
    if (!kest_heap_reset(engine.runtime)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestValue first_word = kest_text(engine.runtime, "the engine", 10);
    if (first_word.text == NULL || !kest_heap_reset(engine.runtime)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (kest_still_holds(engine.runtime, first_word)) {
        fprintf(stderr, "text survived the heap it was on\n");
        return 1;
    }
    KestValue next_word = kest_text(engine.runtime, "the second", 10);
    if (next_word.text == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (next_word.text != first_word.text) {
        fprintf(stderr,
                "the first thing on an emptied heap went somewhere else, so "
                "this host has nothing to say about the text it kept\n");
        return 1;
    }
    if (!kest_still_holds(engine.runtime, first_word) ||
        strcmp(first_word.text, "the second") != 0) {
        fprintf(stderr, "text kept across a reset reads `%s`\n",
                first_word.text);
        return 1;
    }
    printf("and text kept across a heap being thrown away reads what the "
           "machine made next: `%s`\n",
           first_word.text);

    // And the same for a lend, which the paragraph above says has the same
    // shape and this host had never shown. It has one thing text has not: a
    // header on the heap with a place in a list beside it, and a reset takes
    // the list as well. So the machine's answer is not the same — a piece of
    // text kept across a reset reads whatever was made next, and a lend kept
    // across one is refused twice over.
    Row outlived[8];
    memset(outlived, 0, sizeof(outlived));
    KestValue kept = kest_borrow(engine.runtime, outlived, 8, "Row",
                                 sizeof(Row));
    if (kept.object == NULL || !kest_heap_reset(engine.runtime)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (kest_still_holds(engine.runtime, kept)) {
        fprintf(stderr, "a lend outlived the heap its header was on\n");
        return 1;
    }
    if (kest_lend_ends(engine.runtime, kept)) {
        fprintf(stderr, "a lend from before a heap went was taken back\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0637", "not a lend this machine gave")) {
        return 1;
    }
    // And what that leaves a host paying. Nothing was carried over — not the
    // headers on the spare list and not the list of what is lent — so the
    // first frame of lending after a reset buys both again. D561's widest
    // frame is paid for once between resets, which is the number a host that
    // throws the heap away every frame is actually paying: all of it, every
    // frame.
    size_t after_went = 0;
    if (!frames_of_lending(&engine, outlived, 100, &after_went)) {
        return 1;
    }
    if (after_went == 0) {
        fprintf(stderr, "a heap thrown away left the machine holding what "
                        "this host had lent it\n");
        return 1;
    }
    printf("and a lend kept across one is not the machine's to give back; the "
           "first frame of lending after it cost %zu bytes again\n",
           after_went);

    // And which of the two a piece of text is, asked before it is kept rather
    // than after. Both answers above are about the heap: what the program made
    // while running goes when the heap does. What the program was written with
    // is in the build the machine was started from, and a host handed either
    // of them has one pointer and nothing in it to say which. So it asks.
    if (!asks(&engine, AS_WRITTEN)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestValue written = engine.frame[0];
    KestValue made = kest_text(engine.runtime, "made while running", 18);
    if (made.text == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (kest_kept_where(engine.runtime, written) != KEST_KEPT_PROGRAM ||
        kest_kept_where(engine.runtime, made) != KEST_KEPT_HEAP) {
        fprintf(stderr, "text out of the file is %s and text made while "
                        "running is %s\n",
                keeping(kest_kept_where(engine.runtime, written)),
                keeping(kest_kept_where(engine.runtime, made)));
        return 1;
    }
    // Both say yes to the question that has one answer, which is why that one
    // cannot be what a host keeping a value between frames reads.
    if (!kest_still_holds(engine.runtime, written) ||
        !kest_still_holds(engine.runtime, made)) {
        fprintf(stderr, "the machine has text it says it has not\n");
        return 1;
    }
    if (!kest_heap_reset(engine.runtime)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // And what the two are worth afterwards, which is what the asking was for.
    if (kest_kept_where(engine.runtime, made) != KEST_KEPT_NOWHERE) {
        fprintf(stderr, "text made while running outlived the heap\n");
        return 1;
    }
    if (kest_kept_where(engine.runtime, written) != KEST_KEPT_PROGRAM ||
        strcmp(written.text, "written into the file this came from") != 0) {
        fprintf(stderr, "text out of the file reads `%s` after a reset\n",
                written.text);
        return 1;
    }
    printf("and text the file was written with is the one a host may keep: "
           "`%s`, still there after the heap went\n", written.text);

    // And the same question of a lend, which is neither of the two answers
    // above. The block is this host's own and outlasts anything the machine
    // does; the header in front of it is on the heap and goes with it. A host
    // asking whether it may keep a lend is asking about two things at once,
    // and what it is told is which of them it is holding.
    Row the_block[2];
    memset(the_block, 0, sizeof(the_block));
    KestValue borrowed = kest_borrow(engine.runtime, the_block, 2, "Row",
                                     sizeof(Row));
    if (borrowed.object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestKept while_lent = kest_kept_where(engine.runtime, borrowed);
    if (while_lent != KEST_KEPT_LENT) {
        fprintf(stderr, "a lend was answered for as though the block were the "
                        "machine's\n");
        return 1;
    }
    // And what is left when it ends, which is the header: the machine's own
    // memory with nothing in front of it any more, and so the answer anything
    // else on the heap gets.
    if (!kest_lend_ends(engine.runtime, borrowed)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    KestKept when_ended = kest_kept_where(engine.runtime, borrowed);
    if (when_ended != KEST_KEPT_HEAP) {
        fprintf(stderr, "a lend that has ended still says this host's block is "
                        "in front of it\n");
        return 1;
    }
    // And the block on its own, which the machine never had: the same bytes
    // asked about without the header in front of them. A host that keeps the
    // address rather than the handle keeps something the machine has never
    // heard of, which is the honest answer and the useful one.
    KestValue plainly = {0};
    plainly.object = the_block;
    KestKept block_alone = kest_kept_where(engine.runtime, plainly);
    if (block_alone != KEST_KEPT_NOWHERE) {
        fprintf(stderr, "this host's own block was said to be the machine's\n");
        return 1;
    }
    printf("and a lend is %s, which the moment it ends is %s, while the block "
           "on its own is %s\n",
           keeping(while_lent), keeping(when_ended), keeping(block_alone));

    // And the build under them, asked for while they are still standing. What
    // the machines run is on it — the program, the layouts, and the text every
    // diagnostic points at — so this is refused where it is asked for rather
    // than found out about afterwards, and the report says how many are up.
    if (kest_build_free(build)) {
        fprintf(stderr, "a build was freed with machines standing on it\n");
        return 1;
    }
    FILE *under = tmpfile();
    if (under == NULL) {
        fprintf(stderr, "this host has nowhere to read a report back from\n");
        return 1;
    }
    kest_build_report(build, under, KEST_FORM_TEXT);
    rewind(under);
    char refusal[512];
    bool counted = false;
    while (fgets(refusal, sizeof(refusal), under) != NULL) {
        if (strstr(refusal, "K0640") != NULL &&
            strstr(refusal, "2 machines are standing on it") != NULL) {
            counted = true;
        }
    }
    fclose(under);
    if (!counted) {
        fprintf(stderr, "a build refused under its machines did not say how "
                        "many were standing on it\n");
        return 1;
    }
    printf("and refused the build under the two machines still standing\n");

    // And the other side of the answer: outside a call there is nothing
    // standing on the machine, so this is the free that happens. Nothing takes
    // a machine away by force — a host that asked from inside a call and never
    // came back would still be holding this one — so here is where it goes.
    if (!kest_runtime_free(other) || !kest_runtime_free(engine.runtime)) {
        fprintf(stderr, "a machine with nothing running on it was not freed\n");
        return 1;
    }
    // And nothing to free, which is what a host has after this and is not a
    // refusal: what it asked for is that there be no machine.
    if (!kest_runtime_free(NULL)) {
        fprintf(stderr, "freeing no machine was refused\n");
        return 1;
    }
    printf("and the machines went when nothing was running on them\n");
    // Two things a host is told that need a machine of their own, kept to the
    // end and given their own build and their own host: what a machine says
    // above is about the machines this host drives, and a machine started to
    // be refused something is not one of them. See D441.
    {
        KestHost *apart = kest_host_new();
        static Decider quiet = {-1, 1, false, false};
        if (apart == NULL ||
            !kest_host_bind(apart, "Io.write", io_write, stdout) ||
            !kest_host_bind(apart, "Engine.decide", engine_decide, &quiet) ||
            !kest_host_bind(apart, "Engine.name", engine_name, &quiet)) {
            fprintf(stderr, "a host of its own would not be made\n");
            return 1;
        }
    // And text handed over to a machine with no heap left to copy it into.
    // The bytes are copied, so a host handing over more than is left is told
    // rather than given a piece of text that is not there. A machine of its
    // own, because this one has a frame budget the rest of this run is about.
    {
        // A build of its own, because how many machines are standing on this
        // one is a thing this host says out loud further down: a machine
        // started to be refused something is not a machine this host drives.
        KestBuild *aside = kest_build(path, NULL, stderr, KEST_FORM_TEXT);
        KestLimits nothing_left = {0, 0, 0};
        nothing_left.heap_bytes = 64;
        KestRuntime *starved =
            aside == NULL ? NULL : kest_start(aside, apart, &nothing_left);
        if (starved == NULL) {
            fprintf(stderr, "a machine of its own would not start\n");
            return 1;
        }
        char many[512];
        memset(many, 'x', sizeof(many) - 1);
        many[sizeof(many) - 1] = '\0';
        if (kest_text(starved, many, sizeof(many) - 1).text == NULL ||
            kest_text(starved, many, sizeof(many) - 1).text[0] != '\0') {
            fprintf(stderr, "text was made where there was no room for it\n");
            return 1;
        }
        if (!said_that(starved, "K0605", "out of memory")) {
            return 1;
        }
        kest_runtime_free(starved);
        kest_build_free(aside);
        printf("text handed to a machine with %zu bytes was refused\n",
               (size_t)nothing_left.heap_bytes);
    }
    // And a host that did not account for the call it makes from inside one.
    // The numbers above are what the program wants plus what a call back in
    // wants; a machine started with what one function wants runs that function
    // and has nowhere to take the call this host makes from inside it. That is the one
    // thing a host is told here rather than left to find out, and nothing had
    // ever been told it. See D441.
    {
        // What `step` alone wants, which is what a host that knows which
        // function it drives would take and is exactly the number that leaves
        // nothing over for the call this host makes from inside it.
        KestLimits bare = {0, 0, 0};
        if (!kest_needs_of(build, "step", &bare, NULL)) {
            fprintf(stderr, "the program says nothing about what `step` "
                            "needs\n");
            return 1;
        }
        KestBuild *narrowly = kest_build(path, NULL, stderr, KEST_FORM_TEXT);
        KestRuntime *tight =
            narrowly == NULL ? NULL : kest_start(narrowly, apart, &bare);
        if (tight == NULL) {
            fprintf(stderr, "a machine of its own would not start\n");
            return 1;
        }
        quiet.rule = kest_entry(tight, "rule");
        quiet.asks_the_program = true;
        KestValue narrow[6] = {{0}};
        int32_t made = kest_entry(tight, "create");
        int32_t one = kest_entry(tight, "spawn");
        int32_t stepped = kest_entry(tight, "step");
        if (made < 0 || one < 0 || stepped < 0 ||
            !kest_call(tight, made, narrow, 6)) {
            kest_report(tight, stderr, KEST_FORM_TEXT);
            return 1;
        }
        KestValue held = narrow[0];
        narrow[0] = held;
        narrow[1].integer = 3;
        if (!kest_call(tight, one, narrow, 6)) {
            kest_report(tight, stderr, KEST_FORM_TEXT);
            return 1;
        }
        narrow[0] = held;
        // What the working out costs the program, read on either side of the
        // refusal: the answer is worked out on the heap the program is running
        // on, and a refusal that leaves something there is a frame budget that
        // shrinks every time something goes wrong. See D571.
        size_t heap_before = kest_heap_used(tight);
        kest_call(tight, stepped, narrow, 6);
        if (!said_under(tight, "K0602", "this program needs")) {
            return 1;
        }
        if (kest_heap_used(tight) != heap_before) {
            fprintf(stderr, "working out what a program needed cost it %zu "
                            "bytes of its own heap\n",
                    kest_heap_used(tight) - heap_before);
            return 1;
        }
        kest_runtime_free(tight);
        kest_build_free(narrowly);
        printf("a call in from a machine sized for `step` alone was refused, "
               "and told what to ask for at no cost to the program's heap\n");
    }
        kest_host_free(apart);
    }

    // And then the build, which nothing is standing on now.
    if (!kest_build_free(build)) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 1;
    }
    return 0;
}
