// A host that is not the command line. It compiles a file, makes a machine,
// and keeps a world between frames by holding the handle the program gave it.
//
//   make embed && ./examples/embed
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "kest.h"

// The same shape `embed.kest` declares as an enum. A four byte tag at nought
// and the payload after it at its own alignment is what a C tagged union is
// and what D026 says an enum is, so this array is the array Kest walks: the
// host lends it and nothing is copied at the boundary.
enum { EVENT_IDLE, EVENT_MOVED, EVENT_HIT, EVENT_NAMED };

// And what the program calls them, in the order it declares them, because that
// order is what the numbers above are. A host writing them down is a host
// holding a copy of somebody else's list, and `reads_the_cases` is where the
// copy is held against the list. See D702.
static const char *const event_names[] = {"Idle", "Moved", "Hit", "Named"};

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

// The other thing with a flag in it, which is not a tag: an optional is a value
// and a byte after it saying whether the value is there. A host lays one out as
// the value, the byte, and whatever padding the next field's alignment asks for
// — the same arithmetic a struct gets, over a field the program spells with a
// `?`. What an empty one holds where the value would be is what D712 is about.
typedef struct {
    int32_t at;
    _Bool held;
    int32_t n;
} Mark;

// The shape the program keeps in a store, declared here only to be refused: a
// host cannot lend one, because the name in it is the machine's. Laid out the
// way the program lays it out, so that the size is not what refuses it: a
// piece of text is what it is made of and how many bytes that is. See D964.
typedef struct {
    const char *name;
    uint64_t said;
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
enum { CREATE, SPAWN, STEP, ON_EVENTS, SILENCE, DAMAGE_OF, HURT_BY, WORST,
       BLAMED, BLAMED_BY, FOOTED, GREETS, WHO_IS, MARKED, MARKING, MARK,
       UNMARK, HEAVIEST,
       LENGTH_OF,
       BETWEEN, SPREAD, HOARD, PILE, CHURN, READY, FILLING, GLUED,
       JOINED, REPEATED, JOINED_PIECES, READABLE, GREW, POPPED, TOOK,
       EMPTIED, UNDER, NAMED, AT_ONCE, COPIED, BLANK, FIRST,
       BORN, HEALTH_OF, DROPPED, TOTAL_OF, ANSWER_INTO, SAY_INTO, WORN,
       MOVED, PUT_RECORD, OWN_ARRAY, HOW_MANY_ON, REACH,
       HEAVIEST_CELL, AS_WRITTEN, RANKED, APPLY, DOUBLED, GROWS, WEIGHED,
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
    uint32_t length = 0;
    const char *bytes = kest_text_bytes(frame, &length);
    // Written by its length rather than to a nought: text cut out of the
    // middle of some does not end in one. See D964.
    if (bytes != NULL && length > 0) {
        fwrite(bytes, 1, length, (FILE *)context);
    }
}

// The clock a machine times its own walks with. It is a host's job because
// the library is ISO C and there is no monotonic clock in it; what it counts
// in is this host's to decide, and the machine only adds them up. `clock()` is
// coarse and is what the standard has, which is the same trade
// `tools/inward.c` makes and for the same reason.
static uint64_t host_nanoseconds(void *context) {
    (void)context;
    return (uint64_t)clock() * (1000000000ULL / (uint64_t)CLOCKS_PER_SEC);
}

// What a host does with being told what a walk cost. This one counts them and
// keeps the last, which is the least a host can do with the door and enough to
// hold it to what it says; a host with a frame budget keeps every one of them
// and reads the middle and the tail off it, which is what `bench/measure` does.
typedef struct {
    unsigned long long count;
    KestPause last;
} PausesHeard;

static void host_was_told_a_pause(const KestPause *pause, void *context) {
    PausesHeard *heard = context;
    heard->count++;
    heard->last = *pause;
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
    // Whether this host answers the program's question about its own name with
    // text the machine made, which every host here does and one place asks it
    // not to on purpose: what a program is given back it may keep, and a
    // host's own bytes are not the machine's to keep. Written this way round
    // because a host that has lost track of what it handed over has a value
    // here that is neither of the two, and the one to fall into is the one
    // every host means. See D717.
    bool answers_as_the_machine;
    // Whether this host asks, from inside this call, for the two things it may
    // not have while a program is running. Asked for here because here is
    // inside a call: a host holding the machine between calls may have either
    // of them. It is put back to false where it is answered, so a host that
    // finds it still true was never asked.
    bool meddles;
    // And whether it answers a number too wide for the width the program keeps
    // it at. A slot is sixty-four bits and an `i32` is thirty-two, so this is
    // the one thing a host can put in an answer that the program's own
    // arithmetic cannot make. See D837.
    bool answers_too_wide;
} Decider;

// The engine's own policy. Asking the program is calling in from inside a call
// the program made, which is what an engine does when its rules live on both
// sides, and the machine puts what this starts above what is already running.
// Answering by itself is the same function on a different day.
// A crossing that is handed a shape. The machine lays the value out in the
// frame a piece per slot, in the order the layout says, so this reads three
// `f32` out of three slots and writes its answer over the first of them. What
// says those are the three it thinks they are is the layout, held against this
// host's own `Point` piece by piece before anything is bound. See D699.
static void engine_rank(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    (void)context;
    double sum = frame[0].real + frame[1].real + frame[2].real;
    frame[0].integer = (int64_t)sum;
}

// And a crossing handed a value with a tag in it. What is in the slots after
// the tag depends on which case it is, so this host reads the tag first and
// then reads its own union's worth out of the slots — which is what a `match`
// does on the other side of the same call. That the tags are the numbers this
// host thinks they are, and that each case carries what this host reads out of
// it, is held at binding rather than here: a frame is no place to find out.
// See D704.
// Whether the door below says it could not do what it was asked, which is the
// one thing a bound function could not say until D937.
static bool engine_hurt_refuses = false;

static void engine_hurt(KestValue *frame, KestRuntime *runtime, void *context) {
    if (engine_hurt_refuses) {
        frame[0].integer = 4242;
        kest_native_failed(runtime, "nothing here can be hurt");
        return;
    }
    (void)runtime;
    (void)context;
    int64_t cost = 0;
    switch ((int32_t)frame[0].integer) {
    case EVENT_MOVED:
        cost = (int64_t)(frame[1].real + frame[2].real);
        break;
    case EVENT_HIT:
    case EVENT_NAMED:
        cost = frame[1].integer;
        break;
    case EVENT_IDLE:
        break;
    default:
        // A tag this host has no name for is a program that grew a case, which
        // the binding below refuses before anything runs. Nought rather than a
        // read of a slot nobody wrote.
        break;
    }
    frame[0].integer = cost;
}

// And one that answers a value with a tag in it, which is the same reading
// written backwards: the host decides which case it means, writes that tag into
// the first slot and what the case carries into the ones after it. Which member
// of a slot each of those is, is this host's own to know — held at binding by
// `reads_the_cases` over what the crossing says it gives back, the same as what
// it says it takes. See D706.
//
// The context is whether to answer with a tag the program has no case for. One
// machine here asks for that on purpose, because it is the one mistake at this
// crossing nothing can be asked about beforehand — the tag is decided inside the
// call — and the rest bind nothing and are answered honestly.
static void engine_blame(KestValue *frame, KestRuntime *runtime, void *context) {
    (void)runtime;
    const bool *wrongly = context;
    int32_t cost = (int32_t)frame[0].integer;
    if (wrongly != NULL && *wrongly) {
        frame[0].integer = EVENT_NAMED + 1;
        frame[1].integer = cost;
        frame[2].integer = 0;
        return;
    }
    if (cost > 4) {
        frame[0].integer = EVENT_HIT;
        frame[1].integer = cost;
        frame[2].integer = 0;
    } else if (cost > 0) {
        frame[0].integer = EVENT_MOVED;
        frame[1].real = cost / 2.0;
        frame[2].real = cost / 2.0;
    } else {
        frame[0].integer = EVENT_IDLE;
        frame[1].integer = 0;
        frame[2].integer = 0;
    }
}

// And a crossing that answers with a shape rather than with one value: a name
// and a number, where the name is a word the machine has to own. What a host
// writes into the first slot of that is read the way it is read at the other
// end of the call, one field in — and the context says whether to write the
// machine's text there or this host's own bytes. See D719.
static void engine_who(KestValue *frame, KestRuntime *runtime, void *context) {
    const Decider *decider = context;
    const char *said = "one of the host's";
    if (decider != NULL && !decider->answers_as_the_machine) {
        frame[0].text = said;
        frame[1].integer = (int64_t)strlen(said);
    } else {
        kest_text(runtime, said, (uint32_t)strlen(said), frame);
    }
    frame[2].integer = decider != NULL && decider->answers_too_wide
                           ? (int64_t)1 << 40
                           : 3;
}

// What this host weighs something at, which is a number kept narrower than the
// slot it comes back in. A host writing a `double` here is writing a number no
// `f32` holds, and the walk over what came back is what says so. See D838.
static void engine_weigh(KestValue *frame, KestRuntime *runtime,
                         void *context) {
    const Decider *decider = context;
    (void)runtime;
    frame[0].real = decider != NULL && decider->answers_too_wide
                        ? 0.1
                        : (double)(float)0.5;
}

// A call this host makes from inside a call of its own, which is a run of the
// machine standing under a C frame of this host's. What is asked here is that
// it ends like a run: the frames it made go when it does, whether it returned
// or was refused, and the call it was made from carries on and answers what it
// would have answered. A run that ended in a refusal used to leave its frames
// behind, and the call underneath returned through them. See D1079 and D1032.
static int weighing_how;
static int weighing_went;
static int weighing_stopped;

static void weigh_from_inside(KestValue *frame, KestRuntime *runtime,
                              void *context) {
    (void)context;
    if (weighing_how != 0) {
        // Ten calls deep and then arithmetic that cannot be done, which is the
        // deepest refusal this program has.
        int32_t deepest = kest_entry(runtime, "tickWorld");
        uint8_t *code = NULL;
        uint8_t was = 0;
        uint32_t many = 0;
        if (weighing_how == 2) {
            // And the other way a run ends without returning: a breakpoint in
            // it. There is nothing for a resume to carry on into here -- this
            // function is what the run is standing under, and by the time a
            // host could ask, it has returned -- so the machine refuses to
            // stop at all.
            code = kest_code_of(runtime, deepest, &many);
            if (code != NULL) {
                was = code[0];
                code[0] = kest_break_byte();
            }
        }
        KestValue sharing[2] = {{0}};
        weighing_went = kest_call(runtime, deepest, sharing, 2) ? 1 : 0;
        weighing_stopped = kest_stopped(runtime) >= 0 ? 1 : 0;
        if (weighing_how == 2 && code != NULL) {
            code[0] = was;
        }
    }
    frame[0].real = (double)(float)21.0;
}

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
        // And the other half of the same rule: how much heap this machine may
        // have is a promise, and a promise changed while the program is
        // standing on what it promised is not one. A host divides what it has
        // between calls, which is where it knows what it has. See D850.
        if (kest_heap_allow(runtime, 4096)) {
            fprintf(stderr, "how much heap the machine may have was said "
                            "while the program was running\n");
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
        if (refused != 3) {
            fprintf(stderr,
                    "a host asked for three things it may not have and was "
                    "told about %d\n",
                    refused);
            _Exit(1);
        }
        // The count of what nobody read, in the form that gives it a name
        // rather than a sentence. What a machine keeps of what nobody asks
        // for, and the words it counts the rest in, are both held below --
        // by what two hundred unread refusals cost and by a refusal naming
        // `more since`. `notKept` is the same number in the other form, and
        // no command writes it: a command that refuses stops, and what stops
        // says one thing. So a host is the only reader it has. See D786.
        FILE *again = tmpfile();
        if (again == NULL) {
            fprintf(stderr, "this host has nowhere to read a report back\n");
            _Exit(1);
        }
        for (uint32_t asking = 0; asking < KEST_MOST_UNREAD + 4; asking++) {
            kest_heap_reset(runtime);
        }
        kest_report(runtime, again, KEST_FORM_JSON);
        rewind(again);
        bool named_the_rest = false;
        while (fgets(line, sizeof(line), again) != NULL) {
            if (strstr(line, "\"notKept\":4") != NULL) {
                named_the_rest = true;
            }
        }
        fclose(again);
        if (!named_the_rest) {
            fprintf(stderr,
                    "a host that never read was not told in JSON how many "
                    "more there were than the %u kept\n",
                    (unsigned)KEST_MOST_UNREAD);
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
    if (decider != NULL && !decider->answers_as_the_machine) {
        // This host's own bytes, handed over as though they were the
        // machine's. They are here for the life of the program and a stack
        // buffer would not be, which is the difference nothing at this
        // crossing could see. See D717.
        frame[0].text = said;
        frame[1].integer = (int64_t)strlen(said);
        return;
    }
    kest_text(runtime, said, (uint32_t)strlen(said), frame);
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
// The one file this host reads what it was told in, and the one place that
// reading is written. A report goes to a `FILE *`, so reading one means having
// somewhere to put it — and a host that made a file every time it asked would
// make one a frame. This host makes one, winds it back and writes over what was
// there, which is what a host reading every frame wants. See D633.
static FILE *heard = NULL;

// What was said, into the caller's own bytes, and how many bytes there were —
// which is what it needed and not what fitted, the way `snprintf` answers and
// the way this boundary answers everywhere else. A reading that took the
// number of bytes it got could not tell a report that was cut from one that was
// short, and would go looking for words that had been left behind.
//
// Nought is a report with nothing in it, which is a thing to hold rather than a
// failure: what ends a walk is a door that stayed quiet. See D634.
static size_t what_was_said(KestRuntime *runtime, KestBuild *build,
                            KestForm form, char *out, size_t room) {
    out[0] = '\0';
    if (heard == NULL) {
        heard = tmpfile();
        if (heard == NULL) {
            return 0;
        }
    }
    rewind(heard);
    if (runtime != NULL) {
        kest_report(runtime, heard, form);
    } else {
        kest_build_report(build, heard, form);
    }
    // Where this report ends, because what is after it is the last one: the
    // file is wound back rather than emptied, so a shorter report leaves the
    // tail of a longer one behind it.
    long end = ftell(heard);
    rewind(heard);
    size_t said = end < 0 ? 0 : (size_t)end;
    size_t want = said > room - 1 ? room - 1 : said;
    size_t got = want == 0 ? 0 : fread(out, 1, want, heard);
    out[got] = '\0';
    if (said > got) {
        // Said here rather than left to the reading above, which would go
        // looking for words that are in the report and not in these bytes,
        // and say the machine had not said them.
        fprintf(stderr, "what was said is %zu bytes and this host read %zu of "
                        "them\n", said, got);
    }
    return said;
}

// The next line of what was read, and where the one after it starts. A report
// is lines and every reading here is about one of them, so this is the walk all
// of them do.
static const char *line_of(const char *at, char *line, size_t room) {
    if (at == NULL || *at == '\0') {
        return NULL;
    }
    const char *end = strchr(at, '\n');
    size_t length = end == NULL ? strlen(at) : (size_t)(end - at) + 1;
    if (length > room - 1) {
        length = room - 1;
    }
    memcpy(line, at, length);
    line[length] = '\0';
    return end == NULL ? at + strlen(at) : end + 1;
}

static bool build_said_that(KestBuild *build, const char *code,
                            const char *words) {
    char said[8192];
    what_was_said(NULL, build, KEST_FORM_TEXT, said, sizeof(said));
    char line[512];
    bool named = false;
    for (const char *at = said; (at = line_of(at, line, sizeof(line))) != NULL;) {
        if (strstr(line, code) != NULL && strstr(line, words) != NULL) {
            named = true;
        }
    }
    if (!named) {
        fprintf(stderr, "the build refused without saying `%s` and `%s`\n",
                code, words);
    }
    return named;
}

// And the other half of that: a build with nothing to say. What ends a walk of
// what the program asks a host for is a name that is not there, and the thing
// that makes it the end rather than a mistake is that nothing was said about
// it. A check that only ever asks what was said cannot tell a door that stayed
// quiet from one that never spoke. See D582.
static bool build_said_nothing(KestBuild *build, const char *after) {
    char said[8192];
    bool quiet = what_was_said(NULL, build, KEST_FORM_TEXT, said, sizeof(said)) == 0;
    if (!quiet) {
        char line[512];
        line_of(said, line, sizeof(line));
        fprintf(stderr, "%s and the build said `%s`", after, line);
    }
    return quiet;
}

static bool said_that(KestRuntime *runtime, const char *code,
                      const char *words) {
    char said[8192];
    what_was_said(runtime, NULL, KEST_FORM_TEXT, said, sizeof(said));
    char line[512];
    bool named = false;
    for (const char *at = said; (at = line_of(at, line, sizeof(line))) != NULL;) {
        if (strstr(line, code) != NULL && strstr(line, words) != NULL) {
            named = true;
        }
    }
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
    char said[8192];
    what_was_said(runtime, NULL, KEST_FORM_TEXT, said, sizeof(said));
    char line[512];
    bool named = false;
    bool suggested = false;
    for (const char *at = said; (at = line_of(at, line, sizeof(line))) != NULL;) {
        if (strstr(line, code) != NULL) {
            named = true;
        }
        if (named && strstr(line, words) != NULL) {
            suggested = true;
        }
    }
    if (!suggested) {
        fprintf(stderr, "the machine refused without saying `%s` and, under "
                        "it, `%s`\n", code, words);
    }
    return suggested;
}

// And a machine with nothing to say, which is the answer more often than any
// refusal is: a frame that worked says nothing, and a host that reports after
// every one of them prints nothing. What makes that worth holding is what a
// report is — what was said since it was last asked — so a machine that said
// something on a path that worked hands it to whoever asks next, and the frame
// it lands on is not the frame it came from. See D583.
static bool said_nothing(KestRuntime *runtime, const char *after) {
    char said[8192];
    bool quiet = what_was_said(runtime, NULL, KEST_FORM_TEXT, said, sizeof(said)) == 0;
    if (!quiet) {
        char line[512];
        line_of(said, line, sizeof(line));
        fprintf(stderr, "%s and the machine said `%s`", after, line);
    }
    return quiet;
}

// How many places what a machine last said points at: the one it is about and
// the ones under it. A diagnostic holds `KEST_MOST_PLACES` of them and says how
// many more there were, and nothing but a run of calls deeper than that can
// show it. See D620.
static uint32_t places_said(KestRuntime *runtime, const char *words) {
    char said[8192];
    what_was_said(runtime, NULL, KEST_FORM_TEXT, said, sizeof(said));
    char line[512];
    uint32_t places = 0;
    bool named = false;
    for (const char *at = said; (at = line_of(at, line, sizeof(line))) != NULL;) {
        if (strstr(line, "-->") != NULL) {
            places++;
        }
        if (strstr(line, words) != NULL) {
            named = true;
        }
    }
    return named ? places : 0;
}

// What a machine has to be given to call these functions and be called back
// into from inside one of them. It is two questions and not one, because they
// are two things: what each function a host names needs on its own, and where
// the program already is when it reaches a host function (D604). A call taking
// one list would answer the first and look like the whole.
//
// The arithmetic is the host's, and this is all of it: the worst of what the
// names need, against where a call back in starts plus what the function this
// host calls from in there needs. `into` is NULL for a host that is never
// called back into, and then the second half is nothing. See D623.
static bool room_for_calling(KestBuild *build, const char *const *names,
                             const char *into, KestLimits *limits) {
    KestLimits back_in = {0, 0, 0, 0};
    KestLimits from_there = {0, 0, 0, 0};
    if (into != NULL && (!kest_needs_from(build, NULL, &back_in, NULL) ||
                         !kest_needs_of(build, into, &from_there, NULL))) {
        fprintf(stderr, "the program says nothing about calling `%s` from "
                        "inside a host function\n", into);
        return false;
    }
    limits->stack_slots = back_in.stack_slots + from_there.stack_slots;
    limits->call_depth = back_in.call_depth + from_there.call_depth;
    for (uint32_t i = 0; names[i] != NULL; i++) {
        KestLimits one = {0, 0, 0, 0};
        if (!kest_needs_of(build, names[i], &one, NULL)) {
            fprintf(stderr, "`%s` is not there to ask about\n", names[i]);
            return false;
        }
        if (one.stack_slots > limits->stack_slots) {
            limits->stack_slots = one.stack_slots;
        }
        if (one.call_depth > limits->call_depth) {
            limits->call_depth = one.call_depth;
        }
    }
    return true;
}

// The numbers a refusal says a call needs, read back out of what it said. A
// host sized for the functions it calls and refused at one of them is told what
// that one wants, and this is a host doing what the words say. See D622.
static bool needed_for(KestRuntime *runtime, KestLimits *asking) {
    char said[8192];
    what_was_said(runtime, NULL, KEST_FORM_TEXT, said, sizeof(said));
    char line[512];
    bool told = false;
    for (const char *at = said; (at = line_of(at, line, sizeof(line))) != NULL;) {
        const char *at = strstr(line, "calling this needs ");
        unsigned slots = 0;
        unsigned frames = 0;
        if (at != NULL &&
            sscanf(at, "calling this needs %u slots and %u frames", &slots,
                   &frames) == 2) {
            asking->stack_slots = slots;
            asking->call_depth = frames;
            told = true;
        }
    }
    return told;
}

// Whether a machine named both of two things in what it said. `said_that` is
// one line of a report; where a refusal points is a line of its own, and a
// refusal that names two places has three. See D613.
static bool said_in_both(KestRuntime *runtime, const char *one,
                         const char *other) {
    char said[8192];
    what_was_said(runtime, NULL, KEST_FORM_TEXT, said, sizeof(said));
    char line[512];
    bool first = false;
    bool second = false;
    for (const char *at = said; (at = line_of(at, line, sizeof(line))) != NULL;) {
        first = first || strstr(line, one) != NULL;
        second = second || strstr(line, other) != NULL;
    }
    if (!first || !second) {
        fprintf(stderr, "a refusal did not name both `%s` and `%s`\n", one,
                other);
    }
    return first && second;
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

// What this host believes about the cases of the one tagged shape it reads: the
// name of each tag, how many slots the case carries, and what is in them. This
// host reads slot one as a `double` for `Moved` and as a whole number for `Hit`,
// on the strength of four numbers written in this file — and a case added to
// that enum moves the numbers and changes what is in the slots, with nothing in
// a frame to say either. The kinds are the type's own widths, the same as
// everywhere else this host says what it believes; which member of a slot each
// of them is read through follows from the kind, and `kest_slot_of` is where
// that is asked. Every case here carries one kind, so one is written a case.
// See D704.
static bool reads_the_cases(const KestLayout *layout, uint16_t piece) {
    static const struct {
        uint16_t carries;
        uint8_t kind;
    } ours[] = {{0, KEST_L_I32},
                {2, KEST_L_F32},
                {1, KEST_L_I32},
                {1, KEST_L_I32}};
    _Static_assert(sizeof(ours) / sizeof(ours[0]) ==
                       sizeof(event_names) / sizeof(event_names[0]),
                   "every case this host names is one it reads");
    int32_t cases = (int32_t)(sizeof(ours) / sizeof(ours[0]));
    for (int32_t tag = 0; tag < cases; tag++) {
        const KestPiece *carries = NULL;
        uint16_t count = 0;
        const char *named = kest_case_of(layout, piece, tag, &carries, &count);
        if (named == NULL || strcmp(named, event_names[tag]) != 0 ||
            count != ours[tag].carries) {
            fprintf(stderr,
                    "this host reads tag %d as `%s` carrying %u, and the "
                    "program says `%s` carrying %u\n",
                    tag, event_names[tag], ours[tag].carries,
                    named == NULL ? "nothing" : named, count);
            return false;
        }
        for (uint16_t which = 0; which < count; which++) {
            if (carries[which].kind != ours[tag].kind) {
                fprintf(stderr,
                        "`%s` carries something other than what this host "
                        "reads out of it\n",
                        named);
                return false;
            }
        }
    }
    // And one past the last, which is where a host with fewer names than the
    // program has cases finds out. Nothing is the answer for a tag that is no
    // case, the same as for a layout that holds no tag at all.
    if (kest_case_of(layout, piece, cases, NULL, NULL) != NULL) {
        fprintf(stderr, "the program has a case this host has no name for\n");
        return false;
    }
    return true;
}

// Reading a value with a tag in it back out of a frame, which is `engine_hurt`
// in reverse: the same value, read at the end of a call rather than at the
// start of one. The tag comes first because which member of a slot the rest are
// is the tag's to say, and `kest_case_of` says which — this host reads the
// slots the way the case says and puts them where its own union keeps them.
// See D705.
static bool read_event(const KestLayout *gives, const KestValue *frame,
                       Event *into) {
    const KestPiece *carries = NULL;
    uint16_t count = 0;
    int32_t tag = (int32_t)frame[0].integer;
    const char *named = kest_case_of(gives, 0, tag, &carries, &count);
    if (named == NULL) {
        fprintf(stderr, "a result came back with tag %d, which is no case\n",
                tag);
        return false;
    }
    // Read the way the case says, before anything decides where they go: a
    // slot is eight bytes holding whatever was written into it, and what says
    // which of them this is, is the piece.
    double real_of[2] = {0, 0};
    int64_t whole_of[2] = {0, 0};
    if (count > 2) {
        fprintf(stderr, "`%s` carries %u slots and this host has room for 2\n",
                named, count);
        return false;
    }
    for (uint16_t piece = 0; piece < count; piece++) {
        if (kest_slot_of(carries[piece].kind) == KEST_S_REAL) {
            real_of[piece] = frame[1 + piece].real;
        } else {
            whole_of[piece] = frame[1 + piece].integer;
        }
    }
    into->tag = tag;
    switch (tag) {
    case EVENT_MOVED:
        into->as.moved.x = (float)real_of[0];
        into->as.moved.y = (float)real_of[1];
        break;
    case EVENT_HIT:
        into->as.hit = (int32_t)whole_of[0];
        break;
    case EVENT_NAMED:
        into->as.named = (int32_t)whole_of[0];
        break;
    case EVENT_IDLE:
        break;
    default:
        break;
    }
    return true;
}

// And the same for the shape with a tag in it: the tag at nought, and a piece
// for each slot the widest case carries, where that case carries it. What is
// really in one of those is the tag's to say — `KEST_L_PAYLOAD` is a layout
// saying so — and `kest_case_of` is where a host asks which. Written once, for
// the lend and for the crossing, because it is one opinion about one shape.
static void event_pieces(KestPiece event[3]) {
    event[0].offset = (uint16_t)offsetof(Event, tag);
    event[0].kind = KEST_L_TAG;
    event[1].offset = (uint16_t)offsetof(Event, as.moved.x);
    event[1].kind = KEST_L_PAYLOAD;
    event[2].offset = (uint16_t)offsetof(Event, as.moved.y);
    event[2].kind = KEST_L_PAYLOAD;
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
        // How wide it is rather than how many pieces it has: a piece of text
        // is one piece and two slots. See D964.
        at += layout->slots;
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
    uint32_t back = gives == NULL ? 0 : gives->slots;
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

    // A `bool` is one byte and the machine reads it as one, and says which of
    // the two truths it is: `KEST_L_BOOL` until D839 was `KEST_L_U8`, which is
    // what it is and not what it means, and a host that knows which it is
    // writing writes 0 or 1.
    // A value, the byte that says whether it is there, and the number after
    // the padding: three pieces, and the flag is a byte the same as a `bool`
    // is, because that is what it is.
    KestPiece mark[3];
    mark[0].offset = (uint16_t)offsetof(Mark, at);
    mark[0].kind = KEST_L_I32;
    mark[1].offset = (uint16_t)offsetof(Mark, held);
    mark[1].kind = KEST_L_HELD;
    mark[2].offset = (uint16_t)offsetof(Mark, n);
    mark[2].kind = KEST_L_I32;

    KestPiece flagged[2];
    flagged[0].offset = (uint16_t)offsetof(Flagged, kind);
    flagged[0].kind = KEST_L_U16;
    flagged[1].offset = (uint16_t)offsetof(Flagged, on);
    flagged[1].kind = KEST_L_BOOL;

    KestPiece event[3];
    event_pieces(event);

    // And what the program calls each of those pieces, which is the other half
    // of what a host doing schema work needs and used to be behind another
    // door: a save format matching bytes it already has against a program it
    // has just read wants the name and the byte together. `Row` is three
    // `Cell`s laid out where they stand and a tag, so a name here is a path
    // and not a word. See D946.
    {
        const char *called[] = {"cells[0].at", "cells[0].weight",
                                "cells[1].at", "cells[1].weight",
                                "cells[2].at", "cells[2].weight", "tag"};
        const KestLayout *of_a_row = NULL;
        if (kest_build_layout(build, "Row", &of_a_row) != 1 ||
            of_a_row->count != 7) {
            fprintf(stderr, "the program has no one `Row` of seven pieces\n");
            return 1;
        }
        for (uint16_t p = 0; p < of_a_row->count; p++) {
            const char *says = of_a_row->pieces[p].name;
            if (says == NULL || strcmp(says, called[p]) != 0) {
                fprintf(stderr, "`Row` piece %u is called `%s` and this host "
                                "reads `%s`\n",
                        p, says == NULL ? "nothing" : says, called[p]);
                return 1;
            }
        }
    }

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
                   {"Mark", sizeof(Mark), mark, 3, false, _Alignof(Mark)},
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

// A whole number written into shared memory at the width a piece says it is,
// which is the other half of `kest_slot_of`: that one says which member of a
// slot a kind is read through, and this says how many bytes of a block it is.
// A host that lays its own struct over lent memory is remembering; a host that
// writes where the layout says and as wide as the layout says is asking. See
// D899.
static bool wrote_where(unsigned char *at, uint8_t kind, int64_t number) {
    switch (kind) {
    case KEST_L_I8:
    case KEST_L_U8:
    case KEST_L_BOOL:
    case KEST_L_HELD:
    case KEST_L_NOTHING:
    case KEST_L_FLAGS8: {
        uint8_t narrow = (uint8_t)number;
        memcpy(at, &narrow, 1);
        return true;
    }
    case KEST_L_I16:
    case KEST_L_U16:
    case KEST_L_FLAGS16: {
        uint16_t narrow = (uint16_t)number;
        memcpy(at, &narrow, 2);
        return true;
    }
    case KEST_L_I32:
    case KEST_L_U32:
    case KEST_L_TAG:
    case KEST_L_FLAGS32: {
        uint32_t narrow = (uint32_t)number;
        memcpy(at, &narrow, 4);
        return true;
    }
    case KEST_L_I64:
    case KEST_L_U64:
    case KEST_L_FLAGS64: {
        int64_t wide = number;
        memcpy(at, &wide, 8);
        return true;
    }
    case KEST_L_F32: {
        float one = (float)number;
        memcpy(at, &one, 4);
        return true;
    }
    case KEST_L_F64: {
        double one = (double)number;
        memcpy(at, &one, 8);
        return true;
    }
    default:
        // A word, a piece of text, a reference, a function value, or what a
        // case carries: none of them is a number this host has one of, and
        // none of them may be lent.
        return false;
    }
}

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
    case KEST_S_TEXT:
    case KEST_S_TAGGED:
        // None of the three is a number this host has one of: a word is a
        // handle, text is bytes, and a payload is whatever the tag beside it
        // says. A host with one of those lends, hands text over or reads a tag
        // rather than writing a number, and saying so here is what keeps this
        // from writing a slot it does not understand.
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
    case KEST_S_TEXT:
    case KEST_S_TAGGED:
        // The same three this host has no number for going the other way.
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
    size_t before = kest_heap_taken(engine->runtime);
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
            widest = kest_heap_taken(engine->runtime);
        } else if (kest_heap_taken(engine->runtime) != widest) {
            fprintf(stderr, "eight lends a frame grew the heap by %zu after "
                            "frame %d\n",
                    kest_heap_taken(engine->runtime) - widest, frame);
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
        size_t was_small = kest_heap_taken(engine->runtime);
        KestValue small = kest_borrow(engine->runtime, letters, 4, "u8", 1);
        size_t small_cost = kest_heap_taken(engine->runtime) - was_small;
        size_t was_big = kest_heap_taken(engine->runtime);
        KestValue big =
            kest_borrow(engine->runtime, plenty, sizeof(plenty), "u8", 1);
        size_t big_cost = kest_heap_taken(engine->runtime) - was_big;
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
        size_t was_again = kest_heap_taken(engine->runtime);
        KestValue again =
            kest_borrow(engine->runtime, plenty, sizeof(plenty), "u8", 1);
        size_t again_cost = kest_heap_taken(engine->runtime) - was_again;
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

    size_t before_text = kest_heap_taken(engine->runtime);
    if (!kest_call(engine->runtime, engine->entry[READABLE], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    // A lend copies nothing; making text of one copies everything. This is
    // the one place that promise ends, and the number says so: the bytes are
    // the host's and the text is the program's.
    size_t copied = kest_heap_taken(engine->runtime) - before_text;
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

    // And the same bytes with a nought among them, which is a character like
    // any other and so text a program may make: four bytes in and four bytes
    // of text out, with the nought still in the middle of it. See D971.
    letters[2] = 0;
    engine->frame[0] = kest_borrow(engine->runtime, letters, 4, "u8", sizeof(letters[0]));
    if (engine->frame[0].object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    if (!kest_call(engine->runtime, engine->entry[READABLE], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    if (engine->frame[0].integer != 4) {
        fprintf(stderr, "a nought among the bytes cut the text short at %lld\n",
                (long long)engine->frame[0].integer);
        return false;
    }
    printf("and read four bytes with a nought among them as four\n");

    // And a byte that begins no character, which is what text is not. This is
    // the door where a run of bytes and text meet, so it is where being UTF-8
    // is asked about; nothing refuses the lend, because nothing about it is
    // wrong. What refuses is the asking.
    letters[2] = 0xff;
    engine->frame[0] = kest_borrow(engine->runtime, letters, 4, "u8", sizeof(letters[0]));
    if (engine->frame[0].object == NULL) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    if (kest_call(engine->runtime, engine->entry[READABLE], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
        fprintf(stderr, "text was made of a byte that begins no character\n");
        return false;
    }
    if (!said_that(engine->runtime, "K0604", "begins no character")) {
        return false;
    }
    printf("and refused a byte that begins no character\n");
    letters[2] = 'a';

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
static bool spends_the_heap(Engine *engine, KestValue kept) {
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
    KestLimits given = {0, 0, 0, 0};
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
    // And nothing on it, which is the one thing that moves this number the
    // other way. `kest_heap_used` is what a program is holding, so a host
    // watching a frame budget reads the difference between two of them — and
    // a reset that left anything behind would make the first difference after
    // it the leftovers plus the frame. Printed here since D630 and never held
    // until D825. See D825.
    if (kest_heap_used(engine->runtime) != 0) {
        fprintf(stderr, "the heap was thrown away and holds %zu bytes\n",
                kest_heap_used(engine->runtime));
        return false;
    }
    // And nothing refused on it either. The refusal that brought us here is
    // the old heap's, and a host that raises a ceiling by what the last one
    // asked for would raise this one by a number about a heap that is gone.
    // The two numbers go together: what was refused, and which of the two
    // refused it. See D826.
    if (kest_heap_wanted(engine->runtime) != 0 ||
        kest_heap_refused_by(engine->runtime) != KEST_REFUSED_NOTHING) {
        fprintf(stderr,
                "a heap thrown away still says %zu bytes were refused by %d\n",
                kest_heap_wanted(engine->runtime),
                (int)kest_heap_refused_by(engine->runtime));
        return false;
    }
    // And what a host kept on the heap that went. Asked here, while nothing
    // has been made since, because that is the only moment the answer is the
    // machine's rather than the allocator's: the first thing the machine makes
    // goes where something was, and a pointer carries no stamp -- so a host
    // that asks after making anything is asking whether that address is in use
    // now. See D353 and D996.
    if (kest_still_holds(engine->runtime, kept)) {
        fprintf(stderr, "the machine still had text it had thrown away\n");
        return false;
    }
    printf("and the name this host kept is gone with the heap it was on\n");
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
    size_t after_reset = kest_heap_taken(engine->runtime);
    KestValue fresh = kest_borrow(engine->runtime, four, 4, "u8", 1);
    size_t fresh_cost = kest_heap_taken(engine->runtime) - after_reset;
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
    if (!said_that(engine->runtime, "K0617", "bytes it was given")) {
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
    // than the machine can catch it. Asked of the library rather than of this
    // host's own compiler, because the two are not the same question and the
    // compilers do not spell it the same. See D828.
    if (!kest_checked()) {
    Row rows[2];
    memset(rows, 0, sizeof(rows));
    KestValue lent =
        kest_borrow(engine->runtime, rows, 2, "Row", sizeof(Row));
    if (lent.object == NULL || !kest_heap_reset(engine->runtime)) {
        kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
        return false;
    }
    engine->frame[0] = lent;
    // Two ways this goes wrong and one sentence for both. A machine that
    // stopped asking whose a handle is reads whatever is at that address, and
    // what is there decides whether the call comes back at all: sometimes it
    // succeeds and takes the handle, sometimes it refuses for a reason that is
    // not the right one. Both are the same fault to a reader and to whatever
    // is holding this host to catching it, so both say so.
    bool took = kest_call(engine->runtime, engine->entry[HEAVIEST],
                          engine->frame,
                          sizeof(engine->frame) / sizeof(engine->frame[0]));
    if (took || !said_that(engine->runtime, "K0636",
                           "did not come from this machine")) {
        fprintf(stderr,
                "a handle from before the heap was thrown away was taken\n");
        return false;
    }
    printf("and refused an array it lent before the heap was thrown away\n");
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
        size_t spent = kest_heap_taken(engine->runtime);
        if (!kest_call(engine->runtime, engine->entry[asked[which]], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        costs[which] = kest_heap_taken(engine->runtime) - spent;
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
        size_t spent = kest_heap_taken(engine->runtime);
        if (!kest_call(engine->runtime, engine->entry[ways[which]], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
            kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
            return false;
        }
        text_costs[which] = kest_heap_taken(engine->runtime) - spent;
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
    const char *called[2] = {"std.text.repeat", "std.text.join"};
    for (int which = 0; which < 2; which++) {
        size_t cost[2];
        for (int size = 0; size < 2; size++) {
            engine->frame[0].integer = size == 0 ? 200 : 400;
            size_t spent = kest_heap_taken(engine->runtime);
            if (!kest_call(engine->runtime, engine->entry[linear[which]], engine->frame, sizeof(engine->frame) / sizeof(engine->frame[0]))) {
                kest_report(engine->runtime, stderr, KEST_FORM_TEXT);
                return false;
            }
            cost[size] = kest_heap_taken(engine->runtime) - spent;
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
    // The first thing a host does, before it crosses at all: ask the library
    // what shape its doors are in and compare it with the number this host's
    // own compiler read out of the header. They differ when the header and the
    // library are two versions of this project, and everything below is then a
    // promise the library did not make. It costs one comparison at startup and
    // it is the only thing that catches it. See D974.
    if (kest_abi_version() != KEST_ABI_VERSION) {
        fprintf(stderr,
                "this host was built against abi %u and the library it is "
                "linked to is abi %u\n",
                (unsigned)KEST_ABI_VERSION, kest_abi_version());
        return 1;
    }
    {
        uint32_t profile = 0;
        const char *named = kest_profile(&profile);
        printf("host and library agree on abi %u, and `deterministic` here "
               "means %s %u\n",
               kest_abi_version(), named, profile);
    }

    // NULL for the library, which is the compiler finding its own: what
    // `KEST_LIB` says, or where it was installed.
    const char *path = argc > 1 ? argv[1] : "examples/embed.kest";
    KestBuild *build = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
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
    KestBuild *read_again = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
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
    // And what that cost was paid for: every file the build read, which is the
    // one named and everything it imports. A host that reloads when something
    // changes watches these rather than the file it named — an import is a
    // path relative to the file that wrote it, so which files a program is
    // made of is the loader's answer and not a host's. What is read back here
    // is that the list ends, that every file in it is a file this host can
    // open at the size the build says it is, and that the total is the sum of
    // them. See D657.
    size_t added_up = 0;
    uint32_t files = 0;
    for (uint32_t at = 0;; at++) {
        const char *from = kest_build_read(build, at);
        if (from == NULL) {
            break;
        }
        size_t says = kest_build_read_bytes(build, at);
        FILE *open_it = fopen(from, "rb");
        if (open_it == NULL || fseek(open_it, 0, SEEK_END) != 0) {
            fprintf(stderr, "the build says it read `%s` and this host cannot "
                            "open it\n", from);
            return 1;
        }
        long there = ftell(open_it);
        fclose(open_it);
        if (there < 0 || (size_t)there != says) {
            fprintf(stderr, "the build says `%s` is %zu bytes and it is %ld\n",
                    from, says, there);
            return 1;
        }
        added_up += says;
        files++;
    }
    // And what says a file is the same file: a number that moves when its bytes
    // move. A host that reloads compares this rather than the size, because two
    // edits that keep the length are the same size and not the same file. The
    // whole program has one too, folded from the files' own, and two builds of
    // the same program answer alike — which is what a host looking for what it
    // compiled last time is asking. See D658.
    if (kest_build_mark(build) == 0 ||
        kest_build_mark(build) != kest_build_mark(read_again) ||
        kest_build_read_mark(build, files) != 0) {
        fprintf(stderr, "the program marks %016llx and the same program built "
                        "again marks %016llx\n",
                (unsigned long long)kest_build_mark(build),
                (unsigned long long)kest_build_mark(read_again));
        return 1;
    }
    // And the other question a mark answers: what the machine will run, which
    // is not what was read to get there. A host caching what it compiled keys
    // on this one — a comment added moves every byte after it and moves the
    // mark over the file, and the program it runs is the one it ran. Two builds
    // of one program answer alike here for the same reason they do above. See
    // D659.
    if (kest_build_code_mark(build) == 0 ||
        kest_build_code_mark(build) != kest_build_code_mark(read_again)) {
        fprintf(stderr, "what this program runs marks %016llx and the same "
                        "program built again marks %016llx\n",
                (unsigned long long)kest_build_code_mark(build),
                (unsigned long long)kest_build_code_mark(read_again));
        return 1;
    }
    for (uint32_t at = 0; at < files; at++) {
        if (kest_build_read_mark(build, at) == 0) {
            fprintf(stderr, "`%s` was read and marks nothing\n",
                    kest_build_read(build, at));
            return 1;
        }
    }
    if (files == 0 || added_up != kest_build_source(build) ||
        kest_build_read_bytes(build, files) != 0) {
        fprintf(stderr, "the build read %u file(s) adding up to %zu and says "
                        "%zu\n",
                files, added_up, kest_build_source(build));
        return 1;
    }
    printf("it read %u file(s), %zu bytes of source, and compiling them cost "
           "%zu\n", files, added_up, first_build);

    // And what asking that build what the program needs costs it. The walk is
    // six arrays a function wide, the answer does not change after the program
    // is compiled, and it is worked out once: asking again costs nothing, and
    // so does every machine started from it, which used to do the same walk in
    // its own room. Measured on the second build, which nobody has asked
    // anything yet — the first has been asked several times by here. See D607.
    KestLimits asked = {0, 0, 0, 0};
    size_t before_asking = kest_build_cost(read_again);
    if (!kest_needs(read_again, &asked, NULL)) {
        fprintf(stderr, "a build that compiled says nothing about what it "
                        "needs\n");
        return 1;
    }
    size_t one_walk = kest_build_cost(read_again) - before_asking;
    if (!kest_needs(read_again, &asked, NULL)) {
        fprintf(stderr, "a build says what it needs once and not twice\n");
        return 1;
    }
    size_t asking_again = kest_build_cost(read_again) - before_asking - one_walk;
    if (one_walk == 0 || asking_again != 0) {
        fprintf(stderr, "asking what a program needs cost %zu bytes and "
                        "asking again cost %zu\n",
                one_walk, asking_again);
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
    static Decider decider = {-1, 1, true, true, false, false};
    // Whether the crossing that answers an event answers with a tag nobody
    // declared. False everywhere but the one place that asks for the refusal.
    static bool blaming = false;
    if (host == NULL || !kest_host_bind(host, "Io.write", io_write, stdout) ||
        !kest_host_bind(host, "Engine.decide", engine_decide, &decider) ||
        !kest_host_bind(host, "Engine.name", engine_name, &decider) ||
        !kest_host_bind(host, "Engine.rank", engine_rank, &decider) ||
        !kest_host_bind(host, "Engine.hurt", engine_hurt, NULL) ||
        !kest_host_bind(host, "Engine.blame", engine_blame, &blaming) ||
        !kest_host_bind(host, "Engine.weigh", engine_weigh, &decider) ||
        !kest_host_bind(host, "Engine.who", engine_who, &decider)) {
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

    // Where this host's own `Point` keeps its three floats, which is what a
    // crossing handed one is compared against.
    KestPiece crossing[3];
    point_pieces(crossing);
    KestPiece tagging[3];
    event_pieces(tagging);

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
        // And where its pieces are, for a crossing handed a shape: this
        // host's own `offsetof`, written out once and compared with what the
        // program says the argument is laid out as. NULL where the argument
        // is not a shape this host takes apart. See D699.
        const KestPiece *pieces;
        uint16_t count;
        // And whether those pieces are a value with a tag in it, which is the
        // one thing about a layout that changes what the pieces mean: a
        // payload slot holds what the tag says and nothing else does. A host
        // that read a tagged shape as an untagged one of the same width would
        // read the widest case's slots whatever the tag said. See D704.
        bool tagged;
        // And what this host writes back over the frame, which the program
        // reads as whatever it declared: a number written where a piece of
        // text is wanted is a pointer made out of an integer, and the program
        // reads it before anything can say so. One kind a slot, because an
        // answer is a value like any other and a shape answered with is more
        // than one of them; nought slots where nothing comes back. See D700
        // and D719.
        uint8_t writes[4];
        uint16_t back;
    } bound[] = {
        {"Io.write", 1, false, 0, NULL, 0, false, {0}, 0},
        {"Engine.decide", 1, true, sizeof(int32_t), NULL, 0, false,
         {KEST_L_I32}, 1},
        {"Engine.name", 0, true, 0, NULL, 0, false, {KEST_L_TEXT}, 1},
        {"Engine.rank", 1, true, sizeof(Point), crossing, 3, false,
         {KEST_L_I32}, 1},
        {"Engine.hurt", 1, true, sizeof(Event), tagging, 3, true,
         {KEST_L_I32}, 1},
        // The one that answers a value with a tag in it, which is three slots
        // rather than one: the tag, and what the case it names carries.
        {"Engine.blame", 1, true, sizeof(int32_t), NULL, 0, false,
         {KEST_L_TAG, KEST_L_PAYLOAD, KEST_L_PAYLOAD}, 3},
        // And one that answers with a shape: a name and a number, where the
        // name is a word the machine has to own. Nought bytes for what it
        // takes, because it takes nothing.
        {"Engine.who", 0, true, 0, NULL, 0, false,
         {KEST_L_TEXT, KEST_L_I32}, 2},
    };

    // What the program may do, which is a question a host answers before it
    // answers what to bind: a capability is the receiver of an extern, so
    // walking these is walking what the program will ask for in groups rather
    // than one function at a time. A host that will not give a program the
    // filesystem reads this and binds nothing under that name. See D981.
    {
        uint32_t how_many = 0;
        for (uint32_t i = 0; kest_build_capability(build, i) != NULL; i++) {
            const char *asked = kest_build_capability(build, i);
            printf("the program asks for the capability `%s`\n",
                   asked[0] == '\0' ? "(none)" : asked);
            how_many++;
        }
        if (how_many == 0) {
            printf("the program asks for no capability at all\n");
        }
    }

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
            // And what comes back, read the same way: the layout says what
            // the program will make of the slot this host writes, and a host
            // that wrote a number where text is wanted has made a pointer out
            // of an integer. The same question as the one above, about the
            // other end of the same crossing. See D700.
            const KestLayout *answer = kest_extern_gives(build, i);
            if (bound[b].gives) {
                bool agrees = answer != NULL && answer->count == bound[b].back;
                for (uint16_t p = 0; agrees && p < bound[b].back; p++) {
                    agrees = answer->pieces[p].kind == bound[b].writes[p];
                }
                if (!agrees) {
                    fprintf(stderr,
                            "`%s` gives back something other than what this "
                            "host writes\n", wanted);
                    missing = true;
                }
                // And what each case of it carries, for one that answers a
                // value with a tag in it. The kinds above are the widest
                // case's and say `KEST_L_PAYLOAD` where the tag decides, so a
                // host that stopped there has held the shape and not what it
                // will write into one. See D706.
                for (uint16_t p = 0; agrees && p < answer->count; p++) {
                    if (answer->pieces[p].kind == KEST_L_TAG &&
                        !reads_the_cases(answer, p)) {
                        missing = true;
                    }
                }
            }
            // And where the pieces of it are, for a crossing handed a shape.
            // Two shapes of one width with their fields in another order are
            // one width, so a host that compared only the bytes would bind a
            // function that reads the second field as the first and find out
            // at the first call. The same reading a lend gets, because it is
            // the same question about the same layout. See D699.
            if (bound[b].pieces != NULL && first != NULL &&
                !same_pieces(first, bound[b].pieces, bound[b].count,
                             bound[b].tagged)) {
                fprintf(stderr, "`%s` is handed a shape laid out differently "
                                "here\n", wanted);
                missing = true;
            }
            // And what each case of it carries, for a crossing handed a value
            // with a tag in it. The pieces above are the widest case's and say
            // `KEST_L_PAYLOAD` where the tag decides, so a host that stopped
            // there has held the shape and not what it will read out of one.
            // See D704.
            if (bound[b].tagged && first != NULL && !reads_the_cases(first, 0)) {
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
        // The end of the walk, which is the one of the four that says nothing:
        // a host walks until it is handed no name, and a walk that ended with
        // a diagnostic in the report would put one there every time anybody
        // read the list.
        if (!build_said_nothing(build, "a walk of them ended")) {
            return 1;
        }
        // And the three that are not the end, each asked on its own and read
        // back on its own. Asked together they are one complaint in the report
        // and three doors behind it, and a check that reads the report once
        // cannot tell which of them spoke — two could go quiet and this would
        // go on passing. See D582.
        if (kest_extern_takes(build, past) != 0 ||
            !build_said_that(build, "K0648", "there is nothing at")) {
            fprintf(stderr, "asking how many a %uth function takes\n", past);
            return 1;
        }
        if (kest_extern_layout(build, past, 0) != NULL ||
            !build_said_that(build, "K0648", "there is nothing at")) {
            fprintf(stderr, "asking what a %uth function takes\n", past);
            return 1;
        }
        if (kest_extern_gives(build, past) != NULL ||
            !build_said_that(build, "K0648", "there is nothing at")) {
            fprintf(stderr, "asking what a %uth function gives back\n", past);
            return 1;
        }
        // And the walk inside one of them, which ends the same way: what a
        // function takes is asked for one argument at a time until there is no
        // layout, and the end of that walk is a host that has read all of them
        // rather than one that asked wrongly. See D584.
        uint32_t arguments = kest_extern_takes(build, 0);
        if (kest_extern_layout(build, 0, arguments) != NULL ||
            !build_said_nothing(build, "a walk of what one takes ended")) {
            return 1;
        }
        printf("the program asks for %u of them, a walk of them ends quietly, "
               "and the three questions past the end were each refused\n",
               past);
    }

    // What the program needs, rather than a number this host guessed. A
    // program that can reach itself has no answer, and then a guess is all
    // there is.
    // Asked with a heap already written in, which is what a host that reuses
    // one of these has. What comes back is nought there: the heap is not a
    // number a program has, and all three of these doors say that the same way
    // rather than one of them leaving the field as it found it. A host's own
    // cap goes on after asking, which is what this one does below. See D724.
    KestLimits limits = {0, 0, 64, 0};
    KestReason why = {KEST_REACH_UNASKED, NULL};
    if (kest_needs(build, &limits, &why)) {
        if (limits.heap_bytes != 0) {
            fprintf(stderr, "asking what a program needs left %zu bytes of "
                            "heap written where nothing was answered\n",
                    limits.heap_bytes);
            return 1;
        }
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
        // The same asking of one function, with the same heap written in
        // first: three doors, one shape, one answer about the field none of
        // them knows.
        KestLimits stepping = {0, 0, 64, 0};
        if (kest_needs_of(build, "step", &stepping, NULL) &&
            stepping.heap_bytes != 0) {
            fprintf(stderr, "asking what `step` needs left %zu bytes of heap "
                            "written where nothing was answered\n",
                    stepping.heap_bytes);
            return 1;
        }
        if (kest_needs_of(build, "step", &stepping, NULL) &&
            stepping.stack_slots < limits.stack_slots) {
            printf("  `step` alone needs %u slots and %u frame%s\n",
                   stepping.stack_slots, stepping.call_depth,
                   stepping.call_depth == 1 ? "" : "s");
        }
        // And the same name asked of the door that answers a bound where
        // there is no least. This one has a least, so the bound is the least
        // and the frames are not looked at: a host that asks the second
        // question of a program that can answer the first gets the first,
        // which is what makes it safe to ask always. See D817.
        KestLimits bounded = {0, 0, 0, 0};
        KestReason which_one = {KEST_REACH_UNASKED, NULL};
        if (!kest_bound_of(build, "step", 4, &bounded, &which_one) ||
            which_one.reach != KEST_REACH_KNOWN ||
            bounded.stack_slots != stepping.stack_slots ||
            bounded.call_depth != stepping.call_depth) {
            fprintf(stderr,
                    "`step` needs %u slots and %u frames and is bounded at "
                    "%u and %u\n",
                    stepping.stack_slots, stepping.call_depth,
                    bounded.stack_slots, bounded.call_depth);
            return 1;
        }
        // This host calls back into the program from inside one of its own
        // functions, and what that needs is not a number to double and hope
        // over: it is where the machine already is when it reaches a host
        // function, plus what the function called from there needs on its own.
        KestLimits inside = {0, 0, 0, 0};
        KestLimits rule = {0, 0, 0, 0};
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

    // And what saying which functions this host calls would be worth, which is
    // the question a host writer asks before writing the list. Three numbers:
    // what the program wants, what the ones this host actually calls want, and
    // where a call back in starts from. The first is what a host that says
    // nothing gets; the second is what naming them would get; the third is the
    // floor under both for a host that is called from inside a frame, which
    // this one is. See D604.
    {
        KestLimits everything = {0, 0, 0, 0};
        KestLimits named = {0, 0, 0, 0};
        KestLimits from_inside = {0, 0, 0, 0};
        static const char *const calls[] = {"step", "create", "spawn", NULL};
        if (!kest_needs(build, &everything, NULL)) {
            fprintf(stderr, "the program says nothing about what it needs\n");
            return 1;
        }
        // And the same asked of the door that bounds where there is nothing to
        // answer. This program has a least, so that is what comes back and the
        // frames are not looked at — the first of the three doors holding the
        // same promise as the other two. See D823.
        KestLimits at_most = {0, 0, 0, 0};
        KestReason most_why = {KEST_REACH_UNASKED, NULL};
        if (!kest_bound(build, 4, &at_most, &most_why) ||
            most_why.reach != KEST_REACH_KNOWN ||
            at_most.stack_slots != everything.stack_slots ||
            at_most.call_depth != everything.call_depth) {
            fprintf(stderr,
                    "the program needs %u slots and %u frames and is bounded "
                    "at %u and %u\n",
                    everything.stack_slots, everything.call_depth,
                    at_most.stack_slots, at_most.call_depth);
            return 1;
        }
        if (!room_for_calling(build, calls, NULL, &named)) {
            return 1;
        }
        KestReason where = {KEST_REACH_UNASKED, NULL};
        if (!kest_needs_from(build, NULL, &from_inside, &where)) {
            fprintf(stderr, "nothing says where a call back in starts\n");
            return 1;
        }
        // Naming them is less than the program wants, and the call back in is
        // more than either: a host that binds a function the program calls
        // from deep inside pays for where that is, whichever functions it
        // calls itself. Held rather than printed, because a number that moved
        // would otherwise be a paragraph in a decision that quietly stopped
        // being true.
        if (named.stack_slots >= everything.stack_slots ||
            from_inside.stack_slots <= named.stack_slots) {
            fprintf(stderr, "the program wants %u, these three want %u, and a "
                            "call back in starts at %u\n",
                    everything.stack_slots, named.stack_slots,
                    from_inside.stack_slots);
            return 1;
        }
        printf("the program wants %u slots, the three this host drives want "
               "%u, and a call back in starts at %u\n",
               everything.stack_slots, named.stack_slots,
               from_inside.stack_slots);
        // And which function the call back in is in. The number is what a
        // host has to make room for and the name is the only thing it could
        // do anything about, so a host told one and not the other knows how
        // much it is paying and nothing about why. What holds the name is
        // asking that function the same question: it reaches the host from
        // less far in than the program does, because it is the function the
        // call is in rather than the one at the top of the chain that gets
        // there. A name that came back as the function asked about would
        // answer the whole program's own number. See D605.
        KestLimits there = {0, 0, 0, 0};
        if (where.where == NULL ||
            !kest_needs_from(build, where.where, &there, NULL) ||
            there.stack_slots == 0 ||
            there.stack_slots >= from_inside.stack_slots) {
            fprintf(stderr, "a call back in starts at %u, said to be in `%s`, "
                            "which gets there at %u\n",
                    from_inside.stack_slots,
                    where.where == NULL ? "nothing" : where.where,
                    there.stack_slots);
            return 1;
        }
        // And that reaching the host from inside that function is no more
        // than everything it reaches. What a call back in starts on is the
        // function's own widest plus the worst reach below it, and what it
        // needs altogether is its widest plus the worst reach of any kind --
        // the host reaches being some of those, never more. A number the
        // other way round would be a host told to make room for a place the
        // program cannot get to. See D801.
        KestLimits everywhere = {0, 0, 0, 0};
        if (!kest_needs_of(build, where.where, &everywhere, NULL) ||
            there.stack_slots > everywhere.stack_slots) {
            fprintf(stderr,
                    "`%s` reaches this host %u slots in and reaches "
                    "everything it reaches at %u\n",
                    where.where, there.stack_slots, everywhere.stack_slots);
            return 1;
        }
        printf("and the call is in `%s`, which reaches this host %u slots "
               "in on its own\n",
               where.where, there.stack_slots);
        // And the third door asked of a program that can answer it without a
        // bound, which has to be the answer and not a bound above it. See
        // D818.
        KestLimits bounded_from = {0, 0, 0, 0};
        KestReason which_from = {KEST_REACH_UNASKED, NULL};
        if (!kest_bound_from(build, NULL, 4, &bounded_from, &which_from) ||
            which_from.reach != KEST_REACH_KNOWN ||
            bounded_from.stack_slots != from_inside.stack_slots ||
            bounded_from.call_depth != from_inside.call_depth) {
            fprintf(stderr,
                    "a call back in starts at %u slots and %u frames and is "
                    "bounded at %u and %u\n",
                    from_inside.stack_slots, from_inside.call_depth,
                    bounded_from.stack_slots, bounded_from.call_depth);
            return 1;
        }
    }

    // And what a hundred refused calls cost a host that reads what it was
    // told. A program refused every frame says the same sentence every frame
    // — the numbers in it are the call's, so it cannot be said once (D616) —
    // and the room the words were written in is this machine's. Read, and it
    // goes back: what a host has been told is the host's. See D617.
    {
        KestLimits tight = {0, 0, 64, 0};
        KestRuntime *filling = kest_start(build, host, &tight);
        FILE *told = tmpfile();
        if (filling == NULL || told == NULL) {
            fprintf(stderr, "a machine with a heap of 64 bytes would not "
                            "start\n");
            return 1;
        }
        int32_t fills = kest_entry(filling, "filling");
        KestValue asking[4] = {{0}};
        asking[0].integer = 40;
        if (fills < 0 || kest_call(filling, fills, asking, 4)) {
            fprintf(stderr, "a program with 64 bytes of heap filled an "
                            "array\n");
            return 1;
        }
        kest_report(filling, told, KEST_FORM_TEXT);
        size_t after_reading = kest_runtime_cost(filling);
        long said_once = ftell(told);
        uint32_t answered = 0;
        for (uint32_t again = 0; again < 100; again++) {
            long before_saying = ftell(told);
            asking[0].integer = 40;
            if (kest_call(filling, fills, asking, 4)) {
                fprintf(stderr, "a heap that was full filled an array\n");
                return 1;
            }
            kest_report(filling, told, KEST_FORM_TEXT);
            if (ftell(told) > before_saying) {
                answered++;
            }
        }
        long said_again = ftell(told);
        if (kest_runtime_cost(filling) != after_reading || answered != 100) {
            fprintf(stderr, "a hundred refused calls read back cost the "
                            "machine %zu bytes and %u of them said why\n",
                    kest_runtime_cost(filling) - after_reading, answered);
            return 1;
        }
        // And a host that never asks. A machine does not end, so what nobody
        // has asked for is held to the size the list is made at and the rest
        // is counted: a hundred more refused calls hold what sixteen of them
        // said, and the report says how many there were rather than stopping
        // where a reader would take it for the end. See D618.
        size_t holding = kest_runtime_cost(filling);
        for (uint32_t quiet = 0; quiet < 100; quiet++) {
            asking[0].integer = 40;
            if (kest_call(filling, fills, asking, 4)) {
                fprintf(stderr, "a heap that was full filled an array\n");
                return 1;
            }
        }
        size_t unread = kest_runtime_cost(filling) - holding;
        // And a hundred more on top of those, which cost nothing at all: what
        // is kept is full, so every one of them is counted and none of them is
        // written down. That is the claim, and it does not depend on how long
        // a sentence is.
        size_t full = kest_runtime_cost(filling);
        for (uint32_t quiet = 0; quiet < 100; quiet++) {
            asking[0].integer = 40;
            if (kest_call(filling, fills, asking, 4)) {
                fprintf(stderr, "a heap that was full filled an array\n");
                return 1;
            }
        }
        char left_out[64];
        snprintf(left_out, sizeof(left_out), "and %u more since",
                 200 - KEST_MOST_UNREAD);
        if (kest_runtime_cost(filling) != full ||
            !said_in_both(filling, left_out, "did not keep")) {
            fprintf(stderr, "two hundred refused calls nobody read cost the "
                            "machine %zu bytes and then %zu\n",
                    unread, kest_runtime_cost(filling) - full);
            return 1;
        }
        printf("and two hundred nobody read cost it %zu and then nothing, "
               "holding %u of them and counting the rest\n",
               unread, KEST_MOST_UNREAD);

        // And what a reader is told when the words do not fit. The number is
        // what the report needed and not what was read, so a reading that
        // takes it can tell a report that was cut from one that was short —
        // and this host says so rather than going looking for words it left
        // behind. The biggest report this host reads is 3925 bytes, which is
        // why what it reads into is more than that. See D634.
        for (uint32_t again = 0; again < KEST_MOST_UNREAD; again++) {
            asking[0].integer = 40;
            if (kest_call(filling, fills, asking, 4)) {
                fprintf(stderr, "a heap that was full filled an array\n");
                return 1;
            }
        }
        char little[64];
        size_t whole = what_was_said(filling, NULL, KEST_FORM_TEXT, little, sizeof(little));
        if (whole <= sizeof(little) || strlen(little) != sizeof(little) - 1) {
            fprintf(stderr, "a report of %zu bytes read into %zu of this "
                            "host's own left %zu\n",
                    whole, sizeof(little), strlen(little));
            return 1;
        }
        printf("a report of %zu bytes read into %zu says so and says how many "
               "there were\n", whole, sizeof(little));

        // And what the two forms of the same refusal cost. A refusal that
        // points is a message, an arrow, the line it happened on and a caret
        // under it: the drawing is most of what a report is in bytes, and it
        // costs no memory at all, because where a byte is in a file was worked
        // out when the file was read. The form written for a tool is bigger
        // than the one written for a person — names cost more than art — so a
        // host reporting every frame and counting bytes wants the words. See
        // D636.
        char shown[8192];
        char sent[8192];
        asking[0].integer = 40;
        if (kest_call(filling, fills, asking, 4)) {
            fprintf(stderr, "a heap that was full filled an array\n");
            return 1;
        }
        size_t in_words = what_was_said(filling, NULL, KEST_FORM_TEXT, shown,
                                        sizeof(shown));
        asking[0].integer = 40;
        if (kest_call(filling, fills, asking, 4)) {
            fprintf(stderr, "a heap that was full filled an array\n");
            return 1;
        }
        size_t in_json = what_was_said(filling, NULL, KEST_FORM_JSON, sent,
                                       sizeof(sent));
        const char *message = strstr(shown, "the program has used");
        size_t words_alone = message == NULL ? 0 : strcspn(message, "\n");
        if (words_alone == 0 || words_alone >= in_words ||
            in_json <= in_words || strstr(sent, "\"line\":") == NULL ||
            strstr(shown, "embed.kest:") == NULL) {
            fprintf(stderr, "a refusal is %zu bytes of words, %zu shown and "
                            "%zu sent\n", words_alone, in_words, in_json);
            return 1;
        }
        printf("a refusal is %zu bytes of words and %zu shown with the line it "
               "happened on, and %zu sent to a tool\n",
               words_alone, in_words, in_json);
        fclose(told);
        if (!kest_runtime_free(filling)) {
            fprintf(stderr, "the machine with no heap left was not freed\n");
            return 1;
        }
        printf("a hundred refused calls said %ld bytes of why and cost the "
               "machine nothing to say them\n", said_again - said_once);
    }

    // And the two answers that used to be one. A name the program has not got
    // and a build that did not compile are different things for a host to be
    // told: the first is a string of this host's own to fix, and the second is
    // a program's worth of diagnostics sitting in the report. A host told
    // `nothing was asked` for both has to guess which it is looking at, and
    // guessing is what this boundary is written not to make anybody do.
    // See D566.
    KestLimits nowhere = {0, 0, 0, 0};
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
    size_t build_before_machine = kest_build_cost(build);
    engine.runtime = kest_start(build, host, &limits);
    if (engine.runtime == NULL) {
        // Nothing started, so there is nothing to ask what went wrong: what a
        // host has then is the build, and it has been told.
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        kest_host_free(host);
        kest_build_free(build);
        return 1;
    }

    // What that machine cost the build, which is nothing at all. A machine is
    // the arena it is made of and everything in it, including what it says:
    // the report was one place on the build's arena until D1071, and one place
    // is one bump pointer that every thread starting a machine reads and
    // writes. The reference says a host may start a machine from any thread,
    // so what a start writes on a build is the count of how many are standing
    // on it and nothing else — and that is an atomic.
    //
    // Held as nothing rather than as less than a walk, which is what D607
    // asked for: less than a walk is satisfied by one byte, and one byte is a
    // race. The walk is still here because a machine has to be cheaper than
    // one, and now it is cheaper by all of it.
    size_t machine_on_build = kest_build_cost(build) - build_before_machine;
    if (one_walk == 0 || machine_on_build != 0 ||
        kest_runtime_cost(engine.runtime) >= one_walk) {
        fprintf(stderr, "a machine is %zu bytes and cost the build %zu, and a "
                        "walk of the program is %zu\n",
                kest_runtime_cost(engine.runtime), machine_on_build, one_walk);
        return 1;
    }
    printf("a machine is %zu bytes and cost this build nothing at all, against "
           "the %zu of the walk it was handed\n",
           kest_runtime_cost(engine.runtime), one_walk);

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
    static Decider apart = {-1, 2, true, true, false, false};
    KestHost *elsewhere = kest_host_new();
    if (elsewhere == NULL ||
        !kest_host_bind(elsewhere, "Io.write", io_write, stdout) ||
        !kest_host_bind(elsewhere, "Engine.decide", engine_decide, &apart) ||
        !kest_host_bind(elsewhere, "Engine.name", engine_name, &apart) ||
        !kest_host_bind(elsewhere, "Engine.rank", engine_rank, &apart) ||
        !kest_host_bind(elsewhere, "Engine.hurt", engine_hurt, NULL) ||
        !kest_host_bind(elsewhere, "Engine.blame", engine_blame, NULL) ||
        !kest_host_bind(elsewhere, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(elsewhere, "Engine.who", engine_who, NULL)) {
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


    // What this host looks up, and what it means to put in the frame before it
    // calls each of them and read back out of it afterwards: one kind a slot,
    // in the order `kest_frame_layout` lays the arguments out. Said here
    // because every call site below fills a frame by hand and a slot holds
    // whatever was written into it — `kest_call` sees how wide a frame is and
    // cannot see what a host meant to put in it, so a host that is wrong about
    // one argument of one name is wrong for the whole run and hears nothing.
    // `frame_adds_up` below holds the widths; this holds what is in them.
    // A struct is a slot a piece, which is why `lengthOf` is three and not
    // one, and a name that gives nothing back reads nought slots. See D701.
    static const struct {
        const char *name;
        uint8_t fills[8];
        uint32_t takes;
        uint8_t reads[4];
        uint32_t gives;
    } wanted[] = {
        {"create", {0}, 0, {KEST_L_WORD}, 1},
        {"spawn", {KEST_L_WORD, KEST_L_I32}, 2, {KEST_L_I32}, 1},
        {"step", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"onEvents", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"silence", {KEST_L_WORD, KEST_L_I32}, 2, {KEST_L_BOOL}, 1},
        // The tag, and what the case it names carries. A layout says
        // `KEST_L_PAYLOAD` for the slots after a tag because which type
        // is in one is the tag's to say, and this host says the same
        // back rather than picking one of the cases to be right about.
        {"damageOf", {KEST_L_TAG, KEST_L_PAYLOAD, KEST_L_PAYLOAD}, 3,
         {KEST_L_I32}, 1},
        {"hurtBy", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        // The one name here that answers a value with a tag in it: three
        // slots back rather than one, and the two after the tag are the
        // tag's to explain, the same as they are going the other way.
        {"worst", {KEST_L_WORD}, 1,
         {KEST_L_TAG, KEST_L_PAYLOAD, KEST_L_PAYLOAD}, 3},
        {"blamed", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        // The one that takes a tag which is not in the first slot of what
        // it takes — it is, here, because the `Event` is the first field of
        // the shape, and what makes it different is that the cases belong to
        // the field rather than to the argument.
        {"blamedBy", {KEST_L_I32, KEST_L_TAG, KEST_L_PAYLOAD, KEST_L_PAYLOAD},
         4, {KEST_L_I32}, 1},
        // A tag and a number, which is the pair that read alike until a tag
        // said what it was: an enum whose cases carry nothing is one slot of
        // four bytes, and so is an `i32`.
        {"footed", {KEST_L_TAG, KEST_L_I32}, 2, {KEST_L_I32}, 1},
        // A shape with a piece of text in it, handed over by value: two slots,
        // and the first is a word the machine has to own.
        {"greets", {KEST_L_TEXT, KEST_L_I32}, 2, {KEST_L_I32}, 1},
        {"whoIs", {0}, 0, {KEST_L_I32}, 1},
        // The other shape with a flag in it: a value, the byte that says
        // whether it is there, and a number. The flag is a byte the same as
        // a `bool` is, because that is what it is.
        {"marked", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"marking", {KEST_L_I32, KEST_L_HELD, KEST_L_I32}, 3, {KEST_L_I32}, 1},
        {"mark", {KEST_L_WORD, KEST_L_I32, KEST_L_I32}, 3, {0}, 0},
        {"unmark", {KEST_L_WORD, KEST_L_I32}, 2, {0}, 0},
        {"heaviest", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"lengthOf", {KEST_L_F32, KEST_L_F32, KEST_L_F32}, 3, {KEST_L_F32}, 1},
        {"between",
         {KEST_L_F32, KEST_L_F32, KEST_L_F32, KEST_L_F32, KEST_L_F32,
          KEST_L_F32},
         6,
         {KEST_L_F32},
         1},
        {"spread", {KEST_L_WORD}, 1, {KEST_L_F32}, 1},
        {"hoard", {0}, 0, {KEST_L_I32}, 1},
        {"pile", {0}, 0, {KEST_L_I32}, 1},
        {"churn", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"ready", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"filling", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"glued", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"joined", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"repeated", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"joinedPieces", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"readable", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"grew", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"popped", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"took", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"emptied", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"under", {0}, 0, {KEST_L_TEXT}, 1},
        {"named", {KEST_L_TEXT}, 1, {KEST_L_I32}, 1},
        {"atOnce", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"copied", {KEST_L_WORD}, 1, {KEST_L_TEXT}, 1},
        {"blank", {KEST_L_WORD, KEST_L_I32}, 2, {KEST_L_I32}, 1},
        {"first", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        // A place in a store, which is a number rather than a handle: what
        // comes back here is the slot and how many times it has been handed
        // out, packed into one. See D715.
        {"born", {KEST_L_WORD}, 1, {KEST_L_REF}, 1},
        {"healthOf", {KEST_L_WORD, KEST_L_REF}, 2, {KEST_L_I32}, 1},
        {"dropped", {KEST_L_WORD, KEST_L_REF}, 2, {KEST_L_I32}, 1},
        {"totalOf", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"answerInto", {KEST_L_WORD, KEST_L_I32, KEST_L_I32}, 3,
         {KEST_L_I32}, 1},
        {"sayInto", {KEST_L_WORD, KEST_L_I32, KEST_L_TEXT}, 3,
         {KEST_L_I32}, 1},
        {"worn", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"moved", {KEST_L_F32, KEST_L_F32, KEST_L_F32, KEST_L_F32}, 4,
         {KEST_L_F32, KEST_L_F32, KEST_L_F32}, 3},
        {"putRecord", {KEST_L_WORD, KEST_L_I32, KEST_L_I32}, 3, {0}, 0},
        {"ownArray", {0}, 0, {KEST_L_WORD}, 1},
        {"howManyOn", {KEST_L_WORD}, 1, {KEST_L_I32}, 1},
        {"reach", {KEST_L_F32, KEST_L_F32, KEST_L_F32, KEST_L_BOOL}, 4,
         {KEST_L_F32}, 1},
        {"heaviestCell", {KEST_L_WORD, KEST_L_I32}, 2,
         {KEST_L_I32, KEST_L_F32}, 2},
        {"asWritten", {0}, 0, {KEST_L_TEXT}, 1},
        {"ranked", {KEST_L_F32, KEST_L_F32, KEST_L_F32}, 3, {KEST_L_I32}, 1},
        // A function value is one slot holding which function it is, which is
        // a word like any other handle — and a word carries no promise, which
        // is the whole of why the machine asks the chunk. See D834.
        {"apply", {KEST_L_FN, KEST_L_I32}, 2, {KEST_L_I32}, 1},
        {"doubled", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"grows", {KEST_L_I32}, 1, {KEST_L_I32}, 1},
        {"weighed", {0}, 0, {KEST_L_I32}, 1}};
    _Static_assert(sizeof(wanted) / sizeof(wanted[0]) == ENTRIES,
                   "every name this host asks for has somewhere to be put");
    // And what walking the names costs a host in news, which is nothing. The
    // walk below asks for a second function of every name it looks up, and
    // most names have one — so a machine that said something at the end of
    // that walk would hand a host one complaint per name it ever looked up,
    // every one of them about a question the host was right to ask. The walk
    // ends in silence; asking for a name that is not there at all does not,
    // and that half is held further down. See D584.
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
        if (kest_entry_of(engine.runtime, wanted[i].name, 1) >= 0) {
            engine.entry[i] = -1;
            for (uint32_t at = 0; engine.entry[i] < 0; at++) {
                int32_t candidate = kest_entry_of(engine.runtime, wanted[i].name, at);
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
            engine.entry[i] = kest_entry(engine.runtime, wanted[i].name);
        }
        // And what the walk said, which is nothing, whatever it found. A host
        // walks this for every name it looks up and most names are one
        // function, so a machine that explained at the end of the walk would
        // hand a host one complaint per name at the start of every run, about
        // a question it was right to ask. Read here rather than after the loop
        // because the question below is one that does speak, and two doors
        // read together cannot say which of them spoke. See D584.
        if (!said_nothing(engine.runtime, "a walk of the names ended")) {
            return 1;
        }
        // And what asking for the name itself says, which is the thing this
        // walk exists to avoid: a name that is several functions has no one
        // index, and a host that asks anyway is told so rather than given the
        // first of them. Asked here because this is where a host meets it.
        // See D421.
        if (kest_entry_of(engine.runtime, wanted[i].name, 1) >= 0) {
            if (kest_entry(engine.runtime, wanted[i].name) >= 0) {
                fprintf(stderr, "`%s` is several functions and one index came "
                                "back for it\n",
                        wanted[i].name);
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
                    wanted[i].name, sizeof(engine.frame) / sizeof(engine.frame[0]));
            return 1;
        }
        // And what the frame is, asked here for the same reason the name is:
        // once, before anything runs. A host that finds out at the first call
        // that a width and a run of layouts disagree finds out inside a frame.
        if (!frame_adds_up(engine.runtime, engine.entry[i], wanted[i].name)) {
            return 1;
        }
        // And what this host is about to put in it, said to the program before
        // any of it is written. The width above says how many slots there are
        // and nothing says what belongs in one, so a host that writes
        // `integer` where the program reads `real` writes a number nobody can
        // read and is told at no point. Both directions here, because reading
        // a result back out of the same slots is the same mistake the other
        // way round.
        if (!kest_frame_fills(engine.runtime, engine.entry[i],
                              wanted[i].fills, wanted[i].takes) ||
            !kest_frame_reads(engine.runtime, engine.entry[i],
                              wanted[i].reads, wanted[i].gives)) {
            fprintf(stderr, "this host is wrong about what `%s` crosses with\n",
                    wanted[i].name);
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    // And what all of that left behind, which is nothing: every name looked
    // up, every walk ended, every one that is several functions read back.
    if (!said_nothing(engine.runtime, "every name was looked up")) {
        return 1;
    }
    if (!asks(&engine, CREATE)) {
        return 1;
    }
    engine.world = engine.frame[0];
    // Said once, because this host keeps the world in its own memory and does
    // not hand it into every call it makes. The machine gives back what the
    // program can no longer reach, and it cannot read this host's variables:
    // without this the world would be memory nothing names the first time a
    // call allocates enough to set off a walk. See D996.
    if (!kest_keeps(engine.runtime, engine.world)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }

    // What a frame costs, which is the heap on either side of it. The running
    // total is a number without a scale — every host that watches a frame
    // budget wants the difference, and the difference is a subtraction this
    // host does rather than a thing it is given.
    for (int i = 0; i < 5; i++) {
        engine.frame[0] = engine.world;
        engine.frame[1].integer = i + 1;
        size_t spent = kest_heap_taken(engine.runtime);
        if (!asks(&engine, SPAWN)) {
            return 1;
        }
        printf("frame %d: spawned, %lld alive, %zu bytes this frame\n", i,
               (long long)engine.frame[0].integer, kest_heap_taken(engine.runtime) - spent);
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
        size_t spent = kest_heap_taken(engine.runtime);
        if (!asks(&engine, STEP)) {
            return 1;
        }
        printf("frame %d: stepped, %lld alive, %zu bytes this frame, %s\n",
               i + 5, (long long)engine.frame[0].integer,
               kest_heap_taken(engine.runtime) - spent,
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

    // And the same question answered with this host's own bytes rather than
    // with text the machine made. It is the one crossing where nothing said
    // so: the door reads a piece of text a host hands in and reads nothing in
    // what it hands back — and what a program is given back it may keep, so a
    // host's string outlives the call it came from only for as long as the
    // host says, which is a thing the program cannot ask about. Asked for on
    // purpose, because a literal would have worked and a stack buffer would
    // not. See D717.
    decider.answers_as_the_machine = false;
    if (asks(&engine, UNDER)) {
        fprintf(stderr, "this host's own bytes were kept as the machine's\n");
        return 1;
    }
    decider.answers_as_the_machine = true;
    if (!said_that(engine.runtime, "K0652",
                   "answers with text in slot 0 that did not come from this "
                   "machine")) {
        return 1;
    }
    // And the machine runs on, so the next answer is read the same as the one
    // before the refusal.
    if (!asks(&engine, UNDER) ||
        kest_gave_text(engine.runtime, engine.entry[UNDER], engine.frame, about,
                       sizeof(about)) < 0) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("and a host's own bytes handed back as the machine's were refused: "
           "%s\n",
           about);


    // And the same mistake one field in, which is where the answer was read no
    // further than the top of: a crossing that answers with a shape writes a
    // name and a number, and the name is a word the machine has to own. The
    // walk that reads what a host hands in reads what it hands back now, so
    // the field is read where the whole value would be. See D719.
    if (!asks(&engine, WHO_IS) || engine.frame[0].integer != 20) {
        fprintf(stderr, "a shape answered with came back as %lld\n",
                (long long)engine.frame[0].integer);
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    decider.answers_as_the_machine = false;
    if (asks(&engine, WHO_IS)) {
        fprintf(stderr, "a name inside a shape answered with was kept\n");
        return 1;
    }
    decider.answers_as_the_machine = true;
    if (!said_that(engine.runtime, "K0652",
                   "answers with text in slot 0 that did not come from this "
                   "machine")) {
        return 1;
    }
    printf("a name inside a shape answered with is read where a name on its "
           "own is\n");
    kest_runtime_free(apart_at);
    // And the same crossing answering a number too wide for the field it goes
    // in. `Npc` is a piece of text and an `i32`, so what comes back is walked
    // — and until D837 the walk read the text and stepped over the number,
    // which is the slot the host actually wrote. See D837.
    decider.answers_too_wide = true;
    if (asks(&engine, WHO_IS)) {
        fprintf(stderr, "a number too wide for its field was answered with\n");
        return 1;
    }
    decider.answers_too_wide = false;
    if (!said_that(engine.runtime, "K0652", "`i32` in slot 2")) {
        return 1;
    }
    printf("and a crossing that answered a number too wide for the field it "
           "goes in\n");

    // And the same at the width a slot cannot say: a slot holds a double and
    // an `f32` holds less, so a host answering `0.1` answers a number the
    // program's own `f32` literals do not equal. It is the one of these three
    // that a program can tell without counting — it compares and finds them
    // apart. See D838.
    decider.answers_too_wide = true;
    if (asks(&engine, WEIGHED)) {
        fprintf(stderr, "a number no `f32` holds was answered with\n");
        return 1;
    }
    decider.answers_too_wide = false;
    if (!said_that(engine.runtime, "K0652", "`f32` in slot 0")) {
        return 1;
    }
    if (!asks(&engine, WEIGHED) || engine.frame[0].integer != 1) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "a number an `f32` holds was refused\n");
        return 1;
    }
    printf("and one no `f32` holds, where the one that fits weighed %lld\n",
           (long long)engine.frame[0].integer);

    // And a store is a thing the language has no text for, which it says
    // rather than inventing one. What the host wants of a store, only the host
    // knows. Since D876 this is the shape that reads that message — a struct
    // writes itself now — so the message is read here rather than only the
    // answer: minus one on its own is what a host cannot tell apart from an
    // index that is no function.
    if (kest_gave_text(engine.runtime, engine.entry[CREATE], engine.frame, said, sizeof(said))
        >= 0) {
        fprintf(stderr, "a store has no text and something wrote one\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0646", "no text of its own")) {
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
    // And fewer slots than the arguments take, which is a host saying what
    // some of them hold and nothing about the rest. Saying part of it is not
    // checking it, so it is refused rather than taken for what it covers —
    // the slots nobody spoke for are exactly the ones a host is wrong about.
    // See D831.
    if (kest_frame_fills(engine.runtime, engine.entry[LENGTH_OF], writing, 2)) {
        fprintf(stderr, "a frame said to be two slots wide where it is three "
                        "was agreed to\n");
        return 1;
    }
    if (!said_under(engine.runtime, "K0634", "kest_frame_layout")) {
        return 1;
    }
    // And the sharpest shape of the same mistake: a function that takes
    // nothing and gives one slot. A host that says one is judged against
    // nought — and until D832 was sent to `kest_frame_slots`, which answers
    // the wider of the two and so answers the number it just used. What it is
    // sent to now is the door that gave the number it was judged by.
    const uint8_t one_slot[1] = {KEST_L_I32};
    if (kest_frame_fills(engine.runtime, engine.entry[HOARD], one_slot, 1)) {
        fprintf(stderr, "a function that takes nothing took a slot\n");
        return 1;
    }
    if (!said_under(engine.runtime, "K0634", "kest_frame_layout") ||
        kest_frame_slots(engine.runtime, engine.entry[HOARD]) != 1) {
        return 1;
    }
    printf("a frame said to hold what it does not was refused, and one that "
           "spoke for two of its three slots, and one that spoke for a slot "
           "of a function that takes none\n");

    // And a number no `f32` holds, written into a frame rather than answered
    // with. `ranked` takes three of them, and this is the path that does not
    // walk the type: three pieces, all numbers, read where the walk would
    // have been skipped. See D838.
    engine.frame[0].real = 0.1;
    engine.frame[1].real = (double)(float)1.0;
    engine.frame[2].real = (double)(float)2.0;
    if (kest_call(engine.runtime, engine.entry[RANKED], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a number no `f32` holds was written into a frame\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636", "`f32` in slot 0")) {
        return 1;
    }
    printf("and a number no `f32` holds, written into a frame\n");

    // And a truth that is neither. `reach` takes three floats and a `bool`,
    // and a slot holds sixty-four bits where a truth holds one of two: a host
    // writing 7 wrote a value the program reads as true where it asks `if`
    // and as neither where it asks `== true`. One slot, two answers. See D839.
    engine.frame[0].real = (double)(float)1.0;
    engine.frame[1].real = (double)(float)1.0;
    engine.frame[2].real = (double)(float)1.0;
    engine.frame[3].integer = 7;
    if (kest_call(engine.runtime, engine.entry[REACH], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a truth that is neither was taken\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636", "`bool` in slot 3")) {
        return 1;
    }
    printf("and a truth that is neither of the two\n");
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
    // And the same name again. What a host cannot call is a statement about
    // the program rather than something that happened, and the program does
    // not change while a machine runs, so it is said once. Asking a thousand
    // times cost 633940 bytes before it was — 634 an asking, none of it ever
    // handed back — which is what a host polling for a name it might have was
    // paying every frame. See D608.
    size_t after_saying = kest_build_cost(build);
    if (kest_entry(engine.runtime, "Io.write") >= 0 ||
        kest_build_cost(build) != after_saying ||
        !said_nothing(engine.runtime, "the same name asked for twice")) {
        fprintf(stderr, "asking again for a name the program asks the host "
                        "for cost %zu bytes\n",
                kest_build_cost(build) - after_saying);
        return 1;
    }
    printf("a name the program asks the host for is said once, and asking "
           "again costs nothing\n");
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
    // And more slots than it gives, which is the other way to be wrong about
    // a width: a host that reads two where one came back reads the slot above
    // the answer, which is the machine's and not the answer's. The width and
    // what is in it are two questions and this door asks both. See D831.
    const uint8_t read_wide[2] = {KEST_L_F32, KEST_L_F32};
    if (kest_frame_reads(engine.runtime, engine.entry[LENGTH_OF], read_wide,
                         2)) {
        fprintf(stderr, "a result one slot wide was read as two\n");
        return 1;
    }
    if (!said_under(engine.runtime, "K0634", "kest_frame_gives")) {
        return 1;
    }
    printf("a result said to hold what it does not was refused, three "
           "times\n");

    // A promise through the one call neither proof can follow. `apply` takes
    // a value that promises `no.alloc`, and the program may only hand it one
    // that does — the type carries the promise. A host hands it a number, and
    // a number carries nothing: what this host writes into that slot is an
    // index it read from `kest_entry`, and the machine is the only thing left
    // that can ask the chunk whether it promised. `doubled` did and `grows`
    // did not. See D834.
    engine.frame[0].integer = engine.entry[DOUBLED];
    engine.frame[1].integer = 5;
    if (!kest_call(engine.runtime, engine.entry[APPLY], engine.frame, 2) ||
        engine.frame[0].integer != 10) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        fprintf(stderr, "a function value that promises was refused\n");
        return 1;
    }
    engine.frame[0].integer = engine.entry[GROWS];
    engine.frame[1].integer = 5;
    if (kest_call(engine.runtime, engine.entry[APPLY], engine.frame, 2)) {
        fprintf(stderr, "a host handed in a function that allocates and the "
                        "program ran it under a promise\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0623", "no.alloc")) {
        return 1;
    }
    // And one of the right kind and the wrong shape. A function value is one
    // slot holding a number, so every index is the same kind of thing to a
    // frame: `kest_frame_fills` says the slot is a word and a word is what a
    // handle is too. What tells `ranked` from `doubled` is what they take and
    // give, and a host that read the wrong index hands over a number that is
    // in range, promises nothing it should not, and enters a body expecting a
    // frame three slots wide. Until D835 that walked off the caller's frame.
    engine.frame[0].integer = engine.entry[RANKED];
    engine.frame[1].integer = 5;
    if (kest_call(engine.runtime, engine.entry[APPLY], engine.frame, 2)) {
        fprintf(stderr, "a function of another shape was entered\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0657", "takes 3 and gives 1")) {
        return 1;
    }
    printf("a function value this host handed in was asked whether it "
           "promised, and the one that did not was refused, and so was one "
           "of another shape\n");

    // And a number too wide for the slot it was written into. Every other
    // thing a frame holds is something this machine made and says what it is;
    // a number is what the host put there, and a slot is sixty-four bits
    // where an `i32` is thirty-two. Writing one that does not fit is the one
    // way a value this language cannot make gets into a program — it wraps at
    // its own end everywhere else — and until D836 the program counted with
    // it. `blamed` takes an `i32`.
    engine.frame[0].integer = (int64_t)1 << 40;
    if (kest_call(engine.runtime, engine.entry[BLAMED], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a number too wide for its slot was taken\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636", "is not one")) {
        return 1;
    }
    printf("and a number wider than the slot it was written into\n");

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
    KestValue no_bytes[2] = {{0}, {0}};
    if (kest_text(engine.runtime, NULL, 4, no_bytes) ||
        no_bytes[0].text == NULL || no_bytes[0].text[0] != '\0' ||
        no_bytes[1].integer != 0) {
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
        // And asking again, which is the same statement about the same
        // program: how many functions there are does not change while a
        // machine runs. A host that asks this instead of `kest_entry_name`
        // was paying 648 bytes an asking for the answer it could have had for
        // nothing — and the walk of what a program defines is what says so
        // without a word. See D615.
        size_t told_once = kest_build_cost(build);
        for (uint32_t again = 0; again < 100; again++) {
            if (kest_frame_takes(engine.runtime, nobody) != 0 ||
                kest_entry_name(engine.runtime, nobody) != NULL) {
                fprintf(stderr, "there is a frame at %d after all\n", nobody);
                return 1;
            }
        }
        if (kest_build_cost(build) != told_once ||
            !said_nothing(engine.runtime, "an index that is no function asked "
                                          "about a hundred times")) {
            fprintf(stderr, "asking again about an index that is no function "
                            "cost %zu bytes\n",
                    kest_build_cost(build) - told_once);
            return 1;
        }
        // And the other way of asking for nothing: a host that says how many
        // slots it is about to describe and hands nothing to read them from.
        // The index is a function here, so what is wrong is the pair, and
        // this was answered with a sentence about the index. See D615.
        if (kest_frame_fills(engine.runtime, engine.entry[LENGTH_OF], NULL,
                             3) ||
            !said_that(engine.runtime, "K0634", "handed nothing to read them "
                                                "from")) {
            fprintf(stderr, "a host that described three slots and handed "
                            "none was not told what was wrong\n");
            return 1;
        }
        printf("four questions about a frame that is not there were refused "
               "once, and slots said to be somewhere they are not\n");

        // And a refusal deeper than a message holds. The places one shows is
        // the other ceiling a host meets (D619), and nothing a host does can
        // make a machine show more of them: this program has a run of calls
        // ten deep that ends in arithmetic, and what comes back is what a
        // diagnostic holds and a count of the rest. Held here because the
        // command line was the only thing that had ever seen it. See D620.
        int32_t deepest = kest_entry(engine.runtime, "tickWorld");
        KestValue sharing[2] = {{0}};
        if (deepest < 0 || kest_call(engine.runtime, deepest, sharing, 2)) {
            fprintf(stderr, "a run of calls ten deep divided by nothing and "
                            "answered\n");
            return 1;
        }
        uint32_t places = places_said(engine.runtime, "more under it");
        if (places != KEST_MOST_PLACES + 1) {
            fprintf(stderr, "a refusal ten calls in showed %u places and a "
                            "message holds %u\n", places, KEST_MOST_PLACES);
            return 1;
        }
        printf("a refusal ten calls in showed %u places, which is what a "
               "message holds, and counted the rest\n", places);
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
        // And asking for it again, which is the same statement about the same
        // program: said once, like the name the program asks the host for.
        // A host that looks for an optional entry every frame is asking a
        // question, not making something happen. See D608.
        size_t named_once = kest_build_cost(build);
        if (kest_entry(engine.runtime, "pick") >= 0 ||
            kest_build_cost(build) != named_once ||
            !said_nothing(engine.runtime, "a generic name asked for twice")) {
            fprintf(stderr, "asking again for a name that is several "
                            "functions cost %zu bytes\n",
                    kest_build_cost(build) - named_once);
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
        // And what walking off the end of them said, which is nothing. Asking
        // for the name itself is refused and names the copies — that is the
        // door above and it speaks — and the walk that takes them one at a
        // time ends by being handed nothing. A machine that explained at the
        // end of a walk would answer every host that ever read a list with a
        // complaint about the reading. See D584.
        if (!said_nothing(engine.runtime, "a walk of the copies ended")) {
            return 1;
        }
        // And the list itself, which is what a host embedding a program it did
        // not write has instead of names to guess. Walking it from nought ends
        // where the program's functions do, and every index it hands over is
        // one `kest_entry` answers with when it is handed the name back. The
        // copies of `pick` are in it under the names the refusal above spelled
        // out, which is how a host finds a generic without knowing there is
        // one. See D609.
        uint32_t defined = 0;
        uint32_t picks = 0;
        for (int32_t at = 0;; at++) {
            const char *what = kest_entry_name(engine.runtime, at);
            if (what == NULL) {
                break;
            }
            defined++;
            if (strncmp(what, "examples.embed.pick#", 20) == 0) {
                picks++;
            }
            if (kest_entry(engine.runtime, what) != at) {
                fprintf(stderr, "`%s` is at %d and asking for it gave %d\n",
                        what, at, kest_entry(engine.runtime, what));
                return 1;
            }
        }
        if (defined < copies || picks != copies ||
            kest_entry_name(engine.runtime, -1) != NULL ||
            !said_nothing(engine.runtime, "a walk of what a program defines "
                                          "ended")) {
            fprintf(stderr, "a program of %u functions has %u copies of "
                            "`pick` in it\n", defined, picks);
            return 1;
        }
        // And what a host can act on before it calls anything: which of them
        // promised to reach no heap. An engine deciding where a function goes
        // — a frame step, a loading screen, nowhere at all — asks this and
        // nothing else about what a function costs, because this is the one
        // answer it can do something about. The program has both kinds, so a
        // host that read the same answer for all of them would be a host
        // reading a constant. See D680.
        uint32_t promised = 0;
        uint32_t said_nothing_about_it = 0;
        // And the other promise beside it, which is the one a host driving a
        // frame from inside its own lock reads: a step that may call back in
        // is one that reaches this host while this host is in the middle of
        // something. Two questions of one door, because a promise is a thing
        // to name rather than a door to add. See D857.
        uint32_t quiet = 0;
        for (int32_t at = 0;; at++) {
            if (kest_entry_name(engine.runtime, at) == NULL) {
                break;
            }
            if (kest_entry_promises(engine.runtime, at,
                                    KEST_PROMISE_NO_ALLOC)) {
                promised++;
            } else {
                said_nothing_about_it++;
            }
            if (kest_entry_promises(engine.runtime, at,
                                    KEST_PROMISE_NO_HOST)) {
                quiet++;
            }
        }
        if (promised == 0 || said_nothing_about_it == 0 ||
            kest_entry_promises(engine.runtime, -1, KEST_PROMISE_NO_ALLOC) ||
            kest_entry_promises(engine.runtime, (int32_t)defined,
                                KEST_PROMISE_NO_ALLOC)) {
            fprintf(stderr, "%u function(s) promised `no.alloc` and %u did "
                            "not\n", promised, said_nothing_about_it);
            return 1;
        }
        // The second promise is asked of the same functions and answers for
        // fewer of them, because this program calls the host: a walk where
        // every function answered the same to both would be a walk that asked
        // one question twice.
        if (quiet == 0 || quiet >= promised ||
            kest_entry_promises(engine.runtime, -1, KEST_PROMISE_NO_HOST)) {
            fprintf(stderr, "%u function(s) promised `no.host` against %u that "
                            "promised `no.alloc`\n", quiet, promised);
            return 1;
        }
        printf("%u of %u function(s) promised to reach no heap, and %u to "
               "call nothing of this host's\n", promised, defined, quiet);

        // And the same walk read the way every message about a function
        // spells it. What a host reads in a refusal and what it reads in the
        // list were two spellings of one function with nothing tying them
        // together; both copies of `pick` are written `embed.pick`, which is
        // what says they are one function compiled twice. Walking it twice
        // costs nothing, because the spelling is worked out when the function
        // is compiled and not when it is asked for. See D610.
        size_t before_walking = kest_build_cost(build);
        size_t machine_before_walking = kest_runtime_cost(engine.runtime);
        uint32_t written = 0;
        for (int32_t at = 0;; at++) {
            const char *what = kest_entry_name(engine.runtime, at);
            const char *wrote = kest_entry_wrote(engine.runtime, at);
            if (what == NULL || wrote == NULL) {
                break;
            }
            if (strchr(wrote, '#') != NULL ||
                strncmp(what, wrote, strlen(wrote)) != 0) {
                fprintf(stderr, "`%s` is written `%s`\n", what, wrote);
                return 1;
            }
            if (strcmp(wrote, "examples.embed.pick") == 0) {
                written++;
            }
        }
        if (written != picks || kest_entry_wrote(engine.runtime, -1) != NULL ||
            kest_build_cost(build) != before_walking ||
            kest_runtime_cost(engine.runtime) != machine_before_walking) {
            fprintf(stderr, "%u of %u copies of `pick` are written the same, "
                            "and walking the names cost %zu bytes of build "
                            "and %zu of machine\n",
                    written, picks, kest_build_cost(build) - before_walking,
                    kest_runtime_cost(engine.runtime) -
                        machine_before_walking);
            return 1;
        }
        // And the end of the same walk on a machine that has been told
        // nothing. What a machine says about an index that is no function it
        // says once (D615), so a machine that has already said it is quiet
        // for a reason of its own — and what holds the walk's silence is a
        // machine that could have spoken. See D584 and D615.
        for (int32_t at = 0; kest_entry_name(other, at) != NULL; at++) {
            continue;
        }
        if (!said_nothing(other, "a walk of what a program defines on a "
                                 "machine that was not told ended")) {
            return 1;
        }
        printf("host walked %u functions the program defines and found the "
               "%u copies of `pick` among them, both written `embed.pick`\n",
               defined, picks);

        // And the same walk on a machine that has never been told what `pick`
        // is. What makes the silence at the end of a walk a decision is that
        // the machine could have spoken: this one has said nothing about the
        // name yet, so it is quiet because the walk ends quietly and not
        // because it has said its piece already. See D584 and D608.
        for (uint32_t at = 0;; at++) {
            if (kest_entry_of(other, "pick", at) < 0) {
                break;
            }
        }
        if (!said_nothing(other, "a walk of the copies on a machine that was "
                                 "not told ended")) {
            return 1;
        }
        // And where the functions of a name are written, which the refusal
        // says now. The two copies of `pick` are one declaration compiled
        // twice, so it names one place; `lengthOf` is two functions of a name
        // and it names both. Asked of the machine that has not been told,
        // because each of these is said once. See D613.
        if (kest_entry(other, "pick") >= 0 ||
            !said_in_both(other, "embed.kest:188", "ask for one of them") ||
            kest_entry(other, "lengthOf") >= 0 ||
            !said_in_both(other, "embed.kest:149", "embed.kest:168")) {
            fprintf(stderr, "a name that is several functions said where none "
                            "of them is\n");
            return 1;
        }
        printf("a name that is two functions says where both are written, and "
               "one compiled twice says where it is\n");
        // And what a host is told for an index past the last function, which
        // is the number the walk above counted: a walk of what a program asks
        // the host for says how many there are when it is read past the end,
        // and this is the same walk of what a program defines. A host that
        // walked one and handed the next index on reads its own mistake
        // rather than a refusal with nothing in it. See D614.
        char how_many[64];
        snprintf(how_many, sizeof(how_many), "defines %u function", defined);
        if (kest_frame_takes(other, (int32_t)defined) != 0 ||
            !said_in_both(other, how_many, "there is nothing at")) {
            fprintf(stderr, "an index past the last function said nothing "
                            "about how many there are\n");
            return 1;
        }
        printf("and a %u function program says so for the index after the "
               "last\n", defined);
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
        // And the same result asked for as words. A shape had none of its own
        // until D876 and this host wrote it: asking was minus one and a
        // message naming the type. It is written now, the way a program writes
        // one — the name, and the fields in the order they were declared —
        // and a host that wants its own layout still walks it with
        // `kest_frame_gives`, which is the paragraph below. What has no text
        // is what holds a handle, and a store above is asked exactly that.
        char said[64];
        int64_t as_words = kest_gave_text(engine.runtime, engine.entry[MOVED],
                                          engine.frame, said, sizeof(said));
        if (as_words < 0) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            fprintf(stderr, "a shape has words now and this had none\n");
            return 1;
        }
        if (strcmp(said, "Point([1.75, 2.75, 3.75])") != 0) {
            fprintf(stderr, "a `Point` wrote itself as `%s`\n", said);
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

    // And the same shape going the other way: the program hands this host a
    // `Point` and reads back what the host made of it. What says the three
    // slots are the three this host thinks they are is the layout, compared
    // with this host's own pieces before anything was bound. See D699.
    for (uint32_t k = 0; k < 3; k++) {
        engine.frame[k].real = (double)(k + 1);
    }
    if (!asks(&engine, RANKED)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (engine.frame[0].integer != 6) {
        fprintf(stderr, "a point of 1, 2 and 3 was ranked %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    printf("a shape handed to this host came back ranked %lld\n",
           (long long)engine.frame[0].integer);

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
    // written: three `f32` and a `bool`, which a layout calls a truth in a
    // byte — the byte is what it is where memory is shared and the truth is
    // what it means. See D839.
    const uint8_t reaching[4] = {KEST_L_F32, KEST_L_F32, KEST_L_F32,
                                 KEST_L_BOOL};
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
    // And what the machine has to say about a frame that worked, which is
    // nothing. Every check here reads what was said when something was
    // refused; this is the other reading, and it is the one that holds every
    // path that works — a lend made, a call in, a lend ended, a heap thrown
    // away — to leaving nothing behind for the next frame to find. See D583.
    if (!said_nothing(engine.runtime, "a lend was made and read")) {
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
        // And the same body handed a row that is not there. `heaviestCell`
        // promises `no.alloc`, and the reference says the promise is about
        // the Kest program heap and that the machinery which writes a
        // diagnostic is outside it. That is read here rather than from the
        // source: what the program is holding and what it has ever asked for,
        // either side of a refusal raised inside the body that promised.
        // Writing K0604 reads the file again, works out which line asked and
        // draws a caret under it, and none of that may land here. See D1132.
        const size_t held_before = kest_heap_used(engine.runtime);
        const size_t asked_before = kest_heap_taken(engine.runtime);
        engine.frame[0] = again;
        engine.frame[row_at].integer = 99;
        if (kest_call(engine.runtime, engine.entry[HEAVIEST_CELL],
                      engine.frame,
                      sizeof(engine.frame) / sizeof(engine.frame[0]))) {
            fprintf(stderr, "`heaviestCell` answered about a row that is not "
                            "there\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0604", "outside an array")) {
            return 1;
        }
        const size_t held_after = kest_heap_used(engine.runtime);
        const size_t asked_after = kest_heap_taken(engine.runtime);
        if (held_after != held_before || asked_after != asked_before) {
            fprintf(stderr, "a refusal inside a `no.alloc` body moved the "
                            "program heap: holding %zu then %zu, ever asked "
                            "%zu then %zu\n",
                    held_before, held_after, asked_before, asked_after);
            return 1;
        }
        if (!kest_lend_ends(engine.runtime, again)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        printf("host read a %u slot result of two kinds through what the "
               "program says they are: %d weighing %g\n", cell->count,
               (int)answered[0], answered[1]);
        printf("and a refusal inside a body promising `no.alloc` left the "
               "program heap at %zu bytes, %zu ever asked for\n",
               held_after, asked_after);
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
    size_t held = kest_heap_taken(engine.runtime);
    for (int frame = 0; frame < 1000; frame++) {
        KestValue each =
            kest_borrow(engine.runtime, rows, 2, "Row", sizeof(Row));
        if (each.object == NULL || !kest_lend_ends(engine.runtime, each)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    if (kest_heap_taken(engine.runtime) != held) {
        fprintf(stderr, "a thousand frames of lending grew the heap by %zu\n",
                kest_heap_taken(engine.runtime) - held);
        return 1;
    }
    if (!said_nothing(engine.runtime, "a thousand lends were taken back")) {
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
    if (!said_that(engine.runtime, "K0637", "taken this lend back")) {
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
    if (!said_that(engine.runtime, "K0637", "taken this lend back")) {
        return 1;
    }
    printf("and the tail of it went with it\n");

    // And more of them than there are, which is the mistake this whole
    // crossing is shaped around: the count is the host's word, and a program
    // given a longer one walks off the end of somebody else's memory. Nothing
    // in a build that ships can weigh that word — the block is the host's and
    // its end is written down nowhere the library can read — so it is asked
    // where it can be asked, and this host asks it there.
    if (kest_checked()) {
        if (kest_borrow(engine.runtime, rows, 4, "Row", sizeof(Row)).object !=
            NULL) {
            fprintf(stderr, "a lend of four out of two was taken\n");
            return 1;
        }
        printf("a lend of %zu rows out of %zu was refused\n", (size_t)4,
               sizeof(rows) / sizeof(rows[0]));
    }


    // Text is the other thing a host hands over, and the machine copies it:
    // what a program holds it must own. So a host that hands the same name
    // every frame keeps what it was given rather than saying it again — this
    // one asks for the same bytes twice and gets the same text back, which is
    // what makes a name a host says once cost once.
    // Bytes or a handle, which a layout could not say until D896: `text`,
    // `[u8]` and `store<T>` were one kind, and the answer that came with it
    // sent a host to the declaration for the one thing a layout is for.
    // Reading a store's handle through `text` is a walk to a nought byte over
    // the machine's own memory; reading text through `object` and handing it
    // back is the same the other way. They are two kinds now, and the two
    // members are two answers.
    {
        int32_t says = kest_entry(engine.runtime, "under");
        int32_t lends = kest_entry(engine.runtime, "ownArray");
        const KestLayout *of_text = kest_frame_gives(engine.runtime, says);
        const KestLayout *of_handle = kest_frame_gives(engine.runtime, lends);
        if (says < 0 || lends < 0 || of_text == NULL || of_handle == NULL ||
            of_text->count != 1 || of_handle->count != 1 ||
            of_text->pieces[0].kind == of_handle->pieces[0].kind ||
            kest_slot_of(of_text->pieces[0].kind) != KEST_S_TEXT ||
            kest_slot_of(of_handle->pieces[0].kind) != KEST_S_WORD ||
            kest_slot_of(KEST_L_REF) != kest_slot_of(KEST_L_I64)) {
            fprintf(stderr, "a layout says the same of bytes and of a handle\n");
            return 1;
        }
    }

    KestValue name[2] = {{0}, {0}};
    kest_text(engine.runtime, "the engine", 10, name);
    size_t paid = kest_heap_taken(engine.runtime);
    if (name[0].text == NULL || strcmp(name[0].text, "the engine") != 0 ||
        name[1].integer != 10) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // And bytes with a nought among them, which a host may hand over as text:
    // a nought is a character and text carries how long it is, so five bytes
    // in are five bytes out with the nought still the fourth of them. What is
    // refused is a byte that begins no character, because text is UTF-8 and
    // this is the door it arrives through. See D971.
    const char cut[6] = {'h', 'a', 'l', 0, 'f', 0};
    KestValue halved[2] = {{0}, {0}};
    kest_text(engine.runtime, cut, 5, halved);
    if (halved[0].text == NULL || halved[1].integer != 5 ||
        memcmp(halved[0].text, cut, 5) != 0) {
        fprintf(stderr, "bytes with a nought among them were cut short\n");
        return 1;
    }
    printf("and took %zu bytes with a nought among them\n", sizeof(cut) - 1);

    // Written as bytes rather than as characters, because a byte that begins
    // no character is not one and a compiler is right to say so about a `char`
    // that will not hold it.
    const unsigned char broken[3] = {'h', 0xff, 'i'};
    KestValue refused[2] = {{0}, {0}};
    kest_text(engine.runtime, (const char *)broken, 3, refused);
    // What comes back for a refusal is an empty piece of text, which is what
    // the door writes before it asks anything. See D436.
    if (refused[0].text == NULL || refused[1].integer != 0) {
        fprintf(stderr, "a byte that begins no character was taken as text\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0611", "begins no character")) {
        return 1;
    }
    printf("and refused a byte that begins no character\n");

    KestValue again[2] = {{0}, {0}};
    kest_text(engine.runtime, "the engine", 10, again);
    if (again[0].text == NULL || kest_heap_taken(engine.runtime) == paid) {
        fprintf(stderr, "saying the same bytes twice cost nothing\n");
        return 1;
    }
    {
        uint32_t wide = 0;
        kest_text_bytes(name, &wide);
        printf("host said %u bytes of text and paying twice cost %zu more\n",
               wide, kest_heap_taken(engine.runtime) - paid);
    }

    // And what a host must not hand over: bytes of its own, which the program
    // would hold for as long as it liked while this host got on with its life.
    // Nothing about the pointer says where it came from, so what says it is
    // the machine asking whether it gave that address out.
    engine.frame[0] = name[0];
    engine.frame[1] = name[1];
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
    // And no address at all, which is what a host that zeroed a frame and
    // called anyway hands over. Text in this language is never nothing, so a
    // slot with no address in it is a frame nobody filled — and the program
    // reads it at the first thing it does with it. See D629.
    engine.frame[0].text = NULL;
    if (kest_call(engine.runtime, engine.entry[NAMED], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a frame nobody filled was taken as text\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636", "handed no address")) {
        return 1;
    }
    // And the same for a handle. The machine reads the four bytes at the front
    // of one at the instruction that uses it, so a slot of noughts used to be
    // `K0612` where the program stands — which points at the program for
    // something this host did. Said at the door it names the slot. See D630.
    engine.frame[0].object = NULL;
    if (kest_call(engine.runtime, engine.entry[HEAVIEST], engine.frame,
                  sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        fprintf(stderr, "a frame nobody filled was taken as a handle\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636", "handed no handle")) {
        return 1;
    }
    printf("and refused a piece of text this host never had copied, and two "
           "slots it never filled\n");

    // What this host keeps of what it was handed. Text lasts as long as the
    // heap it is on, which is as long as nothing throws that away — so a host
    // holding a name between frames asks the machine rather than remembering
    // for it.
    if (!kest_still_holds(engine.runtime, name[0])) {
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
    KestValue four_events = engine.frame[0];
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
    int64_t inside = engine.frame[0].integer;

    // And the same run of them read by this host instead, an event at a time,
    // through a crossing handed one by value. It is the whole boundary in one
    // call: the host's own memory is lent, the machine walks it, unpacks each
    // `Event` into the tag and what its case carries, and hands those to a
    // function of this host's that reads them back into its own union. What
    // the program works out in a `match` and what this host works out in a
    // `switch` are the same number over the same four events, and neither of
    // them was written from the other. See D704.
    engine.frame[0] = four_events;
    if (!asks(&engine, HURT_BY) || engine.frame[0].integer != inside) {
        fprintf(stderr, "the program answered %lld for these events and this "
                        "host answered %lld\n",
                (long long)inside, (long long)engine.frame[0].integer);
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("and the same events read back a case at a time by this host: %lld "
           "damage\n",
           (long long)engine.frame[0].integer);

    // And a tag this host writes into its own memory after lending it, which
    // is what a lend is: the bytes stay the host's and it goes on writing to
    // them — this one writes through the same array below, and the program
    // writes through the view. Nothing at the lend could hold this, because
    // the lend happened before the mistake did. It is read where the program
    // reads it, which is the only place both the bytes and the type are in
    // one hand. See D710.
    events[2].tag = EVENT_NAMED + 1;
    engine.frame[0] = four_events;
    if (asks(&engine, ON_EVENTS)) {
        fprintf(stderr, "a tag nobody declared was read out of a lend\n");
        return 1;
    }
    events[2].tag = EVENT_IDLE;
    if (!said_that(engine.runtime, "K0651", "has no such case")) {
        return 1;
    }
    // And the machine runs on: a refusal is a call that did not happen rather
    // than a machine that stopped, and the same lend answers again once the
    // bytes behind it say something readable.
    engine.frame[0] = four_events;
    if (!asks(&engine, ON_EVENTS) || engine.frame[0].integer != inside) {
        fprintf(stderr, "the lend that was refused did not answer again\n");
        return 1;
    }
    printf("a tag written into a lend after it was lent was refused, and the "
           "same lend answered again: %lld\n",
           (long long)engine.frame[0].integer);

    // And one of them handed back. A result of three slots where the arguments
    // were, the first of them the tag: this host reads that, asks what the case
    // it names carries, and reads the rest the way the case says. Nothing in
    // this tree gave a value with a tag in it back until now, so nothing had
    // ever read one out of a frame. See D705.
    const KestLayout *answered = kest_frame_gives(engine.runtime,
                                                  engine.entry[WORST]);
    if (answered == NULL || !answered->tagged) {
        fprintf(stderr, "the program gives back an enum and says otherwise\n");
        return 1;
    }
    engine.frame[0] = four_events;
    Event heaviest_event;
    if (!asks(&engine, WORST) ||
        !read_event(answered, engine.frame, &heaviest_event)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (heaviest_event.tag != EVENT_HIT || heaviest_event.as.hit != 4) {
        fprintf(stderr, "the worst of those four came back as `%s`\n",
                event_names[heaviest_event.tag]);
        return 1;
    }
    printf("and the worst of them handed back: `%s` of %d\n",
           event_names[heaviest_event.tag], heaviest_event.as.hit);

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
    // And the same lend again, a hundred times. How wide the program lays a
    // type out does not change while a machine runs, so it is said once: a
    // host lending in a frame was paying 749 bytes a frame for the same
    // sentence, on the arena the build's diagnostics are written in and never
    // handed back. The lend is refused every time, which is the part that
    // matters; what is said once is what there is to say about it. See D616.
    size_t laid_out_once = kest_build_cost(build);
    for (uint32_t again = 0; again < 100; again++) {
        if (kest_borrow(engine.runtime, tiles, 3, "Tile",
                        sizeof(Tile) + 4).object != NULL) {
            fprintf(stderr, "a lend of the wrong size was made\n");
            return 1;
        }
    }
    if (kest_build_cost(build) != laid_out_once ||
        !said_nothing(engine.runtime, "a lend of the wrong size a hundred "
                                      "times over")) {
        fprintf(stderr, "lending the wrong size again cost %zu bytes\n",
                kest_build_cost(build) - laid_out_once);
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
        // The frame now holds a store, and `worn` walks an array. Both kinds
        // of handle begin with what they are, so the door reads which one this
        // is and names the slot it is in: it used to get as far as the
        // instruction that walked it, which said `K0612` about the program for
        // something the host had done. See D716.
        if (kest_call(engine.runtime, engine.entry[WORN], engine.frame,
                      sizeof(engine.frame) / sizeof(engine.frame[0]))) {
            fprintf(stderr, "a store was walked as an array\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0636",
                       "takes an array in slot 0 and this host handed a "
                       "store")) {
            return 1;
        }
        printf("a store handed where an array was wanted was refused at the "
               "door\n");

        // And the other way round, which had nothing to say it at all: an
        // array where a store was wanted. Both are handles out of this heap,
        // so the door's older questions — is there one, and did it come from
        // this machine — both answer yes, and what told them apart was the
        // instruction that used it. See D716.
        if (!asks(&engine, OWN_ARRAY)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        engine.frame[1].integer = 5;
        if (asks(&engine, SPAWN)) {
            fprintf(stderr, "an array was added to as a store\n");
            return 1;
        }
        if (!said_that(engine.runtime, "K0636",
                       "takes a store in slot 0 and this host handed an "
                       "array")) {
            return 1;
        }
        printf("and an array handed where a store was wanted was refused the "
               "same way\n");
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

    // And the same block written through the layout and nothing else: no
    // struct of this host's, no `offsetof` — the bytes go where the program
    // said each piece is, at the width it said each piece is. What is held
    // above is two descriptions agreeing, and two descriptions can agree and
    // both be wrong about memory. What cannot is the program reading back
    // what was written at the places it named. See D899.
    {
        const KestLayout *laid = NULL;
        if (kest_build_layout(build, "Flagged", &laid) != 1 || laid == NULL) {
            fprintf(stderr, "the program has no one `Flagged` to lay out\n");
            return 1;
        }
        unsigned char block[3 * 64] = {0};
        for (unsigned i = 0; i < 3; i++) {
            unsigned char *row = block + (size_t)i * laid->size;
            for (uint16_t k = 0; k < laid->count; k++) {
                // The first piece counts and the second says whether it does,
                // which is what `howManyOn` adds up. Both are whole numbers of
                // the width the kind names.
                if (!wrote_where(row + laid->pieces[k].offset,
                                 laid->pieces[k].kind, k == 0 ? i + 1 : 1)) {
                    fprintf(stderr, "a piece of `Flagged` is a kind this host "
                                    "cannot write\n");
                    return 1;
                }
            }
        }
        KestValue run = kest_borrow(engine.runtime, block, 3, "Flagged",
                                    laid->size);
        if (run.object == NULL) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        engine.frame[0] = run;
        if (!asks(&engine, HOW_MANY_ON) || engine.frame[0].integer != 6) {
            fprintf(stderr, "what was written through the layout read back as "
                            "%lld rather than 6\n",
                    (long long)engine.frame[0].integer);
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        if (!kest_lend_ends(engine.runtime, run)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
        printf("and the same rows written through the layout alone read back "
               "as 6\n");
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
        KestValue whole = kest_borrow(engine.runtime, twins, 2, "examples.embed.Twin",
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
    if (!said_that(engine.runtime, "K0610", "the program counts them with")) {
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
    // Written into the packet where a reader would have put them: at the first
    // address in it that an `Event` may not sit at, which is worked out from
    // what the program says an `Event` is aligned to rather than guessed. One
    // byte in is not one byte out of alignment if the array itself did not
    // begin on a multiple -- and on one of the three platforms this is built
    // for it did not, so the test passed there for a reason that had nothing
    // to do with what it is about.
    const KestLayout *an_event = NULL;
    kest_build_layout(build, "Event", &an_event);
    uint32_t wants =
        an_event == NULL || an_event->align == 0 ? 4 : an_event->align;
    unsigned char *askew = packet;
    while (((uintptr_t)askew % wants) == 0) {
        askew++;
    }
    memcpy(askew, arriving, sizeof(arriving));
    if (kest_borrow(engine.runtime, askew, 2, "Event", sizeof(Event))
            .object != NULL) {
        // What the machine was told, said out loud: a refusal that did not
        // happen is a refusal about *something*, and which of the three
        // numbers it is about is the whole of what to do next.
        fprintf(stderr,
                "a lend of a byte buffer as `Event` was allowed: the address "
                "is %u past a multiple of %u, this host makes an `Event` %zu "
                "bytes, and the program makes it %u wide and %u aligned\n",
                (unsigned)((uintptr_t)askew % wants), wants, sizeof(Event),
                an_event == NULL ? 0u : an_event->size,
                an_event == NULL ? 0u : an_event->align);
        return 1;
    }
    if (!said_that(engine.runtime, "K0610", "past a multiple of that")) {
        return 1;
    }
    Event unpacked[2];
    memcpy(unpacked, askew, sizeof(unpacked));
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
    // And what is under the new tag, which is this host's own bytes read as
    // the case that is there now rather than as the one that was. `Hit` was 4
    // here a moment ago and `Idle` carries nothing, so the four bytes it
    // carried are written as nought along with the tag: the same value is the
    // same bytes whatever the memory held before, which is what a host that
    // compares two of them or writes one out is relying on without saying so.
    // See D711.
    if (events[0].tag != EVENT_IDLE || events[0].as.hit != 0) {
        fprintf(stderr, "a case that carries nothing left %d under its tag\n",
                events[0].as.hit);
        return 1;
    }
    printf("silenced the first: tag is now %d and what the case before it "
           "carried is nought\n",
           events[0].tag);

    engine.frame[0] = lent;
    if (!asks(&engine, ON_EVENTS)) {
        return 1;
    }
    printf("host reads it back: %lld damage\n", (long long)engine.frame[0].integer);

    // And the worst of them now that the first is silent, which is the case
    // this host has not yet been handed back: two floats rather than a whole
    // number, read through the member `kest_slot_of` says and not through the
    // one that worked last time. A host that read every case the way it read
    // the first would have been right once and wrong here.
    engine.frame[0] = lent;
    Event loudest;
    if (!asks(&engine, WORST) ||
        !read_event(kest_frame_gives(engine.runtime, engine.entry[WORST]),
                    engine.frame, &loudest)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (loudest.tag != EVENT_MOVED || loudest.as.moved.x != 1.5f ||
        loudest.as.moved.y != 2.5f) {
        fprintf(stderr, "the worst of the rest came back as `%s` of %g and "
                        "%g\n",
                event_names[loudest.tag], (double)loudest.as.moved.x,
                (double)loudest.as.moved.y);
        return 1;
    }
    printf("and the worst of what is left: `%s` of %g and %g\n",
           event_names[loudest.tag], (double)loudest.as.moved.x,
           (double)loudest.as.moved.y);

    // And the one direction left: this host answering with a value that has a
    // tag in it, which the program then reads a case at a time. Three costs and
    // three cases, so the two slots after the tag are written as two floats
    // once and as a whole number once — the same choice `read_event` makes,
    // made by the host rather than read by it. See D706.
    const struct {
        int32_t cost;
        int64_t damage;
    } blaming_for[] = {{9, 9}, {4, 4}, {0, 0}};
    for (size_t i = 0; i < sizeof(blaming_for) / sizeof(blaming_for[0]); i++) {
        engine.frame[0].integer = blaming_for[i].cost;
        if (!asks(&engine, BLAMED) ||
            engine.frame[0].integer != blaming_for[i].damage) {
            fprintf(stderr, "this host blamed %d on an event worth %lld\n",
                    blaming_for[i].cost, (long long)engine.frame[0].integer);
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    printf("and three costs blamed on three cases this host wrote: %d, %d and "
           "%d\n",
           (int)blaming_for[0].damage, (int)blaming_for[1].damage,
           (int)blaming_for[2].damage);

    // And a tag this host made up, which is the one mistake at this crossing
    // that nothing can be asked about beforehand: the tag is decided inside the
    // call, after everything a host can be held to has been. Every slot after
    // it means whatever it says, so the program would read a payload nobody
    // wrote and have no way to doubt it. The machine reads it the moment this
    // host answers.
    blaming = true;
    engine.frame[0].integer = 9;
    if (asks(&engine, BLAMED)) {
        fprintf(stderr, "a tag nobody declared was handed back and read\n");
        return 1;
    }
    blaming = false;
    if (!said_that(engine.runtime, "K0650",
                   "answers with a tag in slot 0 and 4 is no case of it")) {
        return 1;
    }
    printf("a case this host made up was refused where it was answered\n");

    // And the same made-up tag going the other way, which is the door this
    // host fills by hand at every call: a frame is full before anything runs,
    // so a tag nobody declared is a question that can be asked at the door
    // rather than at the instruction that meets it. See D707.
    engine.frame[0].integer = EVENT_NAMED + 1;
    engine.frame[1].integer = 0;
    engine.frame[2].integer = 0;
    if (asks(&engine, DAMAGE_OF)) {
        fprintf(stderr, "a tag nobody declared was handed over and read\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636", "is no case of it")) {
        return 1;
    }
    printf("and one handed over in a frame was refused at the door\n");

    // And a value with a tag in it inside another shape, which is where that
    // reading has to stop: `Blamed` holds an `Event` and a number, so the tag
    // is the first of four slots and the cases are the `Event`'s. A layout
    // says it holds a tag either way, and a machine that read the two alike
    // refused this host for handing over a shape it had filled correctly.
    // And a piece of text inside a shape, which the door read nothing of until
    // it read an argument by what it is. `Npc` is a name and a number, so the
    // first of its two slots is a word the machine has to own — the same
    // question a piece of text handed over on its own gets, one field in.
    // See D718.
    KestValue a_name[2] = {{0}, {0}};
    if (!kest_text(engine.runtime, "kept", 4, a_name)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.frame[0] = a_name[0];
    engine.frame[1] = a_name[1];
    engine.frame[2].integer = 5;
    if (!asks(&engine, GREETS) || engine.frame[0].integer != 9) {
        fprintf(stderr, "a shape with a name in it greeted %lld\n",
                (long long)engine.frame[0].integer);
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // And the same shape with this host's own bytes in the field. A host that
    // filled it this way wrote a pointer nobody looked at, and the program
    // read it as text the machine owned.
    engine.frame[0].text = "this host's own";
    engine.frame[1].integer = 5;
    if (asks(&engine, GREETS)) {
        fprintf(stderr, "a name this host owns was read as the machine's\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636",
                   "takes text in slot 0 and this did not come from this "
                   "machine")) {
        return 1;
    }
    printf("a name inside a shape is read where a name on its own is\n");

    // Where the tag in that shape is, found rather than known: a host walking
    // the pieces of what it fills meets `KEST_L_TAG` and asks the cases there,
    // which is the same question it asks of a value that is an enum, with the
    // piece it happens to be at. Until a tag said it was one there was nothing
    // to walk for. See D709.
    const KestLayout *shaped =
        kest_frame_layout(engine.runtime, engine.entry[BLAMED_BY], 0);
    uint16_t tag_at = shaped == NULL ? 0 : shaped->count;
    for (uint16_t p = 0; shaped != NULL && p < shaped->count; p++) {
        if (shaped->pieces[p].kind == KEST_L_TAG) {
            tag_at = p;
            break;
        }
    }
    if (shaped == NULL || tag_at == shaped->count ||
        !reads_the_cases(shaped, tag_at)) {
        fprintf(stderr, "the shape this host fills has no tag it can read\n");
        return 1;
    }
    printf("the tag in a shape of %u pieces is the one at %u\n", shaped->count,
           tag_at);

    engine.frame[0].integer = 3;
    engine.frame[1].integer = EVENT_HIT;
    engine.frame[2].integer = 5;
    engine.frame[3].integer = 0;
    if (!asks(&engine, BLAMED_BY) || engine.frame[0].integer != 8) {
        fprintf(stderr, "a shape holding an event was blamed for %lld\n",
                (long long)engine.frame[0].integer);
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("and a shape with an event inside it: %lld\n",
           (long long)engine.frame[0].integer);

    // And the same four slots said the other way round, which is what a host
    // with the fields of a shape in the wrong order says. Both readings were
    // one reading until a tag said it was a tag: a tag and a number are four
    // bytes each and were both `KEST_L_I32`, so a frame with one at either end
    // agreed with itself whichever way round this host had them. See D708.
    const uint8_t backwards[4] = {KEST_L_TAG, KEST_L_PAYLOAD, KEST_L_PAYLOAD,
                                  KEST_L_I32};
    if (kest_frame_fills(engine.runtime, engine.entry[BLAMED_BY], backwards,
                         4)) {
        fprintf(stderr, "a shape filled back to front was agreed to\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0634", "`i32` in slot 0")) {
        return 1;
    }
    printf("and a tag said to be a number where a number is was refused\n");

    // And a made-up tag inside that shape, which is what the door could not see
    // until a tag said it was one: the reading it had was about a value that is
    // an enum, and this is a whole number among whole numbers four slots wide.
    // Refused now, by the same walk, at the same door. See D709.
    engine.frame[0].integer = 3;
    engine.frame[1].integer = EVENT_NAMED + 1;
    engine.frame[2].integer = 5;
    engine.frame[3].integer = 0;
    if (asks(&engine, BLAMED_BY)) {
        fprintf(stderr, "a tag nobody declared inside a shape was read\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636", "is no case of it")) {
        return 1;
    }
    printf("and a made-up tag inside a shape was refused at the same door\n");

    // And the pair the whole reading rests on: an enum whose cases carry
    // nothing is one slot of four bytes and so is an `i32`, so a tag beside a
    // number and a number beside a tag were one run of pieces — same kinds,
    // same offsets — and a host with the two the other way round agreed with
    // itself about a frame it had back to front. Two calls that answer
    // differently, and then the two said the wrong way round. See D713.
    const struct {
        int32_t how;
        int64_t n;
        int64_t answer;
    } footing[] = {{1, 3, 6}, {0, 3, 3}, {2, 3, -3}};
    for (size_t i = 0; i < sizeof(footing) / sizeof(footing[0]); i++) {
        engine.frame[0].integer = footing[i].how;
        engine.frame[1].integer = footing[i].n;
        if (!asks(&engine, FOOTED) ||
            engine.frame[0].integer != footing[i].answer) {
            fprintf(stderr, "footing %d over %lld answered %lld\n",
                    footing[i].how, (long long)footing[i].n,
                    (long long)engine.frame[0].integer);
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    const uint8_t swapped[2] = {KEST_L_I32, KEST_L_TAG};
    if (kest_frame_fills(engine.runtime, engine.entry[FOOTED], swapped, 2)) {
        fprintf(stderr, "a tag and a number were agreed to either way "
                        "round\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0634", "`tag` in slot 0")) {
        return 1;
    }
    // And the tag of one that carries nothing, made up: the walk that reads a
    // tag has no payload slots to skip here and the same question to ask.
    engine.frame[0].integer = 3;
    engine.frame[1].integer = 3;
    if (asks(&engine, FOOTED)) {
        fprintf(stderr, "a tag nobody declared was read where nothing is "
                        "carried\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0636", "is no case of it")) {
        return 1;
    }
    printf("a tag that carries nothing is not the number beside it, either "
           "way round\n");

    // And the machine still runs, because a refusal is a call that did not
    // happen rather than a machine that stopped: the next one answers.
    engine.frame[0].integer = 9;
    if (!asks(&engine, BLAMED) || engine.frame[0].integer != 9) {
        fprintf(stderr, "the crossing that was refused did not answer again\n");
        return 1;
    }

    // And the same shape crossing the other way: an enum by value, in a frame
    // rather than in a lend. The tag goes into the first slot and what the case
    // carries into the ones after it — and which member of a `KestValue` each
    // of those is depends on the tag, which is the one thing a layout cannot
    // say. It says `KEST_L_PAYLOAD` there and leaves it to whoever wrote the
    // tag, so a host that guesses writes a whole number where the program reads
    // a float and nothing anywhere says so. See D702.
    const KestLayout *carries =
        kest_frame_layout(engine.runtime, engine.entry[DAMAGE_OF], 0);
    if (carries == NULL || !carries->tagged) {
        fprintf(stderr, "the program takes an enum and its layout says not\n");
        return 1;
    }
    // What this host calls the cases and what each of them carries, held
    // against what the program says: the same reading the crossing handed one
    // of these gets at binding, because it is the same question about the same
    // shape at the other end of the same boundary.
    if (!reads_the_cases(carries, 0)) {
        return 1;
    }
    // And a layout that holds no tag at all, which has no cases rather than
    // none left: the walk above ends at the end of the list and this ends
    // before it starts, and both of them answer nothing.
    if (kest_case_of(kest_frame_layout(engine.runtime, engine.entry[LENGTH_OF],
                                       0),
                     0, 0, NULL, NULL) != NULL) {
        fprintf(stderr, "a case came back for something that has none\n");
        return 1;
    }
    printf("the program's %d cases are the ones this host has names for\n",
           (int)(sizeof(event_names) / sizeof(event_names[0])));

    // Three of them written into a frame and handed over. What the case carries
    // is a run of pieces like any other, so `kest_slot_of` says which member
    // each slot is — the same reading this host does for a result of two kinds,
    // over kinds the tag decided rather than the declaration.
    const struct {
        int32_t tag;
        double carried[2];
        int64_t answer;
    } handing[] = {{EVENT_MOVED, {1.5, 2.5}, 4},
                   {EVENT_HIT, {9, 0}, 9},
                   {EVENT_IDLE, {0, 0}, 0}};
    for (size_t i = 0; i < sizeof(handing) / sizeof(handing[0]); i++) {
        const KestPiece *pieces = NULL;
        uint16_t count = 0;
        if (kest_case_of(carries, 0, handing[i].tag, &pieces, &count) ==
            NULL) {
            fprintf(stderr, "no case %d to hand over\n", handing[i].tag);
            return 1;
        }
        engine.frame[0].integer = handing[i].tag;
        for (uint16_t p = 0; p < count; p++) {
            if (kest_slot_of(pieces[p].kind) == KEST_S_REAL) {
                engine.frame[1 + p].real = handing[i].carried[p];
            } else {
                engine.frame[1 + p].integer = (int64_t)handing[i].carried[p];
            }
        }
        // The slots this case does not carry are the widest one's, and nothing
        // reads them: a frame is as wide as the widest case, and `Hit` fills
        // one of the two after the tag.
        for (uint16_t p = count; p + 1 < carries->count; p++) {
            engine.frame[1 + p].integer = 0;
        }
        if (!asks(&engine, DAMAGE_OF) ||
            engine.frame[0].integer != handing[i].answer) {
            fprintf(stderr, "`%s` handed over as a value answered %lld\n",
                    event_names[handing[i].tag], (long long)engine.frame[0].integer);
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    printf("and the same shape handed over by value, a case at a time: %s, %s "
           "and %s\n",
           event_names[EVENT_MOVED], event_names[EVENT_HIT],
           event_names[EVENT_IDLE]);

    // A frame is not one call, it is the same call sixty times a second, and
    // a promise that holds once and leaks a little each time is a promise
    // that runs out overnight. `onEvents` says `no.alloc`, so a thousand of
    // them have to leave the heap exactly where they found it — not nearly,
    // since what this is looking for is the byte a frame keeps.
    size_t before = kest_heap_taken(engine.runtime);
    for (int i = 0; i < 1000; i++) {
        engine.frame[0] = lent;
        if (!asks(&engine, ON_EVENTS)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    size_t after = kest_heap_taken(engine.runtime);
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

    // A shape with a flag in it that is not a tag, over memory this host filled
    // with something else first. An optional is a value and a byte saying
    // whether the value is there, so an empty one is the byte set to nought —
    // and what is under it is written as well, the same rule a case that
    // carries nothing is written by. Two empty ones are two of the same bytes
    // because of it, which is what a host comparing, hashing or writing out its
    // own array is relying on. See D712.
    Mark marks[2];
    memset(marks, 0xAB, sizeof(marks));
    KestValue lent_marks =
        kest_borrow(engine.runtime, marks, 2, "Mark", sizeof(Mark));
    if (lent_marks.object == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    const struct {
        int32_t at;
        int32_t n;
    } marking[] = {{0, 5}, {1, 9}};
    for (size_t i = 0; i < sizeof(marking) / sizeof(marking[0]); i++) {
        engine.frame[0] = lent_marks;
        engine.frame[1].integer = marking[i].at;
        engine.frame[2].integer = marking[i].n;
        if (!asks(&engine, MARK)) {
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    engine.frame[0] = lent_marks;
    engine.frame[1].integer = 1;
    if (!asks(&engine, UNMARK)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (marks[0].at != 5 || !marks[0].held) {
        fprintf(stderr, "a value written into a lend reads back as %d, held "
                        "%d\n",
                marks[0].at, (int)marks[0].held);
        return 1;
    }
    if (marks[1].held || marks[1].at != 0 || marks[1].n != 0) {
        fprintf(stderr, "an empty one left %d under a flag that says %d\n",
                marks[1].at, (int)marks[1].held);
        return 1;
    }
    engine.frame[0] = lent_marks;
    if (!asks(&engine, MARKED) || engine.frame[0].integer != 5) {
        fprintf(stderr, "the program counted %lld of what it wrote\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    if (!kest_lend_ends(engine.runtime, lent_marks)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    printf("a flag that is not a tag: %d held and nought under the one that is "
           "not\n",
           marks[0].at);

    // And the same shape crossing a frame, where a host says what it is putting
    // in each slot. A value, the byte that says whether the value is there, and
    // a number are what a number, a `bool` and a number are too — one run of
    // pieces, same kinds and same offsets and same size — so a host could lend
    // either of those two shapes under the other's name and be told nothing.
    // The middle piece is what tells them apart now. See D714.
    const struct {
        int64_t at;
        int64_t held;
        int64_t n;
        int64_t answer;
    } marks_by_value[] = {{5, 1, 2, 7}, {0, 0, 2, 2}};
    for (size_t i = 0; i < sizeof(marks_by_value) / sizeof(marks_by_value[0]);
         i++) {
        engine.frame[0].integer = marks_by_value[i].at;
        engine.frame[1].integer = marks_by_value[i].held;
        engine.frame[2].integer = marks_by_value[i].n;
        if (!asks(&engine, MARKING) ||
            engine.frame[0].integer != marks_by_value[i].answer) {
            fprintf(stderr, "a mark held %lld answered %lld\n",
                    (long long)marks_by_value[i].held,
                    (long long)engine.frame[0].integer);
            kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
            return 1;
        }
    }
    const uint8_t as_a_switch[3] = {KEST_L_I32, KEST_L_BOOL, KEST_L_I32};
    if (kest_frame_fills(engine.runtime, engine.entry[MARKING], as_a_switch,
                         3)) {
        fprintf(stderr, "a flag said to be a byte was agreed to\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0634", "`held` in slot 1")) {
        return 1;
    }
    printf("and the byte that says whether a value is there is not the byte "
           "beside it\n");

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
    KestLimits allowed = {0, 0, 0, 0};
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

    // The name this host kept is handed in, because what happens to it is
    // asked while the heap it was on has just gone and nothing has been made
    // since. Nothing about the pointer changed; what changed is whose memory
    // it is, which is the one thing a host cannot see for itself.
    if (!spends_the_heap(&engine, name[0])) {
        return 1;
    }

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
    if (!kest_keeps(engine.runtime, engine.world)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
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
    // Kept the way the kind says rather than copied whole. A reference is a
    // number — the place it names and how many times that place has been
    // handed out — and copying the slot worked while saying nothing about what
    // was in it: a host that asked `kest_slot_of` about this slot was told to
    // read a pointer out of it, which is a pointer nobody made. See D715.
    int64_t kept_ref = engine.frame[0].integer;
    engine.frame[0] = engine.world;
    engine.frame[1].integer = kept_ref;
    if (!kest_call(engine.runtime, engine.entry[HEALTH_OF], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0])) ||
        engine.frame[0].integer != 5) {
        fprintf(stderr, "a reference this host kept named nothing: %lld\n",
                (long long)engine.frame[0].integer);
        return 1;
    }
    engine.frame[0] = engine.world;
    engine.frame[1].integer = kept_ref;
    if (!kest_call(engine.runtime, engine.entry[DROPPED], engine.frame,
                   sizeof(engine.frame) / sizeof(engine.frame[0]))) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    engine.frame[0] = engine.world;
    engine.frame[1].integer = kept_ref;
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

    // And the pair a reference and a handle were one of. `healthOf` takes a
    // store and a place in it; `twinned` takes two arrays. Both were two
    // machine words, so a host handing two handles where a store and a place
    // were wanted said a frame that agreed with itself, and what the program
    // read as a place was a pointer. The second piece is what tells them apart
    // now. See D715.
    const uint8_t two_handles[2] = {KEST_L_WORD, KEST_L_WORD};
    if (kest_frame_fills(engine.runtime, engine.entry[HEALTH_OF], two_handles,
                         2)) {
        fprintf(stderr, "a place in a store was agreed to as a handle\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0634", "`ref` in slot 1")) {
        return 1;
    }
    // And the two of them side by side, read rather than asserted: eight bytes
    // each, one word and one reference. One width read two ways is what the
    // whole pair was, and what a kind of its own is for.
    const KestLayout *takes_store =
        kest_frame_layout(engine.runtime, engine.entry[HEALTH_OF], 0);
    const KestLayout *takes_place =
        kest_frame_layout(engine.runtime, engine.entry[HEALTH_OF], 1);
    if (takes_store == NULL || takes_place == NULL ||
        takes_store->size != takes_place->size ||
        takes_store->pieces[0].kind != KEST_L_WORD ||
        takes_place->pieces[0].kind != KEST_L_REF) {
        fprintf(stderr, "a store and a place in one are not one width read "
                        "two ways\n");
        return 1;
    }
    printf("a place in a store is %u bytes, the same as the handle it was one "
           "kind with\n",
           takes_place->size);

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
    int64_t elsewhere_ref = engine.frame[0].integer;
    engine.frame[0] = engine.world;
    engine.frame[1].integer = elsewhere_ref;
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
    if (!said_that(engine.runtime, "K0636", "did not come from this machine")) {
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
    // How many there are and how much room there is, which a host had nowhere
    // to ask until D965. A lend is as long as it is lent and has no room over;
    // a number a host wrote into a slot is not an array and answers nought,
    // which is the same answer an empty one gives.
    {
        uint32_t room = 0;
        KestValue not_one = {0};
        not_one.integer = 12345;
        if (kest_array_length(engine.runtime, engine.frame[0], &room) !=
                sizeof(words) ||
            room != sizeof(words) ||
            kest_array_length(engine.runtime, not_one, &room) != 0 ||
            room != 0 || kest_array_length(NULL, engine.frame[0], NULL) != 0) {
            fprintf(stderr, "a lend of %zu said it was %u long\n",
                    sizeof(words),
                    kest_array_length(engine.runtime, engine.frame[0], NULL));
            return 1;
        }
        uint32_t many = kest_array_length(engine.runtime, engine.frame[0],
                                          &room);
        printf("a lend of %zu bytes says it is %u long with room for %u\n",
               sizeof(words), many, room);
    }
    engine.frame[1].integer = 0;
    kest_text(engine.runtime, "kest", 4, &engine.frame[2]);
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
    KestValue first_word[2] = {{0}, {0}};
    kest_text(engine.runtime, "the engine", 10, first_word);
    if (first_word[0].text == NULL || !kest_heap_reset(engine.runtime)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (kest_still_holds(engine.runtime, first_word[0])) {
        fprintf(stderr, "text survived the heap it was on\n");
        return 1;
    }
    KestValue next_word[2] = {{0}, {0}};
    kest_text(engine.runtime, "the second", 10, next_word);
    if (next_word[0].text == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // Whether the first thing made on an emptied heap goes where the last
    // thing on the old one was is the machine's to decide and not a thing a
    // host may lean on: the memory a program stands on is given back to the
    // host a piece at a time now, so the same address comes round again when
    // it happens to and not because anything promised it. What is a rule is
    // the line above -- a pointer the host kept is not the machine's once the
    // heap has gone -- and this is the other half of the same rule: if it does
    // come round again, what is written there is what the machine made next
    // and not what the host put there. See D353 and D996.
    if (next_word[0].text == first_word[0].text &&
        strcmp(first_word[0].text, "the second") != 0) {
        fprintf(stderr, "text kept across a reset reads `%s`\n",
                first_word[0].text);
        return 1;
    }
    if (!said_nothing(engine.runtime, "a heap was thrown away twice")) {
        return 1;
    }
    printf("and text kept across a heap being thrown away is not this "
           "machine's any more\n");

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
    KestValue made[2] = {{0}, {0}};
    kest_text(engine.runtime, "made while running", 18, made);
    if (made[0].text == NULL) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    if (kest_kept_where(engine.runtime, written) != KEST_KEPT_PROGRAM ||
        kest_kept_where(engine.runtime, made[0]) != KEST_KEPT_HEAP) {
        fprintf(stderr, "text out of the file is %s and text made while "
                        "running is %s\n",
                keeping(kest_kept_where(engine.runtime, written)),
                keeping(kest_kept_where(engine.runtime, made[0])));
        return 1;
    }
    // Both say yes to the question that has one answer, which is why that one
    // cannot be what a host keeping a value between frames reads.
    if (!kest_still_holds(engine.runtime, written) ||
        !kest_still_holds(engine.runtime, made[0])) {
        fprintf(stderr, "the machine has text it says it has not\n");
        return 1;
    }
    if (!kest_heap_reset(engine.runtime)) {
        kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
        return 1;
    }
    // And what the two are worth afterwards, which is what the asking was for.
    if (kest_kept_where(engine.runtime, made[0]) != KEST_KEPT_NOWHERE) {
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

    // And the same text handed back as a run of bytes, which is a host lending
    // the machine its own memory. What a program holds of text is a pointer
    // into the heap or into the build, and a lend is memory a program may write
    // into — so this would make the one thing this language says cannot be
    // written into a thing that can, and a literal rewritten that way stays
    // rewritten for every machine the build starts. Asked for on purpose,
    // because a host holding a piece of text and a length has everything it
    // needs to do it by accident. See D720.
    if (kest_borrow(engine.runtime, (void *)(uintptr_t)written.text,
                    (uint32_t)strlen(written.text), "u8", 1)
            .object != NULL) {
        fprintf(stderr, "the machine lent this host its own memory back\n");
        return 1;
    }
    if (!said_that(engine.runtime, "K0653",
                   "at an address this machine owns")) {
        return 1;
    }
    printf("and text of the program's own lent back as bytes was refused\n");

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

    // And what a machine that was never asked takes with it. What a host has
    // been told is the host's and the room it was written in goes back (D617);
    // what it was not told is the machine's, and a machine is freed with it. So
    // a host that means to say why something went wrong asks before it frees —
    // the build is not holding the words, it is holding the file they point at.
    // See D637.
    {
        KestHost *quietly = kest_host_new();
        static Decider unasked = {-1, 1, false, true, false, false};
        if (quietly == NULL ||
            !kest_host_bind(quietly, "Io.write", io_write, stdout) ||
            !kest_host_bind(quietly, "Engine.decide", engine_decide, &unasked) ||
            !kest_host_bind(quietly, "Engine.name", engine_name, &unasked) ||
            !kest_host_bind(quietly, "Engine.rank", engine_rank, &unasked) ||
            !kest_host_bind(quietly, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(quietly, "Engine.blame", engine_blame, NULL) ||
            !kest_host_bind(quietly, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(quietly, "Engine.who", engine_who, NULL)) {
            fprintf(stderr, "a host to say nothing with would not be made\n");
            return 1;
        }
        KestLimits little = {0, 0, 64, 0};
        KestRuntime *silent = kest_start(build, quietly, &little);
        kest_host_free(quietly);
        int32_t fills = silent == NULL ? -1 : kest_entry(silent, "filling");
        KestValue asking[4] = {{0}};
        asking[0].integer = 40;
        size_t before_saying = silent == NULL ? 0 : kest_runtime_cost(silent);
        if (silent == NULL || fills < 0 ||
            kest_call(silent, fills, asking, 4) ||
            kest_runtime_cost(silent) <= before_saying) {
            fprintf(stderr, "a machine with 64 bytes of heap said nothing "
                            "about filling an array\n");
            return 1;
        }
        if (!kest_runtime_free(silent)) {
            fprintf(stderr, "a machine nobody asked was not freed\n");
            return 1;
        }
        if (!build_said_nothing(build, "a machine nobody asked went")) {
            return 1;
        }
        printf("and what a machine nobody asked had to say went with it\n");
    }

    // What a host does about a program that will not stop. Three ceilings here
    // bound memory and this is the one that bounds time: a budget in steps,
    // where a step is a jump that goes back or a call, because those are the
    // two things a program does to go on doing something. A host that runs code
    // it did not write needs it, and one that does not can leave it at nought
    // and pay nothing for it. See D921.
    {
        KestHost *timed = kest_host_new();
        static Decider untimed = {-1, 1, false, true, false, false};
        if (timed == NULL ||
            !kest_host_bind(timed, "Io.write", io_write, stdout) ||
            !kest_host_bind(timed, "Engine.decide", engine_decide, &untimed) ||
            !kest_host_bind(timed, "Engine.name", engine_name, &untimed) ||
            !kest_host_bind(timed, "Engine.rank", engine_rank, &untimed) ||
            !kest_host_bind(timed, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(timed, "Engine.blame", engine_blame, NULL) ||
            !kest_host_bind(timed, "Engine.weigh", engine_weigh, NULL) ||
            !kest_host_bind(timed, "Engine.who", engine_who, NULL)) {
            fprintf(stderr, "a host to time a program with would not be "
                            "made\n");
            return 1;
        }
        // Ten steps, and a loop of a thousand turns to spend them on.
        KestLimits counted = {0, 0, 0, 10};
        KestRuntime *budgeted = kest_start(build, timed, &counted);
        kest_host_free(timed);
        int32_t fills = budgeted == NULL ? -1 : kest_entry(budgeted, "filling");
        KestValue asking[4] = {{0}};
        asking[0].integer = 1000;
        if (budgeted == NULL || fills < 0) {
            fprintf(stderr, "a machine with a budget would not start\n");
            return 1;
        }
        // What the machine was told it may have, read back the way the other
        // three are.
        KestLimits allowed_steps = {0, 0, 0, 0};
        kest_allowed(budgeted, &allowed_steps);
        if (allowed_steps.fuel != 10) {
            fprintf(stderr, "a machine given 10 steps says it was allowed "
                            "%llu\n",
                    (unsigned long long)allowed_steps.fuel);
            return 1;
        }
        if (kest_call(budgeted, fills, asking, 4)) {
            fprintf(stderr, "a thousand turns ran inside a budget of ten\n");
            return 1;
        }
        if (!said_that(budgeted, "K0659", "step(s) it was given")) {
            return 1;
        }
        if (kest_fuel_left(budgeted) != 0) {
            fprintf(stderr, "a machine that ran out says it has %llu left\n",
                    (unsigned long long)kest_fuel_left(budgeted));
            return 1;
        }
        // And what it is for: a machine that stopped is not a machine that
        // broke. Everything it built is where it was, so a host that gives it
        // more carries on rather than starting again.
        kest_fuel_set(budgeted, KEST_FUEL_UNLIMITED);
        asking[0].integer = 1000;
        if (!kest_call(budgeted, fills, asking, 4) || asking[0].integer != 1000) {
            fprintf(stderr, "a machine given fuel again would not run\n");
            return 1;
        }
        // A budget bigger than the work, which is the case a host in a frame
        // is in: what is left afterwards is what the call did not spend, and a
        // machine that kept the whole slice would say nought.
        kest_fuel_set(budgeted, 100000);
        asking[0].integer = 1000;
        if (!kest_call(budgeted, fills, asking, 4)) {
            fprintf(stderr, "a machine with room to spare would not run\n");
            return 1;
        }
        uint64_t left = kest_fuel_left(budgeted);
        if (left == 0 || left >= 100000) {
            fprintf(stderr, "a thousand turns inside a hundred thousand steps "
                            "left %llu\n",
                    (unsigned long long)left);
            return 1;
        }
        printf("a thousand turns cost %llu of a hundred thousand steps\n",
               (unsigned long long)(100000 - left));
        // The other half of the same mechanism: a host that wants it to stop
        // for a reason that is not a budget. One store of one word, so a host
        // may do it from a signal handler or another thread.
        kest_cancel(budgeted);
        if (!kest_cancelled(budgeted)) {
            fprintf(stderr, "a machine asked to stop says nobody asked\n");
            return 1;
        }
        asking[0].integer = 1000;
        if (kest_call(budgeted, fills, asking, 4)) {
            fprintf(stderr, "a machine asked to stop ran anyway\n");
            return 1;
        }
        if (!said_that(budgeted, "K0660", "asked this program to stop")) {
            return 1;
        }
        // And giving it fuel is what takes the asking back, because the two
        // are one counter and a flag beside it.
        kest_fuel_set(budgeted, KEST_FUEL_UNLIMITED);
        if (kest_cancelled(budgeted)) {
            fprintf(stderr, "a machine given fuel is still cancelled\n");
            return 1;
        }
        asking[0].integer = 1000;
        if (!kest_call(budgeted, fills, asking, 4)) {
            fprintf(stderr, "a machine that was cancelled and given fuel "
                            "would not run\n");
            return 1;
        }
        // And the doors answer for a machine that is not there, the way every
        // other door here does.
        kest_fuel_set(NULL, 10);
        kest_cancel(NULL);
        if (kest_fuel_left(NULL) != KEST_FUEL_UNLIMITED ||
            kest_cancelled(NULL)) {
            fprintf(stderr, "a machine that is not there has a budget\n");
            return 1;
        }
        if (!kest_runtime_free(budgeted)) {
            fprintf(stderr, "a machine with a budget was not freed\n");
            return 1;
        }
        printf("and a program that would not stop was stopped, twice over\n");
    }

    // And a handle of the wrong thing. A kind says array or store and a stride
    // says how far apart two of them are; neither says what is inside one, so a
    // run of `Row` handed where `[Tile]` was wanted used to be unpacked by the
    // callee's layout and read past the end of this host's own memory. Both
    // ways round, because the one that is bigger reads further and the one
    // that is smaller reads somebody else's fields. See D927.
    {
        static Row rows[4];
        static Tile tiles[4];
        int32_t wants_tiles = kest_entry(engine.runtime, "worn");
        int32_t wants_rows = kest_entry(engine.runtime, "heaviest");
        if (wants_tiles < 0 || wants_rows < 0) {
            fprintf(stderr, "the program has no `worn` or `heaviest`\n");
            return 1;
        }
        struct {
            void *data;
            const char *lent_as;
            size_t size;
            int32_t entry;
            const char *wanted;
        } crossed[2] = {
            {rows, "Row", sizeof(Row), wants_tiles, "Tile"},
            {tiles, "Tile", sizeof(Tile), wants_rows, "Row"},
        };
        for (int which = 0; which < 2; which++) {
            KestValue lent =
                kest_borrow(engine.runtime, crossed[which].data, 4,
                            crossed[which].lent_as, crossed[which].size);
            if (lent.object == NULL) {
                kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
                return 1;
            }
            KestValue frame[8] = {{0}};
            frame[0] = lent;
            if (kest_call(engine.runtime, crossed[which].entry, frame, 8)) {
                fprintf(stderr,
                        "a run of `%s` was taken where `%s` was wanted\n",
                        crossed[which].lent_as, crossed[which].wanted);
                return 1;
            }
            if (!said_that(engine.runtime, "K0661", "in slot 0 and this")) {
                return 1;
            }
            if (!kest_lend_ends(engine.runtime, lent)) {
                kest_report(engine.runtime, stderr, KEST_FORM_TEXT);
                return 1;
            }
        }
        printf("and a handle of the wrong thing is refused both ways round\n");
    }

    // And a door that could not do what it was asked. A bound function gives
    // nothing back, so one that failed used to write a value that meant nothing
    // and the program carried on with it. It says so now, and the call refuses
    // where it was made, with the host's own words under it. See D937.
    {
        KestHost *sorry = kest_host_new();
        static Decider unasked = {-1, 1, false, true, false, false};
        if (sorry == NULL ||
            !kest_host_bind(sorry, "Io.write", io_write, stdout) ||
            !kest_host_bind(sorry, "Engine.decide", engine_decide, &unasked) ||
            !kest_host_bind(sorry, "Engine.name", engine_name, &unasked) ||
            !kest_host_bind(sorry, "Engine.rank", engine_rank, &unasked) ||
            !kest_host_bind(sorry, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(sorry, "Engine.blame", engine_blame, NULL) ||
            !kest_host_bind(sorry, "Engine.weigh", engine_weigh, NULL) ||
            !kest_host_bind(sorry, "Engine.who", engine_who, NULL)) {
            fprintf(stderr, "a host that refuses would not be made\n");
            return 1;
        }
        KestRuntime *refusing = kest_start(build, sorry, NULL);
        kest_host_free(sorry);
        int32_t asks = refusing == NULL ? -1 : kest_entry(refusing, "hurtBy");
        if (refusing == NULL || asks < 0) {
            fprintf(stderr, "a host that refuses would not start\n");
            return 1;
        }
        // `Engine.hurt` is bound to a body that says it could not: whatever it
        // wrote is not read, and the call refuses.
        static Event one_event[1];
        KestValue lent = kest_borrow(refusing, one_event, 1, "Event",
                                     sizeof(Event));
        if (lent.object == NULL) {
            kest_report(refusing, stderr, KEST_FORM_TEXT);
            return 1;
        }
        engine_hurt_refuses = true;
        KestValue frame[8] = {{0}};
        frame[0] = lent;
        bool ran = kest_call(refusing, asks, frame, 8);
        engine_hurt_refuses = false;
        if (ran) {
            fprintf(stderr, "a door that said it failed was taken as an "
                            "answer\n");
            return 1;
        }
        if (!said_that(refusing, "K0662", "could not do what it was asked")) {
            return 1;
        }
        if (!kest_runtime_free(refusing)) {
            fprintf(stderr, "a machine whose host refused was not freed\n");
            return 1;
        }
        printf("and a door that could not do what it was asked said so\n");
    }

    // What a machine is made of, and what starting one costs the build it was
    // started on. Two machines that differ in one number: the stack is slots
    // of `KestValue`, so the wider of the two is wider by exactly that many
    // times that many bytes, and a host asking rather than working it out is
    // asking about the frames and the table of host functions as well. What it
    // leaves on the build is less than what the machine is made of — it is a
    // list to say things into, and everything else went with the machine —
    // which is a host that reloads paying for one machine rather than for
    // every machine it has ever started. See D574.
    {
        KestLimits narrow_stack = {4096, 16, 0, 0};
        KestLimits wide_stack = {8192, 16, 0, 0};
        size_t build_before = kest_build_cost(build);
        KestHost *sizing = kest_host_new();
        static Decider still = {-1, 1, false, true, false, false};
        if (sizing == NULL ||
            !kest_host_bind(sizing, "Io.write", io_write, stdout) ||
            !kest_host_bind(sizing, "Engine.decide", engine_decide, &still) ||
            !kest_host_bind(sizing, "Engine.name", engine_name, &still) ||
            !kest_host_bind(sizing, "Engine.rank", engine_rank, &still) ||
            !kest_host_bind(sizing, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(sizing, "Engine.blame", engine_blame, NULL) ||
            !kest_host_bind(sizing, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(sizing, "Engine.who", engine_who, NULL)) {
            fprintf(stderr, "a host to size two machines with would not be "
                            "made\n");
            return 1;
        }
        KestRuntime *narrow = kest_start(build, sizing, &narrow_stack);
        KestRuntime *wide = kest_start(build, sizing, &wide_stack);
        kest_host_free(sizing);
        if (narrow == NULL || wide == NULL) {
            kest_build_report(build, stderr, KEST_FORM_TEXT);
            fprintf(stderr, "two machines of two sizes would not start\n");
            return 1;
        }
        size_t narrow_cost = kest_runtime_cost(narrow);
        size_t wide_cost = kest_runtime_cost(wide);
        if (wide_cost - narrow_cost != 4096 * sizeof(KestValue)) {
            fprintf(stderr, "a machine with 4096 more slots is %zu bytes "
                            "wider, and a slot is %zu\n",
                    wide_cost - narrow_cost, sizeof(KestValue));
            return 1;
        }
        if (!kest_runtime_free(narrow) || !kest_runtime_free(wide)) {
            fprintf(stderr, "a machine nothing was running on was not freed\n");
            return 1;
        }
        size_t left_on_the_build = kest_build_cost(build) - build_before;
        if (narrow_cost == 0 || left_on_the_build >= narrow_cost) {
            fprintf(stderr, "starting two machines left %zu bytes on the "
                            "build, and one machine is %zu\n",
                    left_on_the_build, narrow_cost);
            return 1;
        }
        printf("a machine of 4096 slots is %zu bytes and one of 8192 is %zu, "
               "and starting two left %zu on the build\n", narrow_cost,
               wide_cost, left_on_the_build);

        // And the two numbers a host actually has to pick between: what the
        // whole program needs, and what the functions this host calls need
        // with the way back in on top. The program has a run of calls ten
        // deep that nothing here enters (D620), so what saying nothing costs
        // is that chain's frames. The slots it costs are nothing at all —
        // naming what it calls is a slot dearer, not cheaper, because the way
        // back in is nearly the whole program's width and the function called
        // from there is added to it. See D621 and D803.
        KestLimits whole = {0, 0, 0, 0};
        KestLimits driven = {0, 0, 0, 0};
        static const char *const drives[] = {"step", "create", "spawn", NULL};
        if (!kest_needs(build, &whole, NULL)) {
            fprintf(stderr, "the program says nothing about what it needs\n");
            return 1;
        }
        if (!room_for_calling(build, drives, "rule", &driven)) {
            return 1;
        }
        KestHost *picking = kest_host_new();
        if (picking == NULL ||
            !kest_host_bind(picking, "Io.write", io_write, stdout) ||
            !kest_host_bind(picking, "Engine.decide", engine_decide, &still) ||
            !kest_host_bind(picking, "Engine.name", engine_name, &still) ||
            !kest_host_bind(picking, "Engine.rank", engine_rank, &still) ||
            !kest_host_bind(picking, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(picking, "Engine.blame", engine_blame, NULL) ||
            !kest_host_bind(picking, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(picking, "Engine.who", engine_who, NULL)) {
            fprintf(stderr, "a host to size two more machines would not be "
                            "made\n");
            return 1;
        }
        KestRuntime *saying_nothing = kest_start(build, picking, &whole);
        KestRuntime *naming = kest_start(build, picking, &driven);
        kest_host_free(picking);
        if (saying_nothing == NULL || naming == NULL) {
            kest_build_report(build, stderr, KEST_FORM_TEXT);
            fprintf(stderr, "a machine for what this host drives would not "
                            "start\n");
            return 1;
        }
        size_t quiet_cost = kest_runtime_cost(saying_nothing);
        size_t named_cost = kest_runtime_cost(naming);
        // The frames are the whole of what naming buys. The stack goes the
        // other way: the way back in is 32 of the program's 34, and the
        // function called from inside a host function is added to it, so
        // naming asks for more slots than saying nothing does. Refused at
        // equal as well as at less, because equal is what this said for a
        // while and what stopped being true without anything saying so — an
        // order kept by two different arithmetics is the weaker one written
        // down. See D803.
        if (driven.stack_slots <= whole.stack_slots ||
            driven.call_depth >= whole.call_depth || named_cost >= quiet_cost) {
            fprintf(stderr, "saying nothing wants %u slots and %u frames, "
                            "naming wants %u and %u, at %zu bytes against "
                            "%zu\n",
                    whole.stack_slots, whole.call_depth, driven.stack_slots,
                    driven.call_depth, quiet_cost, named_cost);
            return 1;
        }
        if (!kest_runtime_free(saying_nothing) || !kest_runtime_free(naming)) {
            fprintf(stderr, "a machine sized by asking was not freed\n");
            return 1;
        }
        printf("saying nothing is %u slots and %u frames at %zu bytes, and "
               "naming what it calls is %u and %u at %zu — %u slot(s) dearer "
               "and %u frame(s) cheaper\n",
               whole.stack_slots, whole.call_depth, quiet_cost,
               driven.stack_slots, driven.call_depth, named_cost,
               driven.stack_slots - whole.stack_slots,
               whole.call_depth - driven.call_depth);

        // And what happens when this host calls something it did not name. A
        // machine sized by naming is three frames deep and the chain is ten,
        // so the call is refused — and what it is told is what that call
        // needs, at the declaration of the function it called, rather than
        // what the whole program needs, which is the number it asked not to
        // pay for. Sized for what the words say, the same call goes through.
        // See D622.
        KestHost *shallow = kest_host_new();
        if (shallow == NULL ||
            !kest_host_bind(shallow, "Io.write", io_write, stdout) ||
            !kest_host_bind(shallow, "Engine.decide", engine_decide, &still) ||
            !kest_host_bind(shallow, "Engine.name", engine_name, &still) ||
            !kest_host_bind(shallow, "Engine.rank", engine_rank, &still) ||
            !kest_host_bind(shallow, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(shallow, "Engine.blame", engine_blame, NULL) ||
            !kest_host_bind(shallow, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(shallow, "Engine.who", engine_who, NULL)) {
            fprintf(stderr, "a host to be refused with would not be made\n");
            return 1;
        }
        KestRuntime *short_of_it = kest_start(build, shallow, &driven);
        if (short_of_it == NULL) {
            kest_build_report(build, stderr, KEST_FORM_TEXT);
            fprintf(stderr, "a machine sized by naming would not start\n");
            return 1;
        }
        int32_t chain = kest_entry(short_of_it, "tickWorld");
        KestValue among[2] = {{0}};
        among[0].integer = 4;
        KestLimits enough = {0, 0, 0, 0};
        if (chain < 0 || kest_call(short_of_it, chain, among, 2) ||
            !needed_for(short_of_it, &enough) ||
            enough.call_depth <= driven.call_depth) {
            fprintf(stderr, "a chain ten deep ran on a machine three frames "
                            "deep, or said nothing about what it wanted\n");
            return 1;
        }
        KestRuntime *sized = kest_start(build, shallow, &enough);
        kest_host_free(shallow);
        among[0].integer = 4;
        if (sized == NULL || kest_entry(sized, "tickWorld") != chain ||
            !kest_call(sized, chain, among, 2) || among[0].integer != 15) {
            kest_report(sized, stderr, KEST_FORM_TEXT);
            fprintf(stderr, "a machine sized by what the refusal said could "
                            "not make the call\n");
            return 1;
        }
        if (!kest_runtime_free(short_of_it) || !kest_runtime_free(sized)) {
            fprintf(stderr, "a machine that was refused was not freed\n");
            return 1;
        }
        printf("a call refused at %u frames said it wanted %u slots and %u, "
               "and a machine of those made it\n",
               driven.call_depth, enough.stack_slots, enough.call_depth);

        // And a host that picks neither, which is most hosts the first time.
        // What it gets is what the program asked for — including room for the
        // call this host makes from inside one of its own functions, which is
        // the half a machine cannot leave out because it does not know which
        // function a host will call. So it is asked to do exactly that: a
        // frame that steps the world, which asks this host, which asks the
        // program back. See D575.
        KestHost *unasked = kest_host_new();
        static Decider asking = {-1, 1, true, true, false, false};
        if (unasked == NULL ||
            !kest_host_bind(unasked, "Io.write", io_write, stdout) ||
            !kest_host_bind(unasked, "Engine.decide", engine_decide, &asking) ||
            !kest_host_bind(unasked, "Engine.name", engine_name, &asking) ||
            !kest_host_bind(unasked, "Engine.rank", engine_rank, &asking) ||
            !kest_host_bind(unasked, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(unasked, "Engine.blame", engine_blame, NULL) ||
            !kest_host_bind(unasked, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(unasked, "Engine.who", engine_who, NULL)) {
            fprintf(stderr, "a host that picks no numbers would not be made\n");
            return 1;
        }
        KestRuntime *given = kest_start(build, unasked, NULL);
        kest_host_free(unasked);
        if (given == NULL) {
            kest_build_report(build, stderr, KEST_FORM_TEXT);
            fprintf(stderr, "a machine for a host that said nothing would not "
                            "start\n");
            return 1;
        }
        asking.rule = kest_entry(given, "rule");
        KestLimits was_given = {0, 0, 0, 0};
        kest_allowed(given, &was_given);
        // And the heap, which this host said nothing about: nought is what no
        // ceiling is, and the two beside it are always a number because a
        // machine always has a stack and a depth. One of the three answers
        // what a host wrote and the other two what the machine worked out, so
        // a host reading all three the same way is wrong about one. See D830.
        if (was_given.heap_bytes != 0 || was_given.stack_slots == 0 ||
            was_given.call_depth == 0) {
            fprintf(stderr,
                    "a host that said nothing about the heap was given %zu "
                    "bytes, %u slots and %u frames\n",
                    was_given.heap_bytes, was_given.stack_slots,
                    was_given.call_depth);
            return 1;
        }
        if (was_given.stack_slots >= KEST_STACK_SLOTS ||
            was_given.call_depth >= KEST_CALL_DEPTH) {
            fprintf(stderr, "a host that said nothing was given %u slots and "
                            "%u frames\n",
                    was_given.stack_slots, was_given.call_depth);
            return 1;
        }
        // And more than the program's own worst, which is the half of the
        // number a machine cannot leave out: what the program needs is what it
        // needs to be called, and the way back in from a host function is on
        // top of it. This host is the one that goes that way, so it is the one
        // that would find out.
        KestLimits worst = {0, 0, 0, 0};
        if (!kest_needs(build, &worst, NULL) ||
            was_given.stack_slots <= worst.stack_slots ||
            was_given.call_depth <= worst.call_depth) {
            fprintf(stderr, "the program's worst is %u slots and %u frames, "
                            "and a host that said nothing was given %u and "
                            "%u\n",
                    worst.stack_slots, worst.call_depth,
                    was_given.stack_slots, was_given.call_depth);
            return 1;
        }
        // And more by exactly the way back in, which is the other half of the
        // number and the one `kest_needs` does not answer. A host that asks
        // that and writes what it hears gets the smaller machine; asking this
        // one too and adding is what writing nothing does. Held as the sum
        // rather than as an order, because an order is kept by a machine that
        // is merely generous and this is the arithmetic a host has to redo to
        // get the same machine by hand. See D802.
        KestLimits way_back = {0, 0, 0, 0};
        if (!kest_needs_from(build, NULL, &way_back, NULL) ||
            was_given.stack_slots != worst.stack_slots + way_back.stack_slots ||
            was_given.call_depth != worst.call_depth + way_back.call_depth) {
            fprintf(stderr, "a host that said nothing was given %u slots and "
                            "%u frames, against %u and %u for the program and "
                            "%u and %u for the way back in\n",
                    was_given.stack_slots, was_given.call_depth,
                    worst.stack_slots, worst.call_depth,
                    way_back.stack_slots, way_back.call_depth);
            return 1;
        }
        printf("a host that said nothing was given %u slots, which is %u for "
               "the program and %u for the way back in\n",
               was_given.stack_slots, worst.stack_slots,
               way_back.stack_slots);
        KestValue turn[6] = {{0}};
        int32_t made_world = kest_entry(given, "create");
        int32_t put_one = kest_entry(given, "spawn");
        int32_t a_step = kest_entry(given, "step");
        if (made_world < 0 || put_one < 0 || a_step < 0 ||
            !kest_call(given, made_world, turn, 6)) {
            kest_report(given, stderr, KEST_FORM_TEXT);
            return 1;
        }
        KestValue world = turn[0];
        turn[0] = world;
        turn[1].integer = 2;
        if (!kest_call(given, put_one, turn, 6)) {
            kest_report(given, stderr, KEST_FORM_TEXT);
            return 1;
        }
        turn[0] = world;
        if (!kest_call(given, a_step, turn, 6)) {
            kest_report(given, stderr, KEST_FORM_TEXT);
            fprintf(stderr, "a machine given what the program asked for had "
                            "no room for the call this host makes\n");
            return 1;
        }
        size_t asked_for_cost = kest_runtime_cost(given);
        if (asked_for_cost >= narrow_cost) {
            fprintf(stderr, "what the program asked for is %zu bytes and 4096 "
                            "slots is %zu\n", asked_for_cost, narrow_cost);
            return 1;
        }
        if (!kest_runtime_free(given)) {
            fprintf(stderr, "the machine nobody picked numbers for was not "
                            "freed\n");
            return 1;
        }
        printf("and a host that picked nothing got %u slots and %u frames — "
               "%zu bytes — and stepped a world through this host and back\n",
               was_given.stack_slots, was_given.call_depth, asked_for_cost);
    }

    // And what a bound is worth being wrong about. A bound is enough for the
    // frames a host named and says nothing about a program that wants more,
    // so the promise is not that the number is right: it is that a machine
    // built from it refuses at the call that would go past it and says what
    // that call wanted. Shown on a program that reaches itself, because this
    // file's own has a least and a least is not a guess. See D820.
    {
        // The smallest host's program, which reaches itself and asks for
        // one name this host already has a function for. `examples/tree.kest`
        // would do as well and wants `std.math` bound as well, which is four
        // more bindings and nothing more shown.
        const char *deep_path = "examples/least.kest";
        KestBuild *deeply = kest_build(deep_path, NULL, stderr, KEST_FORM_TEXT, 0);
        if (deeply == NULL) {
            fprintf(stderr, "`%s` did not compile\n", deep_path);
            return 1;
        }
        KestLimits nowhere_near = {0, 0, 0, 0};
        KestReason bounded_at = {KEST_REACH_UNASKED, NULL};
        if (!kest_bound_of(deeply, "main", 2, &nowhere_near, &bounded_at) ||
            bounded_at.reach == KEST_REACH_KNOWN ||
            nowhere_near.call_depth != 2) {
            fprintf(stderr, "`%s` bounded at %u slots and %u frames\n",
                    deep_path, nowhere_near.stack_slots,
                    nowhere_near.call_depth);
            return 1;
        }
        KestHost *quiet = kest_host_new();
        if (quiet == NULL || !kest_host_bind(quiet, "Host.write", io_write,
                                             stdout)) {
            fprintf(stderr, "a host for `%s` would not be made\n", deep_path);
            return 1;
        }
        KestRuntime *too_few = kest_start(deeply, quiet, &nowhere_near);
        kest_host_free(quiet);
        if (too_few == NULL) {
            fprintf(stderr, "no machine for `%s` at two frames\n", deep_path);
            return 1;
        }
        int32_t its_main = kest_entry(too_few, "main");
        KestValue nothing_back[2] = {{0}};
        char refused[2048];
        if (its_main < 0 || kest_call(too_few, its_main, nothing_back, 2)) {
            fprintf(stderr, "`%s` ran in two frames\n", deep_path);
            return 1;
        }
        what_was_said(too_few, NULL, KEST_FORM_TEXT, refused, sizeof(refused));
        // The number it wanted, not merely that it wanted more: a host told
        // it ran out and not how far out is a host that raises the ceiling by
        // guesses. `K0602` carries both, and the words under it name the call.
        if (strstr(refused, "K0602") == NULL ||
            strstr(refused, "frames") == NULL ||
            strstr(refused, "this machine was given") == NULL) {
            fprintf(stderr, "a machine of two frames refused `%s` saying %s\n",
                    deep_path, refused);
            return 1;
        }
        if (!kest_runtime_free(too_few) || !kest_build_free(deeply)) {
            fprintf(stderr, "the machine of two frames was not freed\n");
            return 1;
        }
        printf("and a machine bounded at %u slots for 2 frames refused a "
               "program that wants more, and said what it wanted\n",
               nowhere_near.stack_slots);
    }

    // A machine stopped where a breakpoint is, which is what a host writing a
    // debugger does: one instruction written over, a run that comes back
    // without finishing, the frames and what a body called its slots read out
    // of it, and then the byte put back and the machine let go. Nothing here
    // is a refusal and the report is empty, which is why `kest_stopped` exists
    // to tell one from the other. See D991.
    {
        KestBuild *stopping = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
        if (stopping == NULL) {
            fprintf(stderr, "the program would not build a second time\n");
            return 1;
        }
        // A host of its own, because the one above was freed the moment the
        // first machine had read it: what a machine keeps is its own copy.
        KestHost *watching = kest_host_new();
        if (watching == NULL ||
            !kest_host_bind(watching, "Io.write", io_write, stdout) ||
            !kest_host_bind(watching, "Engine.decide", engine_decide,
                            &decider) ||
            !kest_host_bind(watching, "Engine.name", engine_name, &decider) ||
            !kest_host_bind(watching, "Engine.rank", engine_rank, &decider) ||
            !kest_host_bind(watching, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(watching, "Engine.blame", engine_blame,
                            &blaming) ||
            !kest_host_bind(watching, "Engine.weigh", engine_weigh,
                            &decider) ||
            !kest_host_bind(watching, "Engine.who", engine_who, &decider)) {
            fprintf(stderr, "a second host could not be bound\n");
            return 1;
        }
        KestRuntime *watched = kest_start(stopping, watching, NULL);
        kest_host_free(watching);
        if (watched == NULL) {
            kest_build_report(stopping, stderr, KEST_FORM_TEXT);
            kest_build_free(stopping);
            return 1;
        }
        int32_t doubled = kest_entry(watched, "doubled");
        uint32_t many = 0;
        uint8_t *code = kest_code_of(watched, doubled, &many);
        if (doubled < 0 || code == NULL || many == 0) {
            fprintf(stderr, "there is nothing to put a breakpoint in\n");
            return 1;
        }
        // Over the first instruction of the body, which is the one place a
        // breakpoint is certainly at the start of an instruction without
        // walking anything.
        // The instruction nothing compiles to, asked of the machine rather
        // than written down here. It was written down here, and twice the
        // number moved when an instruction was added and this host wrote a
        // byte that had come to mean something else -- which is not a message,
        // it is whatever running that instruction does. See D1012.
        uint8_t was = code[0];
        code[0] = kest_break_byte();
        KestValue asking[4] = {{0}};
        asking[0].integer = 21;
        bool finished = kest_call(watched, doubled, asking, 4);
        if (finished || kest_stopped(watched) != 0 ||
            kest_stopped_in(watched) != doubled) {
            fprintf(stderr, "a machine did not stop where a breakpoint is\n");
            return 1;
        }
        if (kest_frames_deep(watched) != 1 ||
            kest_frame_in(watched, 0) != doubled ||
            kest_frame_ip(watched, 0) != 0) {
            fprintf(stderr, "a stopped machine says the wrong frame\n");
            return 1;
        }
        if (kest_came_from(watched, doubled, 0) < 0) {
            fprintf(stderr, "a stopped machine cannot say where it is\n");
            return 1;
        }
        uint16_t slots = 0;
        uint8_t kind = 0;
        const char *called = kest_frame_name(watched, 0, 0, &slots, &kind);
        // And whether that slot holds where the value is rather than the
        // value. `n` is a number a caller handed over, so it holds what it is
        // called; what does not is an element a `for` bound by address, which
        // `examples/engine.c` stops inside. See D1081.
        if (kest_frame_at_address(watched, 0, 0)) {
            fprintf(stderr, "a number handed in was said to be an address\n");
            return 1;
        }
        KestValue held_here = {0};
        if (called == NULL || strcmp(called, "n") != 0 ||
            kest_frame_wide(watched, 0) == 0 ||
            !kest_frame_slot(watched, 0, 0, &held_here) ||
            held_here.integer != 21) {
            fprintf(stderr, "a stopped machine cannot say what it holds\n");
            return 1;
        }
        // What a host may not do to a stopped machine, which is everything
        // that would take the heap out from under it or move the ceiling on
        // it. A stop is in the middle
        // of a call: nothing of this host's is on the machine's stack, so
        // every door that asks whether the program is running used to hear no
        // and a walk here gave the frames' memory back. Asked here because a
        // host writing a debugger is a host between two stops with time on its
        // hands, which is exactly when a frame loop does its housekeeping.
        // See D1078.
        FILE *told = tmpfile();
        if (told == NULL) {
            fprintf(stderr, "this host has nowhere to read a report back\n");
            return 1;
        }
        if (kest_collect(watched) || kest_heap_reset(watched) ||
            kest_scratch_mark(watched) != 0 ||
            kest_scratch_rewind(watched, 1) ||
            kest_heap_allow(watched, 4096)) {
            fprintf(stderr,
                    "a stopped machine let the heap its frames are standing "
                    "on be taken away\n");
            return 1;
        }
        kest_report(watched, told, KEST_FORM_TEXT);
        rewind(told);
        char what[512];
        int32_t told_off = 0;
        while (fgets(what, sizeof(what), told) != NULL) {
            if (strstr(what, "K0613") != NULL) {
                told_off++;
            }
        }
        fclose(told);
        if (told_off != 5) {
            fprintf(stderr,
                    "a host asked five things of a stopped machine it may not "
                    "have and was told about %d\n",
                    told_off);
            return 1;
        }
        // Put back, and let go: the instruction the byte was written over runs
        // first, so the program is the program again.
        code[0] = was;
        if (!kest_resume(watched, asking, 4) || kest_stopped(watched) >= 0) {
            kest_report(watched, stderr, KEST_FORM_TEXT);
            fprintf(stderr, "a machine that was stopped did not carry on\n");
            return 1;
        }
        if (asking[0].integer != 42) {
            fprintf(stderr, "a machine that carried on answered %lld\n",
                    (long long)asking[0].integer);
            return 1;
        }
        printf("a machine stopped at a breakpoint, said `%s` held %lld, and "
               "carried on to %lld\n",
               called, (long long)held_here.integer,
               (long long)asking[0].integer);
        if (!kest_runtime_free(watched) || !kest_build_free(stopping)) {
            fprintf(stderr, "what was stopped could not be given back\n");
            return 1;
        }
    }

    // And a run this host makes from inside a call of its own, ending the two
    // ways that are not returning. `weighed` asks the host what something is
    // worth and doubles the answer, so what the host does while it is in there
    // is something the program reads the result of: a call back in that
    // divides by nothing ten frames down, and one with a breakpoint written
    // into it. Both have to leave `weighed` answering 42. See D1079.
    {
        KestBuild *inward = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
        if (inward == NULL) {
            fprintf(stderr, "the program would not build a third time\n");
            return 1;
        }
        KestHost *from_inside = kest_host_new();
        if (from_inside == NULL ||
            !kest_host_bind(from_inside, "Io.write", io_write, stdout) ||
            !kest_host_bind(from_inside, "Engine.decide", engine_decide,
                            &decider) ||
            !kest_host_bind(from_inside, "Engine.name", engine_name,
                            &decider) ||
            !kest_host_bind(from_inside, "Engine.rank", engine_rank,
                            &decider) ||
            !kest_host_bind(from_inside, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(from_inside, "Engine.blame", engine_blame,
                            &blaming) ||
            !kest_host_bind(from_inside, "Engine.weigh", weigh_from_inside,
                            NULL) ||
            !kest_host_bind(from_inside, "Engine.who", engine_who, &decider)) {
            fprintf(stderr, "the host that calls back in could not be bound\n");
            return 1;
        }
        KestRuntime *asking_in = kest_start(inward, from_inside, NULL);
        kest_host_free(from_inside);
        if (asking_in == NULL) {
            kest_build_report(inward, stderr, KEST_FORM_TEXT);
            return 1;
        }
        int32_t weighed = kest_entry(asking_in, "weighed");
        if (weighed < 0) {
            fprintf(stderr, "there is nothing to ask what something weighs\n");
            return 1;
        }
        for (weighing_how = 0; weighing_how < 3; weighing_how++) {
            weighing_went = 0;
            weighing_stopped = 0;
            KestValue worth[2] = {{0}};
            bool answered = kest_call(asking_in, weighed, worth, 2);
            if (weighing_how != 0 && weighing_went) {
                fprintf(stderr, "a call made from inside a call ran on "
                                "through %s\n",
                        weighing_how == 1 ? "arithmetic that cannot be done"
                                          : "a breakpoint");
                return 1;
            }
            if (weighing_how == 2 &&
                (weighing_stopped ||
                 !said_that(asking_in, "K0708",
                            "cannot stop in a call the host made back in"))) {
                fprintf(stderr, "a machine stopped in a call this host made "
                                "from inside one of its own\n");
                return 1;
            }
            if (!answered || worth[0].integer != 42) {
                kest_report(asking_in, stderr, KEST_FORM_TEXT);
                fprintf(stderr,
                        "a call with a run of the host's inside it answered "
                        "%lld\n",
                        (long long)worth[0].integer);
                return 1;
            }
        }
        printf("a call the host made from inside one of its own left the call "
               "it was made from answering 42, refused ten frames down and "
               "with a breakpoint in it\n");
        if (!kest_runtime_free(asking_in) || !kest_build_free(inward)) {
            fprintf(stderr, "what called back in could not be given back\n");
            return 1;
        }
    }

    // What a host keeps is the machine's to keep for it until the host says
    // otherwise, and the second saying is not a refusal -- it is the answer
    // that the machine was not keeping it, which is what a host that let go
    // twice wants to be told rather than left to guess. A heap thrown away
    // forgets every one of them, which is why this is asked of something made
    // after the last one. See D996.
    KestValue on_purpose[2] = {{0}, {0}};
    if (!kest_text(engine.runtime, "kept on purpose", 15, on_purpose) ||
        !kest_keeps(engine.runtime, on_purpose[0]) ||
        !kest_lets_go(engine.runtime, on_purpose[0]) ||
        kest_lets_go(engine.runtime, on_purpose[0])) {
        fprintf(stderr, "something kept on purpose was not let go of\n");
        return 1;
    }
    printf("and what this host kept on purpose it let go of, once\n");
    // And the most this machine ever held at once, which is the number that
    // says what a host has to make room for: what it is holding now is where
    // it happens to be, and a frame budget is sized by the worst of a run.
    printf("this machine held %zu bytes at most, and %zu of them now\n",
           kest_heap_most(engine.runtime), kest_heap_used(engine.runtime));

    // And what the heap under it did to get there, which is the other half of
    // the same question: what it holds is where it ended up, and this is the
    // work. Nothing was turned on to get it -- every one of these is at an
    // allocation, a walk or a lend, so they are counted whether or not
    // anybody asks. See D1007.
    KestTelemetry did = {0};
    if (!kest_telemetry(engine.runtime, &did) ||
        kest_telemetry(NULL, &did) || kest_telemetry(engine.runtime, NULL)) {
        fprintf(stderr, "what the heap did was not answered for\n");
        return 1;
    }
    if (did.allocations == 0 || did.asked == 0 || did.given < did.asked) {
        fprintf(stderr, "a run that made things says it made none\n");
        return 1;
    }
    printf("the heap handed out %llu place(s), asked for %llu byte(s) and "
           "cut them from %llu\n",
           (unsigned long long)did.allocations, (unsigned long long)did.asked,
           (unsigned long long)did.given);
    // The clock is this host's, and a machine that was given one says what
    // its walks took. A machine given none says nought and runs the same
    // program: nothing a program answers depends on this.
    kest_clock(engine.runtime, host_nanoseconds, NULL);
    kest_clock(NULL, host_nanoseconds, NULL);
    // And who is told what each walk cost, which is the other half of that:
    // the numbers above are a total and a worst, and a host with a frame to
    // fit into wants each one. Asked of nothing too, which is the shape every
    // door here is asked in.
    PausesHeard pauses_heard;
    memset(&pauses_heard, 0, sizeof pauses_heard);
    kest_collected(engine.runtime, host_was_told_a_pause, &pauses_heard);
    kest_collected(NULL, host_was_told_a_pause, &pauses_heard);

    // And when the machine decides a walk is worth doing on its own, which is
    // the one number there is to turn: a host budgeting a frame leaves it at
    // one and gets the shortest pause, and a host that is not raises it and
    // gets fewer of them for more memory held. Asked of nothing too, and with
    // nought, which means the same as one. See D1045.
    kest_collect_after(engine.runtime, 2);
    kest_collect_after(engine.runtime, 0);
    kest_collect_after(NULL, 3);
    kest_collect_after(engine.runtime, 1);

    // And a walk at a moment this host chose rather than at whatever
    // allocation would have set one off. A host with a frame to fit into asks
    // for it between frames; here it is asked for between calls, which is the
    // only place it is allowed, and asked of nothing to see it refuse.
    KestTelemetry before_walk = {0};
    kest_telemetry(engine.runtime, &before_walk);
    unsigned long long heard_before = pauses_heard.count;
    bool walked = kest_collect(engine.runtime);
    KestTelemetry after_walk = {0};
    kest_telemetry(engine.runtime, &after_walk);
    // It answers whether it walked, and a walk that could not remember where
    // it had got to takes nothing and says so -- which is what a machine with
    // no room left to walk in does. So what is held is the two agreeing:
    // it swept if and only if it said it did.
    if (kest_collect(NULL) ||
        after_walk.sweeps != before_walk.sweeps + (walked ? 1u : 0u)) {
        fprintf(stderr, "a walk this host asked for did not do what it "
                        "said\n");
        return 1;
    }
    printf("and a walk this host asked for %s when it asked\n",
           walked ? "happened" : "said it could not");
    // A walk that swept says so once, to this host, with what it cost. A walk
    // that could not finish took nothing and says nothing, which is why the
    // count is held against whether it swept rather than against whether it
    // was asked for.
    if (pauses_heard.count != heard_before + (walked ? 1u : 0u)) {
        fprintf(stderr, "a walk that swept was not said to have\n");
        return 1;
    }
    if (walked && pauses_heard.last.live > pauses_heard.last.plot_bytes) {
        fprintf(stderr, "a walk says the heap holds more than it was given\n");
        return 1;
    }
    printf("and this host was told what each of the %llu walk(s) cost, the "
           "last of them holding %llu byte(s) in %llu plot(s) of %llu\n",
           pauses_heard.count, (unsigned long long)pauses_heard.last.live,
           (unsigned long long)pauses_heard.last.plots,
           (unsigned long long)pauses_heard.last.plot_bytes);
    kest_collected(engine.runtime, NULL, NULL);

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
        static Decider quiet = {-1, 1, false, true, false, false};
        if (apart == NULL ||
            !kest_host_bind(apart, "Io.write", io_write, stdout) ||
            !kest_host_bind(apart, "Engine.decide", engine_decide, &quiet) ||
            !kest_host_bind(apart, "Engine.name", engine_name, &quiet) ||
            !kest_host_bind(apart, "Engine.rank", engine_rank, &quiet) ||
            !kest_host_bind(apart, "Engine.hurt", engine_hurt, NULL) ||
            !kest_host_bind(apart, "Engine.blame", engine_blame, NULL) ||
            !kest_host_bind(apart, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(apart, "Engine.who", engine_who, NULL)) {
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
        KestBuild *aside = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
        KestLimits nothing_left = {0, 0, 0, 0};
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
        KestValue too_much[2] = {{0}, {0}};
        if (kest_text(starved, many, sizeof(many) - 1, too_much) ||
            too_much[0].text == NULL || too_much[0].text[0] != '\0') {
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
        KestLimits bare = {0, 0, 0, 0};
        if (!kest_needs_of(build, "step", &bare, NULL)) {
            fprintf(stderr, "the program says nothing about what `step` "
                            "needs\n");
            return 1;
        }
        KestBuild *narrowly = kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
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

    // And what a reload costs, which is the question a host that watches a
    // file for changes is really asking: every machine freed, the build freed,
    // the build made again, every machine started again. It is two numbers and
    // no others — what the build costs and what the machines do — and both are
    // the same every time round, because nothing in this library outlives a
    // build. There is no global state to carry anything, which is a rule this
    // project keeps rather than a thing this measures; what this does is show
    // where it would fail. Three times round, because two of anything can
    // agree by accident. See D578.
    {
        size_t built = 0;
        size_t started = 0;
        // And what a host that saved bytes asks before it reads them back into
        // a program it has just built again: whether the shape it saved them
        // as is the shape this build has. The same number is nothing to
        // migrate; a different one is a field that moved, changed width or
        // changed name, and a host that does not ask lays its old bytes over a
        // new struct. See D948.
        const KestLayout *was = NULL;
        uint64_t shaped = kest_build_layout(build, "Row", &was) == 1
                              ? kest_layout_mark(was)
                              : 0;
        for (int cycle = 0; cycle < 3; cycle++) {
            KestHost *over = kest_host_new();
            static Decider quietly = {-1, 1, false, true, false, false};
            KestBuild *reloaded =
                kest_build(path, NULL, stderr, KEST_FORM_TEXT, 0);
            if (over == NULL || reloaded == NULL ||
                !kest_host_bind(over, "Io.write", io_write, stdout) ||
                !kest_host_bind(over, "Engine.decide", engine_decide,
                                &quietly) ||
                !kest_host_bind(over, "Engine.name", engine_name, &quietly) ||
                !kest_host_bind(over, "Engine.rank", engine_rank, &quietly) ||
                !kest_host_bind(over, "Engine.hurt", engine_hurt, NULL) ||
                !kest_host_bind(over, "Engine.blame", engine_blame, NULL) ||
                !kest_host_bind(over, "Engine.weigh", engine_weigh, NULL) ||
        !kest_host_bind(over, "Engine.who", engine_who, NULL)) {
                fprintf(stderr, "a reload would not build\n");
                return 1;
            }
            KestRuntime *again = kest_start(reloaded, over, NULL);
            kest_host_free(over);
            if (again == NULL) {
                kest_build_report(reloaded, stderr, KEST_FORM_TEXT);
                fprintf(stderr, "a reload would not start\n");
                return 1;
            }
            const KestLayout *now = NULL;
            if (kest_build_layout(reloaded, "Row", &now) != 1 ||
                kest_layout_mark(now) != shaped) {
                fprintf(stderr, "`Row` is a different shape after a reload of "
                                "the same file\n");
                return 1;
            }
            size_t costs = kest_build_cost(reloaded);
            size_t machine = kest_runtime_cost(again);
            if (cycle == 0) {
                built = costs;
                started = machine;
            } else if (costs != built || machine != started) {
                fprintf(stderr, "reload %d cost %zu and %zu where the first "
                                "cost %zu and %zu\n",
                        cycle, costs, machine, built, started);
                return 1;
            }
            if (!kest_runtime_free(again) || !kest_build_free(reloaded)) {
                fprintf(stderr, "a reload would not be given back\n");
                return 1;
            }
        }
        printf("a reload of this program is %zu bytes of build and %zu of "
               "machine, three times over\n", built, started);
    }

    // And the same file built inside a ceiling, which is the one thing this
    // door did not take. Last of everything this host asks, because what is
    // read back here is a refusal, and a refusal read before the checks above
    // is one of them answered by the wrong question. See D844. A host that compiles a program somebody else wrote
    // can be handed a file this compiler cannot make sense of, and one of
    // those asked this machine for sixty-five gigabytes — so what a build may
    // have is a number, and the number this host has in front of it is what
    // building this one cost a moment ago. See D844.
    KestBuild *walled = kest_build(path, NULL, stderr, KEST_FORM_TEXT,
                                   first_build);
    if (walled == NULL) {
        fprintf(stderr, "a build given the %zu bytes it costs would not "
                        "build\n",
                first_build);
        return 1;
    }
    if (kest_build_cost(walled) != first_build) {
        fprintf(stderr, "a build given what it costs cost %zu instead\n",
                kest_build_cost(walled));
        return 1;
    }
    kest_build_free(walled);

    // And two ceilings it does not fit in, because there are two ways to be
    // refused by one and a sentence for each: one where there was never enough
    // to begin, and one where a build was under way and the allocation that
    // crossed the ceiling is worth a number. What comes back is nothing either
    // way, and a host that only reads the nothing cannot tell a ceiling it
    // picked from a machine it has to buy.
    const size_t too_little[2] = {1, first_build / 2};
    const char *cramped[2] = {"not enough to begin", "bytes it was given"};
    for (int which = 0; which < 2; which++) {
        FILE *refused = tmpfile();
        if (refused == NULL) {
            fprintf(stderr, "this host has nowhere to read a refusal back "
                            "from\n");
            return 1;
        }
        KestBuild *cannot = kest_build(path, NULL, refused, KEST_FORM_TEXT,
                                       too_little[which]);
        if (cannot != NULL) {
            fprintf(stderr, "a build given %zu of the %zu bytes it costs "
                            "built anyway\n",
                    too_little[which], first_build);
            return 1;
        }
        char why[512] = {0};
        rewind(refused);
        size_t said = fread(why, 1, sizeof why - 1, refused);
        fclose(refused);
        why[said] = '\0';
        if (strstr(why, "K0658") == NULL ||
            strstr(why, cramped[which]) == NULL) {
            fprintf(stderr, "a build given %zu bytes said `%s`\n",
                    too_little[which], why);
            return 1;
        }
    }

    // Every door of this header knocked on with a machine that did not start.
    // `kest_start` answers NULL and says why into the build's report, and a
    // host that carries on regardless is a host with a bug — but the answer to
    // that was thirteen of these taking the process down while eight answered
    // politely, which is a guard written where somebody happened to be rather
    // than a rule. The rule is that a machine that did not start is a machine
    // with nothing in it, and the answer is the one a real machine gives when
    // it has nothing. There is nowhere to say more: a report belongs to a
    // machine and there is none. See D894.
    {
        KestValue nothing = {0};
        int32_t numbers[2] = {1, 2};
        uint8_t kinds[2] = {0, 0};
        const char *words[1] = {"a"};
        char out[8];
        KestValue frame[4] = {{0}};
        KestLimits allowed;
        KestValue nowhere[2] = {{0}, {0}};
        bool quiet = !kest_text(NULL, "hi", 2, nowhere) &&
                     nowhere[0].text != NULL &&
                     kest_borrow(NULL, numbers, 2, "i32", 4).object == NULL &&
                     !kest_lend_ends(NULL, nothing) &&
                     !kest_still_holds(NULL, nothing) &&
                     kest_kept_where(NULL, nothing) == KEST_KEPT_NOWHERE &&
                     !kest_call(NULL, 0, frame, 1) &&
                     kest_entry(NULL, "main") < 0 &&
                     kest_entry_of(NULL, "main", 0) < 0 &&
                     kest_entry_name(NULL, 0) == NULL &&
                     kest_entry_wrote(NULL, 0) == NULL &&
                     !kest_entry_promises(NULL, 0, KEST_PROMISE_NO_ALLOC) &&
                     kest_frame_takes(NULL, 0) == 0 &&
                     kest_frame_at(NULL, 0, 0) == 0 &&
                     kest_frame_layout(NULL, 0, 0) == NULL &&
                     kest_frame_gives(NULL, 0) == NULL &&
                     !kest_frame_fills(NULL, 0, kinds, 2) &&
                     !kest_frame_reads(NULL, 0, kinds, 2) &&
                     !kest_takes_text(NULL, 0, frame, 1, words, 1) &&
                     kest_gave_text(NULL, 0, frame, out, sizeof out) < 0 &&
                     kest_frame_slots(NULL, 0) == 0 &&
                     kest_heap_used(NULL) == 0 &&
                     kest_heap_wanted(NULL) == 0 &&
                     !kest_heap_reset(NULL) && !kest_heap_allow(NULL, 16) &&
                     kest_runtime_cost(NULL) == 0 &&
                     kest_runtime_free(NULL);
        kest_report(NULL, stderr, KEST_FORM_TEXT);
        kest_allowed(NULL, &allowed);
        (void)kest_heap_refused_by(NULL);
        if (!quiet) {
            fprintf(stderr, "a machine that did not start answered as though "
                            "it had\n");
            return 1;
        }
    }

    // And the same question asked of the other two things a host holds. A
    // build that did not compile is the first thing a host meets -- `kest_build`
    // answers NULL for it -- so this is the more likely of the two, and the
    // next line a host writes is `kest_start`. Twenty-five doors, and two of
    // them took the process down. See D895.
    {
        KestLimits least;
        KestReason why = {0, NULL};
        const KestLayout *laid = NULL;
        bool quiet = !kest_needs(NULL, &least, &why) &&
                     !kest_needs_of(NULL, "main", &least, &why) &&
                     !kest_bound(NULL, 4, &least, &why) &&
                     !kest_bound_of(NULL, "main", 4, &least, &why) &&
                     !kest_needs_from(NULL, NULL, &least, &why) &&
                     !kest_bound_from(NULL, NULL, 4, &least, &why) &&
                     !kest_host_bind(NULL, "a", NULL, NULL) &&
                     kest_host_find(NULL, "a", NULL) == NULL &&
                     kest_build_free(NULL) &&
                     kest_build_cost(NULL) == 0 &&
                     kest_build_held(NULL) == 0 &&
                     kest_build_read(NULL, 0) == NULL &&
                     kest_build_read_bytes(NULL, 0) == 0 &&
                     kest_build_read_mark(NULL, 0) == 0 &&
                     kest_build_mark(NULL) == 0 &&
                     kest_build_code_mark(NULL) == 0 &&
                     kest_build_source(NULL) == 0 &&
                     kest_build_extern(NULL, 0) == NULL &&
                     kest_extern_takes(NULL, 0) == 0 &&
                     kest_extern_layout(NULL, 0, 0) == NULL &&
                     kest_extern_gives(NULL, 0) == NULL &&
                     kest_build_layout(NULL, "Npc", &laid) == 0 &&
                     kest_start(NULL, NULL, NULL) == NULL;
        kest_build_report(NULL, stderr, KEST_FORM_TEXT);
        kest_host_free(NULL);
        if (!quiet) {
            fprintf(stderr, "a build that is not there answered as though it "
                            "were\n");
            return 1;
        }
    }

    // And then the build, which nothing is standing on now.
    if (!kest_build_free(build)) {
        kest_build_report(build, stderr, KEST_FORM_TEXT);
        return 1;
    }
    return 0;
}
